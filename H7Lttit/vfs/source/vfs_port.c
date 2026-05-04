#include "vfs_port.h"

#include "comm.h"
#include "fs.h"
#include "vfs.h"

#include "heap.h"
#include <stddef.h>
#include <stdint.h>

struct vfs_fs_file {
    struct inode *inode;
    uint32_t pos;
};

struct vfs_uart_file {
    uint8_t reserved;
};

static int vfs_fs_open(void *self, const char *path, int flags, void **handle_out)
{
    struct vfs_fs_file *file;

    (void)self;

    if ((handle_out == NULL) || (path == NULL) || (path[0] != '/') ||
        ((path[0] == '/') && (path[1] == '\0'))) {
        return -1;
    }

    file = heap_malloc(sizeof(*file));
    if (file == NULL) {
        return -1;
    }

    if (fs_open(path, flags, &file->inode) != 0) {
        heap_free(file);
        return -1;
    }

    file->pos = 0U;
    *handle_out = file;
    return 0;
}

static int vfs_fs_read(void *self, void *handle, void *buf, int len)
{
    struct vfs_fs_file *file;
    int ret;

    (void)self;

    if ((handle == NULL) || (buf == NULL) || (len < 0)) {
        return -1;
    }
    if (len == 0) {
        return 0;
    }

    file = (struct vfs_fs_file *)handle;
    ret = fs_read(file->inode, file->pos, buf, (uint32_t)len);
    if (ret > 0) {
        file->pos += (uint32_t)ret;
    }
    return ret;
}

static int vfs_fs_write(void *self, void *handle, const void *buf, int len)
{
    struct vfs_fs_file *file;
    int ret;

    (void)self;

    if ((handle == NULL) || (buf == NULL) || (len < 0)) {
        return -1;
    }
    if (len == 0) {
        return 0;
    }

    file = (struct vfs_fs_file *)handle;
    ret = fs_write(file->inode, file->pos, buf, (uint32_t)len);
    if (ret > 0) {
        file->pos += (uint32_t)ret;
    }
    return ret;
}

static int vfs_fs_ctl(void *self, void *handle, int cmd, void *arg)
{
    (void)self;
    (void)handle;
    (void)cmd;
    (void)arg;
    return -1;
}

static int vfs_fs_close(void *self, void *handle)
{
    struct vfs_fs_file *file;
    int ret;

    (void)self;

    if (handle == NULL) {
        return -1;
    }

    file = (struct vfs_fs_file *)handle;
    ret = fs_close(file->inode);
    heap_free(file);
    return ret;
}

static int vfs_uart_open(void *self, const char *path, int flags, void **handle_out)
{
    struct vfs_uart_file *file;

    (void)self;
    (void)flags;

    if (handle_out == NULL) {
        return -1;
    }
    if ((path != NULL) && (path[0] != '\0') && !((path[0] == '/') && (path[1] == '\0'))) {
        return -1;
    }

    file = heap_malloc(sizeof(*file));
    if (file == NULL) {
        return -1;
    }

    file->reserved = 0U;
    *handle_out = file;
    return 0;
}

static int vfs_uart_read(void *self, void *handle, void *buf, int len)
{
    uint8_t *dst;
    int count;

    (void)self;

    if ((handle == NULL) || (buf == NULL) || (len < 0)) {
        return -1;
    }
    if (comm == NULL) {
        return -1;
    }
    if (len == 0) {
        return 0;
    }

    dst = (uint8_t *)buf;
    dst[0] = (uint8_t)comm_getc();
    count = 1;
    while ((count < len) && (comm_peek() > 0)) {
        dst[count++] = (uint8_t)comm_getc();
    }
    return count;
}

static int vfs_uart_write(void *self, void *handle, const void *buf, int len)
{
    (void)self;
    (void)handle;

    if ((buf == NULL) || (len < 0)) {
        return -1;
    }
    if (comm == NULL) {
        return -1;
    }
    if (len == 0) {
        return 0;
    }

    comm_write((const char *)buf, len);
    return len;
}

static int vfs_uart_ctl(void *self, void *handle, int cmd, void *arg)
{
    (void)self;
    (void)handle;
    (void)cmd;
    (void)arg;
    return -1;
}

static int vfs_uart_close(void *self, void *handle)
{
    (void)self;

    if (handle == NULL) {
        return -1;
    }

    heap_free(handle);
    return 0;
}

static struct vfs_ops g_fs_ops = {
    .open = vfs_fs_open,
    .read = vfs_fs_read,
    .write = vfs_fs_write,
    .ctl = vfs_fs_ctl,
    .close = vfs_fs_close,
};

static struct vfs_ops g_uart_ops = {
    .open = vfs_uart_open,
    .read = vfs_uart_read,
    .write = vfs_uart_write,
    .ctl = vfs_uart_ctl,
    .close = vfs_uart_close,
};

void vfs_port_init(void)
{
    static int initialized = 0;
    struct vnode *node;

    if (initialized != 0) {
        return;
    }

    vfs_init("stm32h750");

    node = vfs_mkdirs("/fs");
    if (node != NULL) {
        vnode_set_ops(node, &g_fs_ops);
        vnode_set_remote(node, "stm32h750", "/fs");
    }

    node = vfs_mkdirs("/dev");
    if (node != NULL) {
        vnode_set_remote(node, "stm32h750", "/dev");
    }

    node = vfs_mkdirs("/dev/uart1");
    if (node != NULL) {
        vnode_set_ops(node, &g_uart_ops);
        vnode_set_remote(node, "stm32h750", "/uart/1");
    }

    initialized = 1;
}
