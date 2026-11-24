#include "nob.h"
#define SV_NULL ((Nob_String_View){ .count = 0, .data = NULL })

#define CIAO_CAH_MEM_STDLIB
#include "ciao_ca.h"

#include "mfiles.h"

#include "mime-type.h"

#include <time.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <signal.h>
#include <ctype.h>
#include <errno.h>

#define cstr_eq(a, b) (strcmp((a), (b)) == 0)

typedef struct {
  char *items;
  size_t len, cap;
} DString;

typedef struct {
  DString raw;
  char method[256];
  char path[256];
  String_View headers;
  String_View body;
  bool read;
  bool verified;
} Request;

typedef struct {
  MF_File file;
  size_t cursor;
} Response;

typedef struct {
  int fd;
  Request req;
  Response res;
  size_t cursor;
  bool closed;
} Client;

typedef struct {
  Client *items;
  size_t len, cap;
  size_t closed;
} Clients;

struct {
  int fd;
  int backlog_n;
  uint16_t port;
  Clients clients;
  const char *base_dir;
} server = { .fd = -1, .port = 8080, .backlog_n = 10 };

const char *program_name;

void log_debug(const char *fmt, ...) {
  va_list ap;
  fputs("[DEBUG] ", stderr);
  va_start(ap, fmt);
  vfprintf(stderr, fmt, ap);
  va_end(ap);
  fputc('\n', stderr);
}
#define log_error(...) nob_log(ERROR, __VA_ARGS__)
#define log_info(...)  nob_log(INFO, __VA_ARGS__)

const char *sv_find_cstr(Nob_String_View haystack, const char *needle) {
  size_t len = strlen(needle);
  if (haystack.count < len) return NULL;
  if (haystack.count == len) return (memcmp(haystack.data, needle, len) == 0) ? haystack.data : NULL;

  for (size_t i = 0; i <= (haystack.count-len); ++i) {
    if (memcmp(&haystack.data[i], needle, len) == 0) {
      return &haystack.data[i];
    }
  }

  return NULL;
}

int sv_to_int(Nob_String_View sv) {
  if (sv.data == NULL) return 0;
  sv = sv_trim_left(sv);
  if (sv.count == 0) return 0;
  int x = 0;
  bool neg = false;
  if (*sv.data == '-') {
    neg = true;
    sv_chop_left(&sv, 1);
  }
  if (*sv.data == '+') {
    neg = false;
    sv_chop_left(&sv, 1);
  }
  for (const char *it = sv.data; it < sv.data+sv.count; ++it) {
    if (*it < '0' || *it > '9') break;
    x = x * 10 + (*it - '0');
  }
  if (neg) x *= -1;
  return x;
}

char *cstr_to_upper(char *str) {
  for (char *it = str; *it; ++it) *it = (char)toupper(*it);
  return str;
}

char *cstr_to_lower(char *str) {
  for (char *it = str; *it; ++it) *it = (char)tolower(*it);
  return str;
}

void client_close(Client *client) {
  if (client->req.raw.items) free(client->req.raw.items);
  mf_close_read(&client->res.file);
  close(client->fd);
  client->closed = true;
}

String_View client_find_header(Client *client, const char *header_name) {
  size_t reset_point = temp_save();
  char *needle = cstr_to_lower(temp_strdup(header_name));
  String_View result;
  String_View headers = client->req.headers;
  if (headers.data == NULL || headers.count == 0) return_defer(SV_NULL);
  size_t save_point = temp_save();
  while (headers.count > 0) {
    temp_rewind(save_point);
    String_View header = sv_trim(sv_chop_by_delim(&headers, '\n'));
    String_View name_sv = sv_trim(sv_chop_by_delim(&header, ':'));
    const char *name = cstr_to_lower((char*)temp_sv_to_cstr(name_sv));
    if (strcmp(name, needle) == 0) return_defer(header);
  }

  result = SV_NULL;

defer:
  temp_rewind(reset_point);
  return result;
}

bool client_try_get_content_length(Client *c, int *content_length) {
  String_View sv = client_find_header(c, "Content-Length");
  if (!sv.data) return false;
  *content_length = sv_to_int(sv);
  return true;
}

int client_get_content_length(Client *c) {
  int content_length;
  if (!client_try_get_content_length(c, &content_length)) return 0;
  return content_length;
}

