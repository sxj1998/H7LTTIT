#include "vim.h"

#include "comm.h"
#include "fs.h"
#include "heap.h"

#include <stdio.h>
#include <string.h>

struct line {
    char *data;
    int len;
};

struct buffer {
    struct line *lines;
    int line_count;
    int cursor_x;
    int cursor_y;
};

static struct line empty_line = { .data = "", .len = 0 };
static struct buffer empty_buf = {
    .lines = &empty_line,
    .line_count = 1,
    .cursor_x = 0,
    .cursor_y = 0,
};

static char g_vim_status[VIM_SCREEN_BUF];

static char *dup_str(const char *s)
{
    size_t len;
    char *p;

    len = strlen(s) + 1U;
    p = heap_malloc(len);
    if (p == NULL) {
        return NULL;
    }

    memcpy(p, s, len);
    return p;
}

static char *grow_line(char *old, int old_len, int new_len)
{
    char *p;

    p = heap_malloc((size_t)new_len);
    if (p == NULL) {
        return NULL;
    }

    if ((old != NULL) && (old_len > 0)) {
        memcpy(p, old, (size_t)old_len);
    }
    if (old != NULL) {
        heap_free(old);
    }
    return p;
}

static struct line *grow_lines(struct line *old, int old_count, int new_count)
{
    size_t sz;
    struct line *p;

    sz = (size_t)new_count * sizeof(struct line);
    p = heap_malloc(sz);
    if (p == NULL) {
        return NULL;
    }

    if ((old != NULL) && (old_count > 0)) {
        memcpy(p, old, (size_t)old_count * sizeof(struct line));
        heap_free(old);
    }
    return p;
}

static void buf_insert_char(struct buffer *b, char c)
{
    struct line *ln;
    char *tmp;

    ln = &b->lines[b->cursor_y];

    tmp = grow_line(ln->data, ln->len + 1, ln->len + 2);
    if (tmp == NULL) {
        return;
    }

    ln->data = tmp;

    memmove(ln->data + b->cursor_x + 1,
            ln->data + b->cursor_x,
            (size_t)(ln->len - b->cursor_x + 1));

    ln->data[b->cursor_x] = c;
    ln->len++;
    b->cursor_x++;
}

static void buf_backspace(struct buffer *b)
{
    struct line *ln;

    ln = &b->lines[b->cursor_y];

    if (b->cursor_x == 0) {
        int prev;
        struct line *pl;
        char *tmp;

        if (b->cursor_y == 0) {
            return;
        }

        prev = b->cursor_y - 1;
        pl = &b->lines[prev];

        tmp = grow_line(pl->data, pl->len + 1, pl->len + ln->len + 1);
        if (tmp == NULL) {
            return;
        }

        pl->data = tmp;
        memcpy(pl->data + pl->len, ln->data, (size_t)ln->len + 1U);
        pl->len += ln->len;

        heap_free(ln->data);

        memmove(&b->lines[b->cursor_y],
                &b->lines[b->cursor_y + 1],
                (size_t)(b->line_count - b->cursor_y - 1) * sizeof(struct line));

        b->line_count--;
        b->cursor_y--;
        b->cursor_x = pl->len - ln->len;
        return;
    }

    memmove(ln->data + b->cursor_x - 1,
            ln->data + b->cursor_x,
            (size_t)(ln->len - b->cursor_x + 1));

    ln->len--;
    b->cursor_x--;
}

static void buf_newline(struct buffer *b)
{
    struct line *ln;
    char *right;
    int right_len;
    struct line *tmp;

    ln = &b->lines[b->cursor_y];

    right = dup_str(ln->data + b->cursor_x);
    if (right == NULL) {
        return;
    }

    right_len = (int)strlen(right);

    ln->data[b->cursor_x] = '\0';
    ln->len = b->cursor_x;

    tmp = grow_lines(b->lines, b->line_count, b->line_count + 1);
    if (tmp == NULL) {
        heap_free(right);
        return;
    }

    b->lines = tmp;

    memmove(&b->lines[b->cursor_y + 2],
            &b->lines[b->cursor_y + 1],
            (size_t)(b->line_count - b->cursor_y - 1) * sizeof(struct line));

    b->lines[b->cursor_y + 1].data = right;
    b->lines[b->cursor_y + 1].len = right_len;

    b->line_count++;
    b->cursor_y++;
    b->cursor_x = 0;
}

static void buf_delete_char(struct buffer *b)
{
    struct line *ln;

    ln = &b->lines[b->cursor_y];
    if (b->cursor_x >= ln->len) {
        return;
    }

    memmove(ln->data + b->cursor_x,
            ln->data + b->cursor_x + 1,
            (size_t)(ln->len - b->cursor_x));

    ln->len--;
}

