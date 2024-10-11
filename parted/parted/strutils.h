#ifndef STR_UTILS_H_
#define STR_UTILS_H_

#include <stddef.h>

size_t strnlen(const char *s, size_t maxlen);
char *stpcpy(char *__restrict__ dest, const char *__restrict__ src);

#endif
