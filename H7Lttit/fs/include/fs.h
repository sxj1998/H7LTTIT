#ifndef FS_H
#define FS_H

#include <stdint.h>

#define FS_ERR_OK     0
#define FS_ERR_IO    -5
#define FS_ERR_NOMEM -12
#define FS_ERR_INVAL -22

#define S_IFMT   0xF000
#define S_IFREG  0x8000
#define S_IFDIR  0x4000

#define FILE_TYPE_UNKNOWN 0
#define FILE_TYPE_REG     1
#define FILE_TYPE_DIR     2

#define O_RDONLY 0x01
#define O_WRONLY 0x02
#define O_RDWR   0x04
#define O_CREAT  0x08
#define O_EXCL   0x10

#define NAME_MAX 32

struct superblock {
    uint32_t magic;
    uint32_t block_size;
    uint32_t total_blocks;
    uint32_t inode_start;
    uint32_t data_start;
    uint32_t inode_count;
};

struct dinode {
    uint16_t mode;
    uint32_t size;
};

struct inode {
    uint32_t ino;
    struct dinode din;
    uint16_t refcnt;
    void *priv;
};

struct dirent {
    uint8_t type;
    uint32_t ino;
    char name[NAME_MAX];
};

struct fs_blkdev {
    uint32_t block_size;
    uint32_t block_count;
    uint32_t prog_size;
    uint32_t read_size;
    void *ctx;

    int (*read)(void *ctx, uint32_t blk, uint32_t off, void *buf, uint32_t len);
    int (*write)(void *ctx, uint32_t blk, uint32_t off, const void *buf, uint32_t len);
    int (*erase)(void *ctx, uint32_t blk);
    int (*sync)(void *ctx);
};

int fs_mount(struct superblock *sb, struct fs_blkdev *bdev);
int fs_unmount(struct superblock *sb);
int fs_format(struct superblock *sb);

int fs_mkdir(const char *path, struct inode **out);
int fs_readdir(const char *path, struct dirent *buf, int max, int *nread);

int fs_open(const char *path, int flags, struct inode **out);
int fs_read(struct inode *inode, uint32_t off, void *buf, uint32_t len);
int fs_write(struct inode *inode, uint32_t off, const void *buf, uint32_t len);
int fs_close(struct inode *inode);

int fs_truncate(struct inode *inode, uint32_t newsize);
int fs_sync(void);

uint32_t fs_get_size(struct inode *inode);

#endif
