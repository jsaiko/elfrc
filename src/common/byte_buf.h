#ifndef ELFR_COMMON_BYTE_BUF_H
#define ELFR_COMMON_BYTE_BUF_H

#include <stddef.h>

struct byte_buf {
    unsigned char *data;
    size_t len;
    size_t cap;
};

void byte_buf_reserve(struct byte_buf *b, size_t extra);
size_t byte_buf_append(struct byte_buf *b, const void *data, size_t len);
size_t byte_buf_append_zeros(struct byte_buf *b, size_t len);
void byte_buf_align(struct byte_buf *b, size_t alignment);
void byte_buf_free(struct byte_buf *b);

#endif /* ELFR_COMMON_BYTE_BUF_H */
