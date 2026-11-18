/* =========================================================================
 * tinyos - 32-bit kernel / graphical shell
 *  - VESA linear framebuffer (24bpp BGR or 32bpp BGRX)
 *  - double buffered scene + cursor/hover-only updates (no mouse lag)
 *  - desktop, taskbar, draggable windows, PS/2 mouse, keyboard
 *  - TINY BROWSER: fetches real web pages through COM1 -> host helper
 * ========================================================================= */

#include "font.h"

typedef unsigned char  u8;
typedef unsigned short u16;
typedef unsigned int   u32;

/* ---- info filled in by the bootloader at 0x0500 ---- */
static volatile u8 *fb;          /* real VESA framebuffer */
static u8 *scene;                /* full frame WITHOUT cursor and hover */
static u8 *screen_buf;           /* finished frame WITH hover and cursor */
static u8 *dbuf;                 /* current draw target */
static u32 pitch_bytes;
static u32 screen_w, screen_h;
static u32 bytespp, bpp_val;

#define RGB(r, g, b) (((u32)(r) << 16) | ((u32)(g) << 8) | (u32)(b))

static inline void outb(u16 port, u8 val) {
    __asm__ volatile ("outb %0, %1" : : "a"(val), "dN"(port));
}
static inline u8 inb(u16 port) {
    u8 v;
    __asm__ volatile ("inb %1, %0" : "=a"(v) : "dN"(port));
    return v;
}
static inline u32 lerp(u32 a, u32 b, int t) {
    return RGB((((a >> 16) & 255) * (100 - t) + ((b >> 16) & 255) * t) / 100,
               (((a >> 8) & 255)  * (100 - t) + ((b >> 8) & 255)  * t) / 100,
               (((a) & 255)       * (100 - t) + ((b) & 255)       * t) / 100);
}

/* --------------------------- serial (host link) ------------------------- */
/* COM1: "GET <url>\n"  ->  host helper ->  u32 LE length + raw body */
#define PAGE_BUF ((char *)0xA00000)
#define PAGE_MAX 60000

static int starts_ci(const char *s, const char *p);
static void cstr_copy(char *dst, const char *src, int sz);

static u32 page_len = 0;
static int page_lines = 0;
static int page_scroll = 0;
static char page_url[200] = "http://example.com";
static char prev_url[200];
static int prev_valid = 0;
static int url_len = 0;
static char page_title[96] = "TINY BROWSER";
static char status_msg[80] = "READY";
static int url_pos = 0;                 /* caret position inside page_url */
static int ctrl_down = 0;
static int shift_down = 0;
static int mp_need = 3;

typedef struct {
    int x, y, w, h;
    char url[200];
    char label[64];
} Link;
#define MAX_LINKS 64
static Link links[MAX_LINKS];
static int nlinks = 0;

/* Start menu: 8 = OS button, 200..205 = items, 300.. = page links */
#define MENU_X 8
#define MENU_W 232
#define MENU_H 152
#define MENU_ITEMS 6
static int menu_open = 0;
static int mouse4 = 0;                 /* Intellimouse: 4-byte packets */
static u32 ticks = 0;
static int last_click_sec = -1;
static int last_click_win = -1;
static int dd_day = 1, dd_month = 1, dd_year = 2026;

static void serial_init(void) {
    outb(0x3FB, 0x80);            /* DLAB on */
    outb(0x3F8, 0x01);            /* 115200 baud */
    outb(0x3F9, 0x00);
    outb(0x3FB, 0x03);            /* 8N1 */
    outb(0x3FA, 0x01);            /* FIFO on */
    outb(0x3F9, 0x00);            /* polling only */
}

static int serial_put(u8 c) {
    u32 t;
    for (t = 0; t < 2000000; t++)
        if (inb(0x3FD) & 0x20) { outb(0x3F8, c); return 1; }
    return 0;
}

static int serial_get(u8 *c) {
    u32 t;
    for (t = 0; t < 10000000; t++)
        if (inb(0x3FD) & 1) { *c = inb(0x3F8); return 1; }
    return 0;
}

static void serial_puts(const char *s) {
    while (*s) serial_put((u8)*s++);
}

static void page_offline(void) {
    static const char s[] =
        "<h1>OFFLINE</h1>"
        "<p>The host network helper is not running.</p>"
        "<p>Start QEMU with ./run.sh to browse the real internet.</p>";
    int i;
    for (i = 0; s[i]; i++) PAGE_BUF[i] = s[i];
    PAGE_BUF[i] = 0;
    page_len = (u32)i;
    page_scroll = 0;
    cstr_copy(page_title, "OFFLINE", sizeof(page_title));
    cstr_copy(status_msg, "HOST HELPER NOT RUNNING", sizeof(status_msg));
}

static int fetch_page(const char *url) {
    u8 c;
    u32 i, n = 0;
    char *buf = PAGE_BUF;

    serial_puts("GET ");
    serial_puts(url);
    serial_put('\n');

    for (i = 0; i < 4; i++) {
        if (!serial_get(&c)) { page_offline(); return 0; }
        n |= (u32)c << (i * 8);
    }
    if (n == 0 || n > PAGE_MAX) { page_offline(); return 0; }
    for (i = 0; i < n; i++) {
        if (!serial_get(&c)) break;
        buf[i] = (char)c;
    }
    buf[i] = 0;
    page_len = i;
    page_scroll = 0;
    cstr_copy(status_msg, "LOADED", sizeof(status_msg));
    return i > 0;
}

static int cstrlen(const char *s) { int n = 0; while (s[n]) n++; return n; }
static void cstr_copy(char *dst, const char *src, int sz) {
    int i = 0;
    while (src[i] && i < sz - 1) { dst[i] = src[i]; i++; }
    dst[i] = 0;
}

static void page_home(void) {
    static const char home[] =
        "<h1>TINY BROWSER</h1>"
        "<p>Real internet inside TINY OS.</p>"
        "<p>Type an address and press ENTER, or click a link.</p>"
        "<hr>"
        "<ul>"
        "<li><a href=\"http://example.com\">Open example.com</a></li>"
        "<li><a href=\"http://info.cern.ch\">The first website ever</a></li>"
        "</ul>"
        "<hr>"
        "<p><b>EDIT:</b> click the address bar, use LEFT/RIGHT, HOME/END,"
        " BACKSPACE and DELETE. Ctrl+L focuses the bar, Ctrl+R reloads.</p>"
        "<p>UP/DOWN and PageUp/PageDown scroll. BACK returns to the previous page.</p>";
    int i;
    for (i = 0; home[i]; i++) PAGE_BUF[i] = home[i];
    PAGE_BUF[i] = 0;
    page_len = (u32)i;
    page_scroll = 0;
    cstr_copy(page_title, "TINY BROWSER", sizeof(page_title));
    cstr_copy(status_msg, "START PAGE", sizeof(status_msg));
    url_pos = url_len;
}

static void url_set(const char *s) {
    int i = 0;
    while (s[i] && i < 190) { page_url[i] = s[i]; i++; }
    page_url[i] = 0;
    url_len = i;
    url_pos = i;
}

/* Accept bare domains and plain search terms in the address bar. */
static int prepare_url(const char *u, char *out, int sz) {
    int i = 0, j = 0, has_scheme = starts_ci(u, "http://") || starts_ci(u, "https://");
    int has_dot = 0, has_space = 0;
    while (u[i]) {
        if (u[i] == '.') has_dot = 1;
        if (u[i] == ' ') has_space = 1;
        i++;
    }
    i = 0;
    if (!has_scheme && !has_dot) {
        const char *q = "https://lite.duckduckgo.com/lite/?q=";
        while (q[j] && j < sz - 1) { out[j] = q[j]; j++; }
        while (u[i] && j < sz - 1) {
            out[j++] = (u[i] == ' ') ? '+' : u[i];
            i++;
        }
    } else {
        if (!has_scheme) {
            out[j++] = 'h'; out[j++] = 't'; out[j++] = 't'; out[j++] = 'p';
            out[j++] = ':'; out[j++] = '/'; out[j++] = '/';
        }
        while (u[i] && j < sz - 1) out[j++] = u[i++];
    }
    out[j] = 0;
    return has_space;
}

static void navigate(const char *u) {
    int i;
    char full[200];
    if (page_url[0]) {
        for (i = 0; page_url[i]; i++) prev_url[i] = page_url[i];
        prev_url[i] = 0;
        prev_valid = 1;
    }
    prepare_url(u, full, sizeof(full));
    url_set(full);
    page_title[0] = 0;
    fetch_page(page_url);
    if (!page_title[0]) cstr_copy(page_title, "TINY BROWSER", sizeof(page_title));
    cstr_copy(status_msg, page_url, sizeof(status_msg));
}

static void go_back(void) {
    if (!prev_valid) return;
    url_set(prev_url);
    page_title[0] = 0;
    fetch_page(page_url);
    if (!page_title[0]) cstr_copy(page_title, "TINY BROWSER", sizeof(page_title));
    cstr_copy(status_msg, prev_url, sizeof(status_msg));
    prev_valid = 0;
}

static int starts_ci(const char *s, const char *p) {
    while (*p) {
        char a = *s++, b = *p++;
        if (a >= 'A' && a <= 'Z') a += 32;
        if (b >= 'A' && b <= 'Z') b += 32;
        if (a != b) return 0;
    }
    return 1;
}