bool _client__setup_req_base_info(Client *c) {
  DString *ds = &c->req.raw;
  String_View sv = sv_from_parts(ds->items, ds->len);
  const char *line_end = sv_find_cstr(sv, "\r\n");
  if (!line_end) return false;
  String_View raw = sv_trim_right(sv_from_parts(ds->items, (size_t)(line_end - ds->items)));
  char *buf;
  size_t cap, len;

  buf = &c->req.method[0];
  cap = sizeof(c->req.method)-1;

  String_View method = sv_chop_by_delim(&raw, ' ');
  len = method.count < cap ? method.count : cap;
  memcpy(buf, method.data, len);
  buf[len] = '\0';
  cstr_to_upper(buf);

  buf = &c->req.path[0];
  cap = sizeof(c->req.path)-1;

  String_View path = sv_chop_by_delim(&raw, ' ');
  len = path.count < cap ? path.count : cap;
  memcpy(buf, path.data, len);
  buf[len] = '\0';
  cstr_to_lower(buf);

  // TODO: Maybe we should take a look at the HTTP version to make sure we only take HTTP/1
  return true;
}

bool _client__setup_headers(Client *c) {
  if (!_client__setup_req_base_info(c)) return false;
  DString *ds = &c->req.raw;
  String_View sv = sv_from_parts(ds->items, ds->len);
  const char *headers_end = sv_find_cstr(sv, "\r\n\r\n");
  if (!headers_end) return false;
  const char *headers_start = sv_find_cstr(sv, "\r\n");
  size_t headers_len = (size_t)(headers_end - headers_start);
  String_View headers = sv_from_parts(headers_start, headers_len);
  c->req.headers = sv_trim(headers);
  c->req.body = sv_from_parts(headers_end + 4, sv.count - (size_t)((headers_end + 4) - sv.data));
  return true;
}

bool client_write(Client *c, String_View *sv) {
  static size_t WRITE_CHUNK_SIZE = 1024;
  size_t n = sv->count < WRITE_CHUNK_SIZE ? sv->count : WRITE_CHUNK_SIZE;
  ssize_t err_or_n = write(c->fd, sv->data, n);
  if (err_or_n < 0) {
    log_error("Client(%d) write failed: %s", c->fd, strerror(errno));
    return false;
  }
  n = (size_t)err_or_n;
  sv_chop_left(sv, n);
  return true;
}

bool client_write_all(Client *c, Nob_String_View buffer) {
  while (buffer.count) {
    if (!client_write(c, &buffer)) return false;
  }
  return true;
}

