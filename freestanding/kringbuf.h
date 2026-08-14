#ifndef KRINGBUF_H
#define KRINGBUF_H

#include <stddef.h>
#include <stdint.h>

/* Lock-free FIFO ring buffer descriptor */
typedef struct {
    uint8_t *buffer;   /* Storage byte array */
    size_t capacity;   /* Total buffer capacity */
    size_t head;       /* Write pointer offset */
    size_t tail;       /* Read pointer offset */
    size_t count;      /* Current byte count */
} kringbuf_t;

/* Buffer initialization */
void kringbuf_init(kringbuf_t *rb, uint8_t *buffer, size_t capacity);

/* Byte insertion */
int kringbuf_push(kringbuf_t *rb, uint8_t byte);

/* Byte extraction */
int kringbuf_pop(kringbuf_t *rb, uint8_t *out_byte);

/* Byte inspection without extraction */
int kringbuf_peek(const kringbuf_t *rb, uint8_t *out_byte);

/* State queries */
int kringbuf_is_empty(const kringbuf_t *rb);
int kringbuf_is_full(const kringbuf_t *rb);
size_t kringbuf_count(const kringbuf_t *rb);

#endif
