#include "strutils.h"

size_t strnlen(const char *s, size_t maxlen) {
  size_t i;

  for (i = 0; i < maxlen; ++i)
    if (s[i] == '\0')
      break;
  return i;
}

char *stpcpy(char *__restrict__ dest, const char *__restrict__ src) {
  while ((*dest++ = *src++) != '\0') {
  }
  return --dest;
}
