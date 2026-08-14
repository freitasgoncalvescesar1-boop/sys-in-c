#include "kfixed.h"
#include "kmem.h"
#include <stdio.h>

int main(void) {
    /* Output redirection hook - Redirect output here */
    /* Generic output stream */

    printf("==========================================\n");
    printf("[ Freestanding Fixed-Point Calculator (kcalc) ]\n");
    printf("==========================================\n");

    fp32_t a = fp32_from_int(10);
    fp32_t b = fp32_from_int(3);

    fp32_t sum = fp32_add(a, b);
    fp32_t diff = fp32_sub(a, b);
    fp32_t prod = fp32_mul(a, b);
    fp32_t quot = fp32_div(a, b);
    fp32_t sqrt_val = fp32_sqrt(fp32_from_int(64));
    fp32_t sin_val = fp32_sin(102943);

    char str_buf[32];

    fp32_to_str(sum, str_buf, sizeof(str_buf), 2);
    printf("  • 10 + 3 = %s\n", str_buf);

    fp32_to_str(diff, str_buf, sizeof(str_buf), 2);
    printf("  • 10 - 3 = %s\n", str_buf);

    fp32_to_str(prod, str_buf, sizeof(str_buf), 2);
    printf("  • 10 * 3 = %s\n", str_buf);

    fp32_to_str(quot, str_buf, sizeof(str_buf), 4);
    printf("  • 10 / 3 = %s\n", str_buf);

    fp32_to_str(sqrt_val, str_buf, sizeof(str_buf), 2);
    printf("  • sqrt(64) = %s\n", str_buf);

    fp32_to_str(sin_val, str_buf, sizeof(str_buf), 4);
    printf("  • sin(pi/2) = %s\n", str_buf);

    printf("==========================================\n");
    return 0;
}
