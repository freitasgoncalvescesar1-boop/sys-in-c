#include "kringbuf.h"

/* Buffer initialization */
void kringbuf_init(kringbuf_t *rb, uint8_t *buffer, size_t capacity) {
    if (!rb || !buffer || capacity == 0) return;
    rb->buffer = buffer;
    rb->capacity = capacity;
    rb->head = 0;
    rb->tail = 0;
    rb->count = 0;
}

/* Byte insertion */
int kringbuf_push(kringbuf_t *rb, uint8_t byte) {
    if (!rb || !rb->buffer || rb->count >= rb->capacity) return -1;
    rb->buffer[rb->head] = byte;
    rb->head = (rb->head + 1) % rb->capacity;
    rb->count++;
    return 0;
}

/* Byte extraction */
int kringbuf_pop(kringbuf_t *rb, uint8_t *out_byte) {
    if (!rb || !rb->buffer || rb->count == 0) return -1;
    if (out_byte) *out_byte = rb->buffer[rb->tail];
    rb->tail = (rb->tail + 1) % rb->capacity;
    rb->count--;
    return 0;
}

/* Byte inspection */
int kringbuf_peek(const kringbuf_t *rb, uint8_t *out_byte) {
    if (!rb || !rb->buffer || rb->count == 0) return -1;
    if (out_byte) *out_byte = rb->buffer[rb->tail];
    return 0;
}

/* Empty state query */
int kringbuf_is_empty(const kringbuf_t *rb) {
    return (!rb || rb->count == 0);
}

/* Full state query */
int kringbuf_is_full(const kringbuf_t *rb) {
    return (rb && rb->count >= rb->capacity);
}

/* Count query */
size_t kringbuf_count(const kringbuf_t *rb) {
    return rb ? rb->count : 0;
}