void handle_client(Client *client) {
  if (client->closed) return;

  Request *req = &client->req;
  if (!req->read) {
    DString *ds = &req->raw;
    ssize_t n;
    // We have started read on the body
    if (req->body.data) {
      size_t content_length = (size_t)client_get_content_length(client);
      if (req->body.count < content_length) {
        size_t missing_len = (content_length - req->body.count);
        if (!ciao_da_ensure(ds, missing_len)) {
          log_error("Closing client(%d) cause out of memory!", client->fd);
          client_close(client);
          return;
        }

        n = read(client->fd, ds->items + ds->len, ds->cap - ds->len);
        if (n < 0) {
          if (errno != EAGAIN && errno != EWOULDBLOCK) {
            log_error("Closing client(%d) cause of error: %s", client->fd, strerror(errno));
            client_close(client);
          }
          return;
        }
        log_debug("Read %zu bytes from client(%d)", (size_t)n, client->fd);

        ds->len += (size_t)n;
        if ((size_t)n >= missing_len) req->read = true;
        return;
      }

      // If we made it here we have read more than we need or exactly the amount, unclear
      req->read = true;
      req->body.count = content_length;
      return;
    }

    char read_buffer[256];
    n = read(client->fd, read_buffer, sizeof(read_buffer));
    if (n < 0) {
      int e = errno;
      if (e != EAGAIN && e != EWOULDBLOCK) client_close(client);
      log_debug("Read blocked. Excuse: %s", strerror(e));
      return;
    }

    log_debug("Read %zu bytes from client(%d)", (size_t)n, client->fd);

    if (n == 0) {
      req->read = true;
      req->body = sv_from_parts(ds->items + ds->len, 0);
      return;
    }

    if (!ciao_da_push_arr(ds, read_buffer, (size_t)n)) {
      log_error("Ran out of memory closing Client(%d)", client->fd);
      client_close(client);
      return;
    }

    if (!_client__setup_headers(client)) return;

    const char *method = req->method;
    if (strcmp(method, "GET") == 0) {
      req->read = true;
      return;
    }

    // Missing Content-Length header is taken as length of 0
    int content_length = client_get_content_length(client);
    if (content_length <= 0) {
      req->read = true;
      req->body.count = 0;
    } else if (req->body.count >= (size_t)content_length) {
      req->read = true;
      req->body.count = (size_t)content_length;
    }

    return;
  }

  size_t rewind_point = temp_save();

  // If we have the file open already
  if (client->res.file.data) {
    Response *res = &client->res;
    String_View sv = sv_from_parts(
      ((char*)res->file.data)+res->cursor,
      res->file.size - res->cursor
    );
    size_t leftover_count = sv.count;
    if (client_write(client, &sv)) {
      size_t n = leftover_count - sv.count;
      res->cursor += n;
    } else {
      sv.count = 0;
    }

    if (sv.count == 0) client_close(client);
    temp_rewind(rewind_point);
    return;
  }

  log_info("%s %s: Client(%d)", req->method, req->path, client->fd);

  const char *target_path = req->path;
  if (strcmp(target_path, "/") == 0) {
    target_path = "/index";
  }

  if (strrchr(target_path, '.') == NULL) {
    target_path = temp_sprintf("%s%s.html", server.base_dir, target_path);
  } else {
    target_path = temp_sprintf("%s%s", server.base_dir, target_path);
  }

  if (!mf_read_file(target_path, &client->res.file)) {
    log_info("%s %s: Client(%d) - ERROR 404: %s", req->method, req->path, client->fd, target_path);

    const char *content = temp_sprintf("<!DOCTYPE html>\n"
      "<html lang=\"en\">"
      "<h1>ERROR 404:<br>Page Not found</h1>"
      "<p>Unknown page %s</p>"
      "</html>", req->path);
    size_t content_length = strlen(content);

    const char *response = temp_sprintf("HTTP/1.0 404 NOT FOUND\r\n"
      "Content-Type: text/html\r\n"
      "Connection: close\r\n"
      "Content-Length: %zu\r\n\r\n"
      "%s",
      content_length,
      content
    );
    size_t response_length = strlen(response);

    client_write_all(client, nob_sv_from_parts(response, response_length));

    temp_rewind(rewind_point);
    client_close(client);
    return;
  }

  log_info("%s %s: Client(%d) - OK 200", req->method, req->path, client->fd);
  const char *file_ext = nob_temp_file_ext(target_path);
  if (file_ext) file_ext++;
  const char *content_type = mime_type_get_content_type_for_file_ext(file_ext);

  const char *headers = temp_sprintf(
    "HTTP/1.0 200 OK\r\n"
    "Content-Type: %s\r\n"
    "Content-Length: %zu\r\n"
    "Connection: close\r\n\r\n",
    content_type,
    client->res.file.size
  );

  if (!client_write_all(client, sv_from_cstr(headers))) client_close(client);

  temp_rewind(rewind_point);
}

void server_close() {
  if (server.clients.len > 0) {
    ciao_da_foreach(Client, c, &server.clients) {
      if (c->closed) continue;
      client_close(c);
    }
  }

  if (server.fd > -1) close(server.fd);
}

void handle_sigint(int signal_number) {
  (void)signal_number;

  // server_close();
  if (server.fd > -1) close(server.fd);

  signal(SIGINT, SIG_DFL);
  raise(SIGINT);
}

