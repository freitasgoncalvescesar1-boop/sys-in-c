#include "kstring.h"

/* String length */
size_t kstrlen(const char *str) {
    if (!str) return 0;
    size_t len = 0;
    while (str[len] != '\0') len++;
    return len;
}

/* String comparison */
int kstrcmp(const char *s1, const char *s2) {
    while (*s1 && (*s1 == *s2)) {
        s1++;
        s2++;
    }
    return *(const unsigned char *)s1 - *(const unsigned char *)s2;
}

/* Length-bounded string comparison */
int kstrncmp(const char *s1, const char *s2, size_t n) {
    while (n && *s1 && (*s1 == *s2)) {
        s1++;
        s2++;
        n--;
    }
    if (n == 0) return 0;
    return *(const unsigned char *)s1 - *(const unsigned char *)s2;
}

/* String copy */
char *kstrcpy(char *dest, const char *src) {
    char *d = dest;
    while ((*d++ = *src++));
    return dest;
}

/* Length-bounded string copy */
char *kstrncpy(char *dest, const char *src, size_t n) {
    size_t i;
    for (i = 0; i < n && src[i] != '\0'; i++) {
        dest[i] = src[i];
    }
    for (; i < n; i++) {
        dest[i] = '\0';
    }
    return dest;
}

/* Character search */
char *kstrchr(const char *str, int c) {
    while (*str != (char)c) {
        if (!*str) return NULL;
        str++;
    }
    return (char *)str;
}

/* Integer to string conversion */
size_t kitoa(int64_t val, char *buf, int base, int uppercase) {
    static const char digits_lower[] = "0123456789abcdef";
    static const char digits_upper[] = "0123456789ABCDEF";
    const char *digits = uppercase ? digits_upper : digits_lower;

    if (base < 2 || base > 16) return 0;

    char temp[68];
    size_t temp_pos = 0;
    int is_neg = 0;

    if (val < 0 && base == 10) {
        is_neg = 1;
        val = -val;
    }

    uint64_t uval = (uint64_t)val;
    if (uval == 0) {
        temp[temp_pos++] = '0';
    } else {
        while (uval > 0) {
            temp[temp_pos++] = digits[uval % base];
            uval /= base;
        }
    }

    size_t pos = 0;
    if (is_neg) buf[pos++] = '-';

    for (int i = (int)temp_pos - 1; i >= 0; i--) {
        buf[pos++] = temp[i];
    }
    buf[pos] = '\0';
    return pos;
}

/* String to integer parsing */
int64_t katoi(const char *str) {
    if (!str) return 0;
    int64_t res = 0;
    int sign = 1;

    while (*str == ' ' || *str == '\t' || *str == '\n') str++;

    if (*str == '-') {
        sign = -1;
        str++;
    } else if (*str == '+') {
        str++;
    }

    while (*str >= '0' && *str <= '9') {
        res = res * 10 + (*str - '0');
        str++;
    }

    return res * sign;
}

/* String to unsigned long with base parsing */
uint64_t kstrtoul(const char *str, char **endptr, int base) {
    if (!str) return 0;
    uint64_t acc = 0;

    while (*str == ' ' || *str == '\t' || *str == '\n') str++;

    if (base == 0) {
        if (str[0] == '0') {
            if (str[1] == 'x' || str[1] == 'X') {
                base = 16;
                str += 2;
            } else {
                base = 8;
                str += 1;
            }
        } else {
            base = 10;
        }
    } else if (base == 16 && str[0] == '0' && (str[1] == 'x' || str[1] == 'X')) {
        str += 2;
    }

    while (*str) {
        int c = *str;
        int digit = -1;
        if (c >= '0' && c <= '9') digit = c - '0';
        else if (c >= 'a' && c <= 'z') digit = c - 'a' + 10;
        else if (c >= 'A' && c <= 'Z') digit = c - 'A' + 10;

        if (digit < 0 || digit >= base) break;
        acc = acc * base + digit;
        str++;
    }

    if (endptr) *endptr = (char *)str;
    return acc;
}
