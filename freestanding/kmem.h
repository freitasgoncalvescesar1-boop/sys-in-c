#ifndef KMEM_H
#define KMEM_H

#include <stddef.h>
#include <stdint.h>

/* Primary heap initialization */
void kmem_init(void *heap_start, size_t heap_size);

/* Secondary heap region attachment */
int kmem_add_region(void *heap_start, size_t heap_size);

/* Multicore concurrency lock hooks */
void kmem_set_locks(void (*lock_fn)(void), void (*unlock_fn)(void));

/* Memory allocators */
void *kmalloc(size_t size);
void *kzalloc(size_t size);
void *kmalloc_aligned(size_t size, size_t alignment);
void *krealloc(void *ptr, size_t size);
void kfree(void *ptr);

/* Standard aliases */
#define malloc(sz)                kmalloc(sz)
#define calloc(num, sz)          kzalloc((num) * (sz))
#define realloc(ptr, sz)         krealloc(ptr, sz)
#define free(ptr)                kfree(ptr)
#define malloc_aligned(sz, align) kmalloc_aligned(sz, align)

/* Freestanding byte utilities */
void *kmemcpy(void *dest, const void *src, size_t n);
void *kmemset(void *s, int c, size_t n);

/* Heap metrics */
size_t kmem_get_free_bytes(void);
size_t kmem_get_used_bytes(void);
size_t kmem_get_total_bytes(void);

#endif
