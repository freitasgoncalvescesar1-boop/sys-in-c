#include "kmem.h"

#define ALIGN8(x) (((x) + 7) & ~7)

typedef struct kmem_block {
    size_t size;
    int is_free;
    struct kmem_block *next;
    struct kmem_block *prev;
} kmem_block_t;

#define BLOCK_HEADER_SIZE ALIGN8(sizeof(kmem_block_t))

static kmem_block_t *head_block = NULL;
static size_t total_heap_bytes = 0;

static void (*kmem_lock_hook)(void) = NULL;
static void (*kmem_unlock_hook)(void) = NULL;

static inline void lock_heap(void) {
    if (kmem_lock_hook) kmem_lock_hook();
}

static inline void unlock_heap(void) {
    if (kmem_unlock_hook) kmem_unlock_hook();
}

/* Cópia acelerada de memória via instrução nativa de hardware */
void *kmemcpy(void *dest, const void *src, size_t n) {
    uint8_t *d = (uint8_t *)dest;
    const uint8_t *s = (const uint8_t *)src;

#if defined(__x86_64__) || defined(__i386__)
    if (n >= 4) {
        size_t dwords = n / 4;
        __asm__ __volatile__ (
            "cld\n"
            "rep movsl\n"
            : "+D"(d), "+S"(s), "+c"(dwords)
            : : "memory"
        );
        n %= 4;
    }
#else
    while (n >= 4 && (((uintptr_t)d & 3) == 0) && (((uintptr_t)s & 3) == 0)) {
        *(uint32_t *)d = *(const uint32_t *)s;
        d += 4;
        s += 4;
        n -= 4;
    }
#endif

    while (n--) {
        *d++ = *s++;
    }
    return dest;
}

/* Preenchimento acelerado de memória */
void *kmemset(void *s, int c, size_t n) {
    uint8_t *p = (uint8_t *)s;
    uint8_t byte_val = (uint8_t)c;

#if defined(__x86_64__) || defined(__i386__)
    if (n >= 4) {
        uint32_t val32 = ((uint32_t)byte_val << 24) | ((uint32_t)byte_val << 16) | ((uint32_t)byte_val << 8) | byte_val;
        size_t dwords = n / 4;
        __asm__ __volatile__ (
            "cld\n"
            "rep stosl\n"
            : "+D"(p), "+c"(dwords)
            : "a"(val32)
            : "memory"
        );
        n %= 4;
    }
#else
    uint32_t val32 = ((uint32_t)byte_val << 24) | ((uint32_t)byte_val << 16) | ((uint32_t)byte_val << 8) | byte_val;
    while (n >= 4 && (((uintptr_t)p & 3) == 0)) {
        *(uint32_t *)p = val32;
        p += 4;
        n -= 4;
    }
#endif

    while (n--) {
        *p++ = byte_val;
    }
    return s;
}

void kmem_set_locks(void (*lock_fn)(void), void (*unlock_fn)(void)) {
    kmem_lock_hook = lock_fn;
    kmem_unlock_hook = unlock_fn;
}

void kmem_init(void *heap_start, size_t heap_size) {
    if (!heap_start || heap_size < (BLOCK_HEADER_SIZE + 32)) return;

    total_heap_bytes = heap_size;
    head_block = (kmem_block_t *)heap_start;
    head_block->size = ALIGN8(heap_size - BLOCK_HEADER_SIZE);
    head_block->is_free = 1;
    head_block->next = NULL;
    head_block->prev = NULL;
}

int kmem_add_region(void *heap_start, size_t heap_size) {
    if (!heap_start || heap_size < (BLOCK_HEADER_SIZE + 32)) return -1;

    lock_heap();
    kmem_block_t *new_region = (kmem_block_t *)heap_start;
    new_region->size = ALIGN8(heap_size - BLOCK_HEADER_SIZE);
    new_region->is_free = 1;
    new_region->prev = NULL;

    if (!head_block) {
        head_block = new_region;
        new_region->next = NULL;
    } else {
        kmem_block_t *curr = head_block;
        while (curr->next) curr = curr->next;
        curr->next = new_region;
        new_region->prev = curr;
    }

    total_heap_bytes += heap_size;
    unlock_heap();
    return 0;
}

