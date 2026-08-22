#ifndef CINC_LIST_H
#define CINC_LIST_H

#define LIST(type) \
    struct {       \
        type *head;\
        type *tail;\
    }

#define LIST_INIT(list) \
    (*(list) = (typeof(*(list))){})

#define LIST_APPEND(list, node)          \
    do {                                 \
        (node)->next = nullptr;          \
                                         \
        if ((list)->tail)                \
            (list)->tail->next = (node); \
        else                             \
            (list)->tail = (node);       \
                                         \
        (list)->tail = (node);           \
    } while (0)

#define LIST_PREPEND(list, node)     \
    do {                             \
        (node)->next = (list)->head; \
        (list)->head = (node);       \
                                     \
        if (!(list)->tail)           \
            (list)->tail = (node);   \
    } while(0)

#define LIST_FOREACH(list)                          \
    for (typeof((list)->head) *node = (list)->head; \
            node != nullptr; node = node->next)

#endif
