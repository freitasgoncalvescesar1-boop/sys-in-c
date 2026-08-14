/* Multiboot1 Specification Header */
.set ALIGN,    1<<0
.set MEMINFO,  1<<1
.set VIDEO,    1<<2
.set FLAGS,    ALIGN | MEMINFO | VIDEO
.set MAGIC,    0x1BADB002
.set CHECKSUM, -(MAGIC + FLAGS)

.section .multiboot
.align 4
.long MAGIC
.long FLAGS
.long CHECKSUM
.long 0, 0, 0, 0, 0
.long 0                     /* Linear graphics mode */
.long 800                   /* Width */
.long 600                   /* Height */
.long 32                    /* Depth (32 bpp ARGB) */

.section .bss
.align 16
stack_bottom:
.skip 16384                 /* 16 KiB Stack */
stack_top:

.section .text
.global _start
.type _start, @function
_start:
	mov $stack_top, %esp
	push %ebx                   /* Multiboot Info Pointer */
	push %eax                   /* Multiboot Magic */
	call kernel_main
	cli
1:	hlt
	jmp 1b

.section .note.GNU-stack,"",@progbits
