#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <inttypes.h>
#include <ncurses.h>

#define ASCII_W 24

#define PAIR_NORMAL 0
#define PAIR_SELECT 1

static struct {
    char *data;
    size_t size;
    size_t alloc;

    char *path;

    int running;
    int win_w, win_h;
    int ascii_w;
    int pos;
    int offset;
    int mode;
} hex;

static void init_curses();
static void deinit_curses();

static void update();

static char *read_file(const char *path, size_t *size);
static void write_file(const char *path, char *data, size_t size);

static void
init_curses()
{
    initscr();
    raw();
    noecho();
    curs_set(0);
    keypad(stdscr, TRUE);
    
    start_color();
    init_pair(PAIR_NORMAL, COLOR_WHITE, COLOR_BLACK);
    init_pair(PAIR_SELECT, COLOR_RED, COLOR_BLACK);
}

static void
deinit_curses()
{
    curs_set(1);
    endwin();
}

static char*
read_file(const char *path, size_t *size)
{
    char *data;
    FILE *stream = fopen(path, "rb");

    fseek(stream, 0, SEEK_END);
    *size = ftell(stream);
    rewind(stream);
    if (*size == 0)
        return NULL;

    data = calloc(1, *size);
    fread(data, *size, 1, stream);

    fclose(stream);
    return data;
}

static void
write_file(const char *path, char *data, size_t size)
{
    FILE *stream = fopen(path, "wb");
    fwrite(data, size, 1, stream);
    fclose(stream);
}

static void
update()
{
    getmaxyx(stdscr, hex.win_h, hex.win_w);

    // render
    clear();

    int byte_w = 3;
    int view_w = (hex.win_w - hex.ascii_w) / byte_w;
    int npages = hex.size / view_w + 1;

    char *hmode = (hex.mode == 0)? "hex" : "ascii";

    move(0, 0);
    printw("[view %d] [nbytes %d] [npages %d] [pos %d] [mode %s]",
            view_w, hex.size, npages, hex.pos, hmode);

    for (int page = 0; page < npages; ++page) {
        if (page - hex.offset < 0 || page - hex.offset >= hex.win_h-1)
            break;

        move(page + 2, 0);
        for (int i = 0; i < view_w; ++i) {
            int c = page * view_w + i;
            /* int c = i * (page+1); */
            if (c >= hex.size) break;

            if (c == hex.pos)
                attron(COLOR_PAIR(PAIR_SELECT));
            printw("%.2x ", hex.data[c]);
            if (c == hex.pos)
                attroff(COLOR_PAIR(PAIR_SELECT));
        }
    }

    if (hex.ascii_w) {
        int ascii_w = hex.win_w - (hex.win_w - hex.ascii_w);
        int nascii = hex.size / ascii_w + 1;

        for (int line = 0; line < nascii; ++line) {
            if (line - hex.offset < 0 || line - hex.offset >= hex.win_h-1)
                break;

            move(line + 2, hex.win_w - hex.ascii_w);
            for (int i = 0; i < ascii_w; ++i) {
                /* int c = i * (line+1); */
                int c = line * ascii_w + i;
                if (c >= hex.size) break;

                if (c == hex.pos)
                    attron(COLOR_PAIR(PAIR_SELECT));
                if (isgraph(hex.data[c]))
                    printw("%c", hex.data[c]);
                else
                    printw(".");
                if (c == hex.pos)
                    attroff(COLOR_PAIR(PAIR_SELECT));
            }
        }
    }

    // update
    int ch = getch();

    if (isxdigit(ch) && hex.mode == 0) {
        char xnum[3] = {0};
        xnum[0] = ch;

        int ch2 = getch();
        if (isxdigit(ch2))
            xnum[1] = ch2;

        int num = strtol(xnum, &xnum+1, 16);
        if (hex.pos >= hex.size) {
            hex.size++;
            if (hex.size >= hex.alloc) {
                hex.alloc += 500;
                hex.data = realloc(hex.data, hex.alloc);
            }
        }

        hex.data[hex.pos] = (char)num;
        if (hex.pos < hex.size)
            hex.pos++;
    }

    if (ch == 'q' && hex.mode == 0) {
        hex.running = FALSE;
        return;
    }
    if ((ch == 's' || ch == '\n') && hex.mode == 0) {
        hex.running = FALSE;
        write_file(hex.path, hex.data, hex.size);
    }

    switch (ch) {
    case KEY_DOWN:
        hex.pos += view_w;
        if (hex.pos >= hex.size)
            hex.pos = hex.size;
        break;
    case KEY_UP:
        hex.pos -= view_w;
        if (hex.pos <= 0)
            hex.pos = 0;
        break;
    case KEY_LEFT:
        if (hex.pos > 0)
            hex.pos--;
        break;
    case KEY_RIGHT:
        if (hex.pos < hex.size)
            hex.pos++;
        break;
    case '\t':
        hex.mode = !hex.mode;
        break;
    case KEY_BACKSPACE:
        memcpy(hex.data+hex.pos, hex.data+hex.pos+1, hex.size-hex.pos+1);
        hex.size--;
        if (hex.pos > 0)
            hex.pos--;
        break;
    default:
        if (hex.mode == 1) {
            if (hex.pos >= hex.size) {
                hex.size++;
                if (hex.size >= hex.alloc) {
                    hex.alloc += 500;
                    hex.data = realloc(hex.data, hex.alloc);
                }
            }
            hex.data[hex.pos] = ch;
            if (hex.pos < hex.size)
                hex.pos++;
        }
        break;
    }
}

int
main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: %s <file>\n", argv[0]);
        return 1;
    }

    hex.win_w = hex.win_h = 1;
    hex.ascii_w = ASCII_W;
    hex.offset = hex.pos = 0;
    hex.mode = 0;
    hex.running = TRUE;

    hex.path = argv[1];
    hex.size = 0;
    hex.data = read_file(hex.path, &hex.size);
    hex.alloc = hex.size;

    if (!hex.size || !hex.data) {
        fprintf(stderr, "error: could not read file '%s'\n", hex.path);
        return 1;
    }

    init_curses();

    while (hex.running) {
        update();
    }

    free(hex.data);
    deinit_curses();
    return 0;
}
