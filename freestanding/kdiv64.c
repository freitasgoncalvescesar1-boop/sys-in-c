#include <stdint.h>

/* =========================================================================
 *  ROTINAS FREESTANDING DE DIVISÃO E RESTO DE 64-BIT PARA x86 32-BIT
 *  Resolve __divdi3, __udivmoddi4, __udivdi3 e __umoddi3 em hardware de 32 bits
 * ========================================================================= */

uint64_t __udivmoddi4(uint64_t num, uint64_t den, uint64_t *rem_p) {
    if (den == 0) {
        if (rem_p) *rem_p = 0;
        return 0;
    }

    uint64_t quot = 0;
    uint64_t rem = 0;

    for (int i = 63; i >= 0; i--) {
        rem = (rem << 1) | ((num >> i) & 1);
        if (rem >= den) {
            rem -= den;
            quot |= (1ULL << i);
        }
    }

    if (rem_p) *rem_p = rem;
    return quot;
}

int64_t __divdi3(int64_t a, int64_t b) {
    int neg = 0;
    uint64_t ua, ub;

    if (a < 0) {
        ua = (uint64_t)(-(a + 1)) + 1;
        neg = !neg;
    } else {
        ua = (uint64_t)a;
    }

    if (b < 0) {
        ub = (uint64_t)(-(b + 1)) + 1;
        neg = !neg;
    } else {
        ub = (uint64_t)b;
    }

    uint64_t res = __udivmoddi4(ua, ub, (void*)0);
    return neg ? -(int64_t)res : (int64_t)res;
}

int64_t __moddi3(int64_t a, int64_t b) {
    int neg = (a < 0);
    uint64_t ua = (a < 0) ? ((uint64_t)(-(a + 1)) + 1) : (uint64_t)a;
    uint64_t ub = (b < 0) ? ((uint64_t)(-(b + 1)) + 1) : (uint64_t)b;
    uint64_t rem = 0;

    __udivmoddi4(ua, ub, &rem);
    return neg ? -(int64_t)rem : (int64_t)rem;
}

uint64_t __udivdi3(uint64_t a, uint64_t b) {
    return __udivmoddi4(a, b, (void*)0);
}

uint64_t __umoddi3(uint64_t a, uint64_t b) {
    uint64_t rem = 0;
    __udivmoddi4(a, b, &rem);
    return rem;
}
