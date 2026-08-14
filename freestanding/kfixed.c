#include "kfixed.h"

/* Fixed-point addition */
fp32_t fp32_add(fp32_t a, fp32_t b) {
    return a + b;
}

/* Fixed-point subtraction */
fp32_t fp32_sub(fp32_t a, fp32_t b) {
    return a - b;
}

/* Fixed-point multiplication */
fp32_t fp32_mul(fp32_t a, fp32_t b) {
    int64_t res = ((int64_t)a * (int64_t)b) >> FP32_SHIFT;
    return (fp32_t)res;
}

/* Fixed-point division */
fp32_t fp32_div(fp32_t a, fp32_t b) {
    if (b == 0) return 0;
    int64_t res = (((int64_t)a) << FP32_SHIFT) / b;
    return (fp32_t)res;
}

/* Fixed-point absolute value */
fp32_t fp32_abs(fp32_t a) {
    return (a < 0) ? -a : a;
}

/* Fixed-point square root */
fp32_t fp32_sqrt(fp32_t a) {
    if (a <= 0) return 0;
    fp32_t root = a;
    for (int i = 0; i < 8; i++) {
        root = (root + fp32_div(a, root)) >> 1;
    }
    return root;
}

/* Fixed-point sine approximation */
fp32_t fp32_sin(fp32_t rad) {
    fp32_t pi = 205887;
    fp32_t two_pi = 411774;

    while (rad > pi) rad -= two_pi;
    while (rad < -pi) rad += two_pi;

    fp32_t x2 = fp32_mul(rad, rad);
    fp32_t x3 = fp32_mul(rad, x2);
    fp32_t x5 = fp32_mul(x3, x2);

    fp32_t term3 = fp32_div(x3, fp32_from_int(6));
    fp32_t term5 = fp32_div(x5, fp32_from_int(120));

    return fp32_sub(fp32_add(rad, term5), term3);
}

/* Fixed-point cosine approximation */
fp32_t fp32_cos(fp32_t rad) {
    fp32_t half_pi = 102943;
    return fp32_sin(rad + half_pi);
}

/* Fixed-point string conversion (freestanding) */
size_t fp32_to_str(fp32_t fp, char *buf, size_t max_len, int decimals) {
    if (max_len < 10) return 0;

    size_t pos = 0;
    if (fp < 0) {
        buf[pos++] = '-';
        fp = -fp;
    }

    int int_part = fp32_to_int(fp);
    uint32_t frac_part = (uint32_t)(fp & (FP32_ONE - 1));

    char temp[16];
    int temp_pos = 0;
    if (int_part == 0) {
        temp[temp_pos++] = '0';
    } else {
        int n = int_part;
        while (n > 0) {
            temp[temp_pos++] = '0' + (n % 10);
            n /= 10;
        }
    }
    for (int i = temp_pos - 1; i >= 0 && pos < max_len - 1; i--) {
        buf[pos++] = temp[i];
    }

    if (decimals > 0 && pos < max_len - 2) {
        buf[pos++] = '.';
        for (int i = 0; i < decimals && pos < max_len - 1; i++) {
            frac_part *= 10;
            int digit = frac_part >> FP32_SHIFT;
            buf[pos++] = '0' + (digit % 10);
            frac_part &= (FP32_ONE - 1);
        }
    }
    buf[pos] = '\0';
    return pos;
}
