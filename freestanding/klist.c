#include "klist.h"

/* List initialization */
void klist_init(klist_node_t *head) {
    if (!head) return;
    head->next = head;
    head->prev = head;
}

/* Node insertion at head */
void klist_add_head(klist_node_t *head, klist_node_t *new_node) {
    if (!head || !new_node) return;
    new_node->next = head->next;
    new_node->prev = head;
    head->next->prev = new_node;
    head->next = new_node;
}

/* Node insertion at tail */
void klist_add_tail(klist_node_t *head, klist_node_t *new_node) {
    if (!head || !new_node) return;
    new_node->next = head;
    new_node->prev = head->prev;
    head->prev->next = new_node;
    head->prev = new_node;
}

/* Node removal */
void klist_remove(klist_node_t *node) {
    if (!node || !node->next || !node->prev) return;
    node->prev->next = node->next;
    node->next->prev = node->prev;
    node->next = node;
    node->prev = node;
}

/* Empty state query */
int klist_is_empty(const klist_node_t *head) {
    return (head && head->next == head);
}
