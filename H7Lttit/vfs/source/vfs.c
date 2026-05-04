#include "vfs.h"

#include "heap.h"

#include <stdio.h>
#include <stdint.h>
#include <string.h>

#define VFS_FD_MAX 8

struct vfs g_vfs;

struct vfs_fd_entry {
    uint8_t used;
    struct vnode *node;
    void *handle;
};

static struct vfs_fd_entry g_vfs_fds[VFS_FD_MAX];

static char *vfs_strdup(const char *src)
{
    size_t len;
    char *dst;

    if (src == NULL) {
        return NULL;
    }

    len = strlen(src) + 1U;
    dst = heap_malloc((uint32_t)len);
    if (dst == NULL) {
        return NULL;
    }

    memcpy(dst, src, len);
    return dst;
}

static void vfs_free_str(char **str)
{
    if ((str != NULL) && (*str != NULL)) {
        heap_free(*str);
        *str = NULL;
    }
}

static const char *normalize_path(const char *path)
{
    while ((path != NULL) && (*path == '/')) {
        path++;
    }
    return path;
}

static int path_is_prefix_match(const char *key, const char *path)
{
    size_t key_len;

    if ((key == NULL) || (path == NULL)) {
        return 0;
    }

    key_len = strlen(key);
    if (key_len == 0U) {
        return 1;
    }
    if (strncmp(key, path, key_len) != 0) {
        return 0;
    }
    if ((path[key_len] != '\0') && (path[key_len] != '/')) {
        return 0;
    }
    return 1;
}

struct resolve_ctx {
    const char *path;
    const char *best_key;
    struct vnode *best_node;
};

static int resolve_cb(const char *key, void *value, void *arg)
{
    struct resolve_ctx *ctx;

    ctx = (struct resolve_ctx *)arg;
    if (!path_is_prefix_match(key, ctx->path)) {
        return 0;
    }

    if ((ctx->best_key == NULL) || (strlen(key) > strlen(ctx->best_key))) {
        ctx->best_key = key;
        ctx->best_node = (struct vnode *)value;
    }
    return 0;
}

static struct vnode *vfs_resolve_node(const char *path, const char **subpath_out)
{
    const char *p;
    struct resolve_ctx ctx;

    p = normalize_path(path);
    if (p == NULL) {
        return NULL;
    }

    ctx.path = p;
    ctx.best_key = NULL;
    ctx.best_node = NULL;
    prefix_map_iter(&g_vfs.tree, resolve_cb, &ctx);
    if (ctx.best_node == NULL) {
        return NULL;
    }

    if (subpath_out != NULL) {
        size_t key_len;

        key_len = strlen(ctx.best_key);
        if (p[key_len] == '\0') {
            *subpath_out = "/";
        } else {
            *subpath_out = p + key_len;
        }
    }

    return ctx.best_node;
}

static int vfs_fd_alloc(struct vnode *node, void *handle)
{
    int i;

    for (i = 0; i < VFS_FD_MAX; i++) {
        if (g_vfs_fds[i].used == 0U) {
            g_vfs_fds[i].used = 1U;
            g_vfs_fds[i].node = node;
            g_vfs_fds[i].handle = handle;
            return i;
        }
    }
    return -1;
}

static struct vfs_fd_entry *vfs_fd_get(int fd)
{
    if ((fd < 0) || (fd >= VFS_FD_MAX)) {
        return NULL;
    }
    if (g_vfs_fds[fd].used == 0U) {
        return NULL;
    }
    return &g_vfs_fds[fd];
}

static void vfs_fd_release(int fd)
{
    if ((fd < 0) || (fd >= VFS_FD_MAX)) {
        return;
    }

    g_vfs_fds[fd].used = 0U;
    g_vfs_fds[fd].node = NULL;
    g_vfs_fds[fd].handle = NULL;
}

struct vnode *vnode_create(void)
{
    struct vnode *n;

    n = heap_malloc(sizeof(*n));
    if (n == NULL) {
        return NULL;
    }
    n->ops = NULL;
    n->self = NULL;
    n->owner_name = NULL;
    n->remote_path = NULL;
    return n;
}

void vnode_set_ops(struct vnode *n, struct vfs_ops *ops)
{
    if (n != NULL) {
        n->ops = ops;
    }
}

void vnode_set_self(struct vnode *n, void *self)
{
    if (n != NULL) {
        n->self = self;
    }
}

