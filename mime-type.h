#ifndef _MIME_TYPE_H
#define _MIME_TYPE_H

#include <string.h>
#include <stdlib.h>

static const char *MIME_TYPE_TABLE[][2] = {
  // Web Assets
  { "html",  "text/html; charset=utf8" },
  { "htm",   "text/html; charset=utf8" },
  { "css",   "text/css; charset=utf8" },
  { "js",    "text/javascript; charset=utf8" },
  { "mjs",   "text/javascript; charset=utf8" },
  { "wasm",  "application/wasm" },

  // Data & Text
  { "json",  "application/json; charset=utf8" },
  { "txt",   "text/plain; charset=utf8" },
  { "csv",   "text/csv; charset=utf8" },
  { "xml",   "application/xml; charset=utf8" },
  { "yaml",  "application/yaml; charset=utf8" },
  { "yml",   "application/yaml; charset=utf8" },

  // Images
  { "png",   "image/png" },
  { "jpg",   "image/jpeg" },
  { "jpeg",  "image/jpeg" },
  { "webp",  "image/webp" },
  { "svg",   "image/svg+xml" },
  { "gif",   "image/gif" },
  { "ico",   "image/x-icon" },
  { "icon",  "image/x-icon" },

  // Fonts
  { "woff2", "font/woff2" },
  { "woff",  "font/woff" },
  { "ttf",   "font/ttf" },

  // Documents
  { "pdf",   "application/pdf" },
  { "docx",  "application/vnd.openxmlformats-officedocument.wordprocessingml.document" },
  { "xlsx",  "application/vnd.openxmlformats-officedocument.spreadsheetml.sheet" },

  // Archives
  { "zip",   "application/zip" },
  { "gz",    "application/gzip" },

  // Audio & Video
  { "mp3",   "audio/mpeg" },
  { "wav",   "audio/wav" },
  { "mp4",   "video/mp4" },
  { "webm",  "video/webm" }
};

const size_t MIME_TYPE_TABLE_SIZE = ARRAY_LEN(MIME_TYPE_TABLE);


const char *mime_type_get_content_type_for_file_ext(const char *file_ext);

#endif// _MIME_TYPE_H

#ifdef MIME_TYPE_IMPLEMENTATION

const char *mime_type_get_content_type_for_file_ext(const char *file_ext) {
  const char *content_type = "application/octet-stream";
  if (!file_ext || *file_ext == '\0') return content_type;

  for (size_t i = 0; i < MIME_TYPE_TABLE_SIZE; ++i) {
    const char **entry = MIME_TYPE_TABLE[i];
    if (!strcmp(file_ext, entry[0]) == 0) continue;
    content_type = entry[1];
    break;
  }

  return content_type;
}

#endif// MIME_TYPE_IMPL
