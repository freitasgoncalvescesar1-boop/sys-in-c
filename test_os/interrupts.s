.section .text
.global isr0
.global isr13
.global isr14
.global irq0
.global irq1
.global irq12
.extern isr_handler

isr_common_stub:
    pusha
    push %ds
    push %es
    push %fs
    push %gs
    mov $0x10, %ax
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %fs
    mov %ax, %gs

    push %esp
    call isr_handler
    add $4, %esp

    pop %gs
    pop %fs
    pop %es
    pop %ds
    popa
    add $8, %esp
    iret

isr0:
    push $0
    push $0
    jmp isr_common_stub

isr13:
    push $13
    jmp isr_common_stub

isr14:
    push $14
    jmp isr_common_stub

irq0:
    push $0
    push $32
    jmp isr_common_stub

irq1:
    push $0
    push $33
    jmp isr_common_stub

irq12:
    push $0
    push $44
    jmp isr_common_stub

.section .note.GNU-stack,"",@progbits
