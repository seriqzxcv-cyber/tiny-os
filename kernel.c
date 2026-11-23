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


/* ---------------------- CPU exceptions / IDT ---------------------------- */
typedef struct {
    u16 off_lo;
    u16 sel;
    u8 zero;
    u8 type;
    u16 off_hi;
} __attribute__((packed)) IdtEntry;
typedef struct {
    u16 limit;
    u32 base;
} __attribute__((packed)) IdtPtr;

static IdtEntry idt[256];
static IdtPtr idtp;
static void fault_dispatch(u32 *regs) __attribute__((used));

extern void isr_default(void);
extern void isr0(void);  extern void isr1(void);  extern void isr2(void);
extern void isr3(void);  extern void isr4(void);  extern void isr5(void);
extern void isr6(void);  extern void isr7(void);  extern void isr8(void);
extern void isr9(void);  extern void isr10(void); extern void isr11(void);
extern void isr12(void); extern void isr13(void); extern void isr14(void);
extern void isr15(void); extern void isr16(void); extern void isr17(void);
extern void isr18(void); extern void isr19(void); extern void isr20(void);
extern void isr21(void); extern void isr22(void); extern void isr23(void);
extern void isr24(void); extern void isr25(void); extern void isr26(void);
extern void isr27(void); extern void isr28(void); extern void isr29(void);
extern void isr30(void); extern void isr31(void);

__asm__(
".text\n"
".global isr_common\n"
"isr_common:\n"
"  pushal\n"
"  pushl %esp\n"
"  call fault_dispatch\n"
"  addl $4, %esp\n"
"  popal\n"
"  addl $8, %esp\n"
"  iret\n"
".global isr_default\n"
"isr_default:\n"
"  iret\n"
".global isr0\n"
"isr0:\n"
"  pushl $0\n"
"  pushl $0\n"
"  jmp isr_common\n"
".global isr1\n"
"isr1:\n"
"  pushl $0\n"
"  pushl $1\n"
"  jmp isr_common\n"
".global isr2\n"
"isr2:\n"
"  pushl $0\n"
"  pushl $2\n"
"  jmp isr_common\n"
".global isr3\n"
"isr3:\n"
"  pushl $0\n"
"  pushl $3\n"
"  jmp isr_common\n"
".global isr4\n"
"isr4:\n"
"  pushl $0\n"
"  pushl $4\n"
"  jmp isr_common\n"
".global isr5\n"
"isr5:\n"
"  pushl $0\n"
"  pushl $5\n"
"  jmp isr_common\n"
".global isr6\n"
"isr6:\n"
"  pushl $0\n"
"  pushl $6\n"
"  jmp isr_common\n"
".global isr7\n"
"isr7:\n"
"  pushl $0\n"
"  pushl $7\n"
"  jmp isr_common\n"
".global isr8\n"
"isr8:\n"
"  pushl $8\n"
"  jmp isr_common\n"
".global isr9\n"
"isr9:\n"
"  pushl $0\n"
"  pushl $9\n"
"  jmp isr_common\n"
".global isr10\n"
"isr10:\n"
"  pushl $10\n"
"  jmp isr_common\n"
".global isr11\n"
"isr11:\n"
"  pushl $11\n"
"  jmp isr_common\n"
".global isr12\n"
"isr12:\n"
"  pushl $12\n"
"  jmp isr_common\n"
".global isr13\n"
"isr13:\n"
"  pushl $13\n"
"  jmp isr_common\n"
".global isr14\n"
"isr14:\n"
"  pushl $14\n"
"  jmp isr_common\n"
".global isr15\n"
"isr15:\n"
"  pushl $0\n"
"  pushl $15\n"
"  jmp isr_common\n"
".global isr16\n"
"isr16:\n"
"  pushl $0\n"
"  pushl $16\n"
"  jmp isr_common\n"
".global isr17\n"
"isr17:\n"
"  pushl $17\n"
"  jmp isr_common\n"
".global isr18\n"
"isr18:\n"
"  pushl $0\n"
"  pushl $18\n"
"  jmp isr_common\n"
".global isr19\n"
"isr19:\n"
"  pushl $0\n"
"  pushl $19\n"
"  jmp isr_common\n"
".global isr20\n"
"isr20:\n"
"  pushl $0\n"
"  pushl $20\n"
"  jmp isr_common\n"
".global isr21\n"
"isr21:\n"
"  pushl $21\n"
"  jmp isr_common\n"
".global isr22\n"
"isr22:\n"
"  pushl $0\n"
"  pushl $22\n"
"  jmp isr_common\n"
".global isr23\n"
"isr23:\n"
"  pushl $0\n"
"  pushl $23\n"
"  jmp isr_common\n"
".global isr24\n"
"isr24:\n"
"  pushl $0\n"
"  pushl $24\n"
"  jmp isr_common\n"
".global isr25\n"
"isr25:\n"
"  pushl $0\n"
"  pushl $25\n"
"  jmp isr_common\n"
".global isr26\n"
"isr26:\n"
"  pushl $0\n"
"  pushl $26\n"
"  jmp isr_common\n"
".global isr27\n"
"isr27:\n"
"  pushl $0\n"
"  pushl $27\n"
"  jmp isr_common\n"
".global isr28\n"
"isr28:\n"
"  pushl $0\n"
"  pushl $28\n"
"  jmp isr_common\n"
".global isr29\n"
"isr29:\n"
"  pushl $29\n"
"  jmp isr_common\n"
".global isr30\n"
"isr30:\n"
"  pushl $30\n"
"  jmp isr_common\n"
".global isr31\n"
"isr31:\n"
"  pushl $0\n"
"  pushl $31\n"
"  jmp isr_common\n"
);

static void idt_set(int n, u32 handler) {
    idt[n].off_lo = (u16)(handler & 0xFFFF);
    idt[n].sel = 0x08;
    idt[n].zero = 0;
    idt[n].type = 0x8E;
    idt[n].off_hi = (u16)(handler >> 16);
}

