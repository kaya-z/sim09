/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 YUKARI Semiconductor Devices */
/*
 * tty.c - the console server (TASK 1)
 *
 * Owns the ACIA.  Reads are served from a ring buffer, which the receive interrupt of the ACIA (in the kernel) fills: the server
 * tells the kernel where the ring is (mk_rxreg) and is notified (NFY_RX) when characters have arrived.  The board has a FIFO of
 * 256 characters for the time that the CPU is held (a write to the card); a character that finds the ring full is lost.
 *
 *   cooked mode (default)  a line editor: echo, BS/DEL erase one character (multi-byte
 *                          characters of UTF-8 / EUC-JP / Shift_JIS as a whole, with the right
 *                          number of columns), ^U kills the line, ^D at the start of a line
 *                          is end of file, ^C (with TM_ISIG) tells PM to stop the foreground
 *                          program, cursor key sequences are swallowed.  A read returns one line.
 *   raw mode               no editing, no echo (unless TM_ECHO): a read returns what has arrived.
 *
 * The terminal (or the HDMI screen of the FPGA board) does the display of the characters;
 * the server only needs the character set to edit lines.
 */
#include "types.h"
#include "mk09.h"
#include "proto.h"
#include "bsp.h"
#include "memmap.h"
#include "mkstr.h"

#define INQ         RX_RING         /* input ring (power of two) */
#define LINEMAX     120
#define CHUNK       48

/* The ring: the kernel (the receive interrupt) is the writer of head, this server the writer of tail; mk_rxreg() tells the kernel where it is. */
#ifdef MK_MMU
struct rxring { u8 h, t; u8 b[INQ]; };
#endif
#ifdef MK09_V2
/* 2.0: several lines (terminals): the state of a line is in a table, the macros below name the one that is served now (L).  The ring of the line n is in
 * the data of the kernel (the interrupt fills it in any map): the line 0 at K_DATA + K_DSZ - 0x90, the line 1 below it. */
typedef struct tline {
    struct rxring *rx;
    u8 on;                          /* the line is in use (the line 0; the others when PM says so) */
    u8 mode, charset, cols, rows;
    u8 line[LINEMAX];               /* the line being edited / read */
    u8 llen, lpos;
    u8 lready;                      /* the line is complete (llen may be 0: end of file) */
    u8 esc;                         /* swallowing an escape sequence: 1 after ESC, 2 inside */
    u8 lastcr;
    u8 owner;                       /* the only task that may read (set by PM; 0 = everybody) */
    u8 rd_tid;                      /* client waiting in TTY_READ (0 = none) */
    u16 rd_len;
} tline_t;
static tline_t tl[NLINE];
static tline_t *L = &tl[0];
static u8 lineno;                   /* the number of that line */
static u8 tline_of[NTASK];          /* the line of each task (PM sets it; 0 = the first) */
static void sel(u8 n) { if (n >= NLINE) n = 0; lineno = n; L = &tl[n]; }
static struct rxring *tl_ring(u8 i) { return tl[i].rx; }
static void tl_on(u8 i) { tl[i].on = 1; }
static u8 tl_is_on(u8 i) { return tl[i].on; }
static void tl_init(void)           /* (before the macros that name the members of the line) */
{
    u8 i;
    for (i = 0; i < NLINE; i++) {
        tl[i].rx = (struct rxring *)(K_DATA + K_DSZ - 0x90 - 0x90 * (u16)i);
        tl[i].on = (i == 0);
        tl[i].mode = TM_DEFAULT;
#ifdef BOARD_FM11
        tl[i].charset = CS_SJIS;
#else
        tl[i].charset = CS_UTF8;
#endif
        tl[i].cols = 80;
        tl[i].rows = 24;
    }
}
#define qh  L->rx->h
#define qt  L->rx->t
#define inq L->rx->b
#define mode L->mode
#define charset L->charset
#define cols L->cols
#define rows L->rows
#define line L->line
#define llen L->llen
#define lpos L->lpos
#define lready L->lready
#define esc L->esc
#define lastcr L->lastcr
#define owner L->owner
#define rd_tid L->rd_tid
#define rd_len L->rd_len
#else
#ifdef MK_MMU
#define rxr (*(struct rxring *)(K_DATA + K_DSZ - 0x90))   /* in the data of the kernel (the keyboard interrupt of the kernel fills it, in any map) */
#else
static struct { u8 h, t; u8 b[INQ]; } rxr;
#endif
#define qh  rxr.h
#define qt  rxr.t
#define inq rxr.b
static u8 mode = TM_DEFAULT;
#ifdef BOARD_FM11
static u8 charset = CS_SJIS;        /* the console of the FM-11 takes Shift_JIS */
#else
static u8 charset = CS_UTF8;
#endif
static u8 cols = 80, rows = 24;

