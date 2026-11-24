#ifndef __MEMORY_FILE_H
#define __MEMORY_FILE_H

#include <stdbool.h>

#ifdef _WIN32
#    define WIN32_LEAN_AND_MEAN
#    include <windows.h>
#else
#    include <sys/mman.h>
#    include <sys/stat.h>
#    include <fcntl.h>
#    include <unistd.h>
#endif // _WIN32

typedef struct MF_File {
  const char *path;
  void *data;
  size_t size;
#ifdef _WIN32
  HANDLE h2fd;
  HANDLE h2map;
#else
  int fd;
#endif
} MF_File;

bool mf_read_file(const char *file_path, MF_File *outFile);
void mf_close_read(MF_File *file);

#endif // __MEMORY_FILE_H

#if defined(MFILE_IMPLEMENTATION) && !defined(__MFILE_IMPLEMENTATION)
#define MFILE_IMPLEMENTATION

bool mf_read_file(const char *file_path, MF_File *outFile) {
  bool result = true;
  MF_File file = { .path = file_path };

  #ifdef _WIN32
  wchar_t *path_w = NULL;
  int wide_len = MultiByteToWideChar(CP_UTF8, 0, path_utf8, -1, NULL, 0);
  if (wide_len <= 0) {
    fprintf(stderr, "Error: Invalid UTF-8 path.\n");
    result = false; goto defer;
  }
  path_w = (wchar_t*)_alloca(wide_len*sizeof(wchar_t));
  MultiByteToWideChar(CP_UTF8, 0, path_utf8, -1, path_w, wide_len);

  file.h2fd = CreateFileW(path_w, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
  if (file.h2fd == INVALID_HANDLE_VALUE) {
    fprintf(stderr, "Error: Could not open file %s (Win32 Error: %lu)\n", file_path, GetLastError());
    result = false; goto defer;
  }

  LARGE_INTEGER size;
  if (!GetFileSizeEx(file.h2fd, &size)) {
    fprintf(stderr, "Error: Could not get file size for %s\n", file_path);
    CloseHandle(file.h2fd);
    result = false; goto defer;
  }
  file.size = (size_t)size;

  file.h2map = CreateFileMapping(file.h2fd, NULL, PAGE_READONLY, 0, 0, NULL);
  if (!file.h2map) {
    fprintf(stderr, "Error: Could not create file mapping for %s\n", file_path);
    CloseHandle(file.h2fd);
    result = false; goto defer;
  }
  file.data = MapViewOfFile(file.h2map, FILE_MAP_READ, 0, 0, 0);
  if (!file.data) {
    fprintf(stderr, "Error: Could not map view of file %s\n", file_path);
    CloseHandle(file.h2map);
    CloseHandle(file.h2fd);
    result = false; goto defer;
  }
  #else
  file.fd = open(file_path, O_RDONLY);
  if (file.fd < 0) {
    fprintf(stderr, "Error: Could not open file %s\n", file_path);
    result = false; goto defer;
  }

  struct stat st;
  if (fstat(file.fd, &st) < 0) {
    fprintf(stderr, "Error: fstat failed for %s\n", file_path);
    close(file.fd);
    result = false; goto defer;
  }
  file.size = st.st_size;
  file.data = mmap(NULL, file.size, PROT_READ, MAP_PRIVATE, file.fd, 0);
  if (file.data == MAP_FAILED) {
    fprintf(stderr, "Error: mmap failed for %s\n", file_path);
    close(file.fd);
    result = false; goto defer;
  }

  #endif // _WIN32

defer:
  if (result) *outFile = file;
  return result;
}

void mf_close_read(MF_File *file) {
  if (!file->data) return;

  #ifdef _WIN32
  UnmapViewOfFile(file->data);
  CloseHandle(file->h2map);
  CloseHandle(file->h2fd);
  #else
  munmap(file->data, file->size);
  close(file->fd);
  #endif // _WIN32

  memset(file, 0, sizeof(*file));
}

#endif // MFILE_IMPLEMENTATION
