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
            (list)->head = (node);       \
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

#define LIST_CONCAT(dst, src) \
    do {                                         \
        if ((src)->head) {                       \
            if ((dst)->tail)                     \
                (dst)->tail->next = (src)->head; \
            else                                 \
                (dst)->head = (src)->head;       \
                                                 \
            (dst)->tail = (src)->tail;           \
        }                                        \
                                                 \
        LIST_INIT(src);                          \
    } while (0)

#define LIST_FOREACH(it, list)                   \
    for (typeof((list)->head) it = (list)->head; \
            it != nullptr; it = it->next)

#endif