/* Turn a relative link into an absolute URL using the current page. */
static void resolve_url(const char *rel, char *out, int sz) {
    int i = 0, j = 0, p = 0, host_start, host_end, last = -1;

    if (rel[0] == '#') {
        while (page_url[i] && j < sz - 1) out[j++] = page_url[i++];
        out[j] = 0;
        return;
    }
    if (starts_ci(rel, "http://") || starts_ci(rel, "https://")) {
        while (rel[i] && j < sz - 1) out[j++] = rel[i++];
        out[j] = 0;
        return;
    }
    if (rel[0] == '/' && rel[1] == '/') {          /* protocol-relative */
        while (page_url[p] && page_url[p] != ':') out[j++] = page_url[p++];
        if (j > 0 && j < sz - 1) out[j++] = ':';
        while (rel[i] && j < sz - 1) out[j++] = rel[i++];
        out[j] = 0;
        return;
    }
    while (page_url[p] && !(page_url[p] == ':' && page_url[p+1] == '/' && page_url[p+2] == '/')) p++;
    host_start = p + 3;
    host_end = host_start;
    while (page_url[host_end] && page_url[host_end] != '/') host_end++;
    for (i = 0; page_url[i]; i++) if (page_url[i] == '/') last = i;

    if (rel[0] == '/') {
        for (i = 0; i < host_end && j < sz - 1; i++) out[j++] = page_url[i];
    } else if (last <= p + 2) {
        for (i = 0; i < host_end && j < sz - 1; i++) out[j++] = page_url[i];
        if (j < sz - 1) out[j++] = '/';
    } else {
        for (i = 0; i <= last && j < sz - 1; i++) out[j++] = page_url[i];
    }
    i = 0;
    while (rel[i] && j < sz - 1) out[j++] = rel[i++];
    out[j] = 0;
}

/* ----------------------------- primitives ------------------------------ */
static u32 span_bytes;

static void putp(int x, int y, u32 c) {
    if ((u32)x >= screen_w || (u32)y >= screen_h) return;
    u32 off = (u32)y * pitch_bytes + (u32)x * bytespp;
    if (bytespp == 2) {
        u16 v = (u16)((((c >> 16) & 0xF8) << 8) |
                      (((c >> 8) & 0xFC) << 3) |
                      ((c & 0xF8) >> 3));
        *(u16 *)(dbuf + off) = v;
    } else if (bytespp == 3) {
        dbuf[off]     = (u8)c;
        dbuf[off + 1] = (u8)(c >> 8);
        dbuf[off + 2] = (u8)(c >> 16);
    } else {
        ((u32 *)(dbuf + off))[0] = c;
    }
}

static void rect(int x, int y, int w, int h, u32 c) {
    int xx, yy;
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > (int)screen_w) w = (int)screen_w - x;
    if (y + h > (int)screen_h) h = (int)screen_h - y;
    if (w <= 0 || h <= 0) return;

    if (bytespp == 3) {
        u8 b = (u8)c, g = (u8)(c >> 8), r = (u8)(c >> 16);
        for (yy = 0; yy < h; yy++) {
            u8 *p = dbuf + (u32)(y + yy) * pitch_bytes + (u32)x * 3;
            for (xx = 0; xx < w; xx++) { p[0] = b; p[1] = g; p[2] = r; p += 3; }
        }
    } else if (bytespp == 4) {
        for (yy = 0; yy < h; yy++) {
            u32 *p = (u32 *)(dbuf + (u32)(y + yy) * pitch_bytes + (u32)x * 4);
            for (xx = 0; xx < w; xx++) *p++ = c;
        }
    } else {
        for (yy = y; yy < y + h; yy++)
            for (xx = x; xx < x + w; xx++)
                putp(xx, yy, c);
    }
}

static void frame(int x, int y, int w, int h, u32 c) {
    rect(x, y, w, 1, c);
    rect(x, y + h - 1, w, 1, c);
    rect(x, y, 1, h, c);
    rect(x + w - 1, y, 1, h, c);
}

static void rounded_frame(int x, int y, int w, int h, int r, u32 c) {
    int i;
    rect(x + r, y, w - 2 * r, 1, c);
    rect(x + r, y + h - 1, w - 2 * r, 1, c);
    rect(x, y + r, 1, h - 2 * r, c);
    rect(x + w - 1, y + r, 1, h - 2 * r, c);
    for (i = 0; i < r; i++) {
        putp(x + r - 1 - i, y + i, c);         putp(x + w - r + i, y + i, c);
        putp(x + i, y + r - 1 - i, c);         putp(x + w - 1 - i, y + r - 1 - i, c);
        putp(x + r - 1 - i, y + h - 1 - i, c); putp(x + w - r + i, y + h - 1 - i, c);
        putp(x + i, y + h - r + i, c);         putp(x + w - 1 - i, y + h - r + i, c);
    }
}

static void draw_char(int x, int y, char ch, u32 c) {
    const u8 *g = font_glyph(ch);
    int cx, cy;
    if (!g) return;
    for (cy = 0; cy < 7; cy++) {
        u8 row = g[cy];
        for (cx = 0; cx < 5; cx++)
            if (row & (1u << (4 - cx)))
                putp(x + cx, y + cy, c);
    }
}

static void draw_char2(int x, int y, char ch, u32 c) {
    const u8 *g = font_glyph(ch);
    int cx, cy;
    if (!g) return;
    for (cy = 0; cy < 7; cy++) {
        u8 row = g[cy];
        for (cx = 0; cx < 5; cx++)
            if (row & (1u << (4 - cx))) {
                putp(x + cx * 2,     y + cy * 2,     c);
                putp(x + cx * 2 + 1, y + cy * 2,     c);
                putp(x + cx * 2,     y + cy * 2 + 1, c);
                putp(x + cx * 2 + 1, y + cy * 2 + 1, c);
            }
    }
}

static void draw_text(int x, int y, const char *s, u32 c) {
    while (*s) { draw_char(x, y, *s++, c); x += 6; }
}
static void draw_text2(int x, int y, const char *s, u32 c) {
    while (*s) { draw_char2(x, y, *s++, c); x += 12; }
}
static void draw_text_n(int x, int y, const char *s, int max, u32 c) {
    int i = 0;
    while (*s && i < max) { draw_char(x, y, *s++, c); x += 6; i++; }
}
static void draw_num(int x, int y, int v, u32 c) {
    char b[12];
    int i = 0;
    if (v == 0) b[i++] = '0';
    while (v > 0) { b[i++] = (char)('0' + v % 10); v /= 10; }
    while (i > 0) { draw_char(x, y, b[--i], c); x += 6; }
}

/* -------------------------- blit / cursor updates ----------------------- */
static void raw_copy(volatile u8 *dst, u8 *src, u32 n) {
    __asm__ volatile ("cld; rep movsb" : : "S"(src), "D"(dst), "c"(n) : "memory");
}

static void copy_region(volatile u8 *dst, u8 *src, int x, int y, int w, int h) {
    int yy;
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > (int)screen_w) w = (int)screen_w - x;
    if (y + h > (int)screen_h) h = (int)screen_h - y;
    if (w <= 0 || h <= 0) return;
    span_bytes = (u32)w * bytespp;
    for (yy = 0; yy < h; yy++)
        raw_copy(dst + (u32)(y + yy) * pitch_bytes + (u32)x * bytespp,
                 src + (u32)(y + yy) * pitch_bytes + (u32)x * bytespp,
                 span_bytes);
}

static void full_copy(volatile u8 *dst, u8 *src) {
    raw_copy(dst, src, screen_h * pitch_bytes);
}

static int overlap(int ax, int ay, int aw, int ah, int bx, int by, int bw, int bh) {
    return ax < bx + bw && ax + aw > bx && ay < by + bh && ay + ah > by;
}

/* ------------------------------- windows ------------------------------- */
typedef struct {
    int x, y, w, h;
    int ox, oy, ow, oh;
    const char *title;
    u8 open, active, maximized, minimized;
    int z;
    u32 body;
    int kind;                    /* 1 = browser */
} Win;

static Win wins[4] = {
    {  90,  92, 440, 255, 0, 0, 0, 0, "WELCOME TO TINY OS", 1, 1, 0, 0, 1, RGB(14, 34, 66),  0 },
    { 370, 202, 410, 255, 0, 0, 0, 0, "SYSTEM INFO",       1, 0, 0, 0, 2, RGB(26, 29, 38),  0 },
    { 220, 345, 390, 180, 0, 0, 0, 0, "NOTES",             1, 0, 0, 0, 3, RGB(16, 48, 38),  0 },
    {  70,  60, 760, 560, 0, 0, 0, 0, "TINY BROWSER",       1, 0, 0, 0, 4, RGB(16, 20, 30),  1 },
};
static int top_z = 4;
static const char *task_label[4] = { "WELCOME", "SYSTEM", "NOTES", "BROWSER" };
static const u32 task_color[4] = {
    RGB(60, 140, 230), RGB(150, 130, 220), RGB(70, 190, 130), RGB(230, 150, 60)
};