void vnode_set_remote(struct vnode *n, const char *owner_name, const char *remote_path)
{
    if (n == NULL) {
        return;
    }

    vfs_free_str(&n->owner_name);
    vfs_free_str(&n->remote_path);
    n->owner_name = vfs_strdup(owner_name);
    n->remote_path = vfs_strdup(remote_path);
}

void vfs_init(const char *local_name)
{
    prefix_map_init(&g_vfs.tree);
    memset(g_vfs_fds, 0, sizeof(g_vfs_fds));
    g_vfs.local_name = vfs_strdup(local_name);
}

struct vnode *vfs_walk(const char *path)
{
    return vfs_resolve_node(path, NULL);
}

struct vnode *vfs_mkdirs(const char *path)
{
    const char *p;
    struct vnode *n;

    p = normalize_path(path);
    if (p == NULL) {
        return NULL;
    }

    n = prefix_map_get(&g_vfs.tree, p);
    if (n != NULL) {
        return n;
    }

    n = vnode_create();
    if (n == NULL) {
        return NULL;
    }
    if (prefix_map_set(&g_vfs.tree, p, n) < 0) {
        heap_free(n);
        return NULL;
    }
    return n;
}

int vfs_open(const char *path, int flags)
{
    const char *subpath;
    struct vnode *node;
    void *handle;
    int fd;

    node = vfs_resolve_node(path, &subpath);
    if ((node == NULL) || (node->ops == NULL) || (node->ops->open == NULL)) {
        return -1;
    }

    handle = NULL;
    if (node->ops->open(node->self, subpath, flags, &handle) != 0) {
        return -1;
    }

    fd = vfs_fd_alloc(node, handle);
    if (fd < 0) {
        if (node->ops->close != NULL) {
            (void)node->ops->close(node->self, handle);
        }
        return -1;
    }
    return fd;
}

int vfs_read(int fd, void *buf, int len)
{
    struct vfs_fd_entry *entry;

    entry = vfs_fd_get(fd);
    if ((entry == NULL) || (entry->node == NULL) || (entry->node->ops == NULL) ||
        (entry->node->ops->read == NULL)) {
        return -1;
    }
    return entry->node->ops->read(entry->node->self, entry->handle, buf, len);
}

int vfs_write(int fd, const void *buf, int len)
{
    struct vfs_fd_entry *entry;

    entry = vfs_fd_get(fd);
    if ((entry == NULL) || (entry->node == NULL) || (entry->node->ops == NULL) ||
        (entry->node->ops->write == NULL)) {
        return -1;
    }
    return entry->node->ops->write(entry->node->self, entry->handle, buf, len);
}

int vfs_ctl(int fd, int cmd, void *arg)
{
    struct vfs_fd_entry *entry;

    entry = vfs_fd_get(fd);
    if ((entry == NULL) || (entry->node == NULL) || (entry->node->ops == NULL) ||
        (entry->node->ops->ctl == NULL)) {
        return -1;
    }
    return entry->node->ops->ctl(entry->node->self, entry->handle, cmd, arg);
}

int vfs_close(int fd)
{
    struct vfs_fd_entry *entry;
    int ret;

    entry = vfs_fd_get(fd);
    if ((entry == NULL) || (entry->node == NULL) || (entry->node->ops == NULL) ||
        (entry->node->ops->close == NULL)) {
        return -1;
    }

    ret = entry->node->ops->close(entry->node->self, entry->handle);
    vfs_fd_release(fd);
    return ret;
}

static int dump_cb(const char *key, void *value, void *arg)
{
    struct vfs_dump_ctx *c;
    struct vnode *node;
    const char *owner;
    const char *remote;
    int left;
    int n;

    (void)value;

    c = (struct vfs_dump_ctx *)arg;
    node = (struct vnode *)value;
    owner = ((node != NULL) && (node->owner_name != NULL)) ? node->owner_name : "-";
    remote = ((node != NULL) && (node->remote_path != NULL)) ? node->remote_path : "-";
    if (c->pos >= c->len) {
        return -1;
    }

    left = c->len - c->pos;
    n = snprintf(c->buf + c->pos, (size_t)left, "%s -> %s:%s\r\n", key, owner, remote);
    if ((n <= 0) || (n >= left)) {
        return -1;
    }
    c->pos += n;
    return 0;
}

int vfs_dump(struct vfs_dump_ctx *ctx)
{
    prefix_map_iter(&g_vfs.tree, dump_cb, ctx);
    return ctx->pos;
}
