#include "kspinlock.h"

/* Spinlock initialization */
void kspinlock_init(kspinlock_t *lock) {
    if (lock) *lock = 0;
}

/* Spinlock acquisition */
void kspinlock_lock(kspinlock_t *lock) {
    if (!lock) return;
    while (__atomic_test_and_set(lock, __ATOMIC_ACQUIRE)) {
#if defined(__x86_64__) || defined(__i386__)
        __asm__ __volatile__("pause");
#endif
    }
}

/* Spinlock non-blocking acquisition attempt */
int kspinlock_trylock(kspinlock_t *lock) {
    if (!lock) return 0;
    return !__atomic_test_and_set(lock, __ATOMIC_ACQUIRE);
}

/* Spinlock release */
void kspinlock_unlock(kspinlock_t *lock) {
    if (!lock) return;
    __atomic_clear(lock, __ATOMIC_RELEASE);
}

/* Atomic integer addition */
uint32_t katomic_add(volatile uint32_t *ptr, uint32_t val) {
    return __atomic_fetch_add(ptr, val, __ATOMIC_SEQ_CST);
}

/* Atomic integer subtraction */
uint32_t katomic_sub(volatile uint32_t *ptr, uint32_t val) {
    return __atomic_fetch_sub(ptr, val, __ATOMIC_SEQ_CST);
}
