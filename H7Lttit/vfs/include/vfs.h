#ifndef LTTIT_VFS_H
#define LTTIT_VFS_H

#include "prefix.h"

struct vfs_ops {
    int (*open)(void *self, const char *path, int flags, void **handle_out);
    int (*read)(void *self, void *handle, void *buf, int len);
    int (*write)(void *self, void *handle, const void *buf, int len);
    int (*ctl)(void *self, void *handle, int cmd, void *arg);
    int (*close)(void *self, void *handle);
};

struct vnode {
    struct vfs_ops *ops;
    void *self;
    char *owner_name;
    char *remote_path;
};

struct vfs {
    struct prefix_map tree;
    char *local_name;
};

struct vfs_dump_ctx {
    char *buf;
    int len;
    int pos;
};

extern struct vfs g_vfs;

struct vnode *vnode_create(void);
void vnode_set_ops(struct vnode *n, struct vfs_ops *ops);
void vnode_set_self(struct vnode *n, void *self);
void vnode_set_remote(struct vnode *n, const char *owner_name, const char *remote_path);

void vfs_init(const char *local_name);

struct vnode *vfs_walk(const char *path);
struct vnode *vfs_mkdirs(const char *path);

int vfs_open(const char *path, int flags);
int vfs_read(int fd, void *buf, int len);
int vfs_write(int fd, const void *buf, int len);
int vfs_ctl(int fd, int cmd, void *arg);
int vfs_close(int fd);

int vfs_dump(struct vfs_dump_ctx *ctx);

#endif