static u8 line[LINEMAX];            /* the line being edited / read */
static u8 llen, lpos;
static u8 lready;                   /* the line is complete (llen may be 0: end of file) */
static u8 esc;                      /* swallowing an escape sequence: 1 after ESC, 2 inside */
static u8 lastcr;

static u8 owner;                    /* the only task that may read (set by PM; 0 = everybody) */
static u8 rd_tid;                   /* client waiting in TTY_READ (0 = none) */
static u16 rd_len;
#endif

/* ------------------------------------------------------------------ */
static void out(u8 c)
{
#ifdef BOARD_FM11
#ifdef MK09_V2
    if (lineno) {                       /* the second terminal: the RS-232C port (a line that is not in use is not written) */
        if (!tl_is_on(lineno)) return;
        while (!(FM_RS_STAT & 1))
            ;
        FM_RS_DATA = c;
        return;
    }
#endif
    bsp_con_put(c);                     /* collected, sent to the display sub system by bsp_con_flush() before the server waits */
#else
#ifdef MMU_COCO
    if (lineno) {                       /* the second terminal (a line that is not in use is not written: its UART may not be there) */
        if (!tl_is_on(lineno)) return;
        while (!(ACIA1_STATUS & ACIA_TDRE))
            ;
        ACIA1_DATA = c;
        return;
    }
#endif
    while (!(ACIA_STATUS & ACIA_TDRE))
        ;
    ACIA_DATA = c;
#endif
}

static void outc(u8 c)
{
    if (c == '\n' && (mode & TM_CRLF)) out('\r');
    out(c);
}

static void reply(u8 tid, u8 st, u16 a)
{
    msg_t r;
    r.op = st;
    r.a = a;
    r.b = r.c = r.d = 0;
    mk_reply(tid, &r);
}

static u8 qcount(void)
{
    return (u8)(qh - qt) & (INQ - 1);
}

static u8 deq(void)
{
    u8 c = inq[qt];
    qt = (u8)((qt + 1) & (INQ - 1));
    return c;
}

/* ------------------------------------------------------------------ */
/* characters                                                          */
/* ------------------------------------------------------------------ */
/* bytes of the character that starts with c */
static u8 clen(u8 c)
{
    switch (charset) {
    case CS_UTF8:
        if (c >= 0xF0) return 4;
        if (c >= 0xE0) return 3;
        if (c >= 0xC2) return 2;
        return 1;
    case CS_EUC:
        if (c == 0x8F) return 3;
        if (c == 0x8E || (c >= 0xA1 && c <= 0xFE)) return 2;
        return 1;
    case CS_SJIS:
        if ((c >= 0x81 && c <= 0x9F) || (c >= 0xE0 && c <= 0xFC)) return 2;
        return 1;
    }
    return 1;
}

/* columns of the character of n bytes at p */
static u8 cwidth(const u8 *p, u8 n)
{
    switch (charset) {
    case CS_UTF8:
        if (n == 4) return 2;
        if (n == 3) {
            if (p[0] >= 0xE3 && p[0] <= 0xED) return 2;     /* CJK, kana, hangul */
            if (p[0] == 0xEF) {                              /* the last two bytes: EF BC 00 - EF BD A0 fullwidth forms, up to EF BE 9F halfwidth katakana, EF BF A0 - A6 fullwidth signs */
                u16 c = ((u16)p[1] << 8) | p[2];
                if (c <= 0xBDA0 && c >= 0xBC00) return 2;
                if (c >= 0xBFA0 && c <= 0xBFA6) return 2;
            }
        }
        return 1;
    case CS_EUC:
        if (n == 3) return 2;
        if (n == 2) return p[0] == 0x8E ? 1 : 2;
        return 1;
    case CS_SJIS:
        return n == 2 ? 2 : 1;
    }
    return 1;
}

