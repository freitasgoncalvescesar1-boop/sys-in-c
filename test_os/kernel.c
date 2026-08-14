#include "multiboot.h"
#include "idt.h"
#include "ps2.h"
#include "gdt.h"
#include "../freestanding/kmem.h"
#include "../freestanding/kfixed.h"
#include "../freestanding/kprintf.h"
#include "../freestanding/kgfx.h"
#include "../freestanding/kstring.h"
#include "../freestanding/kvfs.h"

static kgfx_fb_t os_fb;

/* 0 = GUI Mode, 1 = CLI Mode */
static int os_mode = 0;

static char cli_input[128] = "";
static size_t cli_pos = 0;

static int prev_mouse_x = 400;
static int prev_mouse_y = 300;

static const char sample_readme[] = "Welcome to utils-in-c OS!\nThis file is read from RAM VFS.";
static const char sample_config[] = "OS_NAME=utils-in-c OS\nVERSION=2.0\nKERNEL=Multiboot1\nVIDEO=VBE800x600";
static const char sample_script[] = "#!/bin/sh\necho 'Running test script inside OS!'";

/* Clean character renderer for KGFX Framebuffer */
static void os_putchar(char c) {
    static int cursor_x = 30;
    static int cursor_y = 100;

    if (c == '\n') {
        cursor_x = 30;
        cursor_y += 14;
    } else if (c == '\b') {
        if (cursor_x >= 38) {
            cursor_x -= 8;
            kgfx_draw_rect(&os_fb, cursor_x, cursor_y, 8, 12, KGFX_DARKGRAY, 1);
        }
    } else {
        kgfx_draw_char(&os_fb, cursor_x, cursor_y, c, KGFX_WHITE, 0);
        cursor_x += 8;
        if (cursor_x > (int)os_fb.width - 40) {
            cursor_x = 30;
            cursor_y += 14;
        }
    }

    /* Screen Scrolling / Reset */
    if (cursor_y > (int)os_fb.height - 40) {
        kgfx_clear(&os_fb, (os_mode == 1) ? KGFX_BLACK : KGFX_DARKGRAY);
        kgfx_draw_rect(&os_fb, 10, 10, os_fb.width - 20, os_fb.height - 20, KGFX_CYAN, 0);
        cursor_y = 40;
        cursor_x = 30;
    }
}

void os_draw_mouse_cursor(void) {
    if (os_mode != 0) return;

    kgfx_mouse_t *mouse = ps2_get_mouse_state();

    /* Erase previous cursor position */
    kgfx_draw_rect(&os_fb, prev_mouse_x, prev_mouse_y, 16, 16, KGFX_DARKGRAY, 1);

    /* Redraw border if mouse moved over window border */
    if (prev_mouse_x < 20 || prev_mouse_x > (int)os_fb.width - 30 || prev_mouse_y < 20) {
        kgfx_draw_rect(&os_fb, 10, 10, os_fb.width - 20, os_fb.height - 20, KGFX_CYAN, 0);
    }

    /* Draw new cursor position */
    kgfx_draw_cursor(&os_fb, mouse);

    prev_mouse_x = mouse->x;
    prev_mouse_y = mouse->y;
}

static void vfs_ls_print_cb(const char *name, size_t size, uint16_t mode) {
    (void)mode;
    kprintf("  • %-20s : %u bytes\n", name, (unsigned int)size);
}

static void execute_cli_command(const char *cmd) {
    kprintf("\n");
    if (kstrcmp(cmd, "help") == 0 || kstrcmp(cmd, "?") == 0) {
        kprintf("  [utils-in-c OS CLI Help]\n");
        kprintf("    • ls / dir      : List VFS files (kls)\n");
        kprintf("    • cat <file>    : Read VFS file contents\n");
        kprintf("    • mem           : Display KMEM heap stats\n");
        kprintf("    • clear         : Clear CLI terminal screen\n");
        kprintf("    • exit          : Switch back to GUI mode\n");
    } else if (kstrcmp(cmd, "ls") == 0 || kstrcmp(cmd, "dir") == 0) {
        kprintf("  [VFS File Directory Listing (kls)]:\n");
        kvfs_list(vfs_ls_print_cb);
    } else if (kstrncmp(cmd, "cat ", 4) == 0) {
        const char *fname = cmd + 4;
        const kvfs_file_t *file = kvfs_open(fname);
        if (file) {
            kprintf("  [Contents of %s]:\n", fname);
            kprintf("    %s\n", (const char *)file->data);
        } else {
            kprintf("  Error: File '%s' not found in VFS.\n", fname);
        }
    } else if (kstrcmp(cmd, "mem") == 0) {
        kprintf("  [KMEM Heap Diagnostics]:\n");
        kprintf("    • Total Memory : %u KB\n", (unsigned int)(kmem_get_total_bytes() / 1024));
        kprintf("    • Free Memory  : %u KB\n", (unsigned int)(kmem_get_free_bytes() / 1024));
        kprintf("    • Used Memory  : %u bytes\n", (unsigned int)kmem_get_used_bytes());
    } else if (kstrcmp(cmd, "clear") == 0) {
        kgfx_clear(&os_fb, KGFX_BLACK);
        kgfx_draw_rect(&os_fb, 10, 10, os_fb.width - 20, os_fb.height - 20, KGFX_CYAN, 0);
        kprintf("[Interactive Kernel CLI Terminal - Press F1, TAB or type 'exit']\n\n");
    } else if (kstrcmp(cmd, "exit") == 0) {
        os_mode = 0;
        kgfx_clear(&os_fb, KGFX_DARKGRAY);
        kprintf("Returned to GUI Mode.\n");
        return;
    } else {
        kprintf("  Unknown command '%s'. Type 'help' for available commands.\n", cmd);
    }
    kprintf("test_os> ");
}

