#ifndef KSPINLOCK_H
#define KSPINLOCK_H

#include <stdint.h>

/* Bare-metal spinlock lock type */
typedef volatile uint32_t kspinlock_t;

/* Spinlock initialization */
void kspinlock_init(kspinlock_t *lock);

/* Spinlock acquisition */
void kspinlock_lock(kspinlock_t *lock);

/* Spinlock non-blocking acquisition attempt */
int kspinlock_trylock(kspinlock_t *lock);

/* Spinlock release */
void kspinlock_unlock(kspinlock_t *lock);

/* Atomic integer addition */
uint32_t katomic_add(volatile uint32_t *ptr, uint32_t val);

/* Atomic integer subtraction */
uint32_t katomic_sub(volatile uint32_t *ptr, uint32_t val);

#endif