static void idt_init(void) {
    static const u32 table[32] = {
        (u32)isr0,  (u32)isr1,  (u32)isr2,  (u32)isr3,
        (u32)isr4,  (u32)isr5,  (u32)isr6,  (u32)isr7,
        (u32)isr8,  (u32)isr9,  (u32)isr10, (u32)isr11,
        (u32)isr12, (u32)isr13, (u32)isr14, (u32)isr15,
        (u32)isr16, (u32)isr17, (u32)isr18, (u32)isr19,
        (u32)isr20, (u32)isr21, (u32)isr22, (u32)isr23,
        (u32)isr24, (u32)isr25, (u32)isr26, (u32)isr27,
        (u32)isr28, (u32)isr29, (u32)isr30, (u32)isr31
    };
    int i;
    for (i = 0; i < 256; i++) idt_set(i, (u32)isr_default);
    for (i = 0; i < 32; i++) idt_set(i, table[i]);
    idtp.limit = (u16)(sizeof(idt) - 1);
    idtp.base = (u32)idt;
    __asm__ volatile ("lidt %0" : : "m"(idtp));
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
static char page_url[200] = "http://example.com";   /* loaded page */
static char url_input[200] = "http://example.com";  /* text in address bar */
static char prev_url[200];
static int prev_valid = 0;
static char next_url[200];
static int next_valid = 0;
static int url_len = 0;
static char page_title[96] = "TINY BROWSER";
static char status_msg[80] = "READY";
static int url_pos = 0;                 /* caret position inside url_input */
static int ctrl_down = 0;
static int shift_down = 0;
static int alt_down = 0;
static int link_focus = -1;            /* keyboard-focused link in the browser */
static int mp_need = 3;

static void compose_full(void);
static void activate(int i);
static void calc_key_rect(int k, int *x, int *y, int *w, int *h);
static void calc_key(char k);

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
#define MENU_H 218
#define MENU_ITEMS 8
static const char *menu_names[MENU_ITEMS] = {
    "WELCOME", "SYSTEM", "NOTES", "BROWSER", "TERMINAL", "CALCULATOR",
    "CHANGE THEME", "CLOSE MENU"
};
static int menu_open = 0;
static int mouse4 = 0;                 /* Intellimouse: 4-byte packets */
static u32 ticks = 0;
static int last_click_sec = -1;
static int last_click_win = -1;
static int dd_day = 1, dd_month = 1, dd_year = 2026;
static char cpu_vendor[16] = "UNKNOWN";
static u32 boot_clock = 0, uptime_sec = 0;

/* PIT channel 0 is used only as a stopwatch for serial timeouts. */
static void pit_init(void) {
    outb(0x43, 0x34);             /* channel 0, lo/hi, mode 2, binary */
    outb(0x40, 0xFF);
    outb(0x40, 0xFF);
}

static u16 pit_read(void) {
    u16 v;
    outb(0x43, 0x00);             /* latch channel 0 */
    v = (u16)inb(0x40);
    v |= (u16)((u16)inb(0x40) << 8);
    return v;
}

/* Wait for a COM1 flag with a real time limit (not a CPU-loop guess). */
static int serial_wait(u8 mask, u32 ms) {
    u32 prev = pit_read(), acc = 0, now;
    for (;;) {
        if (inb(0x3FD) & mask) return 1;
        now = pit_read();
        if (now < prev) acc += 65536u;   /* 16-bit counter wrapped */
        prev = now;
        if ((acc + now) * 1000u / 1193182u >= ms) return 0;
    }
}

static void serial_init(void) {
    pit_init();
    outb(0x3FB, 0x80);            /* DLAB on */
    outb(0x3F8, 0x01);            /* 115200 baud */
    outb(0x3F9, 0x00);
    outb(0x3FB, 0x03);            /* 8N1 */
    outb(0x3FA, 0x01);            /* FIFO on */
    outb(0x3F9, 0x00);            /* polling only */
}

static int serial_put(u8 c) {
    if (!serial_wait(0x20, 2000)) return 0;
    outb(0x3F8, c);
    return 1;
}

static int serial_get_ms(u8 *c, u32 ms) {
    if (!serial_wait(0x01, ms)) return 0;
    *c = inb(0x3F8);
    return 1;
}

static int serial_get(u8 *c) { return serial_get_ms(c, 6000); }

static void pit_delay(u32 ms) {
    u32 prev = pit_read(), acc = 0, now;
    for (;;) {
        now = pit_read();
        if (now < prev) acc += 65536u;
        prev = now;
        if ((acc + now) * 1000u / 1193182u >= ms) return;
    }
}

/* PC speaker is driven by PIT channel 2. */
static void speaker_on(u32 hz) {
    u32 div = 1193182u / (hz ? hz : 1);
    outb(0x43, 0xB6);
    outb(0x42, (u8)(div & 0xFF));
    outb(0x42, (u8)(div >> 8));
    outb(0x61, (u8)(inb(0x61) | 3));
}

static void speaker_off(void) {
    outb(0x61, (u8)(inb(0x61) & 0xFC));
}

static void beep(u32 hz, u32 ms) {
    speaker_on(hz);
    pit_delay(ms);
    speaker_off();
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
    link_focus = -1;
    nlinks = 0;
}

static int fetch_page(const char *url) {
    u8 c;
    u32 i, n = 0;
    char *buf = PAGE_BUF;

    cstr_copy(status_msg, "LOADING...", sizeof(status_msg));
    if (fb) compose_full();        /* show feedback before the blocking read */

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
        "<p>UP/DOWN and PageUp/PageDown scroll. BACK/FWD walk the history.</p>"
        "<p><b>TAB</b> focuses links, <b>ENTER</b> opens the focused link,"
        " ESC clears it. ALT+TAB switches windows, CTRL+W closes one.</p>"
        "<p>Drag a window corner to resize it. F8 opens the start menu.</p>";
    int i;
    for (i = 0; home[i]; i++) PAGE_BUF[i] = home[i];
    PAGE_BUF[i] = 0;
    page_len = (u32)i;
    page_scroll = 0;
    cstr_copy(page_title, "TINY BROWSER", sizeof(page_title));
    cstr_copy(status_msg, "START PAGE", sizeof(status_msg));
    link_focus = -1;
    nlinks = 0;
    cstr_copy(url_input, page_url, sizeof(url_input));
    url_len = cstrlen(url_input);
    url_pos = url_len;
}

static void url_set(const char *s) {
    int i = 0;
    while (s[i] && i < 190) { page_url[i] = s[i]; i++; }
    page_url[i] = 0;
    cstr_copy(url_input, page_url, sizeof(url_input));
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
        const char *q = "https://www.bing.com/search?format=rss&q=";
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
    link_focus = -1;
    nlinks = 0;
    if (page_url[0]) {
        for (i = 0; page_url[i]; i++) prev_url[i] = page_url[i];
        prev_url[i] = 0;
        prev_valid = 1;
    }
    next_valid = 0;
    prepare_url(u, full, sizeof(full));
    url_set(full);
    page_title[0] = 0;
    fetch_page(page_url);
    nlinks = 0;
    if (!page_title[0]) cstr_copy(page_title, "TINY BROWSER", sizeof(page_title));
    cstr_copy(status_msg, page_url, sizeof(status_msg));
}

static void go_back(void) {
    int i;
    if (!prev_valid) return;
    link_focus = -1;
    nlinks = 0;
    for (i = 0; page_url[i]; i++) next_url[i] = page_url[i];
    next_url[i] = 0;
    next_valid = 1;
    url_set(prev_url);
    page_title[0] = 0;
    fetch_page(page_url);
    nlinks = 0;
    if (!page_title[0]) cstr_copy(page_title, "TINY BROWSER", sizeof(page_title));
    cstr_copy(status_msg, prev_url, sizeof(status_msg));
    prev_valid = 0;
}

static void go_forward(void) {
    int i;
    if (!next_valid) return;
    link_focus = -1;
    nlinks = 0;
    for (i = 0; page_url[i]; i++) prev_url[i] = page_url[i];
    prev_url[i] = 0;
    prev_valid = 1;
    url_set(next_url);
    page_title[0] = 0;
    fetch_page(page_url);
    nlinks = 0;
    if (!page_title[0]) cstr_copy(page_title, "TINY BROWSER", sizeof(page_title));
    cstr_copy(status_msg, next_url, sizeof(status_msg));
    next_valid = 0;
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

static const char *exception_name(u32 v) {
    switch (v) {
        case 0:  return "DIVIDE ERROR";
        case 1:  return "DEBUG";
        case 2:  return "NMI";
        case 3:  return "BREAKPOINT";
        case 4:  return "OVERFLOW";
        case 5:  return "BOUND RANGE";
        case 6:  return "INVALID OPCODE";
        case 7:  return "NO FPU";
        case 8:  return "DOUBLE FAULT";
        case 10: return "INVALID TSS";
        case 11: return "SEGMENT NOT PRESENT";
        case 12: return "STACK FAULT";
        case 13: return "GENERAL PROTECTION";
        case 14: return "PAGE FAULT";
        case 16: return "FPU ERROR";
        case 17: return "ALIGNMENT CHECK";
        case 18: return "MACHINE CHECK";
        case 19: return "SIMD ERROR";
        default: return "CPU EXCEPTION";
    }
}

static void hex_str(char *out, u32 v) {
    static const char *digits = "0123456789ABCDEF";
    int i;
    out[0] = '0'; out[1] = 'x';
    for (i = 0; i < 8; i++) out[2 + i] = digits[(v >> (28 - i * 4)) & 15];
    out[10] = 0;
}

static void __attribute__((noreturn)) kernel_panic(u32 vec, u32 err, u32 eip) {
    char hex[12];
    __asm__ volatile ("cli");
    beep(110, 350);
    dbuf = (u8 *)fb;
    rect(0, 0, (int)screen_w, (int)screen_h, RGB(8, 10, 40));
    rect(0, 0, (int)screen_w, 76, RGB(86, 12, 36));
    rect(0, 76, (int)screen_w, 2, RGB(240, 90, 100));
    draw_text2(40, 20, "KERNEL PANIC", RGB(255, 190, 190));
    draw_text(40, 56, "TINY OS stopped to protect the machine.", RGB(255, 220, 220));

    draw_text(40, 110, "EXCEPTION:", RGB(150, 180, 235));
    hex_str(hex, vec);
    draw_text(180, 110, hex, RGB(255, 210, 120));
    draw_text(260, 110, exception_name(vec), RGB(255, 160, 150));

    draw_text(40, 134, "ERROR CODE:", RGB(150, 180, 235));
    hex_str(hex, err);
    draw_text(180, 134, hex, RGB(255, 210, 120));
    draw_text(40, 158, "EIP:", RGB(150, 180, 235));
    hex_str(hex, eip);
    draw_text(180, 158, hex, RGB(255, 210, 120));

    draw_text(40, 200, "THE SYSTEM HAS HALTED. RESTART QEMU TO CONTINUE.",
              RGB(205, 215, 235));
    draw_text(40, 220, "IDT IS ACTIVE: 32 EXCEPTION VECTORS INSTALLED.",
              RGB(130, 160, 210));
    draw_text(40, 240, "TERMINAL: TYPE 'panic' TO SEE THIS SCREEN AGAIN.",
              RGB(130, 160, 210));
    for (;;) __asm__ volatile ("hlt");
}

static void __attribute__((used, noinline)) fault_dispatch(u32 *regs) {
    __asm__ volatile ("cli");
    kernel_panic(regs[8], regs[9], regs[10]);
    for (;;) __asm__ volatile ("hlt");
}

static void draw_num2(int x, int y, int v, u32 c) {
    if (v < 10) { draw_char(x, y, '0', c); x += 6; }
    draw_num(x, y, v, c);
}

static int isqrt_small(int v) {
    int r = 0;
    while ((r + 1) * (r + 1) <= v) r++;
    return r;
}

/* Cheap triangle wave used for the mountain silhouettes. */
static int wave(int x, int period, int amp) {
    int p = (x % period + period) % period;
    if (p > period / 2) p = period - p;
    return p * amp * 2 / period;
}

static void fill_circle(int cx, int cy, int r, u32 c) {
    int y, dx;
    for (y = -r; y <= r; y++) {
        int v = r * r - y * y;
        dx = v >= 0 ? isqrt_small(v) : 0;
        rect(cx - dx, cy + y, dx * 2 + 1, 1, c);
    }
}

/* Classic raised/beveled button - works for any rectangle. */
static void bevel(int x, int y, int w, int h, u32 base, int hot) {
    rect(x, y, w, h, hot ? lerp(base, RGB(255, 255, 255), 24) : base);
    rect(x, y, w, 1, lerp(base, RGB(255, 255, 255), hot ? 65 : 48));
    rect(x, y, 1, h, lerp(base, RGB(255, 255, 255), hot ? 50 : 34));
    rect(x, y + h - 1, w, 1, lerp(base, RGB(0, 0, 0), hot ? 55 : 38));
    rect(x + w - 1, y, 1, h, lerp(base, RGB(0, 0, 0), hot ? 55 : 38));
}

static void draw_app_icon(int x, int y, int idx) {
    int i;
    if (idx == 0) {                       /* welcome window */
        rect(x, y, 10, 10, RGB(38, 92, 160));
        frame(x, y, 10, 10, RGB(205, 228, 255));
        rect(x + 1, y + 1, 8, 2, RGB(110, 170, 235));
        putp(x + 3, y + 5, RGB(255, 255, 255));
        putp(x + 6, y + 5, RGB(255, 255, 255));
        rect(x + 3, y + 7, 4, 1, RGB(255, 255, 255));
    } else if (idx == 1) {                /* monitor */
        rect(x + 1, y, 8, 7, RGB(42, 52, 68));
        frame(x + 1, y, 8, 7, RGB(185, 205, 230));
        rect(x + 2, y + 1, 6, 5, RGB(70, 145, 210));
        rect(x + 4, y + 7, 2, 2, RGB(185, 205, 230));
        rect(x + 2, y + 9, 6, 1, RGB(140, 160, 190));
    } else if (idx == 2) {                /* notepad */
        rect(x + 1, y, 8, 10, RGB(28, 96, 68));
        frame(x + 1, y, 8, 10, RGB(120, 220, 170));
        for (i = 0; i < 3; i++) rect(x + 2, y + 3 + i * 2, 5, 1, RGB(185, 240, 205));
    } else if (idx == 3) {                /* browser globe */
        fill_circle(x + 5, y + 5, 4, RGB(38, 118, 205));
        rect(x + 1, y + 5, 8, 1, RGB(160, 215, 255));
        rect(x + 5, y + 1, 1, 8, RGB(160, 215, 255));
        frame(x + 3, y + 1, 4, 8, RGB(120, 190, 245));
    } else if (idx == 5) {                /* calculator */
        rect(x + 1, y, 9, 10, RGB(58, 48, 28));
        frame(x + 1, y, 9, 10, RGB(240, 205, 130));
        rect(x + 2, y + 1, 7, 2, RGB(250, 220, 150));
        for (i = 0; i < 3; i++) {
            putp(x + 2, y + 5 + i * 2, RGB(255, 240, 200));
            putp(x + 4, y + 5 + i * 2, RGB(255, 240, 200));
            putp(x + 6, y + 5 + i * 2, RGB(255, 240, 200));
        }
    } else {                              /* terminal */
        rect(x + 1, y, 9, 10, RGB(12, 18, 26));
        frame(x + 1, y, 9, 10, RGB(120, 205, 165));
        rect(x + 3, y + 3, 2, 2, RGB(150, 245, 190));
        rect(x + 5, y + 5, 2, 2, RGB(150, 245, 190));
        rect(x + 3, y + 7, 2, 2, RGB(150, 245, 190));
    }
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

#define WIN_N 6
static Win wins[WIN_N] = {
    {  90,  92, 440, 255, 0, 0, 0, 0, "WELCOME TO TINY OS", 1, 1, 0, 0, 1, RGB(14, 34, 66),  0 },
    { 370, 150, 410, 380, 0, 0, 0, 0, "SYSTEM INFO",       1, 0, 0, 0, 2, RGB(26, 29, 38),  0 },
    { 220, 345, 390, 250, 0, 0, 0, 0, "NOTES",             1, 0, 0, 0, 3, RGB(16, 48, 38),  0 },
    {  70,  60, 760, 560, 0, 0, 0, 0, "TINY BROWSER",       1, 0, 0, 0, 4, RGB(16, 20, 30),  1 },
    { 120, 180, 560, 350, 0, 0, 0, 0, "TERMINAL",           0, 0, 0, 0, 5, RGB(8, 14, 22),    0 },
    { 700,  90, 300, 330, 0, 0, 0, 0, "CALCULATOR",         0, 0, 0, 0, 6, RGB(18, 22, 30),   0 },
};
static int top_z = 6;
static const char *task_label[WIN_N] = {
    "WELCOME", "SYSTEM", "NOTES", "BROWSER", "TERMINAL", "CALC"
};

/* A tiny terminal with a pip/pkg-like package manager. */
#define TERM_MAX 40
#define TERM_CMD 128
static char term_lines[TERM_MAX][80];
static int term_head = 0, term_count = 0, term_scroll = 0;
static char term_cmd[TERM_CMD];
static int term_pos = 0;
static char term_cwd[80] = "/";
#define TERM_HIST 12
static char term_hist[TERM_HIST][TERM_CMD];
static int term_hist_count = 0, term_hist_pos = 0;

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

static char notes[256];
static int notes_len = 0;
static char notes_status[40] = "READY";

/* Calculator state (32-bit integer arithmetic). */
static char calc_disp[16] = "0";
static int calc_val = 0, calc_acc = 0, calc_new = 1, calc_err = 0;
static char calc_op = 0;
static const char calc_keys[17] = {
    '7','8','9','/', '4','5','6','*', '1','2','3','-', '0','C','B','+', '='
};

static int mx = 400, my = 300, mb = 0;
static int old_mx = 400, old_my = 300;
static int dragging = -1, drag_ox = 0, drag_oy = 0;
static int resizing = -1, resize_ox = 0, resize_oy = 0;
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
    for (i = 0; i < WIN_N; i++) out[i] = &wins[i];
    for (i = 0; i < WIN_N; i++)
        for (j = i + 1; j < WIN_N; j++)
            if (out[i]->z > out[j]->z) {
                Win *t = out[i]; out[i] = out[j]; out[j] = t;
            }
}

static void draw_button(int x, int y, u32 normal, u32 hover, char glyph, int hot) {
    bevel(x, y, 16, 16, hot ? hover : normal, hot);
    draw_char(x + 6, y + 5, glyph, RGB(10, 12, 18));
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
    if (id >= 100 && id < 100 + WIN_N) {
        int i = id - 100;
        *x = 108 + i * 124; *y = (int)screen_h - 33; *w = 116; *h = 26;
        return 1;
    }
    if (id == 500) {
        *x = MENU_X; *y = (int)screen_h - 33; *w = 86; *h = 26;
        return 1;
    }
    if (id >= 200 && id < 200 + MENU_ITEMS) {
        if (!menu_open) return 0;
        menu_item_rect(id - 200, x, y, w, h);
        return 1;
    }
    if (id >= 400 && id < 417) {
        if (!wins[5].open || wins[5].minimized) return 0;
        calc_key_rect(id - 400, x, y, w, h);
        return 1;
    }
    if (id >= 300 && id < 300 + MAX_LINKS) {
        int k = id - 300;
        if (k >= nlinks || !wins[3].open || wins[3].minimized) return 0;
        *x = links[k].x - 2; *y = links[k].y - 2;
        *w = links[k].w + 4; *h = 13;
        return 1;
    }
    if (id >= 0 && id < WIN_N * 4) {
        int i = id / 4, b = id % 4;
        if (b == 0 || i >= WIN_N || !wins[i].open || wins[i].minimized) return 0;
        *x = wins[i].x + wins[i].w - (b == 1 ? 68 : b == 2 ? 46 : 24);
        *y = wins[i].y + 4; *w = 16; *h = 16;
        return 1;
    }
    return 0;
}

static void draw_hover_item(int id, int hot) {
    if (id == 500) {
        int bx = MENU_X, by = (int)screen_h - 33;
        rect(bx, by, 86, 26, hot ? lerp(accent[wall], RGB(255,255,255), 30)
                                  : accent[wall]);
        frame(bx, by, 86, 26, RGB(155, 195, 240));
        draw_text2(bx + 20, by + 7, "OS", RGB(255, 255, 255));
        return;
    }
    if (id >= 200 && id < 200 + MENU_ITEMS) {
        int i = id - 200, x, y, w, h;
        if (!menu_open) return;
        menu_item_rect(i, &x, &y, &w, &h);
        rect(x, y, w, h, hot ? lerp(accent[wall], RGB(255,255,255), 22)
                             : RGB(34, 39, 52));
        if (i < WIN_N) draw_app_icon(x + 2, y + 2, i);
        else if (i == WIN_N) rect(x + 4, y + 5, 10, 10, accent[wall]);
        draw_text(x + 17, y + 6, menu_names[i],
                  hot ? RGB(255,255,255) : RGB(205, 215, 232));
        return;
    }
    if (id >= 400 && id < 417) {
        int k = id - 400, kx, ky, kw, kh;
        char label = calc_keys[k];
        if (!wins[5].open || wins[5].minimized) return;
        calc_key_rect(k, &kx, &ky, &kw, &kh);
        bevel(kx, ky, kw, kh, hot ? RGB(92, 112, 145) : RGB(42, 48, 60), hot);
        if (label == 'B') draw_text(kx + kw / 2 - 3, ky + 11, "<", RGB(245, 250, 255));
        else draw_char(kx + kw / 2 - 3, ky + 11, label, RGB(245, 250, 255));
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
    if (id >= 100 && id < 100 + WIN_N) {
        int i = id - 100, bx = 108 + i * 124, by = (int)screen_h - 33;
        rect(bx, by, 116, 26,
             hot ? RGB(72, 86, 110) : (wins[i].open ? RGB(48, 55, 70) : RGB(32, 36, 44)));
        frame(bx, by, 116, 26, hot ? RGB(140, 185, 235) : RGB(80, 88, 106));
        if (wins[i].active) rect(bx + 2, by + 24, 112, 2, accent[wall]);
        draw_app_icon(bx + 4, by + 8, i);
        draw_text_n(bx + 18, by + 7, task_label[i], 16,
                    wins[i].open ? RGB(235, 240, 250) : RGB(130, 136, 148));
        return;
    }
    if (id >= 0 && id < WIN_N * 4) {
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
    int bold;
    int underline;
    int ordered;
    int ol_num;
    int pre;
} Rctx;

static void r_emit(Rctx *c, char ch) {
    int sx, sy;
    if (c->col >= c->maxcol) { c->col = 0; c->line++; }
    sx = c->px + c->col * 6;
    sy = c->py + c->line * 10 - c->scroll * 10;
    if (sy >= c->py && sy + 8 <= c->py + c->ph && ch >= 32) {
        draw_char(sx, sy, ch, c->color);
        if (c->bold) draw_char(sx + 1, sy, ch, c->color);
    }
    if (c->link && sy >= c->py && sy + 8 <= c->py + c->ph) {
        rect(sx, sy + 7, 6, 1, c->link_color);
        if (c->link_llen < 63 && ch >= 32) {
            c->link_label[c->link_llen++] = ch;
            c->link_label[c->link_llen] = 0;
        }
    }
    c->col++;
}

static void r_break(Rctx *c) {
    if (c->underline && c->col > 0) {
        int yy = c->py + c->line * 10 - c->scroll * 10 + 8;
        if (yy >= c->py && yy + 1 <= c->py + c->ph)
            rect(c->px, yy, c->col * 6, 1, c->color);
    }
    c->underline = 0;
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
    c.bold = 0;
    c.underline = 0;
    c.ordered = 0;
    c.ol_num = 0;
    c.pre = 0;
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
                if (ci_eq(ns, nl, "h1") || ci_eq(ns, nl, "h2") || ci_eq(ns, nl, "h3")) {
                    r_break(&c);
                    c.color = ci_eq(ns, nl, "h1") ? RGB(18, 58, 132) :
                              ci_eq(ns, nl, "h2") ? RGB(26, 78, 158) : RGB(40, 90, 160);
                    c.bold = 1;
                    c.underline = 1;
                }
                else if (ci_eq(ns, nl, "li")) {
                    r_break(&c); c.color = RGB(35, 40, 52);
                    if (c.ordered) {
                        char nb[8];
                        int v = ++c.ol_num, k = 0, j;
                        if (v > 99) v = 99;
                        if (v >= 10) nb[k++] = (char)('0' + v / 10);
                        nb[k++] = (char)('0' + v % 10);
                        nb[k++] = '.';
                        nb[k++] = ' ';
                        for (j = 0; j < k; j++) r_emit(&c, nb[j]);
                    } else {
                        r_emit(&c, '-');
                        r_emit(&c, ' ');
                    }
                }
                else if (ci_eq(ns, nl, "ol")) {
                    r_break(&c); c.color = RGB(35, 40, 52);
                    c.ordered = 1; c.ol_num = 0;
                }
                else if (ci_eq(ns, nl, "ul")) {
                    r_break(&c); c.color = RGB(35, 40, 52);
                    c.ordered = 0;
                }
                else if (ci_eq(ns, nl, "pre")) {
                    r_break(&c); c.color = RGB(30, 62, 44);
                    c.pre = 1;
                }
                else if (ci_eq(ns, nl, "code") || ci_eq(ns, nl, "tt")) {
                    c.color = RGB(120, 45, 35);
                }
                else if (ci_eq(ns, nl, "blockquote")) {
                    r_break(&c); c.color = RGB(70, 80, 95);
                }
                else if (ci_eq(ns, nl, "p") || ci_eq(ns, nl, "div") ||
                         ci_eq(ns, nl, "tr")) {
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
                else if (ci_eq(ns, nl, "b") || ci_eq(ns, nl, "strong")) {
                    c.color = RGB(15, 20, 30);
                    c.bold = 1;
                }
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
                else if (ci_eq(ns, nl, "b") || ci_eq(ns, nl, "strong")) {
                    c.bold = 0;
                    c.color = RGB(35, 40, 52);
                }
                else if (ci_eq(ns, nl, "h1") || ci_eq(ns, nl, "h2") || ci_eq(ns, nl, "h3")) {
                    r_break(&c);
                    c.bold = 0;
                    c.color = RGB(35, 40, 52);
                }
                else if (ci_eq(ns, nl, "pre")) {
                    r_break(&c);
                    c.pre = 0;
                    c.color = RGB(35, 40, 52);
                }
                else if (ci_eq(ns, nl, "code") || ci_eq(ns, nl, "tt") ||
                         ci_eq(ns, nl, "blockquote"))
                    c.color = RGB(35, 40, 52);
                else if (ci_eq(ns, nl, "ol") || ci_eq(ns, nl, "ul")) {
                    c.ordered = 0;
                    r_break(&c);
                }
                else if (ci_eq(ns, nl, "p") || ci_eq(ns, nl, "div") || ci_eq(ns, nl, "li"))
                    r_break(&c);
            }
            p = *gt ? gt + 1 : gt;
            continue;
        }

        if (c.pre && *p == '\n') { r_break(&c); p++; continue; }
        if (*p == '\n' || *p == '\r' || *p == '\t') { p++; continue; }
        r_emit(&c, *p);
        p++;
    }
    page_lines = c.line + 1;
}

/* ------------------------- window contents ------------------------------ */
static void draw_browser(Win *w) {
    int bx = w->x + 8, by = w->y + 30;
    int fx = w->x + 56;
    int ax = w->x + 104, aw = w->w - 236;
    int gx = w->x + w->w - 86;
    int hx = w->x + w->w - 46;
    int px = w->x + 8, py = w->y + 58;
    int pw = w->w - 16, ph = w->h - 90;
    int vlines = (ph - 16) / 10;
    int vmax = page_lines - vlines;

    /* toolbar */
    rect(w->x + 1, w->y + 24, w->w - 2, 30, RGB(38, 43, 56));
    rect(w->x + 1, w->y + 24, w->w - 2, 1, RGB(72, 80, 100));
    rect(w->x + 1, w->y + 53, w->w - 2, 1, RGB(10, 12, 18));

    /* BACK */
    bevel(bx, by, 44, 20, RGB(78, 88, 110), 0);
    draw_text_n(bx + 7, by + 7, "BACK", 6, RGB(10, 12, 18));
    draw_text_n(bx + 6, by + 6, "BACK", 6, RGB(240, 245, 255));

    /* FWD */
    bevel(fx, by, 44, 20, RGB(64, 96, 104), 0);
    draw_text_n(fx + 7, by + 7, "FWD", 5, RGB(10, 12, 18));
    draw_text_n(fx + 6, by + 6, "FWD", 5, RGB(240, 245, 255));

    /* address bar */
    rect(ax, by, aw, 20, RGB(12, 16, 26));
    frame(ax, by, aw, 20, w->active ? RGB(90, 160, 235) : RGB(70, 80, 100));
    {
        int vis = aw / 6 - 2, start = url_pos - vis + 1;
        if (start < 0) start = 0;
        draw_text_n(ax + 6, by + 6, url_input + start, vis, RGB(215, 232, 255));
        if (w->active)
            rect(ax + 6 + (url_pos - start) * 6, by + 4, 4, 11, RGB(235, 245, 255));
    }

    /* GO */
    bevel(gx, by, 34, 20, RGB(46, 155, 100), 0);
    draw_text_n(gx + 11, by + 7, "GO", 4, RGB(8, 30, 18));
    draw_text_n(gx + 10, by + 6, "GO", 4, RGB(255, 255, 255));

    /* HOME */
    bevel(hx, by, 38, 20, RGB(78, 88, 122), 0);
    draw_text_n(hx + 6, by + 7, "HOME", 5, RGB(10, 12, 22));
    draw_text_n(hx + 5, by + 6, "HOME", 5, RGB(240, 245, 255));

    /* page */
    rect(px, py, pw, ph, RGB(248, 249, 251));
    rect(px, py, pw, 1, RGB(200, 208, 220));
    if (vmax < 0) vmax = 0;
    if (page_scroll > vmax) page_scroll = vmax;
    if (page_scroll < 0) page_scroll = 0;
    render_html(px + 6, py + 8, pw - 20, ph - 16);

    /* keyboard-focused link */
    if (link_focus >= 0 && link_focus < nlinks) {
        Link *l = &links[link_focus];
        if (l->y >= py + 6 && l->y + 10 <= py + ph - 6) {
            frame(l->x - 2, l->y - 2, l->w + 4, 13, RGB(32, 110, 210));
            rect(l->x - 4, l->y + 1, 2, 6, RGB(255, 190, 70));
        }
    }

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

static void draw_notes_text(int x, int y, int maxcol) {
    int i, col = 0, line = 0;
    for (i = 0; i < notes_len; i++) {
        char ch = notes[i];
        if (ch == '\n' || col >= maxcol) {
            col = 0;
            line++;
            if (line >= 6) { line = 5; col = maxcol; break; }
            if (ch == '\n') continue;
        }
        draw_char(x + col * 6, y + line * 12, ch, RGB(225, 255, 230));
        col++;
    }
    rect(x + col * 6, y + line * 12, 4, 8, RGB(225, 255, 230));
}

/* --------------------------- tiny shell / pip --------------------------- */
static char *term_line(int i) { return term_lines[(term_head + i) % TERM_MAX]; }

static void term_print(const char *s) {
    char *dst;
    int i = 0;
    if (term_count >= TERM_MAX) {
        term_head = (term_head + 1) % TERM_MAX;
        term_count--;
    }
    dst = term_line(term_count);
    while (s[i] && i < 79) { dst[i] = s[i]; i++; }
    dst[i] = 0;
    term_count++;
}

static void term_catn(char *dst, const char *a, const char *b, int sz) {
    int i = 0, j;
    while (a[i] && i < sz - 1) { dst[i] = a[i]; i++; }
    for (j = 0; b[j] && i < sz - 1; j++) dst[i++] = b[j];
    dst[i] = 0;
}

static void term_cat(char *dst, const char *a, const char *b) {
    term_catn(dst, a, b, 196);
}

static void num_str(char *out, u32 v) {
    char tmp[12];
    int n = 0, j = 0;
    if (!v) { out[0] = '0'; out[1] = 0; return; }
    while (v) { tmp[n++] = (char)('0' + (v % 10)); v /= 10; }
    while (n > 0) out[j++] = tmp[--n];
    out[j] = 0;
}

static int term_streq(const char *a, const char *b) {
    int i = 0;
    while (a[i] && b[i] && a[i] == b[i]) i++;
    return a[i] == 0 && b[i] == 0;
}

/* Ask nethelper for data using the same COM1 framing as pages. */
static int term_request_ms(const char *req, char *out, int outsz, u32 ms) {
    u8 c;
    u32 i, n = 0;
    int keep;
    serial_puts("GET ");
    serial_puts(req);
    serial_put('\n');
    for (i = 0; i < 4; i++) {
        if (!serial_get_ms(&c, ms)) return 0;
        n |= (u32)c << (i * 8);
    }
    if (n == 0 || n > PAGE_MAX) return 0;
    keep = n < (u32)(outsz - 1) ? (int)n : outsz - 1;
    for (i = 0; i < n; i++) {
        if (!serial_get_ms(&c, ms)) return 0;
        if (i < (u32)keep) out[i] = (char)c;
    }
    out[keep] = 0;
    return 1;
}

static int term_request(const char *req, char *out, int outsz) {
    return term_request_ms(req, out, outsz, 60000);
}

static int term_starts(const char *s, const char *p) {
    while (*p) { if (*s++ != *p++) return 0; }
    return 1;
}

/* Notes are stored on the host as text with \n escaped as two characters,
   so one COM1 line is enough for a multi-line note. */
static void notes_encode(char *dst, int dstsz) {
    int i, j = 0;
    for (i = 0; i < notes_len && j < dstsz - 3; i++) {
        if (notes[i] == '\n') { dst[j++] = '\\'; dst[j++] = 'n'; }
        else dst[j++] = notes[i];
    }
    dst[j] = 0;
}

static void notes_decode(const char *src) {
    int i;
    notes_len = 0;
    for (i = 0; src[i] && notes_len < 255; i++) {
        if (src[i] == '\\' && src[i + 1] == 'n') {
            notes[notes_len++] = '\n';
            i++;
        } else notes[notes_len++] = src[i];
    }
    notes[notes_len] = 0;
}

static void notes_store(int save) {
    char req[600], reply[120];
    if (save) {
        cstr_copy(req, "NOTESSAVE ", 11);
        notes_encode(req + 10, sizeof(req) - 10);
        if (term_request(req, reply, sizeof(reply)))
            cstr_copy(notes_status, "SAVED TO DISK", sizeof(notes_status));
        else
            cstr_copy(notes_status, "OFFLINE", sizeof(notes_status));
    } else {
        if (term_request("NOTESLOAD", reply, sizeof(reply))) {
            notes_decode(reply);
            cstr_copy(notes_status, "LOADED FROM DISK", sizeof(notes_status));
        } else {
            cstr_copy(notes_status, "OFFLINE", sizeof(notes_status));
        }
    }
    scene_dirty = 1;
}

static void term_show_reply(char *reply) {
    char *p = reply;
    int shown = 0;
    while (*p && shown < 24) {
        char *e = p;
        while (*e && *e != '\n') e++;
        char save = *e;
        *e = 0;
        if (*p) term_print(p);
        shown++;
        if (!save) break;
        p = e + 1;
    }
}

static void term_save_history(const char *line) {
    if (!line[0]) return;
    cstr_copy(term_hist[term_hist_count % TERM_HIST], line, TERM_CMD);
    term_hist_count++;
    if (term_hist_count > TERM_HIST) term_hist_count = TERM_HIST;
    term_hist_pos = term_hist_count;
}

/* Build a path shown in the shell ("/" is tinyos_fs on the host). */
static void term_path(char *dst, const char *arg) {
    if (arg[0] == '/') { cstr_copy(dst, arg, 100); return; }
    if (term_streq(arg, ".") || !arg[0]) { cstr_copy(dst, term_cwd, 100); return; }
    if (term_streq(arg, "..")) {
        int n = cstrlen(term_cwd);
        while (n > 1 && term_cwd[n - 1] == '/') n--;
        while (n > 1 && term_cwd[n - 1] != '/') n--;
        if (n > 1) n--;
        cstr_copy(dst, term_cwd, n + 1);
        dst[n] = 0;
        return;
    }
    cstr_copy(dst, term_cwd, 100);
    if (cstrlen(dst) && dst[cstrlen(dst) - 1] != '/') term_catn(dst, dst, "/", 100);
    term_catn(dst, dst, arg, 100);
}

static void term_working(const char *msg) {
    term_print(msg);
    scene_dirty = 1;
    compose_full();
}

static void term_host(const char *req, int show) {
    char buf[4096];
    u32 wait = 60000;
    if (term_starts(req, "APTINSTALL") || term_starts(req, "APTUPDATE") ||
        term_starts(req, "APTUPGRADE") || term_starts(req, "PIPINSTALL") ||
        term_starts(req, "GITCLONE"))
        wait = 180000;
    if (term_request_ms(req, buf, sizeof(buf), wait)) {
        if (show) term_show_reply(buf);
        scene_dirty = 1;
    } else {
        term_print("OFFLINE: start QEMU with ./run.sh");
        scene_dirty = 1;
    }
}

static void term_exec(void) {
    char cmd[TERM_CMD], buf[4096], path[100], *argv[6];
    int argc = 0, i;
    char *p;
    cstr_copy(cmd, term_cmd, sizeof(cmd));
    term_scroll = 0;
    term_cat(buf, "> ", cmd);
    term_print(buf);
    term_save_history(cmd);

    p = cmd;
    while (*p && argc < 6) {
        while (*p == ' ') p++;
        if (!*p) break;
        argv[argc++] = p;
        while (*p && *p != ' ') p++;
        if (*p) *p++ = 0;
    }
    if (argc == 0) { term_cmd[0] = 0; term_pos = 0; return; }

    if (term_streq(argv[0], "help")) {
        term_print("FILES: pwd  ls [DIR]  cd DIR  mkdir DIR  cat FILE");
        term_print("SYSTEM PACKAGES: apt update | apt search TEXT");
        term_print("  apt install NAME | apt remove NAME | apt list");
        term_print("  apt show NAME | apt files NAME | apt clean");
        term_print("PYTHON: pip install NAME | pip show NAME | pip list");
        term_print("GIT: git clone URL | git status | git log");
        term_print("SHELL: clear ver date echo uptime uname which CMD beep");
        term_print("APPS: calc  panic  (Up/Down history, PgUp/PgDn scroll)");
    } else if (term_streq(argv[0], "clear")) {
        term_head = 0; term_count = 0;
    } else if (term_streq(argv[0], "ver")) {
        term_print("TINY OS gen0.4  /  TINY SHELL 1.2");
    } else if (term_streq(argv[0], "date")) {
        buf[0] = (char)('0' + dd_day / 10);
        buf[1] = (char)('0' + dd_day % 10);
        buf[2] = '.';
        buf[3] = (char)('0' + dd_month / 10);
        buf[4] = (char)('0' + dd_month % 10);
        buf[5] = '.';
        {
            const int divs[4] = { 1000, 100, 10, 1 };
            for (i = 0; i < 4; i++) buf[6 + i] = (char)('0' + (dd_year / divs[i]) % 10);
        }
        buf[10] = 0;
        term_print(buf);
    } else if (term_streq(argv[0], "echo")) {
        if (argc > 1) term_print(argv[1]); else term_print("");
    } else if (term_streq(argv[0], "uptime")) {
        cstr_copy(buf, "UPTIME: ", sizeof(buf));
        num_str(buf + 8, uptime_sec);
        term_catn(buf, buf, " SEC", sizeof(buf));
        term_print(buf);
    } else if (term_streq(argv[0], "uname")) {
        term_print("TINY OS gen0.4 i386 / 32-BIT KERNEL / IDT");
    } else if (term_streq(argv[0], "history")) {
        for (i = 0; i < term_hist_count; i++) {
            char nb[8];
            nb[0] = (char)('0' + (i + 1) / 10);
            nb[1] = (char)('0' + (i + 1) % 10);
            nb[2] = ' '; nb[3] = ' '; nb[4] = 0;
            term_catn(buf, nb, term_hist[i], sizeof(buf));
            term_print(buf);
        }
    } else if (term_streq(argv[0], "beep")) {
        beep(880, 120);
        beep(1320, 160);
        term_print("BEEP");
    } else if (term_streq(argv[0], "calc")) {
        activate(5);
        term_print("Calculator opened.");
    } else if (term_streq(argv[0], "panic")) {
        term_print("PANIC: triggering int 3 ...");
        scene_dirty = 1;
        compose_full();
        __asm__ volatile ("int $3");
    } else if (term_streq(argv[0], "pwd")) {
        term_print(term_cwd);
    } else if (term_streq(argv[0], "ls")) {
        term_cat(buf, "FSLIST ", argc > 1 ? argv[1] : term_cwd);
        term_host(buf, 1);
    } else if (term_streq(argv[0], "cd")) {
        if (argc < 2) { term_print("usage: cd DIR"); }
        else {
            term_path(path, argv[1]);
            term_cat(buf, "FSEXIST ", path);
            if (term_request(buf, buf, sizeof(buf))) {
                if (buf[0] == 'Y') { cstr_copy(term_cwd, path, sizeof(term_cwd)); scene_dirty = 1; }
                else { term_cat(path, "No such directory: ", argv[1]); term_print(path); }
            } else term_print("OFFLINE: start QEMU with ./run.sh");
        }
    } else if (term_streq(argv[0], "mkdir")) {
        if (argc < 2) term_print("usage: mkdir DIR");
        else { term_path(path, argv[1]); term_cat(buf, "FSMKDIR ", path); term_host(buf, 1); }
    } else if (term_streq(argv[0], "cat")) {
        if (argc < 2) term_print("usage: cat FILE");
        else { term_path(path, argv[1]); term_cat(buf, "FSCAT ", path); term_host(buf, 1); }
    } else if (term_streq(argv[0], "which")) {
        if (argc < 2) term_print("usage: which CMD");
        else { term_cat(buf, "WHICH ", argv[1]); term_host(buf, 1); }
    } else if (term_streq(argv[0], "git")) {
        if (argc >= 3 && term_streq(argv[1], "clone")) {
            term_cat(buf, "GITCLONE ", argv[2]);
            term_working("Working...");
            term_host(buf, 1);
        } else if (argc >= 2 && term_streq(argv[1], "status")) {
            term_cat(buf, "GITSTATUS ", term_cwd);
            term_host(buf, 1);
        } else if (argc >= 2 && term_streq(argv[1], "log")) {
            term_cat(buf, "GITLOG ", term_cwd);
            term_host(buf, 1);
        } else term_print("usage: git clone URL | git status | git log");
    } else if (term_streq(argv[0], "apt") || term_streq(argv[0], "pkg") ||
               term_streq(argv[0], "dnf")) {
        if (argc >= 2 && term_streq(argv[1], "update")) {
            term_working("Refreshing repositories...");
            term_host("APTUPDATE", 1);
        } else if (argc >= 3 && (term_streq(argv[1], "install") ||
                   term_streq(argv[1], "add"))) {
            cstr_copy(buf, "APTINSTALL", sizeof(buf));
            for (i = 2; i < argc; i++) {
                term_catn(buf, buf, " ", sizeof(buf));
                term_catn(buf, buf, argv[i], sizeof(buf));
            }
            term_working("Working: dnf download + unpack...");
            term_host(buf, 1);
        } else if (argc >= 3 && (term_streq(argv[1], "remove") ||
                   term_streq(argv[1], "uninstall") || term_streq(argv[1], "delete"))) {
            cstr_copy(buf, "APTREMOVE", sizeof(buf));
            for (i = 2; i < argc; i++) {
                term_catn(buf, buf, " ", sizeof(buf));
                term_catn(buf, buf, argv[i], sizeof(buf));
            }
            term_host(buf, 1);
        } else if (argc >= 3 && (term_streq(argv[1], "show") ||
                   term_streq(argv[1], "info"))) {
            term_cat(buf, "APTINFO ", argv[2]);
            term_host(buf, 1);
        } else if (argc >= 3 && (term_streq(argv[1], "search") ||
                   term_streq(argv[1], "find"))) {
            cstr_copy(buf, "APTSEARCH", sizeof(buf));
            for (i = 2; i < argc; i++) {
                term_catn(buf, buf, " ", sizeof(buf));
                term_catn(buf, buf, argv[i], sizeof(buf));
            }
            term_host(buf, 1);
        } else if (argc >= 2 && term_streq(argv[1], "list")) {
            term_host("APTLIST", 1);
        } else if (argc >= 3 && term_streq(argv[1], "files")) {
            term_cat(buf, "APTFILES ", argv[2]);
            term_host(buf, 1);
        } else if (argc >= 2 && (term_streq(argv[1], "upgrade") ||
                   term_streq(argv[1], "update-all"))) {
            cstr_copy(buf, "APTUPGRADE", sizeof(buf));
            for (i = 2; i < argc; i++) {
                term_catn(buf, buf, " ", sizeof(buf));
                term_catn(buf, buf, argv[i], sizeof(buf));
            }
            term_working("Checking for upgrades...");
            term_host(buf, 1);
        } else if (argc >= 2 && term_streq(argv[1], "clean")) {
            term_host("APTCLEAN", 1);
        } else {
            term_print("usage: apt update | search TEXT | install NAME");
            term_print("apt remove NAME | list | show NAME | files NAME");
        }
    } else if (term_streq(argv[0], "pip")) {
        if (argc >= 3 && (term_streq(argv[1], "install") || term_streq(argv[1], "add"))) {
            term_cat(buf, "PIPINSTALL ", argv[2]);
            term_working("Working...");
            term_host(buf, 1);
        } else if (argc >= 3 && (term_streq(argv[1], "uninstall") ||
                   term_streq(argv[1], "remove") || term_streq(argv[1], "delete"))) {
            term_cat(buf, "PIPUNINSTALL ", argv[2]);
            term_host(buf, 1);
        } else if (argc >= 3 && (term_streq(argv[1], "show") || term_streq(argv[1], "info"))) {
            term_cat(buf, "PIPSHOW ", argv[2]);
            term_host(buf, 1);
        } else if (argc >= 2 && term_streq(argv[1], "list")) {
            term_host("PKGLIST", 1);
        } else {
            term_print("usage: pip install PKG | pip show PKG | pip uninstall PKG | pip list");
        }
    } else {
        /* Run a program installed by apt in the host sandbox. */
        cstr_copy(buf, "EXECCD ", sizeof(buf));
        term_catn(buf, buf, term_cwd, sizeof(buf));
        for (i = 0; i < argc; i++) {
            term_catn(buf, buf, " ", sizeof(buf));
            term_catn(buf, buf, argv[i], sizeof(buf));
        }
        term_host(buf, 1);
    }
    term_cmd[0] = 0;
    term_pos = 0;
    scene_dirty = 1;
}

static void draw_terminal(Win *w) {
    int x = w->x + 12, y = w->y + 52;
    int ph = w->h - 76;
    int rows = (ph - 30) / 12;
    int i;
    if (rows > TERM_MAX) rows = TERM_MAX;
    rect(x, y, w->w - 24, ph, RGB(5, 9, 15));
    frame(x, y, w->w - 24, ph, RGB(55, 95, 80));
    {
        int base = term_count - rows - term_scroll;
        if (base < 0) base = 0;
        for (i = 0; i < rows; i++) {
            int idx = base + i;
            if (idx >= term_count) break;
            draw_text(x + 5, y + 4 + i * 12, term_line(idx),
                      idx == term_count - 1 ? RGB(215, 245, 228) : RGB(125, 205, 165));
        }
        if (term_count > rows) {
            int maxs = term_count - rows, bar_x = x + w->w - 30;
            int th = ph * rows / term_count, ty;
            if (th < 16) th = 16;
            rect(bar_x, y + 4, 3, ph - 8, RGB(16, 38, 28));
            ty = y + 4 + (ph - 8 - th) * term_scroll / (maxs > 0 ? maxs : 1);
            rect(bar_x, ty, 3, th - 2, RGB(120, 215, 165));
        }
    }
    i = y + ph - 16;
    rect(x + 4, i, w->w - 32, 12, RGB(12, 24, 30));
    draw_text(x + 6, i, "> ", RGB(120, 240, 170));
    draw_text(x + 20, i, term_cmd, RGB(225, 245, 235));
    if (w->active) rect(x + 20 + term_pos * 6, i, 4, 10, RGB(255, 255, 255));
}

static void calc_text(char *out, int v) {
    char tmp[12];
    int n = 0, j = 0, neg = 0;
    unsigned uv;
    if (v < 0) { neg = 1; uv = (unsigned)(-(v + 1)) + 1u; } else uv = (unsigned)v;
    do { tmp[n++] = (char)('0' + (int)(uv % 10)); uv /= 10; } while (uv);
    if (neg) out[j++] = '-';
    while (n > 0) out[j++] = tmp[--n];
    out[j] = 0;
}

static void calc_show(void) { calc_text(calc_disp, calc_val); }

static void calc_reset(void) {
    calc_val = 0; calc_acc = 0; calc_op = 0;
    calc_new = 1; calc_err = 0;
    cstr_copy(calc_disp, "0", sizeof(calc_disp));
}

static void calc_overflow(void) {
    calc_err = 1;
    calc_op = 0;
    cstr_copy(calc_disp, "OVERFLOW", sizeof(calc_disp));
}

static void calc_apply(void) {
    int a = calc_acc, b = calc_val;
    if (!calc_op) return;
    if (calc_op == '+') {
        if ((b > 0 && a > 99999999 - b) || (b < 0 && a < -99999999 - b)) { calc_overflow(); return; }
        calc_acc = a + b;
    } else if (calc_op == '-') {
        if ((b < 0 && a > 99999999 + b) || (b > 0 && a < -99999999 + b)) { calc_overflow(); return; }
        calc_acc = a - b;
    } else if (calc_op == '*') {
        if (a != 0 && (b > 99999999 / a || b < -99999999 / a)) { calc_overflow(); return; }
        calc_acc = a * b;
    } else if (calc_op == '/') {
        if (b == 0) {
            calc_err = 1; calc_op = 0;
            cstr_copy(calc_disp, "DIV BY 0", sizeof(calc_disp));
            return;
        }
        calc_acc = a / b;
    }
    calc_val = calc_acc;
    calc_show();
}

static void calc_key(char k) {
    if (calc_err && k != 'C') calc_reset();
    if (k >= '0' && k <= '9') {
        int d = k - '0';
        if (calc_new) { calc_val = d; calc_new = 0; }
        else if (calc_val > 9999999 || calc_val < -9999999) { calc_overflow(); return; }
        else calc_val = calc_val * 10 + (calc_val < 0 ? -d : d);
        calc_show();
    } else if (k == '+' || k == '-' || k == '*' || k == '/') {
        if (calc_op && !calc_new) calc_apply();
        if (calc_err) return;
        calc_acc = calc_val;
        calc_op = k;
        calc_new = 1;
    } else if (k == '=') {
        if (calc_op && !calc_new) {
            calc_apply();
            calc_op = 0;
            calc_new = 1;
        }
    } else if (k == 'C') {
        calc_reset();
    } else if (k == 'B') {
        if (calc_err) calc_reset();
        else if (!calc_new) {
            calc_val /= 10;
            calc_show();
        }
    }
    scene_dirty = 1;
}

static void calc_key_rect(int k, int *x, int *y, int *w, int *h) {
    Win *c = &wins[5];
    int gx = c->x + 16, gy = c->y + 118;
    if (k == 16) { *x = gx; *y = gy + 4 * 36; *w = 268; *h = 32; return; }
    *x = gx + (k % 4) * 67;
    *y = gy + (k / 4) * 36;
    *w = 63;
    *h = 32;
}

static void draw_calculator(Win *w) {
    int x = w->x + 16, y = w->y + 52, k;
    rect(x, y, w->w - 32, 56, RGB(6, 12, 20));
    frame(x, y, w->w - 32, 56, RGB(90, 120, 160));
    draw_text(x + 8, y + 6, calc_err ? "ERROR" : (calc_op ? "OPERATION" : "READY"),
              calc_err ? RGB(255, 140, 120) : RGB(130, 190, 230));
    draw_text2(x + w->w - 40 - cstrlen(calc_disp) * 12, y + 24, calc_disp, RGB(255, 225, 150));

    for (k = 0; k < 17; k++) {
        int kx, ky, kw, kh;
        u32 base = RGB(42, 48, 60);
        char label = calc_keys[k];
        calc_key_rect(k, &kx, &ky, &kw, &kh);
        if (label == '=' || label == 'C') base = RGB(120, 70, 45);
        else if (label == '+' || label == '-' || label == '*' || label == '/') base = RGB(52, 70, 92);
        bevel(kx, ky, kw, kh, base, 0);
        if (label == 'B') draw_text(kx + kw / 2 - 3, ky + 11, "<", RGB(235, 240, 250));
        else draw_char(kx + kw / 2 - 3, ky + 11, label, RGB(235, 240, 250));
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
        rect(x, y + 60, w->w - 32, 2, RGB(60, 110, 175));
        draw_text(x, y + 76, "DRAG TITLE BAR: MOVE", RGB(165, 190, 225));
        draw_text(x, y + 92, "DRAG CORNER: RESIZE", RGB(165, 190, 225));
        draw_text(x, y + 108,"ALT+TAB: SWITCH  CTRL+W: CLOSE", RGB(165, 190, 225));
        draw_text(x, y + 124,"TAB: LINK  ENTER: OPEN", RGB(165, 190, 225));
        draw_text(x, y + 140,"F1-F4 WINDOWS  F5 BROWSER", RGB(165, 190, 225));
        draw_text(x, y + 156,"OS OR F8: START MENU", RGB(165, 190, 225));
        draw_text(x, y + 172,"F9 TERMINAL   F10 CALCULATOR", RGB(165, 190, 225));
    } else if (w == &wins[1]) {
        draw_text(x, y,      "SYSTEM INFORMATION", RGB(150, 205, 255));
        rect(x, y + 18, w->w - 32, 2, RGB(70, 80, 100));
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
        draw_text(x, y + 184, "DATE: ", RGB(140, 170, 210));
        draw_num2(x + 36, y + 184, dd_day, RGB(255, 215, 120));
        draw_char(x + 48, y + 184, '.', RGB(255, 215, 120));
        draw_num2(x + 54, y + 184, dd_month, RGB(255, 215, 120));
        draw_char(x + 66, y + 184, '.', RGB(255, 215, 120));
        draw_num(x + 72, y + 184, dd_year, RGB(255, 215, 120));
        draw_text(x, y + 200, "LINKS ON PAGE: ", RGB(140, 170, 210));
        draw_num(x + 84, y + 200, nlinks, RGB(255, 215, 120));
        draw_text(x + 120, y + 200, "PAGE LINES: ", RGB(140, 170, 210));
        draw_num(x + 192, y + 200, page_lines, RGB(255, 215, 120));
        draw_text(x, y + 216, "CPU: ", RGB(140, 170, 210));
        draw_text(x + 30, y + 216, cpu_vendor, RGB(255, 215, 120));
        draw_text(x, y + 232, "IDT: 32 EXCEPTION VECTORS", RGB(210, 215, 225));
        draw_text(x, y + 248, "UPTIME: ", RGB(140, 170, 210));
        draw_num(x + 48, y + 248, (int)uptime_sec, RGB(255, 215, 120));
        draw_text(x + 78, y + 248, " SEC", RGB(140, 170, 210));
    } else if (w == &wins[4]) {
        draw_terminal(w);
    } else if (w == &wins[5]) {
        draw_calculator(w);
    } else {
        draw_text(x, y, "TYPE WITH THE KEYBOARD:", RGB(150, 230, 180));
        rect(x, y + 18, w->w - 32, 2, RGB(60, 120, 90));
        draw_notes_text(x, y + 30, (w->w - 40) / 6);
        draw_text(x, y + 116, "ENTER: NEW LINE  BACKSPACE: DEL", RGB(140, 180, 155));
        draw_text(x, y + 132, "F4: THEME", RGB(140, 180, 155));
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
    } else if (w == &wins[4]) {
        draw_text(w->x + 10, sy, "TINY SHELL - APT / PIP", RGB(120, 200, 165));
        draw_text(w->x + w->w - 160, sy, "F9: TERMINAL", RGB(160, 190, 225));
        draw_text(w->x + w->w - 70, sy, "READY", RGB(120, 200, 150));
    } else if (w == &wins[2]) {
        draw_text(w->x + 10, sy, notes_status, RGB(120, 200, 150));
        draw_text(w->x + w->w - 178, sy, "CTRL+S / CTRL+O", RGB(160, 190, 225));
        draw_text(w->x + w->w - 70, sy, "READY", RGB(120, 200, 150));
    } else {
        draw_text(w->x + 10, sy, "TINY OS gen0.4", RGB(120, 150, 185));
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
    draw_app_icon(w->x + 8, w->y + 7, idx);
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
    if (!w->maximized) {
        int k;
        u32 gc = w->active ? RGB(205, 220, 238) : RGB(125, 138, 160);
        for (k = 0; k < 4; k++) {
            rect(w->x + w->w - 5 - k * 3, w->y + w->h - 5, 2, 2, gc);
            rect(w->x + w->w - 5, w->y + w->h - 5 - k * 3, 2, 2, gc);
        }
    }
    window_content(w);
}

/* ------------------------------- drawing -------------------------------- */
static void draw_all(void) {
    Win *order[WIN_N];
    int i, x;
    u32 top = wall_top[wall];
    u32 sh = screen_h - 40;

    for (i = 0; i < (int)sh; i++) {
        int k = (int)((sh - (u32)i) * 100 / sh);
        rect(0, i, (int)screen_w, 1,
             RGB((((top >> 16) & 255) * k) / 100 + 4,
                 (((top >> 8) & 255)  * k) / 100 + 6,
                 ((top & 255)         * k) / 100 + 12));
    }
    /* night sky: stars, a glowing sun, then two mountain layers */
    for (i = 0; i < 90; i++) {
        int sx = (i * 137 + 53) % (int)screen_w;
        int sy = (i * 89 + 17) % ((int)sh * 2 / 3);
        u32 c = (((i + (int)(ticks / 16)) & 3) == 0)
              ? RGB(210, 225, 255) : RGB(120, 145, 190);
        putp(sx, sy, c);
    }
    fill_circle(838, 116, 34, RGB(58, 68, 92));
    fill_circle(838, 116, 27, RGB(122, 116, 106));
    fill_circle(838, 116, 21, RGB(255, 208, 122));
    for (x = 0; x < (int)screen_w; x++) {
        int far  = (int)sh * 72 / 100 - wave(x,      170, 96) - wave(x + 40, 260, 40);
        int near = (int)sh * 84 / 100 - wave(x + 90, 220, 64);
        if (far < 0) far = 0;
        if (near < 0) near = 0;
        rect(x, far, 1, (int)sh - far, lerp(wall_top[wall], RGB(8, 12, 28), 45));
        rect(x, near, 1, (int)sh - near, lerp(wall_top[wall], RGB(2, 4, 12), 72));
    }

    draw_text2((int)screen_w / 2 - 82, 46, "TINY OS", RGB(175, 220, 255));
    draw_text((int)screen_w / 2 - 116, 68, "GRAPHICAL OS FOR QEMU  -  gen0.4", RGB(120, 160, 210));
    rect((int)screen_w / 2 - 130, 82, 260, 2, RGB(70, 130, 200));

    rect(0, (int)screen_h - 40, (int)screen_w, 40, RGB(24, 27, 34));
    rect(0, (int)screen_h - 40, (int)screen_w, 2, accent[wall]);
    draw_hover_item(500, 0);

    for (i = 0; i < WIN_N; i++)
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
    for (i = 0; i < WIN_N; i++)
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
            if (i < WIN_N) draw_app_icon(x + 2, y + 2, i);
            else if (i == WIN_N) rect(x + 4, y + 5, 10, 10, accent[wall]);
            draw_text(x + 17, y + 6, menu_names[i], RGB(205, 215, 232));
        }
        rect(MENU_X + 6, my0 + MENU_H - 20, MENU_W - 12, 1, RGB(70, 82, 104));
        draw_text(MENU_X + 8, my0 + MENU_H - 16, "TINY OS gen0.4", RGB(130, 160, 200));
        draw_text(MENU_X + MENU_W - 88, my0 + MENU_H - 16, theme_name[wall], RGB(180, 205, 235));
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
    for (k = 0; k < WIN_N; k++) wins[k].active = 0;
    wins[i].active = 1;
    wins[i].open = 1;
    wins[i].minimized = 0;
    wins[i].z = ++top_z;
}

static void cycle_window(void) {
    int i, start = -1;
    for (i = 0; i < WIN_N; i++)
        if (wins[i].open && !wins[i].minimized && wins[i].active) start = i;
    if (start < 0) {
        for (i = 0; i < WIN_N; i++)
            if (wins[i].open && !wins[i].minimized) { activate(i); return; }
        return;
    }
    for (i = 1; i <= WIN_N; i++) {
        int k = (start + i) % WIN_N;
        if (wins[k].open && !wins[k].minimized) { activate(k); return; }
    }
}

/* After closing or minimizing the active window, focus the top one left. */
static void activate_topmost(void) {
    Win *order[WIN_N];
    int i;
    sort_windows(order);
    for (i = WIN_N - 1; i >= 0; i--)
        if (order[i]->open && !order[i]->minimized) {
            activate((int)(order[i] - wins));
            return;
        }
}

/* Scroll the focused link into the visible part of the page. */
static void focus_link(int idx) {
    int py, ph, vlines, maxs;
    if (idx < 0 || idx >= nlinks) return;
    py = wins[3].y + 58 + 8;
    ph = wins[3].h - 90 - 16;
    vlines = ph / 10;
    maxs = page_lines - vlines;
    if (maxs < 0) maxs = 0;
    if (links[idx].y < py)
        page_scroll -= (py - links[idx].y + 9) / 10;
    else if (links[idx].y + 10 > py + ph)
        page_scroll += (links[idx].y + 10 - (py + ph) + 9) / 10;
    if (page_scroll < 0) page_scroll = 0;
    if (page_scroll > maxs) page_scroll = maxs;
}

/* Topmost visible window under a point, or -1. */
static int window_at(int x, int y) {
    Win *order[WIN_N];
    int i;
    sort_windows(order);
    for (i = WIN_N - 1; i >= 0; i--) {
        Win *w = order[i];
        if (!w->open || w->minimized) continue;
        if (x >= w->x && x < w->x + w->w && y >= w->y && y < w->y + w->h)
            return (int)(w - wins);
    }
    return -1;
}

static int compute_hover(void) {
    Win *order[WIN_N];
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
        my >= (int)screen_h - 33 && my < (int)screen_h - 7) return 500;
    if (wins[5].open && !wins[5].minimized && window_at(mx, my) == 5) {
        int k, kx, ky, kw, kh;
        for (k = 0; k < 17; k++) {
            calc_key_rect(k, &kx, &ky, &kw, &kh);
            if (mx >= kx && mx < kx + kw && my >= ky && my < ky + kh) return 400 + k;
        }
    }
    if (wins[3].open && !wins[3].minimized && window_at(mx, my) == 3) {
        for (i = 0; i < nlinks; i++)
            if (mx >= links[i].x - 2 && mx < links[i].x + links[i].w + 2 &&
                my >= links[i].y - 2 && my < links[i].y + 11) return 300 + i;
    }
    sort_windows(order);
    for (i = WIN_N - 1; i >= 0; i--) {
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
        for (i = 0; i < WIN_N; i++, bx += 124)
            if (mx >= bx && mx < bx + 116) return 100 + i;
    }
    return -1;
}

static void mouse_packet(u8 *p) {
    int dx = (int)(signed char)p[1];
    int dy = (int)(signed char)p[2];
    int nb = p[0] & 1;
    int h;
    Win *order[WIN_N];
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
                        if (i < WIN_N) activate(i);
                        else if (i == WIN_N) { wall = (wall + 1) % 5; cstr_copy(status_msg, theme_name[wall], sizeof(status_msg)); }
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

        /* bottom-right grip: start resizing the topmost window */
        {
            int under = window_at(mx, my);
            if (under >= 0 && !wins[under].maximized) {
                Win *w = &wins[under];
                if (mx >= w->x + w->w - 14 && mx < w->x + w->w &&
                    my >= w->y + w->h - 14 && my < w->y + w->h) {
                    activate(under);
                    resizing = under;
                    resize_ox = mx - (w->x + w->w);
                    resize_oy = my - (w->y + w->h);
                    scene_dirty = 1;
                    goto after_click;
                }
            }
        }

        sort_windows(order);
        for (i = WIN_N - 1; i >= 0; i--) {
            Win *w = order[i];
            int idx = (int)(w - wins);
            int cx = w->x + w->w - 24;
            int ax = w->x + w->w - 46;
            int nx = w->x + w->w - 68;
            int cy = w->y + 4;
            if (!w->open || w->minimized) continue;
            if (mx >= cx && mx < cx + 16 && my >= cy && my < cy + 16) {
                int was_active = w->active;
                w->open = 0; w->active = 0;
                if (was_active) activate_topmost();
                scene_dirty = 1;
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
                int was_active = w->active;
                w->minimized = 1; w->active = 0;
                if (was_active) activate_topmost();
                scene_dirty = 1;
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

        /* calculator keypad */
        if (wins[5].open && !wins[5].minimized && window_at(mx, my) == 5) {
            int k, kx, ky, kw, kh;
            for (k = 0; k < 17; k++) {
                calc_key_rect(k, &kx, &ky, &kw, &kh);
                if (mx >= kx && mx < kx + kw && my >= ky && my < ky + kh) {
                    activate(5);
                    calc_key(calc_keys[k]);
                    scene_dirty = 1;
                    goto after_click;
                }
            }
        }

        /* browser: BACK / address / GO / HOME / links */
        if (wins[3].open) {
            Win *b = &wins[3];
            int by = b->y + 30;
            if (window_at(mx, my) == 3) link_focus = -1;
            int fx = b->x + 56;
            int ax = b->x + 104, aw = b->w - 236;
            int gx = b->x + b->w - 86;
            int hx = b->x + b->w - 46;
            int px = b->x + 14, py = b->y + 66;
            int pw = b->w - 28, ph = b->h - 106;
            if (mx >= b->x + 8 && mx < b->x + 52 && my >= by && my < by + 20) {
                go_back(); activate(3); scene_dirty = 1;
                goto after_click;
            }
            if (mx >= fx && mx < fx + 44 && my >= by && my < by + 20) {
                go_forward(); activate(3); scene_dirty = 1;
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
                navigate(url_input); activate(3); scene_dirty = 1;
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

        for (i = WIN_N - 1; i >= 0; i--) {
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
            for (i = 0; i < WIN_N; i++, bx += 124)
                if (mx >= bx && mx < bx + 116) {
                    if (wins[i].open && wins[i].active && !wins[i].minimized) {
                        wins[i].minimized = 1;
                        wins[i].active = 0;
                        activate_topmost();
                    } else {
                        activate(i);
                    }
                    scene_dirty = 1;
                    break;
                }
        }
    }
after_click:
    if (!nb && mb) { dragging = -1; resizing = -1; }

    if (resizing >= 0) {
        Win *w = &wins[resizing];
        int nw = mx - resize_ox - w->x;
        int nh = my - resize_oy - w->y;
        int minw, minh;
        if (w->kind == 1) { minw = 460; minh = 320; }
        else if (w == &wins[0]) { minw = 360; minh = 240; }
        else if (w == &wins[1]) { minw = 360; minh = 290; }
        else { minw = 300; minh = 230; }
        if (nw < minw) nw = minw;
        if (nh < minh) nh = minh;
        if (w->x + nw > (int)screen_w) nw = (int)screen_w - w->x;
        if (w->y + nh > (int)screen_h - 40) nh = (int)screen_h - 40 - w->y;
        if (nw >= 120 && nh >= 100) { w->w = nw; w->h = nh; }
        scene_dirty = 1;
    }

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
    [0x0C]='-', [0x0D]='=', [0x27]=':', [0x33]=',', [0x34]='.',
    [0x35]='/', [0x28]='\'',
    [0x1E]='A', [0x30]='B', [0x2E]='C', [0x20]='D', [0x12]='E',
    [0x21]='F', [0x22]='G', [0x23]='H', [0x17]='I', [0x24]='J',
    [0x25]='K', [0x26]='L', [0x32]='M', [0x31]='N', [0x18]='O',
    [0x19]='P', [0x10]='Q', [0x13]='R', [0x1F]='S', [0x14]='T',
    [0x16]='U', [0x2F]='V', [0x11]='W', [0x2D]='X', [0x15]='Y',
    [0x2C]='Z', [0x39]=' ', [0x0E]=8,
};

/* Shifted punctuation - only characters the 5x7 font actually has. */
static char shifted_char(char c) {
    if (c >= '0' && c <= '9') {
        switch (c) {
            case '1': return '!';
            case '2': return '@';
            case '3': return '#';
            case '5': return '%';
            case '7': return '&';
            case '8': return '*';
            case '9': return '(';
            case '0': return ')';
        }
    }
    if (c == '-') return '_';
    if (c == '=') return '+';
    if (c == '.') return '>';
    if (c == ',') return '<';
    if (c == '/') return '?';
    if (c == '\'') return '"';
    return c;
}

static void key_byte(u8 sc) {
    int a;
    if (sc & 0x80) {
        sc &= 0x7F;
        if (sc == 0x1D) ctrl_down = 0;
        if (sc == 0x2A || sc == 0x36) shift_down = 0;
        if (sc == 0x38) alt_down = 0;
        return;
    }
    sc &= 0x7F;
    if (sc == 0x1D) { ctrl_down = 1; return; }
    if (sc == 0x2A || sc == 0x36) { shift_down = 1; return; }
    if (sc == 0x38) { alt_down = 1; return; }

    if (alt_down && sc == 0x0F) {           /* Alt+Tab: next window */
        cycle_window();
        scene_dirty = 1;
        return;
    }
    if (ctrl_down && sc == 0x11) {          /* Ctrl+W: close active window */
        int k;
        for (k = 0; k < WIN_N; k++)
            if (wins[k].active && wins[k].open) {
                wins[k].open = 0; wins[k].active = 0;
                activate_topmost();
                scene_dirty = 1;
                break;
            }
        return;
    }

    if (ctrl_down && sc == 0x1F) {          /* Ctrl+S: save Notes on the host */
        if (wins[2].active && wins[2].open) notes_store(1);
        return;
    }
    if (ctrl_down && sc == 0x18) {          /* Ctrl+O: load Notes from the host */
        if (wins[2].active && wins[2].open) notes_store(0);
        return;
    }
    if (ctrl_down && sc == 0x26) {          /* Ctrl+L: new address (select all) */
        activate(3);
        url_len = 0;
        url_pos = 0;
        url_input[0] = 0;
        link_focus = -1;
        scene_dirty = 1;
        return;
    }
    if (ctrl_down && sc == 0x4B) {          /* Ctrl+Left: history back */
        go_back(); scene_dirty = 1; return;
    }
    if (ctrl_down && sc == 0x4D) {          /* Ctrl+Right: history forward */
        go_forward(); scene_dirty = 1; return;
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
    if (sc == 0x01) {                      /* ESC: clear link focus / start page */
        if (wins[3].open && wins[3].active) {
            if (link_focus >= 0) {
                link_focus = -1;
                cstr_copy(status_msg, "READY", sizeof(status_msg));
            } else {
                page_home();
            }
            scene_dirty = 1;
        }
        return;
    }
    if (sc == 0x40) {                      /* F6: history back */
        if (wins[3].active && wins[3].open) { go_back(); scene_dirty = 1; }
        return;
    }
    if (sc == 0x41) {                      /* F7: history forward */
        if (wins[3].active && wins[3].open) { go_forward(); scene_dirty = 1; }
        return;
    }
    if (wins[3].active && wins[3].open && sc == 0x0F) {  /* Tab: focus links */
        if (nlinks > 0) {
            if (link_focus < 0)
                link_focus = shift_down ? nlinks - 1 : 0;
            else
                link_focus = (link_focus + (shift_down ? nlinks - 1 : 1)) % nlinks;
            focus_link(link_focus);
            cstr_copy(status_msg, links[link_focus].label, sizeof(status_msg));
        } else {
            link_focus = -1;
            cstr_copy(status_msg, "NO LINKS", sizeof(status_msg));
        }
        scene_dirty = 1;
        return;
    }
    if (sc == 0x3E) { wall = (wall + 1) % 5; scene_dirty = 1; return; }
    if (sc == 0x42) { menu_open = !menu_open; scene_dirty = 1; return; }  /* F8: menu */
    if (sc == 0x43) {                      /* F9: terminal */
        activate(4);
        if (term_count == 0) term_print("TINY SHELL - type 'help' for commands.");
        scene_dirty = 1;
        return;
    }
    if (sc == 0x44) {                      /* F10: calculator */
        activate(5);
        scene_dirty = 1;
        return;
    }
    if (wins[5].active && wins[5].open) {
        char cc = scmap[sc];
        if (cc >= 'A' && cc <= 'Z' && !shift_down) cc += 32;
        if (shift_down && cc) cc = shifted_char(cc);
        if (sc == 0x1C) { calc_key('='); return; }
        if (sc == 0x0E) { calc_key('B'); return; }
        if (sc == 0x01) { calc_key('C'); return; }
        if (cc >= '0' && cc <= '9') { calc_key(cc); return; }
        if (cc == '+' || cc == '-' || cc == '*' || cc == '/' || cc == '=') {
            calc_key(cc);
            return;
        }
        if (cc == 'c' || cc == 'C') { calc_key('C'); return; }
    }
    if (wins[4].active && wins[4].open) {
        int tlen = cstrlen(term_cmd), k;
        if (sc == 0x1C) { term_exec(); return; }
        if (sc == 0x0E) {                  /* Backspace */
            if (term_pos > 0) {
                for (k = term_pos - 1; k < tlen; k++) term_cmd[k] = term_cmd[k + 1];
                term_pos--;
            }
            scene_dirty = 1;
            return;
        }
        if (sc == 0x53) {                  /* Delete */
            if (term_pos < tlen)
                for (k = term_pos; k < tlen; k++) term_cmd[k] = term_cmd[k + 1];
            scene_dirty = 1;
            return;
        }
        if (sc == 0x48 || sc == 0x50) {  /* Up/Down: command history */
            if (term_hist_count > 0) {
                if (sc == 0x48) { if (term_hist_pos > 0) term_hist_pos--; }
                else if (term_hist_pos < term_hist_count) term_hist_pos++;
                if (term_hist_pos >= term_hist_count) term_cmd[0] = 0;
                else cstr_copy(term_cmd, term_hist[term_hist_pos], TERM_CMD);
                term_pos = cstrlen(term_cmd);
                scene_dirty = 1;
            }
            return;
        }
        if (sc == 0x49 || sc == 0x51) {  /* PageUp/PageDown: scroll output */
            int rows = (wins[4].h - 76 - 30) / 12;
            int maxs;
            if (rows > TERM_MAX) rows = TERM_MAX;
            maxs = term_count - rows;
            if (maxs < 0) maxs = 0;
            if (sc == 0x49) { if (term_scroll < maxs) term_scroll++; }
            else { if (term_scroll > 0) term_scroll--; }
            scene_dirty = 1;
            return;
        }
        if (sc == 0x4B) { if (term_pos > 0) term_pos--; scene_dirty = 1; return; }
        if (sc == 0x4D) { if (term_pos < tlen) term_pos++; scene_dirty = 1; return; }
        if (sc == 0x47) { term_pos = 0; scene_dirty = 1; return; }
        if (sc == 0x4F) { term_pos = tlen; scene_dirty = 1; return; }
    }
    if (sc == 0x1C) {
        if (wins[3].active && wins[3].open) {
            if (link_focus >= 0 && link_focus < nlinks) {
                char full[200];
                cstr_copy(full, links[link_focus].url, sizeof(full));
                link_focus = -1;
                navigate(full);
            } else {
                navigate(url_input);
            }
            scene_dirty = 1;
        } else if (wins[2].active && wins[2].open) {
            if (notes_len < 255) {
                notes[notes_len++] = '\n';
                notes[notes_len] = 0;
                scene_dirty = 1;
            }
        }
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
                for (k = url_pos - 1; k < url_len; k++) url_input[k] = url_input[k + 1];
                url_pos--;
                url_len--;
            } else if (sc == 0x53 && url_pos < url_len) {
                for (k = url_pos; k < url_len - 1; k++) url_input[k] = url_input[k + 1];
                url_len--;
            }
            url_input[url_len] = 0;
            scene_dirty = 1;
            return;
        }
    }

    for (a = 0; a < WIN_N; a++) if (wins[a].active && wins[a].open) {
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
        if (c >= 'A' && c <= 'Z') {
            if (!shift_down) c += 32;
        } else if (shift_down) {
            c = shifted_char(c);
        }
        if (wins[3].active && wins[3].open) {
            if (c && url_len < 190) {
                int k;
                link_focus = -1;
                for (k = url_len; k > url_pos; k--) url_input[k] = url_input[k - 1];
                url_input[url_pos++] = c;
                url_len++;
                url_input[url_len] = 0;
            }
        } else if (wins[2].active && wins[2].open) {
            if (c == 8) { if (notes_len > 0) notes[--notes_len] = 0; }
            else if (c >= 32 && notes_len < 255) {
                notes[notes_len++] = c;
                notes[notes_len] = 0;
            }
        } else if (wins[4].active && wins[4].open) {
            int len = cstrlen(term_cmd);
            if (c >= 32 && len < TERM_CMD - 1) {
                for (a = len; a > term_pos; a--) term_cmd[a] = term_cmd[a - 1];
                term_cmd[term_pos++] = c;
                term_cmd[len + 1] = 0;
            }
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

static void cpu_detect(void) {
    u32 a, b, c, d, regs[3];
    int r, k, i = 0;
    __asm__ volatile ("cpuid" : "=a"(a), "=b"(b), "=c"(c), "=d"(d) : "a"(0));
    regs[0] = b; regs[1] = d; regs[2] = c;
    for (r = 0; r < 3; r++)
        for (k = 0; k < 4; k++) {
            char ch = (char)((regs[r] >> (k * 8)) & 0xFF);
            if (ch && i < 15) cpu_vendor[i++] = ch;
        }
    cpu_vendor[i] = 0;
}

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

    idt_init();
    serial_init();
    cpu_detect();
    url_len = cstrlen(page_url);
    page_home();
    mouse_init();
    read_clock();
    boot_clock = (u32)(hh * 3600 + mm * 60 + ss);
    last_clock = (int)boot_clock;

    for (;;) {
        ticks++;
        poll_input();
        read_clock();
        if (hh * 3600 + mm * 60 + ss != last_clock) {
            u32 now = (u32)(hh * 3600 + mm * 60 + ss);
            last_clock = (int)now;
            if (now >= boot_clock)
                uptime_sec = now - boot_clock;
            else if (boot_clock - now > 43200u)
                uptime_sec = now + 86400u - boot_clock;
            else
                uptime_sec = 0;  /* clock stepped back slightly at boot */
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