void os_handle_keypress(char c) {
    if (os_mode == 1) { /* CLI Mode */
        if (c == '\b') {
            if (cli_pos > 0) cli_input[--cli_pos] = '\0';
        } else if (c == '\n') {
            execute_cli_command(cli_input);
            cli_pos = 0;
            cli_input[0] = '\0';
        } else if (c && cli_pos < sizeof(cli_input) - 1) {
            cli_input[cli_pos++] = c;
            cli_input[cli_pos] = '\0';
            kprintf("%c", c);
        }
    }
}

void os_toggle_cli_mode(void) {
    os_mode = !os_mode;
    if (os_mode == 1) {
        kgfx_clear(&os_fb, KGFX_BLACK);
        kgfx_draw_rect(&os_fb, 10, 10, os_fb.width - 20, os_fb.height - 20, KGFX_CYAN, 0);
        kprintf("\n[Interactive Kernel CLI Terminal Active - Type 'help' or 'exit']\n\n");
        kprintf("test_os> ");
    } else {
        kgfx_clear(&os_fb, KGFX_DARKGRAY);
        kprintf("Returned to GUI Mode.\n");
    }
}

static void trigger_divide_by_zero_test(void) {
    volatile int zero = 0;
    volatile int result = 100 / zero;
    (void)result;
}

void kernel_main(uint32_t magic, multiboot_info_t *mb_info) {
    if (magic != MULTIBOOT_BOOTLOADER_MAGIC || !mb_info) return;

    uint32_t *fb_ptr = (uint32_t *)(uintptr_t)mb_info->framebuffer_addr;
    uint32_t width = mb_info->framebuffer_width ? mb_info->framebuffer_width : 800;
    uint32_t height = mb_info->framebuffer_height ? mb_info->framebuffer_height : 600;

    if (!fb_ptr) fb_ptr = (uint32_t *)0xFD000000;

    /* 1. Initialize Framebuffer & VFS */
    kgfx_init(&os_fb, fb_ptr, width, height);
    kgfx_clear(&os_fb, KGFX_DARKGRAY);
    kset_putchar(os_putchar);

    kvfs_init();
    kvfs_create_file("readme.txt", sample_readme, sizeof(sample_readme) - 1, 0644);
    kvfs_create_file("kernel.config", sample_config, sizeof(sample_config) - 1, 0644);
    kvfs_create_file("hello.sh", sample_script, sizeof(sample_script) - 1, 0755);

    /* 2. Initialize GDT, IDT & PS/2 Ports */
    gdt_init();
    idt_init();
    ps2_init();

    /* 3. Draw Kernel UI Window */
    kgfx_draw_rect(&os_fb, 10, 10, width - 20, height - 20, KGFX_CYAN, 0);
    kgfx_draw_rect(&os_fb, 12, 12, width - 24, 32, KGFX_BLUE, 1);
    kgfx_draw_string(&os_fb, 20, 22, "utils-in-c OS (Press F1, TAB or Ctrl+V for Real CLI)", KGFX_WHITE, KGFX_BLUE);

    kgfx_draw_circle(&os_fb, width - 100, 180, 50, KGFX_YELLOW);

    /* 4. Freestanding Diagnostics */
    static uint8_t os_heap_pool[2 * 1024 * 1024];
    kmem_init(os_heap_pool, sizeof(os_heap_pool));
    void *page_table = kmalloc_aligned(4096, 4096);

    fp32_t a = fp32_from_int(100);
    fp32_t sqrt_res = fp32_sqrt(a);
    char math_buf[32];
    fp32_to_str(sqrt_res, math_buf, sizeof(math_buf), 2);

    kprintf("[Kernel Core Subsystems Active]\n");
    kprintf("  • GDT & IDT Status  : GDT Loaded | IDT Loaded | PIC Remapped\n");
    kprintf("  • PS/2 Drivers      : Keyboard (IRQ 1) & Mouse (IRQ 12) Active\n");
    kprintf("  • KVFS Filesystem   : 3 RAM Files Registered (ls / cat ready)\n");
    kprintf("  • KMEM Heap Pool    : %d KB | Free: %d KB\n", (int)(sizeof(os_heap_pool)/1024), (int)(kmem_get_free_bytes()/1024));
    kprintf("  • KFIXED Sqrt(100)  : %s\n", math_buf);
    kprintf("  • Shortcut Tip      : Press F1, TAB or Ctrl+V to open Real CLI!\n");

    trigger_divide_by_zero_test();

    kfree(page_table);

    /* 5. Main Loop: Render Mouse Cursor in Real-Time */
    while (1) {
        os_draw_mouse_cursor();
        __asm__ __volatile__ ("hlt");
    }
}