static int buf_load(struct buffer *b, const char *path)
{
    struct inode *ino;
    uint32_t fsize;
    char small[VIM_SMALL_LOAD_BUF];
    char *buf;
    char *heapbuf;
    int r;
    char *save_ptr;
    char *p;

    if (fs_open(path, 0, &ino) < 0) {
        b->lines = heap_malloc(sizeof(struct line));
        if (b->lines == NULL) {
            return -1;
        }

        b->lines[0].data = dup_str("");
        if (b->lines[0].data == NULL) {
            heap_free(b->lines);
            b->lines = NULL;
            return -1;
        }
        b->lines[0].len = 0;
        b->line_count = 1;
        b->cursor_x = 0;
        b->cursor_y = 0;
        return 0;
    }

    fsize = fs_get_size(ino);
    buf = NULL;
    heapbuf = NULL;

    if ((fsize + 1U) <= sizeof(small)) {
        buf = small;
    } else {
        heapbuf = heap_malloc((size_t)fsize + 1U);
        if (heapbuf == NULL) {
            fs_close(ino);
            return -1;
        }
        buf = heapbuf;
    }

    r = fs_read(ino, 0U, buf, fsize);
    fs_close(ino);

    if (r < 0) {
        r = 0;
    }
    buf[r] = '\0';

    b->line_count = 0;
    b->lines = NULL;

    p = strtok_r(buf, "\n", &save_ptr);
    while (p != NULL) {
        struct line *new_lines;

        new_lines = grow_lines(b->lines, b->line_count, b->line_count + 1);
        if (new_lines == NULL) {
            if (heapbuf != NULL) {
                heap_free(heapbuf);
            }
            return -1;
        }

        b->lines = new_lines;
        b->lines[b->line_count].data = dup_str(p);
        if (b->lines[b->line_count].data == NULL) {
            if (heapbuf != NULL) {
                heap_free(heapbuf);
            }
            return -1;
        }
        b->lines[b->line_count].len = (int)strlen(p);
        b->line_count++;

        p = strtok_r(NULL, "\n", &save_ptr);
    }

    if (b->line_count == 0) {
        b->lines = heap_malloc(sizeof(struct line));
        if (b->lines == NULL) {
            if (heapbuf != NULL) {
                heap_free(heapbuf);
            }
            return -1;
        }
        b->lines[0].data = dup_str("");
        if (b->lines[0].data == NULL) {
            heap_free(b->lines);
            b->lines = NULL;
            if (heapbuf != NULL) {
                heap_free(heapbuf);
            }
            return -1;
        }
        b->lines[0].len = 0;
        b->line_count = 1;
    }

    b->cursor_x = 0;
    b->cursor_y = 0;

    if (heapbuf != NULL) {
        heap_free(heapbuf);
    }

    return 0;
}

static int buf_save(struct buffer *b, const char *path)
{
    struct inode *ino;
    uint32_t total;
    int i;

    if (fs_open(path, O_CREAT | O_RDWR, &ino) < 0) {
        return -1;
    }

    total = 0U;
    for (i = 0; i < b->line_count; i++) {
        total += (uint32_t)b->lines[i].len + 1U;
    }

    if (total <= VIM_SMALL_SAVE_BUF) {
        char tmp[VIM_SMALL_SAVE_BUF];
        uint32_t off;

        off = 0U;
        for (i = 0; i < b->line_count; i++) {
            memcpy(tmp + off, b->lines[i].data, (size_t)b->lines[i].len);
            off += (uint32_t)b->lines[i].len;
            tmp[off++] = '\n';
        }

        fs_truncate(ino, 0U);
        fs_write(ino, 0U, tmp, total);
        fs_close(ino);
        return 0;
    }

    {
        char *mem;
        uint32_t off;

        mem = heap_malloc((size_t)total);
        if (mem == NULL) {
            fs_close(ino);
            return -1;
        }

        off = 0U;
        for (i = 0; i < b->line_count; i++) {
            memcpy(mem + off, b->lines[i].data, (size_t)b->lines[i].len);
            off += (uint32_t)b->lines[i].len;
            mem[off++] = '\n';
        }

        fs_truncate(ino, 0U);
        fs_write(ino, 0U, mem, total);
        heap_free(mem);
    }

    fs_close(ino);
    return 0;
}

static void buf_free(struct buffer *b)
{
    int i;

    if (b->lines == NULL) {
        return;
    }

    for (i = 0; i < b->line_count; i++) {
        heap_free(b->lines[i].data);
    }

    heap_free(b->lines);
    b->lines = NULL;
    b->line_count = 0;
}

static void vim_set_status(const char *msg)
{
    if (msg == NULL) {
        g_vim_status[0] = '\0';
        return;
    }

    strncpy(g_vim_status, msg, sizeof(g_vim_status) - 1U);
    g_vim_status[sizeof(g_vim_status) - 1U] = '\0';
}

static char vim_read_key(int *skip_next_lf)
{
    char c;

    for (;;) {
        c = comm_getc();

        if ((*skip_next_lf != 0) && (c == '\n')) {
            *skip_next_lf = 0;
            continue;
        }

        if (c == '\r') {
            *skip_next_lf = 1;
            return '\n';
        }

        *skip_next_lf = 0;
        return c;
    }
}

