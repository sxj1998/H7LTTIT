#include "fs_port.h"

#include <stddef.h>

void fs_port_init(void)
{
}

int fs_port_mount(struct superblock *sb)
{
    return fs_mount(sb, NULL);
}

void fs_port_deinit(struct superblock *sb)
{
    (void)fs_unmount(sb);
}
