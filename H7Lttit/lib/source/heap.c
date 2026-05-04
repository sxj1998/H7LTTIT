#include "heap.h"

#include "FreeRTOS.h"
#include "task.h"

void heap_init(void)
{
}

void *heap_malloc_dbg(size_t size, const char *file, int line)
{
    (void)file;
    (void)line;
    return pvPortMalloc(size);
}

void heap_free_dbg(void *ptr, const char *file, int line)
{
    (void)file;
    (void)line;

    if (ptr != NULL) {
        vPortFree(ptr);
    }
}

struct heap_stats heap_get_stats(void)
{
    struct heap_stats st;

    st.remain_size = xPortGetFreeHeapSize();
    st.free_size_iter = xPortGetFreeHeapSize();
    st.max_free_block = xPortGetMinimumEverFreeHeapSize();
    st.free_blocks = 0U;
    return st;
}

void heap_debug_dump_leaks(void)
{
}