static const u32 wall_top[5] = {
    RGB(20, 46, 96), RGB(66, 18, 74), RGB(14, 70, 56),
    RGB(72, 42, 20), RGB(42, 26, 78)
};
static const u32 accent[5] = {
    RGB(60, 140, 230), RGB(205, 110, 190), RGB(70, 190, 130),
    RGB(230, 155, 70), RGB(150, 130, 235)
};
static const char *theme_name[5] = {
    "OCEAN", "SUNSET", "FOREST", "AMBER", "NEBULA"
};
static int wall = 0;

static char notes[64];
static int notes_len = 0;

static int mx = 400, my = 300, mb = 0;
static int old_mx = 400, old_my = 300;
static int dragging = -1, drag_ox = 0, drag_oy = 0;
static int hover_id = -1;
static int scene_dirty = 1, cursor_dirty = 1;

static int hh = 0, mm = 0, ss = 0, last_clock = -1;

#define CUR_W 11
#define CUR_H 15
static const char *cur_mask[CUR_H] = {
    "X..........", "XX.........", "X.X........", "X..X.......", "X...X......",
    "X....X.....", "X.....X....", "X......X...", "X.......X..", "X........X.",
    "X....XXXXX.", "X...X......", "X..X.......", "XX.X.......", "X..........",
};

static void draw_cursor(int x, int y, u32 c) {
    int r, col;
    for (r = 0; r < CUR_H; r++)
        for (col = 0; col < CUR_W; col++)
            if (cur_mask[r][col] == 'X')
                putp(x + col, y + r, c);
}

static void sort_windows(Win **out) {
    int i, j;
    for (i = 0; i < 4; i++) out[i] = &wins[i];
    for (i = 0; i < 4; i++)
        for (j = i + 1; j < 4; j++)
            if (out[i]->z > out[j]->z) {
                Win *t = out[i]; out[i] = out[j]; out[j] = t;
            }
}

static void draw_button(int x, int y, u32 normal, u32 hover, char glyph, int hot) {
    rect(x, y, 16, 16, hot ? lerp(hover, RGB(255, 255, 255), 28) : hover);
    frame(x, y, 16, 16, normal);
    draw_char(x + 5, y + 4, glyph, RGB(255, 255, 255));
}

/* ---------------------- hover: cheap partial updates -------------------- */
static void menu_item_rect(int idx, int *x, int *y, int *w, int *h) {
    *x = MENU_X + 6;
    *y = (int)screen_h - 40 - MENU_H + 8 + idx * 22;
    *w = MENU_W - 12;
    *h = 20;
}

static int hover_rect(int id, int *x, int *y, int *w, int *h) {
    if (id >= 100 && id < 104) {
        int i = id - 100;
        *x = 108 + i * 124; *y = (int)screen_h - 33; *w = 116; *h = 26;
        return 1;
    }
    if (id == 104) {
        *x = MENU_X; *y = (int)screen_h - 33; *w = 86; *h = 26;
        return 1;
    }
    if (id >= 200 && id < 200 + MENU_ITEMS) {
        if (!menu_open) return 0;
        menu_item_rect(id - 200, x, y, w, h);
        return 1;
    }
    if (id >= 300 && id < 300 + MAX_LINKS) {
        int k = id - 300;
        if (k >= nlinks || !wins[3].open || wins[3].minimized) return 0;
        *x = links[k].x - 2; *y = links[k].y - 2;
        *w = links[k].w + 4; *h = 13;
        return 1;
    }
    if (id >= 0 && id < 16) {
        int i = id / 4, b = id % 4;
        if (b == 0 || i >= 4 || !wins[i].open || wins[i].minimized) return 0;
        *x = wins[i].x + wins[i].w - (b == 1 ? 68 : b == 2 ? 46 : 24);
        *y = wins[i].y + 4; *w = 16; *h = 16;
        return 1;
    }
    return 0;
}

static void draw_hover_item(int id, int hot) {
    if (id == 104) {
        int bx = MENU_X, by = (int)screen_h - 33;
        rect(bx, by, 86, 26, hot ? lerp(accent[wall], RGB(255,255,255), 30)
                                  : accent[wall]);
        frame(bx, by, 86, 26, RGB(155, 195, 240));
        draw_text2(bx + 20, by + 7, "OS", RGB(255, 255, 255));
        return;
    }
    if (id >= 200 && id < 200 + MENU_ITEMS) {
        int i = id - 200, x, y, w, h;
        static const char *names[MENU_ITEMS] = {
            "WELCOME", "SYSTEM", "NOTES", "BROWSER", "THEME", "CLOSE MENU"
        };
        if (!menu_open) return;
        menu_item_rect(i, &x, &y, &w, &h);
        rect(x, y, w, h, hot ? lerp(accent[wall], RGB(255,255,255), 22)
                             : RGB(34, 39, 52));
        if (i < 4) rect(x + 3, y + 5, 4, 10, task_color[i]);
        draw_text(x + 14, y + 6, names[i],
                  hot ? RGB(255,255,255) : RGB(205, 215, 232));
        return;
    }
    if (id >= 300 && id < 300 + MAX_LINKS) {
        int k = id - 300;
        if (k >= nlinks) return;
        rect(links[k].x - 2, links[k].y - 2,
             links[k].w + 4, 12, hot ? RGB(255, 235, 160) : RGB(248, 249, 251));
        draw_text_n(links[k].x, links[k].y, links[k].label, 64,
                    hot ? RGB(120, 40, 20) : RGB(28, 92, 205));
        return;
    }
    if (id >= 100 && id < 104) {
        int i = id - 100, bx = 108 + i * 124, by = (int)screen_h - 33;
        rect(bx, by, 116, 26,
             hot ? RGB(72, 86, 110) : (wins[i].open ? RGB(48, 55, 70) : RGB(32, 36, 44)));
        frame(bx, by, 116, 26, hot ? RGB(140, 185, 235) : RGB(80, 88, 106));
        rect(bx + 5, by + 5, 4, 16, wins[i].open ? task_color[i] : RGB(85, 90, 105));
        draw_text_n(bx + 14, by + 7, task_label[i], 16,
                    wins[i].open ? RGB(235, 240, 250) : RGB(130, 136, 148));
        return;
    }
    if (id >= 0 && id < 16) {
        int i = id / 4, b = id % 4;
        Win *wnd = &wins[i];
        int x = wnd->x + wnd->w - (b == 1 ? 68 : b == 2 ? 46 : 24);
        if (b == 1) draw_button(x, wnd->y + 4, RGB(190, 160, 60), RGB(226, 190, 70), '-', hot);
        else if (b == 2) draw_button(x, wnd->y + 4, RGB(60, 170, 110), RGB(80, 200, 135), '+', hot);
        else draw_button(x, wnd->y + 4, RGB(190, 60, 60), RGB(226, 72, 72), 'X', hot);
    }
}

/* Restore a rectangle from the clean scene, then re-apply hover and cursor. */
static void restore_rect(int x, int y, int w, int h) {
    int hx, hy, hw, hh2;
    copy_region(screen_buf, scene, x, y, w, h);
    if (hover_rect(hover_id, &hx, &hy, &hw, &hh2) &&
        overlap(x, y, w, h, hx, hy, hw, hh2)) {
        dbuf = screen_buf;
        draw_hover_item(hover_id, 1);
        dbuf = scene;
    }
    if (overlap(x, y, w, h, mx - 1, my - 1, CUR_W + 2, CUR_H + 2)) {
        dbuf = screen_buf;
        draw_cursor(mx + 1, my + 1, RGB(0, 0, 0));
        draw_cursor(mx, my, RGB(255, 255, 255));
        dbuf = scene;
    }
}

/* Called on hover change: redraw at most two tiny rectangles. */
static void apply_hover(int old, int neu) {
    int x, y, w, h;
    if (hover_rect(old, &x, &y, &w, &h)) {
        restore_rect(x, y, w, h);
        copy_region(fb, screen_buf, x, y, w, h);
    }
    if (hover_rect(neu, &x, &y, &w, &h)) {
        restore_rect(x, y, w, h);
        copy_region(fb, screen_buf, x, y, w, h);
    }
}

/* ---------------------------- tiny HTML engine -------------------------- */
static int ci_eq(const char *s, int n, const char *lit) {
    int i = 0;
    while (i < n && lit[i]) {
        char a = s[i], b = lit[i];
        if (a >= 'A' && a <= 'Z') a += 32;
        if (b >= 'A' && b <= 'Z') b += 32;
        if (a != b) return 0;
        i++;
    }
    return i == n && lit[i] == 0;
}

/* Compare two tag names with explicit lengths (they are not NUL-terminated). */
static int tag_eq(const char *a, int an, const char *b, int bn) {
    int i;
    if (an != bn) return 0;
    for (i = 0; i < an; i++) {
        char x = a[i], y = b[i];
        if (x >= 'A' && x <= 'Z') x += 32;
        if (y >= 'A' && y <= 'Z') y += 32;
        if (x != y) return 0;
    }
    return 1;
}

static const char *skip_to_close(const char *p, const char *name, int nlen) {
    while (*p) {
        if (*p == '<' && p[1] == '/') {
            const char *q = p + 2;
            int i = 0;
            while (q[i] && q[i] != '>' && q[i] != ' ' && q[i] != '\n') i++;
            if (tag_eq(q, i, name, nlen)) {
                while (*p && *p != '>') p++;
                return *p ? p + 1 : p;
            }
        }
        p++;
    }
    return p;
}

