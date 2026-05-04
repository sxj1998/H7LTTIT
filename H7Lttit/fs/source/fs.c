#include "fs.h"

#include "heap.h"
#include "littlefs_sd.h"

#include <stdio.h>
#include <string.h>

struct fs_file_handle {
    lfs_file_t file;
    char path[96];
};

static struct superblock g_fs_sb;
static uint8_t g_fs_mounted;
static uint32_t g_next_ino = 1U;

static const char *fs_lfs_path(const char *path)
{
    if (path == NULL) {
        return NULL;
    }

    while ((path[0] == '/') && (path[1] != '\0')) {
        path++;
    }
    return path;
}

static int fs_to_lfs_flags(int flags)
{
    int lfs_flags = 0;

    if ((flags & O_RDWR) != 0) {
        lfs_flags |= LFS_O_RDWR;
    } else if ((flags & O_WRONLY) != 0) {
        lfs_flags |= LFS_O_WRONLY;
    } else {
        lfs_flags |= LFS_O_RDONLY;
    }

    if ((flags & O_CREAT) != 0) {
        lfs_flags |= LFS_O_CREAT;
    }
    if ((flags & O_EXCL) != 0) {
        lfs_flags |= LFS_O_EXCL;
    }
    return lfs_flags;
}

int fs_mount(struct superblock *sb, struct fs_blkdev *bdev)
{
    int err;

    (void)bdev;

    if (g_fs_mounted != 0U) {
        if (sb != NULL) {
            *sb = g_fs_sb;
        }
        return 0;
    }

    err = LittleFs_SdMount(1U);
    if (err != LFS_ERR_OK) {
        return err;
    }

    g_fs_sb.magic = 0x4C465353U;
    g_fs_sb.block_size = LITTLEFS_SD_BLOCK_SIZE;
    g_fs_sb.total_blocks = LittleFs_SdGetBlockCount();
    g_fs_sb.inode_start = 0U;
    g_fs_sb.data_start = 0U;
    g_fs_sb.inode_count = 0U;
    if (sb != NULL) {
        *sb = g_fs_sb;
    }
    g_fs_mounted = 1U;
    return 0;
}

int fs_unmount(struct superblock *sb)
{
    (void)sb;

    if (g_fs_mounted == 0U) {
        return 0;
    }

    g_fs_mounted = 0U;
    return LittleFs_SdUnmount();
}

int fs_format(struct superblock *sb)
{
    int err;

    if (g_fs_mounted != 0U) {
        (void)LittleFs_SdUnmount();
        g_fs_mounted = 0U;
    }

    err = LittleFs_SdMount(1U);
    if (err == LFS_ERR_OK) {
        g_fs_mounted = 1U;
        (void)fs_mount(sb, NULL);
    }
    return err;
}

int fs_mkdir(const char *path, struct inode **out)
{
    struct inode *ino;
    const char *lpath;
    int err;

    if (fs_mount(NULL, NULL) != 0) {
        return -1;
    }

    lpath = fs_lfs_path(path);
    if ((lpath == NULL) || (strcmp(lpath, "/") == 0) || (lpath[0] == '\0')) {
        if (out != NULL) {
            *out = NULL;
        }
        return 0;
    }

    err = lfs_mkdir(LittleFs_SdGetHandle(), lpath);
    if ((err != LFS_ERR_OK) && (err != LFS_ERR_EXIST)) {
        return err;
    }

    if (out != NULL) {
        ino = heap_malloc(sizeof(*ino));
        if (ino == NULL) {
            return FS_ERR_NOMEM;
        }
        memset(ino, 0, sizeof(*ino));
        ino->ino = g_next_ino++;
        ino->din.mode = S_IFDIR;
        ino->refcnt = 1U;
        *out = ino;
    }
    return 0;
}

int fs_readdir(const char *path, struct dirent *buf, int max, int *nread)
{
    lfs_dir_t dir;
    struct lfs_info info;
    const char *lpath;
    int count = 0;

    if ((buf == NULL) || (max < 0) || (nread == NULL)) {
        return FS_ERR_INVAL;
    }
    *nread = 0;

    if (fs_mount(NULL, NULL) != 0) {
        return -1;
    }

    lpath = fs_lfs_path(path);
    if ((lpath == NULL) || (lpath[0] == '\0')) {
        lpath = "/";
    }

    if (lfs_dir_open(LittleFs_SdGetHandle(), &dir, lpath) != LFS_ERR_OK) {
        return -1;
    }

    while ((count < max) && (lfs_dir_read(LittleFs_SdGetHandle(), &dir, &info) > 0)) {
        if ((strcmp(info.name, ".") == 0) || (strcmp(info.name, "..") == 0)) {
            continue;
        }
        memset(&buf[count], 0, sizeof(buf[count]));
        buf[count].type = (info.type == LFS_TYPE_DIR) ? FILE_TYPE_DIR : FILE_TYPE_REG;
        buf[count].ino = (uint32_t)(count + 1);
        strncpy(buf[count].name, info.name, NAME_MAX - 1U);
        count++;
    }

    (void)lfs_dir_close(LittleFs_SdGetHandle(), &dir);
    *nread = count;
    return 0;
}

