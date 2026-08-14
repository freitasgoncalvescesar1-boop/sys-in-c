#ifndef KPRINTF_H
#define KPRINTF_H

#include <stddef.h>
#include <stdint.h>
#include <stdarg.h>

/* Output character hook signature */
typedef void (*kputchar_fn_t)(char c);

/* Output hook assignment */
void kset_putchar(kputchar_fn_t putchar_fn);

/* Formatted stream printer */
int kprintf(const char *fmt, ...);

/* Formatted variadic stream printer */
int kvprintf(const char *fmt, va_list args);

/* Formatted memory buffer printer */
int ksnprintf(char *buf, size_t max_len, const char *fmt, ...);

/* Formatted variadic memory buffer printer */
int kvsnprintf(char *buf, size_t max_len, const char *fmt, va_list args);

#endif