void print_usage();
int main(int argc, char **argv) {
  program_name = shift(argv, argc);

  struct sigaction action = {0};
  action.sa_handler = handle_sigint;
  sigemptyset(&action.sa_mask);
  action.sa_flags = 0;
  sigaction(SIGINT, &action, NULL); // Not needed to run but would be nice if it worked out
  signal(SIGPIPE, SIG_IGN);

  while (argc) {
    const char *arg = shift(argv, argc);

    if (strcmp(arg, "-h") == 0 || strcmp(arg, "--help") == 0) {
      print_usage();
      return 0;
    }

    if (strcmp(arg, "-backlog") == 0) {
      if (argc == 0) {
        log_error("Missing to provide backlog number");
        return 1;
      }

      arg = shift(argv, argc);
      server.backlog_n = (int)strtol(arg, NULL, 0);
      if (server.backlog_n <= 0) server.backlog_n = 10;
      continue;
    }

    if (strcmp(arg, "-p") == 0) {
      if (argc == 0) {
        log_error("Missing to provide port number");
        return 1;
      }

      arg = shift(argv, argc);
      server.port = (uint16_t)strtoul(arg, NULL, 0);
      if (server.port == 0) server.port = 8080;
      continue;
    }

    if (*arg == '-') {
      log_error("Unknown flag: %s", arg);
      print_usage();
      return 1;
    }

    if (server.base_dir == NULL) {
      server.base_dir = arg;
      continue;
    }

    nob_log(WARNING, "A path has already been provided, ignoring path: %s", arg);
  }
  int result = 0;

  if (server.base_dir == NULL) server.base_dir = ".";

  server.fd = socket(AF_INET, SOCK_STREAM, 0);
  if (server.fd < 0) {
    log_error("socket(3p) failed: %s", strerror(errno));
    return_defer(1);
  }

  {
    int opt = 1;
    if (setsockopt(server.fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt))) {
      log_error("setsockopt(3p) failed: %s", strerror(errno));
      return_defer(1);
    }
  }

  struct sockaddr_in address = {0};
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = INADDR_ANY;
  address.sin_port = htons(server.port);
  if (bind(server.fd, (const struct sockaddr *)&address, sizeof(address)) < 0) {
    log_error("bind(3p) failed: %s", strerror(errno));
    return_defer(1);
  }

  if (listen(server.fd, server.backlog_n) < 0) {
    log_error("listen(3p) failed: %s", strerror(errno));
    return_defer(1);
  }

  int flags = fcntl(server.fd, F_GETFL);
  if (flags < 0) {
    log_error("fcntl(3p) Failed to get fd flags: %s", strerror(errno));
    return_defer(1);
  }
  if (fcntl(server.fd, F_SETFL, flags | O_NONBLOCK) < 0) {
    log_error("fcntl(3p) Failed to set fd flags with O_NONBLOCK: %s", strerror(errno));
    return_defer(1);
  }

  printf(
    "===================================\n"
    "| Server Running: http://localhost:%u\n"
    "---------------------------------\n"
    "| Directory: %s\n"
    "| Port: %u\n"
    "| Backlog: %d\n"
    "===================================\n",
    server.port,
    server.base_dir,
    server.port,
    server.backlog_n
  );

  struct timespec remainder_time, request_time = { .tv_nsec = 1000000 }; // 1ms sleep time
  for (;;) {
    Clients *clients = &server.clients;
    ciao_da_foreach(Client, c, clients) {
      if (c->closed) continue;
      handle_client(c);
      if (c->closed) clients->closed++;
    }

    int fd = accept(server.fd, NULL, NULL);
    if (fd < 0) {
      if (errno != EAGAIN && errno != EWOULDBLOCK) {
        log_error("Failed to accept connection: %s", strerror(errno));
        continue;
      }
      // Only sleep if got nothing to do
      if (clients->len == 0 || clients->len == clients->closed) {
        nanosleep(&request_time, &remainder_time);
      }
      continue;
    }

    Client client = { .fd = fd };
    if (server.clients.closed > 0) {
      ciao_da_foreach(Client, c, clients) {
        if (!c->closed) continue;
        *c = client;
        clients->closed -= 1;
        break;
      }
      
      continue;
    }

    if (!ciao_da_push(clients, client)) {
      for (size_t i = 0; true; i = (i + 1) % clients->len) {
        Client *c = &clients->items[i];
        handle_client(c);
        if (!c->closed) continue;
        // This client has now closed so we can safely override it
        // we don't increment clients->closed since we are instantly
        // putting the closed connection into use
        *c = client;
        break;
      }
    }
  }

defer:
  server_close();
  return result;
}

void print_usage() {
  printf("Usage: %s [FLAGS]\n", program_name);
  printf("FLAGS:\n");
  printf("    -h,--help        ----    Print this help message\n");
}

#define MIME_TYPE_IMPLEMENTATION
#include "mime-type.h"

#define MFILE_IMPLEMENTATION
#include "mfiles.h"

#define CIAO_IMPLEMENTATION
#include "ciao_ca.h"

#define NOB_IMPLEMENTATION
#include "nob.h"