int fs_open(const char *path, int flags, struct inode **out)
{
    struct inode *ino;
    struct fs_file_handle *fh;
    const char *lpath;
    int err;

    if ((path == NULL) || (out == NULL)) {
        return FS_ERR_INVAL;
    }
    if (fs_mount(NULL, NULL) != 0) {
        return -1;
    }

    lpath = fs_lfs_path(path);
    if ((lpath == NULL) || (lpath[0] == '\0') || (strcmp(lpath, "/") == 0)) {
        return FS_ERR_INVAL;
    }

    ino = heap_malloc(sizeof(*ino));
    fh = heap_malloc(sizeof(*fh));
    if ((ino == NULL) || (fh == NULL)) {
        heap_free(ino);
        heap_free(fh);
        return FS_ERR_NOMEM;
    }

    memset(ino, 0, sizeof(*ino));
    memset(fh, 0, sizeof(*fh));
    strncpy(fh->path, lpath, sizeof(fh->path) - 1U);

    err = lfs_file_open(LittleFs_SdGetHandle(), &fh->file, lpath, fs_to_lfs_flags(flags));
    if (err != LFS_ERR_OK) {
        heap_free(fh);
        heap_free(ino);
        return err;
    }

    ino->ino = g_next_ino++;
    ino->refcnt = 1U;
    ino->din.mode = S_IFREG;
    ino->din.size = (uint32_t)lfs_file_size(LittleFs_SdGetHandle(), &fh->file);
    ino->priv = fh;
    *out = ino;
    return 0;
}

int fs_read(struct inode *inode, uint32_t off, void *buf, uint32_t len)
{
    struct fs_file_handle *fh;

    if ((inode == NULL) || (inode->priv == NULL) || (buf == NULL)) {
        return FS_ERR_INVAL;
    }
    fh = (struct fs_file_handle *)inode->priv;
    if (lfs_file_seek(LittleFs_SdGetHandle(), &fh->file, (lfs_soff_t)off, LFS_SEEK_SET) < 0) {
        return FS_ERR_IO;
    }
    return (int)lfs_file_read(LittleFs_SdGetHandle(), &fh->file, buf, len);
}

int fs_write(struct inode *inode, uint32_t off, const void *buf, uint32_t len)
{
    struct fs_file_handle *fh;
    lfs_ssize_t written;

    if ((inode == NULL) || (inode->priv == NULL) || (buf == NULL)) {
        return FS_ERR_INVAL;
    }
    fh = (struct fs_file_handle *)inode->priv;
    if (lfs_file_seek(LittleFs_SdGetHandle(), &fh->file, (lfs_soff_t)off, LFS_SEEK_SET) < 0) {
        return FS_ERR_IO;
    }

    written = lfs_file_write(LittleFs_SdGetHandle(), &fh->file, buf, len);
    if (written > 0) {
        uint32_t end = off + (uint32_t)written;
        if (end > inode->din.size) {
            inode->din.size = end;
        }
    }
    return (int)written;
}

int fs_close(struct inode *inode)
{
    struct fs_file_handle *fh;
    int err;

    if (inode == NULL) {
        return FS_ERR_INVAL;
    }

    fh = (struct fs_file_handle *)inode->priv;
    err = LFS_ERR_OK;
    if (fh != NULL) {
        err = lfs_file_close(LittleFs_SdGetHandle(), &fh->file);
        heap_free(fh);
    }
    heap_free(inode);
    return err;
}

int fs_truncate(struct inode *inode, uint32_t newsize)
{
    struct fs_file_handle *fh;

    if ((inode == NULL) || (inode->priv == NULL)) {
        return FS_ERR_INVAL;
    }

    fh = (struct fs_file_handle *)inode->priv;
    if (lfs_file_truncate(LittleFs_SdGetHandle(), &fh->file, (lfs_off_t)newsize) != LFS_ERR_OK) {
        return FS_ERR_IO;
    }
    inode->din.size = newsize;
    return 0;
}

int fs_sync(void)
{
    if (g_fs_mounted == 0U) {
        return 0;
    }
    return lfs_fs_mkconsistent(LittleFs_SdGetHandle());
}

uint32_t fs_get_size(struct inode *inode)
{
    struct fs_file_handle *fh;
    lfs_soff_t size;

    if ((inode == NULL) || (inode->priv == NULL)) {
        return 0U;
    }

    fh = (struct fs_file_handle *)inode->priv;
    size = lfs_file_size(LittleFs_SdGetHandle(), &fh->file);
    return (size > 0) ? (uint32_t)size : 0U;
}
