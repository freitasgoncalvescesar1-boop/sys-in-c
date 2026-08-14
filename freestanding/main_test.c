#include "kmem.h"
#include "kfixed.h"
#include "kprintf.h"
#include "kgfx.h"
#include "kringbuf.h"
#include "kstring.h"
#include "klist.h"
#include "kspinlock.h"
#include "kvfs.h"
#include <stdio.h>

static uint8_t kernel_ram_region[1024 * 1024];
static uint32_t video_buffer[320 * 200];
static uint8_t ring_storage[64];

typedef struct {
    int task_id;
    klist_node_t node;
} test_task_t;

static klist_node_t task_list;
static kspinlock_t smp_lock;

static void test_putchar(char c) {
    putchar(c);
}

static void vfs_ls_cb(const char *name, size_t size, uint16_t mode) {
    (void)mode;
    kprintf("    • %-16s : %u bytes\n", name, (unsigned int)size);
}

int main(void) {
    kset_putchar(test_putchar);

    kprintf("==========================================\n");
    kprintf("[ OS Kernel Freestanding Subsystem Test ]\n");
    kprintf("==========================================\n");

    /* 1. Memory Test (kmem) */
    kmem_init(kernel_ram_region, sizeof(kernel_ram_region));
    uint8_t *page_table = (uint8_t *)kmalloc_aligned(4096, 4096);
    kprintf("  • kmem: Initialized Heap | Page Table: %p\n", (void*)page_table);

    /* 2. Math Test (kfixed) */
    fp32_t a = fp32_from_int(10);
    fp32_t b = fp32_from_int(3);
    fp32_t div_res = fp32_div(a, b);
    char math_buf[32];
    fp32_to_str(div_res, math_buf, sizeof(math_buf), 4);
    kprintf("  • kfixed: Fixed-point 10 / 3 = %s\n", math_buf);

    /* 3. Intrusive List Test (klist) */
    klist_init(&task_list);
    test_task_t task1 = { .task_id = 42 };
    klist_add_tail(&task_list, &task1.node);
    test_task_t *retrieved = klist_entry(task_list.next, test_task_t, node);
    kprintf("  • klist: Intrusive List Entry Task ID = %d\n", retrieved->task_id);

    /* 4. Spinlock & Atomics Test (kspinlock) */
    kspinlock_init(&smp_lock);
    kspinlock_lock(&smp_lock);
    volatile uint32_t counter = 100;
    katomic_add(&counter, 50);
    kspinlock_unlock(&smp_lock);
    kprintf("  • kspinlock: Atomic Add (100 + 50) = %u\n", (unsigned int)counter);

    /* 5. Ring Buffer Test (kringbuf) */
    kringbuf_t rb;
    kringbuf_init(&rb, ring_storage, sizeof(ring_storage));
    kringbuf_push(&rb, 'A');
    uint8_t pop_val = 0;
    kringbuf_pop(&rb, &pop_val);
    kprintf("  • kringbuf: Popped = %c\n", pop_val);

    /* 6. String Test (kstring) */
    char parsed_num[32];
    kitoa(12345, parsed_num, 10, 0);
    kprintf("  • kstring: kitoa(12345) = %s\n", parsed_num);

    /* 7. Graphics Test (kgfx) */
    kgfx_fb_t fb;
    kgfx_init(&fb, video_buffer, 320, 200);
    kgfx_clear(&fb, KGFX_DARKGRAY);
    kprintf("  • kgfx: Rendered Framebuffer\n");

    /* 8. VFS Test (kvfs / kls) */
    kvfs_init();
    kvfs_create_file("test.txt", "Hello VFS", 9, 0644);
    kprintf("  • kvfs (kls): File Listing:\n");
    kvfs_list(vfs_ls_cb);

    kfree(page_table);
    kprintf("==========================================\n");

    return 0;
}