void *kmalloc(size_t size) {
    if (size == 0 || !head_block) return NULL;

    lock_heap();
    size_t alloc_size = ALIGN8(size);
    size_t required_space = alloc_size + sizeof(kmem_block_t *);
    kmem_block_t *curr = head_block;

    while (curr) {
        if (curr->is_free && curr->size >= required_space) {
            if (curr->size >= required_space + BLOCK_HEADER_SIZE + 32) {
                kmem_block_t *new_block = (kmem_block_t *)((uint8_t *)curr + BLOCK_HEADER_SIZE + required_space);
                new_block->size = curr->size - required_space - BLOCK_HEADER_SIZE;
                new_block->is_free = 1;
                new_block->next = curr->next;
                new_block->prev = curr;

                if (curr->next) curr->next->prev = new_block;
                curr->next = new_block;
                curr->size = required_space;
            }

            curr->is_free = 0;
            uint8_t *payload = (uint8_t *)curr + BLOCK_HEADER_SIZE + sizeof(kmem_block_t *);
            ((kmem_block_t **)payload)[-1] = curr;

            unlock_heap();
            return (void *)payload;
        }
        curr = curr->next;
    }

    unlock_heap();
    return NULL;
}

void *kzalloc(size_t size) {
    void *ptr = kmalloc(size);
    if (ptr) kmemset(ptr, 0, size);
    return ptr;
}

void *kmalloc_aligned(size_t size, size_t alignment) {
    if (size == 0 || alignment == 0) return NULL;
    if ((alignment & (alignment - 1)) != 0) return NULL;

    lock_heap();
    size_t alloc_size = ALIGN8(size) + alignment + sizeof(kmem_block_t *) + BLOCK_HEADER_SIZE;
    unlock_heap();

    uint8_t *raw_ptr = (uint8_t *)kmalloc(alloc_size);
    if (!raw_ptr) return NULL;

    lock_heap();
    uintptr_t raw_addr = (uintptr_t)raw_ptr;
    uintptr_t aligned_addr = (raw_addr + sizeof(kmem_block_t *) + alignment - 1) & ~(alignment - 1);

    uint8_t *aligned_ptr = (uint8_t *)aligned_addr;
    kmem_block_t *original_block = ((kmem_block_t **)raw_ptr)[-1];
    ((kmem_block_t **)aligned_ptr)[-1] = original_block;

    unlock_heap();
    return (void *)aligned_ptr;
}

void kfree(void *ptr) {
    if (!ptr) return;

    lock_heap();
    kmem_block_t *block = ((kmem_block_t **)ptr)[-1];
    if (!block) {
        unlock_heap();
        return;
    }

    block->is_free = 1;

    if (block->next && block->next->is_free) {
        block->size += BLOCK_HEADER_SIZE + block->next->size;
        block->next = block->next->next;
        if (block->next) block->next->prev = block;
    }

    if (block->prev && block->prev->is_free) {
        block->prev->size += BLOCK_HEADER_SIZE + block->size;
        block->prev->next = block->next;
        if (block->next) block->next->prev = block->prev;
    }

    unlock_heap();
}

void *krealloc(void *ptr, size_t size) {
    if (!ptr) return kmalloc(size);
    if (size == 0) {
        kfree(ptr);
        return NULL;
    }

    lock_heap();
    kmem_block_t *block = ((kmem_block_t **)ptr)[-1];
    size_t old_size = block->size;
    unlock_heap();

    if (old_size >= size) return ptr;

    void *new_ptr = kmalloc(size);
    if (new_ptr) {
        kmemcpy(new_ptr, ptr, old_size);
        kfree(ptr);
    }
    return new_ptr;
}

size_t kmem_get_free_bytes(void) {
    size_t free_bytes = 0;
    lock_heap();
    kmem_block_t *curr = head_block;
    while (curr) {
        if (curr->is_free) free_bytes += curr->size;
        curr = curr->next;
    }
    unlock_heap();
    return free_bytes;
}

size_t kmem_get_used_bytes(void) {
    size_t used_bytes = 0;
    lock_heap();
    kmem_block_t *curr = head_block;
    while (curr) {
        if (!curr->is_free) used_bytes += curr->size;
        curr = curr->next;
    }
    unlock_heap();
    return used_bytes;
}

size_t kmem_get_total_bytes(void) {
    return total_heap_bytes;
}
