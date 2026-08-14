#include "kprintf.h"

/* Active character output hook */
static kputchar_fn_t kputchar_hook = NULL;

/* Hook assignment */
void kset_putchar(kputchar_fn_t putchar_fn) {
    kputchar_hook = putchar_fn;
}

/* Integer to string conversion buffer */
static size_t fmt_number(uint64_t val, int base, int is_signed, int uppercase, char *out_buf) {
    static const char digits_lower[] = "0123456789abcdef";
    static const char digits_upper[] = "0123456789ABCDEF";
    const char *digits = uppercase ? digits_upper : digits_lower;

    char temp[68];
    size_t temp_pos = 0;

    int is_neg = 0;
    if (is_signed && (int64_t)val < 0) {
        is_neg = 1;
        val = (uint64_t)(-(int64_t)val);
    }

    if (val == 0) {
        temp[temp_pos++] = '0';
    } else {
        while (val > 0) {
            temp[temp_pos++] = digits[val % base];
            val /= base;
        }
    }

    size_t out_pos = 0;
    if (is_neg) out_buf[out_pos++] = '-';

    for (int i = (int)temp_pos - 1; i >= 0; i--) {
        out_buf[out_pos++] = temp[i];
    }
    out_buf[out_pos] = '\0';

    return out_pos;
}

/* Variadic memory buffer formatting */
int kvsnprintf(char *buf, size_t max_len, const char *fmt, va_list args) {
    if (!buf || max_len == 0) return 0;

    size_t pos = 0;

    for (size_t i = 0; fmt[i] != '\0' && pos < max_len - 1; i++) {
        if (fmt[i] != '%') {
            buf[pos++] = fmt[i];
            continue;
        }

        i++;
        if (fmt[i] == '\0') break;

        char num_buf[70];
        size_t num_len = 0;

        switch (fmt[i]) {
            case '%':
                buf[pos++] = '%';
                break;
            case 'c': {
                char c = (char)va_arg(args, int);
                buf[pos++] = c;
                break;
            }
            case 's': {
                const char *s = va_arg(args, const char *);
                if (!s) s = "(null)";
                while (*s && pos < max_len - 1) {
                    buf[pos++] = *s++;
                }
                break;
            }
            case 'd':
            case 'i': {
                int val = va_arg(args, int);
                num_len = fmt_number((uint64_t)(int64_t)val, 10, 1, 0, num_buf);
                for (size_t k = 0; k < num_len && pos < max_len - 1; k++) {
                    buf[pos++] = num_buf[k];
                }
                break;
            }
            case 'u': {
                unsigned int val = va_arg(args, unsigned int);
                num_len = fmt_number(val, 10, 0, 0, num_buf);
                for (size_t k = 0; k < num_len && pos < max_len - 1; k++) {
                    buf[pos++] = num_buf[k];
                }
                break;
            }
            case 'x': {
                unsigned int val = va_arg(args, unsigned int);
                num_len = fmt_number(val, 16, 0, 0, num_buf);
                for (size_t k = 0; k < num_len && pos < max_len - 1; k++) {
                    buf[pos++] = num_buf[k];
                }
                break;
            }
            case 'X': {
                unsigned int val = va_arg(args, unsigned int);
                num_len = fmt_number(val, 16, 0, 1, num_buf);
                for (size_t k = 0; k < num_len && pos < max_len - 1; k++) {
                    buf[pos++] = num_buf[k];
                }
                break;
            }
            case 'p': {
                uintptr_t val = (uintptr_t)va_arg(args, void *);
                if (pos < max_len - 2) {
                    buf[pos++] = '0';
                    buf[pos++] = 'x';
                }
                num_len = fmt_number(val, 16, 0, 0, num_buf);
                for (size_t k = 0; k < num_len && pos < max_len - 1; k++) {
                    buf[pos++] = num_buf[k];
                }
                break;
            }
            case 'b': {
                unsigned int val = va_arg(args, unsigned int);
                if (pos < max_len - 2) {
                    buf[pos++] = '0';
                    buf[pos++] = 'b';
                }
                num_len = fmt_number(val, 2, 0, 0, num_buf);
                for (size_t k = 0; k < num_len && pos < max_len - 1; k++) {
                    buf[pos++] = num_buf[k];
                }
                break;
            }
            default:
                buf[pos++] = fmt[i];
                break;
        }
    }

    buf[pos] = '\0';
    return (int)pos;
}

/* Memory buffer formatting */
int ksnprintf(char *buf, size_t max_len, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    int len = kvsnprintf(buf, max_len, fmt, args);
    va_end(args);
    return len;
}

/* Variadic stream printer */
int kvprintf(const char *fmt, va_list args) {
    char buf[1024];
    int len = kvsnprintf(buf, sizeof(buf), fmt, args);
    for (int i = 0; i < len; i++) {
        if (kputchar_hook) kputchar_hook(buf[i]);
    }
    return len;
}

/* Formatted stream printer */
int kprintf(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    int len = kvprintf(fmt, args);
    va_end(args);
    return len;
}
