#include "byte_buf.h"

#include <stdlib.h>
#include <string.h>

void byte_buf_reserve(struct byte_buf *b, size_t extra) {
    if (b->len + extra <= b->cap) {
        return;
    }
    size_t newcap = b->cap ? b->cap * 2 : 4096;
    while (newcap < b->len + extra) {
        newcap *= 2;
    }
    b->data = realloc(b->data, newcap);
    b->cap = newcap;
}

size_t byte_buf_append(struct byte_buf *b, const void *data, size_t len) {
    byte_buf_reserve(b, len);
    size_t off = b->len;
    if (len) {
        memcpy(b->data + off, data, len);
    }
    b->len += len;
    return off;
}

size_t byte_buf_append_zeros(struct byte_buf *b, size_t len) {
    byte_buf_reserve(b, len);
    size_t off = b->len;
    memset(b->data + off, 0, len);
    b->len += len;
    return off;
}

void byte_buf_align(struct byte_buf *b, size_t alignment) {
    size_t rem = b->len % alignment;
    if (rem != 0) {
        byte_buf_append_zeros(b, alignment - rem);
    }
}

void byte_buf_free(struct byte_buf *b) {
    free(b->data);
    b->data = NULL;
    b->len = 0;
    b->cap = 0;
}
