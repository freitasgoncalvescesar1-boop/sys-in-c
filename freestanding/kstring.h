#ifndef KSTRING_H
#define KSTRING_H

#include <stddef.h>
#include <stdint.h>

/* String length */
size_t kstrlen(const char *str);

/* String comparison */
int kstrcmp(const char *s1, const char *s2);
int kstrncmp(const char *s1, const char *s2, size_t n);

/* String copy */
char *kstrcpy(char *dest, const char *src);
char *kstrncpy(char *dest, const char *src, size_t n);

/* String concatenation */
char *kstrcat(char *dest, const char *src);
char *kstrncat(char *dest, const char *src, size_t n);

/* Character and substring search */
char *kstrchr(const char *str, int c);
char *kstrstr(const char *haystack, const char *needle);

/* Memory operations */
int kmemcmp(const void *s1, const void *s2, size_t n);
void *kmemmove(void *dest, const void *src, size_t n);

/* Integer to ASCII string conversion */
size_t kitoa(int64_t val, char *buf, int base, int uppercase);

/* ASCII string to integer parsing */
int64_t katoi(const char *str);

/* String to unsigned long with base parsing (hex/octal/dec) */
uint64_t kstrtoul(const char *str, char **endptr, int base);

/* Macros de compatibilidade para código freestanding */
#define strcmp(s1, s2)       kstrcmp(s1, s2)
#define strncmp(s1, s2, n)   kstrncmp(s1, s2, n)
#define strlen(s)            kstrlen(s)
#define strcpy(d, s)         kstrcpy(d, s)
#define strncpy(d, s, n)     kstrncpy(d, s, n)
#define strcat(d, s)         kstrcat(d, s)
#define strncat(d, s, n)     kstrncat(d, s, n)
#define strchr(s, c)         kstrchr(s, c)
#define strstr(h, n)         kstrstr(h, n)
#define atoi(s)              katoi(s)
#define memcmp(s1, s2, n)    kmemcmp(s1, s2, n)
#define memmove(d, s, n)     kmemmove(d, s, n)

#endif