/* Grab the <title> text for the browser status line. */
static void extract_title(const char *p, const char *name, int nlen) {
    int i = 0;
    while (*p) {
        if (*p == '<' && p[1] == '/') {
            const char *q = p + 2;
            int k = 0;
            while (q[k] && q[k] != '>' && q[k] != ' ' && q[k] != '\n') k++;
            if (tag_eq(q, k, name, nlen)) break;
        }
        if (i < 95 && *p >= 32) page_title[i++] = *p;
        p++;
    }
    page_title[i] = 0;
}

static int find_attr(const char *s, const char *e, const char *name, char *out, int sz) {
    while (s < e) {
        const char *n;
        int nl;
        while (s < e && (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r')) s++;
        n = s;
        while (s < e && ((*s >= 'a' && *s <= 'z') || (*s >= 'A' && *s <= 'Z'))) s++;
        nl = (int)(s - n);
        while (s < e && (*s == ' ' || *s == '=' || *s == '\t')) s++;
        if (s < e && (*s == '"' || *s == '\'')) {
            char q = *s++;
            int j = 0;
            while (s < e && *s != q && j < sz - 1) out[j++] = *s++;
            out[j] = 0;
            if (s < e) s++;
            if (ci_eq(n, nl, name)) return 1;
        } else {
            const char *v = s;
            int j = 0;
            while (s < e && *s != ' ' && *s != '\t') s++;
            if (ci_eq(n, nl, name)) {
                while (v < s && j < sz - 1) out[j++] = *v++;
                out[j] = 0;
                return 1;
            }
        }
    }
    return 0;
}

typedef struct {
    int px, py, pw, ph, maxcol, line, col, scroll;
    u32 color, link_color;
    int link, link_x, link_y;
    char link_label[64];
    int link_llen;
    int bullet;
} Rctx;

static void r_emit(Rctx *c, char ch) {
    int sx, sy;
    if (c->bullet && c->col == 0) {
        c->bullet = 0;
        r_emit(c, '-');
        r_emit(c, ' ');
    }
    if (c->col >= c->maxcol) { c->col = 0; c->line++; }
    sx = c->px + c->col * 6;
    sy = c->py + c->line * 10 - c->scroll * 10;
    if (sy >= c->py && sy + 8 <= c->py + c->ph && ch >= 32)
        draw_char(sx, sy, ch, c->color);
    if (c->link && sy >= c->py && sy + 8 <= c->py + c->ph) {
        rect(sx, sy + 7, 6, 1, c->link_color);
        if (c->link_llen < 63 && ch >= 32) c->link_label[c->link_llen++] = ch;
    }
    c->col++;
}

static void r_break(Rctx *c) {
    c->bullet = 0;
    if (c->col > 0) { c->col = 0; c->line++; }
}

static void r_text(Rctx *c, const char *s) {
    while (*s) r_emit(c, *s++);
}

static void render_html(int px, int py, int pw, int ph) {
    Rctx c;
    const char *p = PAGE_BUF;
    char cur_url[200];

    c.px = px; c.py = py; c.pw = pw; c.ph = ph;
    c.maxcol = pw / 6; if (c.maxcol < 8) c.maxcol = 8;
    c.line = 0; c.col = 0; c.scroll = page_scroll;
    c.color = RGB(35, 40, 52);
    c.link_color = RGB(28, 92, 205);
    c.link = 0; c.link_x = px; c.link_y = py;
    c.link_label[0] = 0; c.link_llen = 0;
    c.bullet = 0;
    nlinks = 0;

    while (*p) {
        /* Parse the whole document so page_lines is the real total: even
         * off-screen lines increase page_lines, which makes scrolling work. */

        if (*p == '&') {
            if (p[1] == 'a' && p[2] == 'm' && p[3] == 'p' && p[4] == ';') { r_emit(&c, '&'); p += 5; continue; }
            if (p[1] == 'l' && p[2] == 't' && p[3] == ';')  { r_emit(&c, '<'); p += 4; continue; }
            if (p[1] == 'g' && p[2] == 't' && p[3] == ';')  { r_emit(&c, '>'); p += 4; continue; }
            if (p[1] == 'q' && p[2] == 'u' && p[3] == 'o' && p[4] == 't' && p[5] == ';') { r_emit(&c, '"'); p += 6; continue; }
            if (p[1] == 'n' && p[2] == 'b' && p[3] == 's' && p[4] == 'p' && p[5] == ';') { r_emit(&c, ' '); p += 6; continue; }
            if (p[1] == '#') {
                int v = 0, k = 2;
                while (p[k] >= '0' && p[k] <= '9') { v = v * 10 + (p[k] - '0'); k++; }
                if (p[k] == ';' && v >= 32 && v < 127) r_emit(&c, (char)v);
                p += k + (p[k] == ';' ? 1 : 0);
                continue;
            }
            r_emit(&c, '&'); p++; continue;
        }

        if (*p == '<') {
            const char *q = p + 1, *ns, *gt;
            int closing = 0, nl;

            if (*q == '!') {                       /* comment / doctype */
                while (*q && *q != '>') q++;
                p = *q ? q + 1 : q;
                continue;
            }
            if (*q == '/') { closing = 1; q++; }
            ns = q;
            while (*q && ((*q >= 'a' && *q <= 'z') || (*q >= 'A' && *q <= 'Z') ||
                          (*q >= '0' && *q <= '9'))) q++;
            nl = (int)(q - ns);
            gt = q;
            while (*gt && *gt != '>') gt++;

            if (!closing && ci_eq(ns, nl, "title")) {
                extract_title(*gt ? gt + 1 : gt, ns, nl);
                p = skip_to_close(*gt ? gt + 1 : gt, ns, nl);
                continue;
            }
            if (!closing && (ci_eq(ns, nl, "script") || ci_eq(ns, nl, "style") ||
                             ci_eq(ns, nl, "noscript") || ci_eq(ns, nl, "iframe"))) {
                p = skip_to_close(*gt ? gt + 1 : gt, ns, nl);
                continue;
            }

            if (!closing) {
                if (ci_eq(ns, nl, "h1")) { r_break(&c); c.color = RGB(18, 58, 132); }
                else if (ci_eq(ns, nl, "h2")) { r_break(&c); c.color = RGB(26, 78, 158); }
                else if (ci_eq(ns, nl, "h3")) { r_break(&c); c.color = RGB(40, 90, 160); }
                else if (ci_eq(ns, nl, "li")) {
                    r_break(&c); c.color = RGB(35, 40, 52);
                    c.bullet = 1;
                }
                else if (ci_eq(ns, nl, "p") || ci_eq(ns, nl, "div") ||
                         ci_eq(ns, nl, "tr") || ci_eq(ns, nl, "ul") ||
                         ci_eq(ns, nl, "ol") || ci_eq(ns, nl, "blockquote")) {
                    r_break(&c); c.color = RGB(35, 40, 52);
                }
                else if (ci_eq(ns, nl, "br")) { r_break(&c); }
                else if (ci_eq(ns, nl, "hr")) {
                    int yy;
                    r_break(&c);
                    yy = c.py + c.line * 10 - c.scroll * 10 + 4;
                    if (yy >= c.py && yy + 1 <= c.py + c.ph)
                        rect(c.px, yy, c.pw - 16, 1, RGB(165, 176, 192));
                    r_break(&c);
                }
                else if (ci_eq(ns, nl, "img")) {
                    char alt[96];
                    alt[0] = 0;
                    if (*gt) find_attr(ns + nl, gt, "alt", alt, sizeof(alt));
                    if (!alt[0]) cstr_copy(alt, "IMAGE", sizeof(alt));
                    r_text(&c, "[IMG: ");
                    r_text(&c, alt);
                    r_emit(&c, ']');
                    r_break(&c);
                }
                else if (ci_eq(ns, nl, "b") || ci_eq(ns, nl, "strong"))
                    c.color = RGB(15, 20, 30);
                else if (ci_eq(ns, nl, "a")) {
                    cur_url[0] = 0;
                    if (*gt)
                        find_attr(ns + nl, gt, "href", cur_url, sizeof(cur_url));
                    if (cur_url[0]) {
                        c.link = 1;
                        c.link_x = c.px + c.col * 6;
                        c.link_y = c.py + c.line * 10 - c.scroll * 10;
                        c.link_label[0] = 0;
                        c.link_llen = 0;
                        c.color = c.link_color;
                    }
                }
            } else {
                if (ci_eq(ns, nl, "a")) {
                    if (c.link) {
                        if (nlinks < MAX_LINKS) {
                            int w = c.px + c.col * 6 - c.link_x;
                            if (w < 6) w = 6;
                            links[nlinks].x = c.link_x;
                            links[nlinks].y = c.link_y;
                            links[nlinks].w = w;
                            links[nlinks].h = 9;
                            cstr_copy(links[nlinks].label, c.link_label,
                                      sizeof(links[nlinks].label));
                            resolve_url(cur_url, links[nlinks].url, 200);
                            nlinks++;
                        }
                        c.link_llen = 0;
                        c.link_label[0] = 0;
                        c.link = 0;
                        c.color = RGB(35, 40, 52);
                    }
                }
                else if (ci_eq(ns, nl, "b") || ci_eq(ns, nl, "strong") ||
                         ci_eq(ns, nl, "h1") || ci_eq(ns, nl, "h2") || ci_eq(ns, nl, "h3"))
                    c.color = RGB(35, 40, 52);
                else if (ci_eq(ns, nl, "p") || ci_eq(ns, nl, "div") || ci_eq(ns, nl, "li"))
                    r_break(&c);
            }
            p = *gt ? gt + 1 : gt;
            continue;
        }

        if (*p == '\n' || *p == '\r' || *p == '\t') { p++; continue; }
        r_emit(&c, *p);
        p++;
    }
    page_lines = c.line + 1;
}

/* ------------------------- window contents ------------------------------ */
static void draw_browser(Win *w) {
    int bx = w->x + 8, by = w->y + 30;
    int ax = w->x + 58, aw = w->w - 190;
    int gx = w->x + w->w - 86;
    int hx = w->x + w->w - 46;
    int px = w->x + 8, py = w->y + 58;
    int pw = w->w - 16, ph = w->h - 90;

    /* toolbar */
    rect(w->x + 1, w->y + 24, w->w - 2, 30, RGB(38, 43, 56));
    rect(w->x + 1, w->y + 53, w->w - 2, 1, RGB(10, 12, 18));

    /* BACK */
    rect(bx, by, 44, 20, RGB(70, 78, 96));
    frame(bx, by, 44, 20, RGB(120, 135, 160));
    draw_text_n(bx + 6, by + 6, "BACK", 6, RGB(235, 240, 250));

    /* address bar */
    rect(ax, by, aw, 20, RGB(12, 16, 26));
    frame(ax, by, aw, 20, w->active ? RGB(90, 160, 235) : RGB(70, 80, 100));
    {
        int vis = aw / 6 - 2, start = url_pos - vis + 1;
        if (start < 0) start = 0;
        draw_text_n(ax + 6, by + 6, page_url + start, vis, RGB(215, 232, 255));
        if (w->active)
            rect(ax + 6 + (url_pos - start) * 6, by + 4, 4, 11, RGB(235, 245, 255));
    }

    /* GO */
    rect(gx, by, 34, 20, RGB(40, 140, 90));
    frame(gx, by, 34, 20, RGB(110, 210, 160));
    draw_text_n(gx + 10, by + 6, "GO", 4, RGB(255, 255, 255));

    /* HOME */
    rect(hx, by, 38, 20, RGB(70, 78, 110));
    frame(hx, by, 38, 20, RGB(145, 160, 200));
    draw_text_n(hx + 5, by + 6, "HOME", 5, RGB(235, 240, 255));

    /* page */
    rect(px, py, pw, ph, RGB(248, 249, 251));
    rect(px, py, pw, 1, RGB(200, 208, 220));
    render_html(px + 6, py + 8, pw - 20, ph - 16);

    /* scrollbar: thumb shows the current position in the document */
    {
        int bar_x = px + pw - 11, bar_y = py + 3, bar_h = ph - 6;
        int vis_lines = (ph - 16) / 10;
        int maxs = page_lines - vis_lines, th, ty;
        rect(bar_x, bar_y, 5, bar_h, RGB(224, 229, 238));
        frame(bar_x, bar_y, 5, bar_h, RGB(160, 170, 185));
        if (maxs < 0) maxs = 0;
        th = bar_h * vis_lines / (page_lines > 0 ? page_lines : 1);
        if (th < 12) th = 12;
        if (th > bar_h) th = bar_h;
        ty = bar_y + (maxs > 0 ? (bar_h - th) * page_scroll / maxs : 0);
        rect(bar_x + 1, ty, 3, th - 2, RGB(70, 130, 205));
    }
}

static void window_content(Win *w) {
    int x = w->x + 16, y = w->y + 52;
    int sy = w->y + w->h - 24;

    if (w->kind == 1) {
        draw_browser(w);
    } else if (w == &wins[0]) {
        draw_text(x, y,      "HELLO FROM TINY OS!", RGB(150, 205, 255));
        draw_text(x, y + 22, "A REAL BOOTABLE KERNEL", RGB(205, 215, 235));
        draw_text(x, y + 38, "WRITTEN FROM SCRATCH.", RGB(205, 215, 235));
        rect(x, y + 60, 320, 2, RGB(60, 110, 175));
        draw_text(x, y + 76, "DRAG TITLE BAR TO MOVE", RGB(165, 190, 225));
        draw_text(x, y + 92, "F1-F4 WINDOWS  F5 BROWSER", RGB(165, 190, 225));
        draw_text(x, y + 108,"ENTER: FETCH PAGE  BACK: HISTORY", RGB(165, 190, 225));
        draw_text(x, y + 124,"DOUBLE-CLICK TITLE: MAXIMIZE", RGB(165, 190, 225));
        draw_text(x, y + 140,"OS BUTTON: START MENU", RGB(165, 190, 225));
    } else if (w == &wins[1]) {
        draw_text(x, y,      "SYSTEM INFORMATION", RGB(150, 205, 255));
        rect(x, y + 18, 320, 2, RGB(70, 80, 100));
        draw_text(x, y + 32, "RESOLUTION: 1024x768", RGB(210, 215, 225));
        draw_text(x, y + 48, "COLOR DEPTH: ", RGB(210, 215, 225));
        draw_num(x + 13 * 6, y + 48, (int)bpp_val, RGB(255, 215, 120));
        draw_text(x + 16 * 6, y + 48, " BPP", RGB(210, 215, 225));
        draw_text(x, y + 64, "VIDEO: VESA LINEAR FB", RGB(210, 215, 225));
        draw_text(x, y + 80, "INPUT: PS/2 MOUSE + KBD", RGB(210, 215, 225));
        draw_text(x, y + 96, "KERNEL: 32-BIT PROTECTED", RGB(210, 215, 225));
        draw_text(x, y + 112,"NET: COM1 -> HOST HELPER", RGB(210, 215, 225));
        draw_text(x, y + 128,"THEME: ", RGB(140, 170, 210));
        draw_text(x + 48, y + 128, theme_name[wall], RGB(255, 215, 120));
        draw_text(x, y + 148,"MOUSE X:", RGB(140, 170, 210));
        draw_num(x + 72, y + 148, mx, RGB(255, 215, 120));
        draw_text(x + 130, y + 148,"Y:", RGB(140, 170, 210));
        draw_num(x + 148, y + 148, my, RGB(255, 215, 120));
        draw_text(x, y + 168,"WHEEL: ", RGB(140, 170, 210));
        draw_text(x + 48, y + 168, mouse4 ? "ENABLED" : "OFF", RGB(255, 215, 120));
    } else {
        draw_text(x, y, "TYPE WITH THE KEYBOARD:", RGB(150, 230, 180));
        rect(x, y + 18, 320, 2, RGB(60, 120, 90));
        draw_text(x, y + 38, notes, RGB(225, 255, 230));
        rect(x + notes_len * 6, y + 38, 4, 7, RGB(225, 255, 230));
        draw_text(x, y + 78, "F4: THEME   BACKSPACE: DEL", RGB(140, 180, 155));
    }

    rect(w->x + 5, sy - 4, w->w - 10, 1, RGB(70, 85, 110));
    if (w->kind == 1) {
        draw_text_n(w->x + 10, sy, page_title, 24, RGB(150, 190, 230));
        draw_text_n(w->x + 170, sy, status_msg, 24, RGB(175, 205, 235));
        draw_text(w->x + 360, sy, "LINE ", RGB(130, 165, 200));
        draw_num(w->x + 390, sy, page_scroll + 1, RGB(180, 205, 235));
        draw_char(w->x + 420, sy, '/', RGB(130, 165, 200));
        draw_num(w->x + 426, sy, page_lines, RGB(180, 205, 235));
        draw_num(w->x + w->w - 150, sy, (int)page_len, RGB(155, 210, 180));
        draw_text(w->x + w->w - 100, sy, "BYTES", RGB(120, 170, 150));
    } else {
        draw_text(w->x + 10, sy, "TINY OS v0.1", RGB(120, 150, 185));
        draw_text(w->x + w->w - 132, sy, theme_name[wall], RGB(160, 190, 225));
        draw_text(w->x + w->w - 70, sy, "READY", RGB(120, 200, 150));
    }
}

static void draw_window(Win *w) {
    int idx = (int)(w - wins);
    int cx = w->x + w->w - 24;
    int mxb = w->x + w->w - 46;
    int nob = w->x + w->w - 68;
    int cy = w->y + 4;
    int row;

    rect(w->x + 7, w->y + 7, w->w, w->h, RGB(0, 0, 0));
    rect(w->x + 5, w->y + 5, w->w, w->h, RGB(6, 8, 14));

    for (row = 0; row < 23; row++) {
        u32 c = w->active
            ? lerp(accent[wall], RGB(8, 16, 32), row * 100 / 23)
            : lerp(RGB(92, 100, 122), RGB(52, 58, 76), row * 100 / 23);
        rect(w->x + 1, w->y + 1 + row, w->w - 2, 1, c);
    }
    rect(w->x + 1, w->y + 23, w->w - 2, 1, RGB(8, 12, 22));
    rect(w->x + 8, w->y + 7, 10, 10, task_color[idx]);
    frame(w->x + 8, w->y + 7, 10, 10, RGB(230, 240, 255));
    draw_text(w->x + 24, w->y + 7, w->title, RGB(255, 255, 255));
    if (w->active)
        rect(w->x + 24, w->y + 19, 26, 2, RGB(140, 205, 255));

    /* hover is applied later directly on screen_buf - scene stays clean */
    draw_button(nob, cy, RGB(190, 160, 60), RGB(226, 190, 70), '-', 0);
    draw_button(mxb, cy, RGB(60, 170, 110), RGB(80, 200, 135), '+', 0);
    draw_button(cx,  cy, RGB(190, 60, 60), RGB(226, 72, 72), 'X', 0);

    rect(w->x + 1, w->y + 24, w->w - 2, w->h - 25, w->body);
    rounded_frame(w->x, w->y, w->w, w->h, 5,
                  w->active ? accent[wall] : RGB(90, 100, 122));
    window_content(w);
}

/* ------------------------------- drawing -------------------------------- */
static void draw_all(void) {
    Win *order[4];
    int i, x, y;
    u32 top = wall_top[wall];
    u32 sh = screen_h - 40;

    for (i = 0; i < (int)sh; i++) {
        int k = (int)((sh - (u32)i) * 100 / sh);
        rect(0, i, (int)screen_w, 1,
             RGB((((top >> 16) & 255) * k) / 100 + 4,
                 (((top >> 8) & 255)  * k) / 100 + 6,
                 ((top & 255)         * k) / 100 + 12));
    }
    for (y = 24; y < (int)sh - 8; y += 32)
        for (x = 24; x < (int)screen_w - 8; x += 32)
            putp(x, y, RGB(90, 115, 155));

    draw_text2((int)screen_w / 2 - 82, 46, "TINY OS", RGB(175, 220, 255));
    draw_text((int)screen_w / 2 - 116, 68, "GRAPHICAL OS FOR QEMU  -  v0.1", RGB(120, 160, 210));
    rect((int)screen_w / 2 - 130, 82, 260, 2, RGB(70, 130, 200));

    rect(0, (int)screen_h - 40, (int)screen_w, 40, RGB(24, 27, 34));
    rect(0, (int)screen_h - 40, (int)screen_w, 2, accent[wall]);
    draw_hover_item(104, 0);

    for (i = 0; i < 4; i++)
        draw_hover_item(100 + i, 0);

    rect((int)screen_w - 132, (int)screen_h - 33, 124, 26, RGB(32, 36, 46));
    frame((int)screen_w - 132, (int)screen_h - 33, 124, 26, RGB(80, 90, 110));
    draw_num((int)screen_w - 124, (int)screen_h - 29, dd_day, RGB(160, 195, 230));
    draw_char((int)screen_w - 108, (int)screen_h - 29, '.', RGB(160, 195, 230));
    draw_num((int)screen_w - 102, (int)screen_h - 29, dd_month, RGB(160, 195, 230));
    draw_num((int)screen_w - 78, (int)screen_h - 29, hh, RGB(255, 220, 140));
    draw_char((int)screen_w - 54, (int)screen_h - 29, (ss & 1) ? ':' : ' ', RGB(255, 220, 140));
    draw_num((int)screen_w - 48, (int)screen_h - 29, mm, RGB(255, 220, 140));
    draw_char((int)screen_w - 24, (int)screen_h - 29, (ss & 1) ? ':' : ' ', RGB(255, 220, 140));
    draw_num((int)screen_w - 18, (int)screen_h - 29, ss, RGB(255, 220, 140));

    sort_windows(order);
    for (i = 0; i < 4; i++)
        if (order[i]->open && !order[i]->minimized)
            draw_window(order[i]);

    if (menu_open) {
        int my0 = (int)screen_h - 40 - MENU_H;
        rect(MENU_X + 4, my0 + 4, MENU_W, MENU_H, RGB(0, 0, 0));
        rect(MENU_X, my0, MENU_W, MENU_H, RGB(26, 30, 40));
        frame(MENU_X, my0, MENU_W, MENU_H, RGB(120, 140, 175));
        rect(MENU_X, my0, MENU_W, 18, accent[wall]);
        draw_text(MENU_X + 8, my0 + 5, "TINY OS MENU", RGB(255, 255, 255));
        for (i = 0; i < MENU_ITEMS; i++) {
            int x, y, w, h;
            menu_item_rect(i, &x, &y, &w, &h);
            rect(x, y, w, h, RGB(34, 39, 52));
            if (i < 4) rect(x + 3, y + 5, 4, 10, task_color[i]);
            draw_text(x + 14, y + 6,
                      i == 0 ? "WELCOME" : i == 1 ? "SYSTEM" : i == 2 ? "NOTES" :
                      i == 3 ? "BROWSER" : i == 4 ? "CHANGE THEME" : "CLOSE MENU",
                      RGB(205, 215, 232));
        }
    }
}

static void compose_full(void) {
    dbuf = scene;
    draw_all();
    full_copy(screen_buf, scene);
    if (hover_id >= 0) {
        dbuf = screen_buf;
        draw_hover_item(hover_id, 1);
        dbuf = scene;
    }
    dbuf = screen_buf;
    draw_cursor(mx + 1, my + 1, RGB(0, 0, 0));
    draw_cursor(mx, my, RGB(255, 255, 255));
    dbuf = scene;
    full_copy(fb, screen_buf);
    old_mx = mx;
    old_my = my;
}

static void compose_cursor(void) {
    restore_rect(old_mx, old_my, CUR_W + 1, CUR_H + 1);
    dbuf = screen_buf;
    draw_cursor(mx + 1, my + 1, RGB(0, 0, 0));
    draw_cursor(mx, my, RGB(255, 255, 255));
    dbuf = scene;
    copy_region(fb, screen_buf, old_mx, old_my, CUR_W + 1, CUR_H + 1);
    copy_region(fb, screen_buf, mx, my, CUR_W + 1, CUR_H + 1);
    old_mx = mx;
    old_my = my;
}

/* ------------------------------- input ---------------------------------- */
static void activate(int i) {
    int k;
    for (k = 0; k < 4; k++) wins[k].active = 0;
    wins[i].active = 1;
    wins[i].open = 1;
    wins[i].minimized = 0;
    wins[i].z = ++top_z;
}

/* Topmost visible window under a point, or -1. */
static int window_at(int x, int y) {
    Win *order[4];
    int i;
    sort_windows(order);
    for (i = 3; i >= 0; i--) {
        Win *w = order[i];
        if (!w->open || w->minimized) continue;
        if (x >= w->x && x < w->x + w->w && y >= w->y && y < w->y + w->h)
            return (int)(w - wins);
    }
    return -1;
}

static int compute_hover(void) {
    Win *order[4];
    int i;
    if (menu_open && mx >= MENU_X && mx < MENU_X + MENU_W &&
        my >= (int)screen_h - 40 - MENU_H && my < (int)screen_h - 40) {
        for (i = 0; i < MENU_ITEMS; i++) {
            int x, y, w, h;
            menu_item_rect(i, &x, &y, &w, &h);
            if (mx >= x && mx < x + w && my >= y && my < y + h) return 200 + i;
        }
        return -1;
    }
    if (mx >= MENU_X && mx < MENU_X + 86 &&
        my >= (int)screen_h - 33 && my < (int)screen_h - 7) return 104;
    if (wins[3].open && !wins[3].minimized && window_at(mx, my) == 3) {
        for (i = 0; i < nlinks; i++)
            if (mx >= links[i].x - 2 && mx < links[i].x + links[i].w + 2 &&
                my >= links[i].y - 2 && my < links[i].y + 11) return 300 + i;
    }
    sort_windows(order);
    for (i = 3; i >= 0; i--) {
        Win *w = order[i];
        int idx, cx, ax, nx, cy;
        if (!w->open || w->minimized) continue;
        idx = (int)(w - wins);
        cx = w->x + w->w - 24;
        ax = w->x + w->w - 46;
        nx = w->x + w->w - 68;
        cy = w->y + 4;
        if (mx >= cx && mx < cx + 16 && my >= cy && my < cy + 16) return idx * 4 + 3;
        if (mx >= ax && mx < ax + 16 && my >= cy && my < cy + 16) return idx * 4 + 2;
        if (mx >= nx && mx < nx + 16 && my >= cy && my < cy + 16) return idx * 4 + 1;
    }
    if (my >= (int)screen_h - 33 && my < (int)screen_h - 7) {
        int bx = 108;
        for (i = 0; i < 4; i++, bx += 124)
            if (mx >= bx && mx < bx + 116) return 100 + i;
    }
    return -1;
}

static void mouse_packet(u8 *p) {
    int dx = (int)(signed char)p[1];
    int dy = (int)(signed char)p[2];
    int nb = p[0] & 1;
    int h;
    Win *order[4];
    int i;

    mx += dx;
    my -= dy;
    if (mx < 0) mx = 0;
    if (my < 0) my = 0;
    if (mx > (int)screen_w - CUR_W) mx = (int)screen_w - CUR_W;
    if (my > (int)screen_h - CUR_H) my = (int)screen_h - CUR_H;

    if (mouse4 && p[3]) {
        int step = (int)(signed char)p[3];
        int visible = (wins[3].h - 98) / 10;
        int maxs = page_lines - visible;
        if (maxs < 0) maxs = 0;
        if (wins[3].open && !wins[3].minimized && window_at(mx, my) == 3) {
            page_scroll -= step * 3;
            if (page_scroll < 0) page_scroll = 0;
            if (page_scroll > maxs) page_scroll = maxs;
            cstr_copy(status_msg, "SCROLL", sizeof(status_msg));
            scene_dirty = 1;
        }
    }

    h = compute_hover();
    if (h != hover_id) {
        int old = hover_id;
        hover_id = h;
        apply_hover(old, h);          /* no full redraw on hover */
    }
    cursor_dirty = 1;

    if (nb && !mb) {
        /* Start menu first - it floats above the desktop. */
        if (menu_open) {
            int inside = mx >= MENU_X && mx < MENU_X + MENU_W &&
                         my >= (int)screen_h - 40 - MENU_H && my < (int)screen_h - 40;
            if (inside) {
                for (i = 0; i < MENU_ITEMS; i++) {
                    int x, y, w, h;
                    menu_item_rect(i, &x, &y, &w, &h);
                    if (mx >= x && mx < x + w && my >= y && my < y + h) {
                        if (i < 4) activate(i);
                        else if (i == 4) { wall = (wall + 1) % 5; cstr_copy(status_msg, theme_name[wall], sizeof(status_msg)); }
                        menu_open = 0;
                        scene_dirty = 1;
                        goto after_click;
                    }
                }
            } else {
                menu_open = 0;
                scene_dirty = 1;
            }
        }
        if (mx >= MENU_X && mx < MENU_X + 86 &&
            my >= (int)screen_h - 33 && my < (int)screen_h - 7) {
            menu_open = !menu_open;
            scene_dirty = 1;
            goto after_click;
        }
        sort_windows(order);
        for (i = 3; i >= 0; i--) {
            Win *w = order[i];
            int idx = (int)(w - wins);
            int cx = w->x + w->w - 24;
            int ax = w->x + w->w - 46;
            int nx = w->x + w->w - 68;
            int cy = w->y + 4;
            if (!w->open || w->minimized) continue;
            if (mx >= cx && mx < cx + 16 && my >= cy && my < cy + 16) {
                w->open = 0; w->active = 0; scene_dirty = 1;
                goto after_click;
            }
            if (mx >= ax && mx < ax + 16 && my >= cy && my < cy + 16) {
                if (w->maximized) {
                    w->x = w->ox; w->y = w->oy; w->w = w->ow; w->h = w->oh;
                    w->maximized = 0;
                } else {
                    w->ox = w->x; w->oy = w->y; w->ow = w->w; w->oh = w->h;
                    w->x = 16; w->y = 16;
                    w->w = (int)screen_w - 32;
                    w->h = (int)screen_h - 40 - 32;
                    w->maximized = 1;
                }
                activate(idx); scene_dirty = 1;
                goto after_click;
            }
            if (mx >= nx && mx < nx + 16 && my >= cy && my < cy + 16) {
                w->minimized = 1; w->active = 0; scene_dirty = 1;
                goto after_click;
            }
        }

        /* clicking an inactive window raises it */
        {
            int under = window_at(mx, my);
            if (under >= 0 && !wins[under].active) {
                activate(under);
                scene_dirty = 1;
            }
        }

        /* browser: BACK / address / GO / HOME / links */
        if (wins[3].open) {
            Win *b = &wins[3];
            int by = b->y + 30;
            int ax = b->x + 58, aw = b->w - 190;
            int gx = b->x + b->w - 86;
            int hx = b->x + b->w - 46;
            int px = b->x + 14, py = b->y + 66;
            int pw = b->w - 28, ph = b->h - 106;
            if (mx >= b->x + 8 && mx < b->x + 52 && my >= by && my < by + 20) {
                go_back(); activate(3); scene_dirty = 1;
                goto after_click;
            }
            if (mx >= ax && mx < ax + aw && my >= by && my < by + 20) {
                activate(3);
                url_pos = (mx - ax - 6) / 6;
                if (url_pos < 0) url_pos = 0;
                if (url_pos > url_len) url_pos = url_len;
                scene_dirty = 1;
                goto after_click;
            }
            if (mx >= gx && mx < gx + 34 && my >= by && my < by + 20) {
                navigate(page_url); activate(3); scene_dirty = 1;
                goto after_click;
            }
            if (mx >= hx && mx < hx + 38 && my >= by && my < by + 20) {
                page_home(); activate(3); scene_dirty = 1;
                goto after_click;
            }
            if (mx >= px && mx < px + pw && my >= py && my < py + ph) {
                int k;
                activate(3);
                scene_dirty = 1;
                for (k = 0; k < nlinks; k++) {
                    if (mx >= links[k].x && mx < links[k].x + links[k].w &&
                        my >= links[k].y - 2 && my < links[k].y + 11) {
                        char full[200];
                        resolve_url(links[k].url, full, sizeof(full));
                        navigate(full);
                        scene_dirty = 1;
                        goto after_click;
                    }
                }
            }
        }

        for (i = 3; i >= 0; i--) {
            Win *w = order[i];
            if (w->open && !w->minimized && mx >= w->x && mx < w->x + w->w &&
                my >= w->y && my < w->y + 24) {
                int idx2 = (int)(w - wins);
                if (ss == last_click_sec && idx2 == last_click_win) {
                    /* double click on title bar: toggle maximize */
                    if (w->maximized) {
                        w->x = w->ox; w->y = w->oy; w->w = w->ow; w->h = w->oh;
                        w->maximized = 0;
                    } else {
                        w->ox = w->x; w->oy = w->y; w->ow = w->w; w->oh = w->h;
                        w->x = 16; w->y = 16;
                        w->w = (int)screen_w - 32;
                        w->h = (int)screen_h - 72;
                        w->maximized = 1;
                    }
                    last_click_sec = -1;
                    activate(idx2);
                    dragging = -1;
                    scene_dirty = 1;
                    goto after_click;
                } else {
                    last_click_sec = ss;
                    last_click_win = idx2;
                }
                activate(idx2);
                dragging = idx2;
                drag_ox = mx - w->x;
                drag_oy = my - w->y;
                scene_dirty = 1;
                goto after_click;
            }
        }
        if (my >= (int)screen_h - 33 && my < (int)screen_h - 7) {
            int bx = 108;
            for (i = 0; i < 4; i++, bx += 124)
                if (mx >= bx && mx < bx + 116) {
                    if (wins[i].open && wins[i].active && !wins[i].minimized) {
                        wins[i].minimized = 1;
                        wins[i].active = 0;
                    } else {
                        activate(i);
                    }
                    scene_dirty = 1;
                    break;
                }
        }
    }
after_click:
    if (!nb && mb) dragging = -1;

    if (dragging >= 0) {
        Win *w = &wins[dragging];
        w->x = mx - drag_ox;
        w->y = my - drag_oy;
        if (w->x < 0) w->x = 0;
        if (w->y < 0) w->y = 0;
        if (w->x > (int)screen_w - w->w) w->x = (int)screen_w - w->w;
        if (w->y > (int)screen_h - 76) w->y = (int)screen_h - 76;
        scene_dirty = 1;
    }
    mb = nb;
}

static const char scmap[128] = {
    [0x02]='1', [0x03]='2', [0x04]='3', [0x05]='4', [0x06]='5',
    [0x07]='6', [0x08]='7', [0x09]='8', [0x0A]='9', [0x0B]='0',
    [0x0C]='-', [0x27]=':', [0x34]='.', [0x35]='/',
    [0x1E]='A', [0x30]='B', [0x2E]='C', [0x20]='D', [0x12]='E',
    [0x21]='F', [0x22]='G', [0x23]='H', [0x17]='I', [0x24]='J',
    [0x25]='K', [0x26]='L', [0x32]='M', [0x31]='N', [0x18]='O',
    [0x19]='P', [0x10]='Q', [0x13]='R', [0x1F]='S', [0x14]='T',
    [0x16]='U', [0x2F]='V', [0x11]='W', [0x2D]='X', [0x15]='Y',
    [0x2C]='Z', [0x39]=' ', [0x0E]=8,
};

static void key_byte(u8 sc) {
    int a;
    if (sc & 0x80) {
        sc &= 0x7F;
        if (sc == 0x1D) ctrl_down = 0;
        if (sc == 0x2A || sc == 0x36) shift_down = 0;
        return;
    }
    sc &= 0x7F;
    if (sc == 0x1D) { ctrl_down = 1; return; }
    if (sc == 0x2A || sc == 0x36) { shift_down = 1; return; }

    if (ctrl_down && sc == 0x26) {          /* Ctrl+L: new address (select all) */
        activate(3);
        url_len = 0;
        url_pos = 0;
        page_url[0] = 0;
        scene_dirty = 1;
        return;
    }
    if (ctrl_down && sc == 0x13) {          /* Ctrl+R: reload page */
        if (wins[3].open && wins[3].active) {
            fetch_page(page_url);
            cstr_copy(status_msg, "RELOADED", sizeof(status_msg));
            scene_dirty = 1;
        }
        return;
    }

    if (sc == 0x3B) { activate(0); scene_dirty = 1; return; }
    if (sc == 0x3C) { activate(1); scene_dirty = 1; return; }
    if (sc == 0x3D) { activate(2); scene_dirty = 1; return; }
    if (sc == 0x3F) {                      /* F5: open browser / reload */
        if (wins[3].open && wins[3].active) {
            fetch_page(page_url);
            cstr_copy(status_msg, "RELOADED", sizeof(status_msg));
        } else activate(3);
        scene_dirty = 1;
        return;
    }
    if (sc == 0x01) {                      /* ESC: browser start page */
        if (wins[3].open && wins[3].active) { page_home(); scene_dirty = 1; }
        return;
    }
    if (sc == 0x3E) { wall = (wall + 1) % 5; scene_dirty = 1; return; }
    if (sc == 0x1C) {
        if (wins[3].active && wins[3].open) { navigate(page_url); scene_dirty = 1; }
        return;
    }

    /* address bar is a real editor when the browser is active */
    if (wins[3].active && wins[3].open) {
        if (sc == 0x4B) {                  /* Left */
            if (url_pos > 0) url_pos--;
            scene_dirty = 1;
            return;
        }
        if (sc == 0x4D) {                  /* Right */
            if (url_pos < url_len) url_pos++;
            scene_dirty = 1;
            return;
        }
        if (sc == 0x47) { url_pos = 0; scene_dirty = 1; return; }        /* Home */
        if (sc == 0x4F) { url_pos = url_len; scene_dirty = 1; return; }   /* End */
        if (sc == 0x0E || sc == 0x53) {    /* Backspace / Delete */
            int k;
            if (sc == 0x0E && url_pos > 0) {
                for (k = url_pos - 1; k < url_len; k++) page_url[k] = page_url[k + 1];
                url_pos--;
                url_len--;
            } else if (sc == 0x53 && url_pos < url_len) {
                for (k = url_pos; k < url_len - 1; k++) page_url[k] = page_url[k + 1];
                url_len--;
            }
            page_url[url_len] = 0;
            scene_dirty = 1;
            return;
        }
    }

    for (a = 0; a < 4; a++) if (wins[a].active && wins[a].open) {
        if (wins[a].kind == 1 && (sc == 0x48 || sc == 0x50 || sc == 0x49 || sc == 0x51)) {
            int visible = (wins[a].h - 98) / 10;
            int maxs = page_lines - visible;
            int step = (sc == 0x49 || sc == 0x51) ? 5 : 1;
            if (maxs < 0) maxs = 0;
            if (sc == 0x48 || sc == 0x49) {
                page_scroll -= step;
                if (page_scroll < 0) page_scroll = 0;
            } else {
                page_scroll += step;
                if (page_scroll > maxs) page_scroll = maxs;
            }
            scene_dirty = 1;
            return;
        }
        if (sc == 0x4B) { wins[a].x -= 8; if (wins[a].x < 0) wins[a].x = 0; }
        else if (sc == 0x4D) {
            wins[a].x += 8;
            if (wins[a].x > (int)screen_w - wins[a].w) wins[a].x = (int)screen_w - wins[a].w;
        }
        else if (sc == 0x48) { wins[a].y -= 8; if (wins[a].y < 0) wins[a].y = 0; }
        else if (sc == 0x50) {
            wins[a].y += 8;
            if (wins[a].y > (int)screen_h - 76) wins[a].y = (int)screen_h - 76;
        }
        break;
    }

    {
        char c = scmap[sc];
        if (c >= 'A' && c <= 'Z' && !shift_down) c += 32;
        if (wins[3].active && wins[3].open) {
            if (c && url_len < 190) {
                int k;
                for (k = url_len; k > url_pos; k--) page_url[k] = page_url[k - 1];
                page_url[url_pos++] = c;
                url_len++;
                page_url[url_len] = 0;
            }
        } else if (wins[2].active && wins[2].open) {
            if (c == 8) { if (notes_len > 0) notes[--notes_len] = 0; }
            else if (c && notes_len < 60) notes[notes_len++] = c;
        }
    }
    scene_dirty = 1;
}

static u8 mp[4];
static int mp_idx = 0;

static void poll_input(void) {
    int n;
    for (n = 0; n < 16; n++) {
        u8 st = inb(0x64);
        u8 d;
        if (!(st & 1)) break;
        d = inb(0x60);
        if (st & 0x20) {
            mp[mp_idx++] = d;
            if (mp_idx >= mp_need) { mp_idx = 0; mouse_packet(mp); }
        } else {
            key_byte(d);
        }
    }
}

/* ---------------------------- PS/2 mouse init --------------------------- */
static void kbd_wait(void) {
    int t = 100000;
    while ((inb(0x64) & 2) && --t) { }
}
static void aux_wait(void) {
    int t = 100000;
    while (!(inb(0x64) & 1) && --t) { }
}
static void send_mouse(u8 v) {
    kbd_wait(); outb(0x64, 0xD4);
    kbd_wait(); outb(0x60, v);
    aux_wait(); (void)inb(0x60);
}
static u8 send_mouse_get(u8 v) {
    u8 r;
    kbd_wait(); outb(0x64, 0xD4);
    kbd_wait(); outb(0x60, v);
    aux_wait(); r = inb(0x60);          /* ACK 0xFA */
    if (r == 0xFA) {                    /* 0xF2 also returns the device ID */
        aux_wait();
        r = inb(0x60);
    }
    return r;
}
static void mouse_init(void) {
    u8 cfg;
    while (inb(0x64) & 1) (void)inb(0x60);

    kbd_wait(); outb(0x64, 0xA8);
    aux_wait(); (void)inb(0x60);

    kbd_wait(); outb(0x64, 0x20);
    aux_wait(); cfg = inb(0x60);
    cfg |= 0x02;
    cfg &= (u8)~0x10;
    kbd_wait(); outb(0x64, 0x60);
    kbd_wait(); outb(0x60, cfg);

    kbd_wait(); outb(0x64, 0xD4);
    kbd_wait(); outb(0x60, 0xFF);
    aux_wait(); (void)inb(0x60);
    aux_wait(); (void)inb(0x60);
    aux_wait(); (void)inb(0x60);

    /* Detect an Intellimouse wheel: sample rate 200, 1, then read ID. */
    send_mouse(0xF3);
    send_mouse(200);
    send_mouse(0xF3);
    send_mouse(1);
    {
        u8 id = send_mouse_get(0xF2);
        if (id == 3 || id == 4) { mouse4 = 1; mp_need = 4; }
    }
    send_mouse(0xF4);

    mx = (int)screen_w / 2;
    my = (int)screen_h / 2;
    old_mx = mx;
    old_my = my;
}

/* ------------------------------ RTC clock ------------------------------- */
static int bcd2(u8 v) { return ((v >> 4) & 0x0F) * 10 + (v & 0x0F); }

static void read_clock(void) {
    u8 h, m, s, d, mo, y;
    outb(0x70, 0x85); h = inb(0x71);
    outb(0x70, 0x84); m = inb(0x71);
    outb(0x70, 0x80); s = inb(0x71);
    outb(0x70, 0x07); d = inb(0x71);
    outb(0x70, 0x08); mo = inb(0x71);
    outb(0x70, 0x09); y = inb(0x71);
    h &= 0x7F; m &= 0x7F; s &= 0x7F;
    hh = bcd2(h); mm = bcd2(m); ss = bcd2(s);
    dd_day = bcd2(d); dd_month = bcd2(mo);
    dd_year = 2000 + bcd2(y);
    if (hh > 23) hh = 0;
    if (mm > 59) mm = 0;
    if (ss > 59) ss = 0;
}

/* --------------------------------- main --------------------------------- */
void kernel_main(void) {
    u32 lfb = *(volatile u32 *)0x0500;
    u32 bpp = *(volatile u8 *)0x050A;

    pitch_bytes = *(volatile u16 *)0x0504;
    screen_w = *(volatile u16 *)0x0506;
    screen_h = *(volatile u16 *)0x0508;
    bpp_val = bpp;
    bytespp = bpp / 8;
    if (bytespp != 2 && bytespp != 3 && bytespp != 4) bytespp = 4;

    fb = (volatile u8 *)lfb;
    scene = (u8 *)0x200000;      /* 0x200000 - 0x440000 */
    screen_buf = (u8 *)0x600000; /* 0x600000 - 0x840000 */
    dbuf = scene;

    serial_init();
    url_len = cstrlen(page_url);
    page_home();
    mouse_init();
    read_clock();
    last_clock = hh * 3600 + mm * 60 + ss;

    for (;;) {
        ticks++;
        poll_input();
        read_clock();
        if (hh * 3600 + mm * 60 + ss != last_clock) {
            last_clock = hh * 3600 + mm * 60 + ss;
            scene_dirty = 1;
        }
        if (scene_dirty) {
            compose_full();
            scene_dirty = 0;
            cursor_dirty = 0;
        } else if (cursor_dirty) {
            compose_cursor();
            cursor_dirty = 0;
        }
    }
}