static int vim_read_command(char *cmd, int cmd_len, int *skip_next_lf)
{
    int pos;

    pos = 0;
    for (;;) {
        char k;

        k = vim_read_key(skip_next_lf);
        if (k == '\n') {
            cmd[pos] = '\0';
            return 0;
        }

        if ((k == 127) || (k == '\b')) {
            if (pos > 0) {
                pos--;
                comm_write("\b \b", 3);
            }
            continue;
        }

        if ((unsigned char)k < 0x20U) {
            continue;
        }

        if (pos < (cmd_len - 1)) {
            cmd[pos++] = k;
            comm_putc(k);
        }
    }
}

static void screen_draw(struct buffer *b, int insert_mode)
{
    char buf[VIM_SCREEN_BUF];
    int i;
    int n;

    comm_write("\x1b[2J", 4);
    comm_write("\x1b[H", 3);

    for (i = 0; i < b->line_count; i++) {
        comm_write(b->lines[i].data, b->lines[i].len);
        comm_write("\r\n", 2);
    }

    n = snprintf(buf, sizeof(buf), "\x1b[%d;1H", b->line_count + 2);
    if (n > 0) {
        comm_write(buf, n);
    }

    comm_write("\x1b[2K", 4);
    if (insert_mode != 0) {
        comm_write("-- INSERT --", 12);
    } else if (g_vim_status[0] != '\0') {
        comm_write(g_vim_status, (int)strlen(g_vim_status));
    }

    n = snprintf(buf, sizeof(buf), "\x1b[%d;%dH", b->cursor_y + 1, b->cursor_x + 1);
    if (n > 0) {
        comm_write(buf, n);
    }
}

void vim_main(const char *path)
{
    struct buffer buf;
    int insert_mode;
    int skip_next_lf;

    memset(&buf, 0, sizeof(buf));
    vim_set_status(NULL);

    if (buf_load(&buf, path) < 0) {
        return;
    }

    insert_mode = 0;
    skip_next_lf = 0;

    for (;;) {
        char c;

        if (buf.cursor_y >= buf.line_count) {
            buf.cursor_y = buf.line_count - 1;
        }
        if (buf.cursor_y < 0) {
            buf.cursor_y = 0;
        }

        if (buf.cursor_x > buf.lines[buf.cursor_y].len) {
            buf.cursor_x = buf.lines[buf.cursor_y].len;
        }
        if (buf.cursor_x < 0) {
            buf.cursor_x = 0;
        }

        screen_draw(&buf, insert_mode);

        c = vim_read_key(&skip_next_lf);

        if ((unsigned char)c == 0x1BU) {
            insert_mode = 0;
            vim_set_status(NULL);
            continue;
        }

        if (insert_mode == 0) {
            if (c == 'i') {
                insert_mode = 1;
                vim_set_status(NULL);
            } else if (c == 'h') {
                if (buf.cursor_x > 0) {
                    buf.cursor_x--;
                }
            } else if (c == 'l') {
                if (buf.cursor_x < buf.lines[buf.cursor_y].len) {
                    buf.cursor_x++;
                }
            } else if (c == 'j') {
                if (buf.cursor_y < (buf.line_count - 1)) {
                    buf.cursor_y++;
                }
            } else if (c == 'k') {
                if (buf.cursor_y > 0) {
                    buf.cursor_y--;
                }
            } else if (c == 'x') {
                buf_delete_char(&buf);
            } else if (c == ':') {
                char cmd[VIM_CMD_BUF_SIZE];
                int pos;
                char tmp[VIM_SCREEN_BUF];
                int n;

                pos = 0;
                n = snprintf(tmp, sizeof(tmp), "\x1b[%d;1H:", buf.line_count + 2);
                if (n > 0) {
                    comm_write(tmp, n);
                }
                (void)pos;
                (void)vim_read_command(cmd, (int)sizeof(cmd), &skip_next_lf);

                if (strcmp(cmd, "w") == 0) {
                    if (buf_save(&buf, path) == 0) {
                        vim_set_status("written");
                    } else {
                        vim_set_status("write failed");
                    }
                } else if ((strcmp(cmd, "wq") == 0) || (strcmp(cmd, "x") == 0)) {
                    if (buf_save(&buf, path) == 0) {
                        screen_draw(&empty_buf, 0);
                        comm_write("\r\n", 2);
                        buf_free(&buf);
                        return;
                    }
                    vim_set_status("write failed");
                } else if (strcmp(cmd, "q") == 0) {
                    screen_draw(&empty_buf, 0);
                    comm_write("\r\n", 2);
                    buf_free(&buf);
                    return;
                } else {
                    vim_set_status("unknown command");
                }
                continue;
            }
            continue;
        }

        vim_set_status(NULL);

        if (c == '\n') {
            buf_newline(&buf);
            continue;
        }
        if ((c == 127) || (c == '\b')) {
            buf_backspace(&buf);
            continue;
        }
        if ((unsigned char)c < 0x20U) {
            continue;
        }

        buf_insert_char(&buf, c);
    }
}
