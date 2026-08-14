#ifndef KLIST_H
#define KLIST_H

#include <stddef.h>
#include <stdint.h>

/* Intrusive doubly-linked list node */
typedef struct klist_node {
    struct klist_node *next;
    struct klist_node *prev;
} klist_node_t;

/* List initialization */
void klist_init(klist_node_t *head);

/* Node insertion at head */
void klist_add_head(klist_node_t *head, klist_node_t *new_node);

/* Node insertion at tail */
void klist_add_tail(klist_node_t *head, klist_node_t *new_node);

/* Node removal */
void klist_remove(klist_node_t *node);

/* Empty state query */
int klist_is_empty(const klist_node_t *head);

/* Parent container offset macro */
#define klist_entry(ptr, type, member) \
    ((type *)((uint8_t *)(ptr) - offsetof(type, member)))

#endif