static void erase_last(void)
{
    u8 i = 0, st = 0, w, n;
    if (!llen) return;
    while (i < llen) {                  /* find where the last character starts */
        st = i;
        n = clen(line[i]);
        if (n > llen - i) n = llen - i;
        i += n;
    }
    w = cwidth(line + st, llen - st);
    llen = st;
    if (mode & TM_ECHO) {
        for (i = 0; i < w; i++) out(8);
        for (i = 0; i < w; i++) out(' ');
        for (i = 0; i < w; i++) out(8);
    }
}

static void edit(u8 c)
{
    if (esc) {                          /* swallow ESC [ ... final / ESC O x */
        if (esc == 1) esc = (c == '[' || c == 'O') ? 2 : 0;
        else if (c >= 0x40 && c <= 0x7E) esc = 0;
        return;
    }
    if (c == '\n' && lastcr) { lastcr = 0; return; }
    lastcr = (c == '\r');
    switch (c) {
    case 0x1B:
        esc = 1;
        return;
    case '\r':
    case '\n':
        if (mode & TM_ECHO) outc('\n');
        line[llen++] = '\n';
        lpos = 0;
        lready = 1;
        return;
    case 0x08:
    case 0x7F:
        erase_last();
        return;
    case 0x15:                          /* ^U */
        while (llen) erase_last();
        return;
    case 0x03:                          /* ^C */
        if (mode & TM_ISIG) {
            if (mode & TM_ECHO) {
                out('^');
                out('C');
                outc('\n');
            }
            llen = lpos = 0;
            if (rd_tid) {                       /* the reader gets an empty line */
                line[0] = '\n';
                llen = 1;
                lready = 1;
            }
#ifdef MK09_V2
            mk_notify(TID_PM, lineno ? NFY_INT1 : NFY_INT);
#else
            mk_notify(TID_PM, NFY_INT);
#endif
            return;
        }
        break;
    case 0x04:                          /* ^D at the start of a line: end of file */
        if (!llen) {
            lpos = 0;
            lready = 1;
        }
        return;
    }
    if (c < 0x20) return;
    if (llen < LINEMAX - 2) {
        line[llen++] = c;
        if (mode & TM_ECHO) out(c);
    }
}

static void reset_line(void)
{
    llen = lpos = lready = esc = lastcr = 0;
}

/* run the line editor on what has arrived */
static void pump(void)
{
    if (mode & TM_RAW) return;
    while (!lready && qcount())
        edit(deq());
}

/* ------------------------------------------------------------------ */
/* requests                                                            */
/* ------------------------------------------------------------------ */
static void try_reader(void)
{
    u16 n, done;
    u8 tmp[CHUNK];
    u8 c, i;

    if (!rd_tid) return;
    pump();
    if (mode & TM_RAW) {
#ifdef MK09_V2
        if (llen > lpos) {              /* 2.0: what was typed before the program asked for raw input (a password) is read first, not lost */
            n = llen - lpos;
            if (n > rd_len) n = rd_len;
            if (mk_copyto(rd_tid, 0, line + lpos, n)) n = 0;
            lpos += (u8)n;
            if (lpos >= llen) reset_line();
            reply(rd_tid, E_OK, n);
            rd_tid = 0;
            return;
        }
#endif
        n = qcount();
        if (!n) {
            if (mode & TM_NOWAIT) { reply(rd_tid, E_OK, 0); rd_tid = 0; }
            return;
        }
        if (n > rd_len) n = rd_len;
        done = 0;
        while (done < n) {
            c = (u8)((n - done) > CHUNK ? (u16)CHUNK : (n - done));
            for (i = 0; i < c; i++) tmp[i] = deq();
            if (mk_copyto(rd_tid, done, tmp, c)) break;
            done += c;
        }
        reply(rd_tid, E_OK, done);
        rd_tid = 0;
        return;
    }
    if (!lready) {
        if (mode & TM_NOWAIT) { reply(rd_tid, E_OK, 0); rd_tid = 0; }
        return;
    }
    n = llen - lpos;
    if (n > rd_len) n = rd_len;
    if (n && mk_copyto(rd_tid, 0, line + lpos, n)) n = 0;
    lpos += (u8)n;
    if (lpos >= llen) reset_line();
    reply(rd_tid, E_OK, n);
    rd_tid = 0;
}

static void do_write(msg_t *m)
{
    u16 n = m->c, off = 0;
    u8 buf[CHUNK];
    u8 c, i, st = E_OK;

    while (off < n) {
        c = (u8)((n - off) > CHUNK ? (u16)CHUNK : (n - off));
        st = mk_copyfrom(m->src, off, buf, c);
        if (st) break;
        for (i = 0; i < c; i++) outc(buf[i]);
        off += c;
    }
    reply(m->src, off ? E_OK : st, off);
}

