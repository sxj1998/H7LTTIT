#ifndef LTTIT_HEAP_H
#define LTTIT_HEAP_H

#include <stddef.h>

struct heap_stats {
    size_t remain_size;
    size_t free_size_iter;
    size_t max_free_block;
    size_t free_blocks;
};

void heap_init(void);
void *heap_malloc_dbg(size_t size, const char *file, int line);
void heap_free_dbg(void *ptr, const char *file, int line);
struct heap_stats heap_get_stats(void);
void heap_debug_dump_leaks(void);

#define heap_malloc(size) heap_malloc_dbg((size), NULL, 0)
#define heap_free(ptr)    heap_free_dbg((ptr), NULL, 0)

#endif
