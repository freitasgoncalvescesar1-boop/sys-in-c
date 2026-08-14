#ifndef KFIXED_H
#define KFIXED_H

#include <stdint.h>
#include <stddef.h>

/* 16.16 Fixed-point type definition */
typedef int32_t fp32_t;

#define FP32_SHIFT 16
#define FP32_ONE   (1 << FP32_SHIFT)
#define FP32_HALF  (1 << (FP32_SHIFT - 1))

/* Conversion utilities */
static inline fp32_t fp32_from_int(int i) {
    return (fp32_t)(i << FP32_SHIFT);
}

static inline int fp32_to_int(fp32_t fp) {
    return (int)(fp >> FP32_SHIFT);
}

/* Basic arithmetic operations */
fp32_t fp32_add(fp32_t a, fp32_t b);
fp32_t fp32_sub(fp32_t a, fp32_t b);
fp32_t fp32_mul(fp32_t a, fp32_t b);
fp32_t fp32_div(fp32_t a, fp32_t b);
fp32_t fp32_abs(fp32_t a);
fp32_t fp32_sqrt(fp32_t a);
fp32_t fp32_sin(fp32_t rad);
fp32_t fp32_cos(fp32_t rad);

/* String formatting helper (freestanding) */
size_t fp32_to_str(fp32_t fp, char *buf, size_t max_len, int decimals);

#endif
