#include "shell.h"

#include "comm.h"
#include "fs.h"
#include "heap.h"
#include "vfs.h"
#include "vim.h"

#include "FreeRTOS.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char linebuf[SHELL_MAX_LINE];
static char *argv_buf[SHELL_MAX_ARGS];
static char cwd[96];
static char shell_path[96];
static int line_pos;
static int prompt_shown;

static int shell_is_delim(char c)
{
    return (c == ' ') || (c == '\t') || (c == '\r') || (c == '\n');
}

static void shell_write_text(const char *buf, int len)
{
    if ((buf == NULL) || (len <= 0)) {
        return;
    }

    for (int i = 0; i < len; i++) {
        if (buf[i] == '\n') {
            comm_write("\r\n", 2);
        } else {
            comm_putc(buf[i]);
        }
    }
}

static void make_abs_path(char *out, const char *in)
{
    if ((in == NULL) || (in[0] == '\0')) {
        strcpy(out, cwd[0] != '\0' ? cwd : "/");
        return;
    }

    if (in[0] == '/') {
        strncpy(out, in, 95U);
        out[95] = '\0';
        return;
    }

    if ((cwd[0] == '\0') || (strcmp(cwd, "/") == 0)) {
        snprintf(out, 96U, "/%s", in);
    } else {
        snprintf(out, 96U, "%s/%s", cwd, in);
    }
}

static int cmd_help(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    comm_write("commands: help mem pwd ls cat touch write mkdir cd vfs vim reboot\r\n",
               (int)(sizeof("commands: help mem pwd ls cat touch write mkdir cd vfs vim reboot\r\n") - 1U));
    return 0;
}

static int cmd_mem(int argc, char **argv)
{
    char buf[128];
    struct heap_stats st;
    int len;

    (void)argc;
    (void)argv;
    st = heap_get_stats();
    len = snprintf(buf, sizeof(buf), "heap_free=%lu heap_min=%lu\r\n",
                   (unsigned long)st.remain_size,
                   (unsigned long)st.max_free_block);
    if (len > 0) {
        comm_write(buf, len);
    }
    return 0;
}

static int cmd_pwd(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    comm_write(cwd, (int)strlen(cwd));
    comm_write("\r\n", 2);
    return 0;
}

static int cmd_ls(int argc, char **argv)
{
    struct dirent entries[16];
    int nread = 0;

    make_abs_path(shell_path, (argc > 1) ? argv[1] : cwd);
    if (fs_readdir(shell_path, entries, (int)(sizeof(entries) / sizeof(entries[0])), &nread) != 0) {
        comm_write("ls failed\r\n", 11);
        return -1;
    }

    for (int i = 0; i < nread; i++) {
        if (entries[i].type == FILE_TYPE_DIR) {
            comm_write("[d] ", 4);
        } else {
            comm_write("    ", 4);
        }
        comm_write(entries[i].name, (int)strlen(entries[i].name));
        comm_write("\r\n", 2);
    }
    return 0;
}

static int cmd_touch(int argc, char **argv)
{
    struct inode *ino;

    if (argc < 2) {
        comm_write("usage: touch <path>\r\n", 21);
        return -1;
    }

    make_abs_path(shell_path, argv[1]);
    if (fs_open(shell_path, O_CREAT | O_RDWR, &ino) != 0) {
        comm_write("touch failed\r\n", 14);
        return -1;
    }
    fs_close(ino);
    return 0;
}

static int cmd_write(int argc, char **argv)
{
    struct inode *ino;
    const char *text;
    int len;

    if (argc < 3) {
        comm_write("usage: write <path> <text>\r\n", 28);
        return -1;
    }

    make_abs_path(shell_path, argv[1]);
    text = argv[2];
    if (fs_open(shell_path, O_CREAT | O_RDWR, &ino) != 0) {
        comm_write("write open failed\r\n", 19);
        return -1;
    }

    (void)fs_truncate(ino, 0U);
    len = (int)strlen(text);
    if (fs_write(ino, 0U, text, (uint32_t)len) != len) {
        fs_close(ino);
        comm_write("write failed\r\n", 14);
        return -1;
    }
    fs_write(ino, (uint32_t)len, "\n", 1U);
    fs_close(ino);
    return 0;
}

static int cmd_cat(int argc, char **argv)
{
    struct inode *ino;
    char buf[128];
    int len;

    if (argc < 2) {
        comm_write("usage: cat <path>\r\n", 19);
        return -1;
    }

    make_abs_path(shell_path, argv[1]);
    if (fs_open(shell_path, O_RDONLY, &ino) != 0) {
        comm_write("cat failed\r\n", 12);
        return -1;
    }

    for (uint32_t off = 0U;;) {
        len = fs_read(ino, off, buf, sizeof(buf));
        if (len <= 0) {
            break;
        }
        shell_write_text(buf, len);
        off += (uint32_t)len;
    }
    fs_close(ino);
    comm_write("\r\n", 2);
    return 0;
}

static int cmd_mkdir(int argc, char **argv)
{
    struct inode *ino = NULL;

    if (argc < 2) {
        comm_write("usage: mkdir <path>\r\n", 21);
        return -1;
    }

    make_abs_path(shell_path, argv[1]);
    if (fs_mkdir(shell_path, &ino) != 0) {
        comm_write("mkdir failed\r\n", 14);
        return -1;
    }
    fs_close(ino);
    return 0;
}

static int cmd_cd(int argc, char **argv)
{
    struct dirent entries[1];
    int nread;

    if (argc < 2) {
        strcpy(cwd, "/");
        return 0;
    }

    make_abs_path(shell_path, argv[1]);
    if (fs_readdir(shell_path, entries, 1, &nread) != 0) {
        comm_write("cd failed\r\n", 11);
        return -1;
    }

    strncpy(cwd, shell_path, sizeof(cwd) - 1U);
    cwd[sizeof(cwd) - 1U] = '\0';
    return 0;
}