static void do_ioctl(msg_t *m)
{
    msg_t r;
    u8 oldraw = mode & TM_RAW;

    r.op = E_OK;
    r.a = r.b = r.c = r.d = 0;
    switch (m->a) {
    case TTYC_GETMODE:
        r.a = mode;
        r.b = charset;
        r.c = cols;
        r.d = rows;
        break;
    case TTYC_SETMODE:
        mode = (u8)m->b;
        if (m->c != 0xFFFF && m->c <= CS_SJIS) charset = (u8)m->c;
        if (m->d != 0xFFFF) { cols = (u8)(m->d >> 8); rows = (u8)m->d; }
#ifdef MK09_V2
        if (oldraw && !(mode & TM_RAW)) reset_line();         /* (from cooked to raw the line that is being typed stays) */
#else
        if (oldraw != (mode & TM_RAW)) reset_line();
#endif
        break;
    case TTYC_READY:
        pump();
        r.a = (mode & TM_RAW) ? (u16)qcount() : (lready ? (u16)(llen - lpos) : (u16)0);
        break;
    case TTYC_FLUSH:
        qt = qh;                    /* (the kernel writes the head) */
        reset_line();
        break;
    case TTYC_SETFG:
#ifdef MK09_V2
        if (m->src == TID_PM) { sel((u8)m->c); owner = (u8)m->b; } else r.op = E_PERM;      /* the line is in c */
#else
        if (m->src == TID_PM) owner = (u8)m->b; else r.op = E_PERM;
#endif
        break;
#ifdef MK09_V2
    case TTYC_LINEON:                   /* PM: the line c is in use */
        if (m->src == TID_PM && m->c < NLINE) { tl_on((u8)m->c); mk_rxregl((u8)m->c, tl_ring((u8)m->c)->b); } else r.op = E_PERM;
        break;
    case TTYC_SETLINE:                  /* PM: the task b uses the line c */
        if (m->src == TID_PM && m->b < NTASK) tline_of[m->b] = (u8)m->c; else r.op = E_PERM;
        break;
#endif
    default:
        r.op = E_INVAL;
    }
    mk_reply(m->src, &r);
}

int main(void)
{
    msg_t m;
    taskinfo_t ti;
#ifdef MK09_V2
    u8 i;
#endif

#ifdef BOARD_FM11
    bsp_con_init();
#endif
    mk_hook(NFY_TICK);          /* the tick, and NFY_RX of the receive interrupt (the kernel notifies the hook task) */
#ifdef MK09_V2
    tl_init();
    mk_rxregl(0, tl_ring(0)->b);        /* tell the kernel the ring: the receive interrupt is on from here (the other lines when PM says that they are in use) */
#else
    mk_rxreg(rxr.b);            /* tell the kernel the ring: the receive interrupt is on from here */
#endif
    for (;;) {
#ifdef BOARD_FM11
        bsp_con_flush();
#endif
        if (mk_recv(TID_ANY, &m)) continue;
        if (m.src == TID_HW) {
#ifdef MK09_V2
            for (i = 0; i < NLINE; i++) if (tl_is_on(i)) { sel(i); pump(); try_reader(); }
#else
            pump();
            try_reader();
#endif
            continue;
        }
#ifdef MK09_V2
        i = 0;                                                         /* (not ?: here: CMOC and the types of the two sides) */
        if (m.src < NTASK) i = tline_of[m.src];
        sel(i);                                                        /* the console of the task that asks */
#endif
        switch (m.op) {
        case TTY_WRITE:
            do_write(&m);
            break;
        case TTY_READ:
            if (owner && m.src != owner) { reply(m.src, E_BUSY, 0); break; }      /* a background program */
            if (rd_tid && rd_tid != m.src) {        /* is the previous reader still waiting? */
                if (!mk_tinfo(rd_tid, &ti) && ti.state == TS_RECV) {
                    reply(m.src, E_BUSY, 0);
                    break;
                }
            }
            rd_tid = m.src;
            rd_len = m.c;
            try_reader();
            break;
        case TTY_IOCTL:
            do_ioctl(&m);
            break;
        default:
            reply(m.src, E_NOSYS, 0);
        }
    }
    return 0;
}