static int cmd_vfs(int argc, char **argv)
{
    char buf[192];
    struct vfs_dump_ctx ctx;
    int len;

    (void)argc;
    (void)argv;
    memset(buf, 0, sizeof(buf));
    ctx.buf = buf;
    ctx.len = (int)sizeof(buf);
    ctx.pos = 0;

    len = vfs_dump(&ctx);
    if (len <= 0) {
        comm_write("vfs empty\r\n", 11);
        return 0;
    }
    comm_write(buf, len);
    return 0;
}

static int cmd_vim(int argc, char **argv)
{
    if (argc < 2) {
        comm_write("usage: vim <path>\r\n", 19);
        return -1;
    }

    make_abs_path(shell_path, argv[1]);
    vim_main(shell_path);
    return 0;
}

static int cmd_reboot(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    NVIC_SystemReset();
    return 0;
}

struct cmd_entry {
    const char *name;
    int (*func)(int argc, char **argv);
};

static const struct cmd_entry cmd_table[] = {
    {"help", cmd_help},
    {"mem", cmd_mem},
    {"pwd", cmd_pwd},
    {"ls", cmd_ls},
    {"cat", cmd_cat},
    {"touch", cmd_touch},
    {"write", cmd_write},
    {"mkdir", cmd_mkdir},
    {"cd", cmd_cd},
    {"vfs", cmd_vfs},
    {"vim", cmd_vim},
    {"reboot", cmd_reboot},
    {NULL, NULL}
};

void shell_init(void)
{
    line_pos = 0;
    prompt_shown = 0;
    strcpy(cwd, "/");
    memset(linebuf, 0, sizeof(linebuf));
}

int shell_readline(char *buf, int max)
{
    int pos = 0;

    for (;;) {
        char c = comm_getc();
        if ((c == '\r') || (c == '\n')) {
            comm_write("\r\n", 2);
            buf[pos] = '\0';
            return pos;
        }

        if ((c == '\b') || (c == 127)) {
            if (pos > 0) {
                pos--;
                comm_write(SHELL_BACKSPACE_SEQ, SHELL_BACKSPACE_SEQ_LEN);
            }
            continue;
        }

        if (pos < (max - 1)) {
            buf[pos++] = c;
            comm_putc(c);
        }
    }
}

int shell_parse(char *line, char **argv, int max)
{
    int argc = 0;

    while ((*line != '\0') && (argc < max)) {
        while (shell_is_delim(*line)) {
            line++;
        }
        if (*line == '\0') {
            break;
        }

        argv[argc++] = line;
        while ((*line != '\0') && !shell_is_delim(*line)) {
            line++;
        }
        if (*line != '\0') {
            *line++ = '\0';
        }
    }
    return argc;
}

void shell_exec(int argc, char **argv)
{
    if ((argc <= 0) || (argv == NULL) || (argv[0] == NULL) || (argv[0][0] == '\0')) {
        return;
    }

    for (int i = 0; cmd_table[i].name != NULL; i++) {
        if (strcmp(argv[0], cmd_table[i].name) == 0) {
            (void)cmd_table[i].func(argc, argv);
            return;
        }
    }

    comm_write("unknown command: ", 17);
    comm_write(argv[0], (int)strlen(argv[0]));
    comm_write("\r\n", 2);
}

void shell_main(void)
{
    int len;
    int argc;

    comm_write(SHELL_PROMPT, (int)strlen(SHELL_PROMPT));
    len = shell_readline(linebuf, SHELL_MAX_LINE);
    if (len <= 0) {
        return;
    }

    argc = shell_parse(linebuf, argv_buf, SHELL_MAX_ARGS);
    if (argc > 0) {
        shell_exec(argc, argv_buf);
    }
}

void shell_on_message(const char *msg, int len)
{
    int argc;

    if ((msg == NULL) || (len <= 0)) {
        return;
    }

    if (len >= SHELL_MAX_LINE) {
        len = SHELL_MAX_LINE - 1;
    }

    memcpy(linebuf, msg, (size_t)len);
    linebuf[len] = '\0';
    argc = shell_parse(linebuf, argv_buf, SHELL_MAX_ARGS);
    if (argc > 0) {
        shell_exec(argc, argv_buf);
    }
}

void shell_poll(void)
{
    char c;
    int argc;
    int drained = 0;

    if (prompt_shown == 0) {
        comm_write(SHELL_PROMPT, 2);
        prompt_shown = 1;
    }

    while ((comm_peek() >= 0) && (drained < 32)) {
        drained++;
        c = comm_getc();
        if ((c == '\r') || (c == '\n')) {
            comm_write("\r\n", 2);
            linebuf[line_pos] = '\0';
            argc = shell_parse(linebuf, argv_buf, SHELL_MAX_ARGS);
            if (argc > 0) {
                shell_exec(argc, argv_buf);
            }
            line_pos = 0;
            linebuf[0] = '\0';
            comm_write(SHELL_PROMPT, 2);
            continue;
        }

        if ((c == '\b') || (c == 127)) {
            if (line_pos > 0) {
                line_pos--;
                linebuf[line_pos] = '\0';
                comm_write(SHELL_BACKSPACE_SEQ, SHELL_BACKSPACE_SEQ_LEN);
            }
            continue;
        }

        if (line_pos < (SHELL_MAX_LINE - 1)) {
            linebuf[line_pos++] = c;
            linebuf[line_pos] = '\0';
            comm_putc(c);
        }
    }
}
