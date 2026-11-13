/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 YUKARI Semiconductor Devices */
/*
 * console.c - FM-11: the text output goes to the display sub system (PUT STRING, command $03).
 *
 * The programs (jwp, basic, ...) move the cursor and clear the screen with ANSI sequences.  The console of the sub system knows other
 * "orders" (F-BASIC style; found in the ROM of the sub CPU, subsys.rom):
 *     ESC = row col  (both + $20)   cursor position      ESC *  clear the screen     ESC T  erase to the end of the line
 *     ESC Y  erase to the end of the screen               ESC E / ESC R  insert / delete a line   ESC G a  attribute (colour bits 0-2, 3 = reverse)
 * so bsp_con_put() translates   ESC [ r;c H   ESC [ J   ESC [ K   ESC [ n L / M   ESC [ ... m   and drops the other sequences.
 */
#include "bsp.h"

#define CON_MAX 100                     /* the command has room for 124 bytes of string */
#define SHARED  ((u8 *)0xFC80)

static u8 cbuf[CON_MAX];
static u8 cn;
static u8 st;                           /* 0 text, 1 after ESC, 2 inside ESC [ */
static u8 np, pv[3];
static u8 attr = 7;

void bsp_con_flush(void)
{
    u8 *p = SHARED;
    u8 i;

    if (!cn)
        return;
    while (FM_SUBSTAT & 0x80)           /* the sub CPU is still busy with the last command */
        ;
    FM_SUBSTAT = 0x80;                  /* HALT request */
    while (!(FM_SUBSTAT & 0x80))        /* wait until it is halted */
        ;
    p[1] = 0;                           /* no continuation */
    p[2] = 0x03;                        /* PUT STRING */
    p[3] = cn;
    for (i = 0; i < cn; i++)
        p[4 + i] = cbuf[i];
    FM_SUBSTAT = 0;                     /* release: the sub CPU runs the command */
    cn = 0;
}

static void emit(u8 c)
{
    if (cn >= CON_MAX)
        bsp_con_flush();
    cbuf[cn++] = c;
}

static void order(u8 c)
{
    emit(0x1B);
    emit(c);
}

static void setattr(void)
{
    order('G');
    emit(0x40 | (attr & 0x3F));
}

static void sgr(u8 n)
{
    if (n == 0)
        attr = 7;
    else if (n == 7)
        attr |= 0x08;
    else if (n == 27)
        attr &= ~0x08;
    else if (n >= 30 && n <= 37) {      /* ANSI: 1 red, 2 green, 4 blue; FM-11: 1 blue, 2 red, 4 green */
        n -= 30;
        attr = (attr & 0x38) | ((n & 4) ? 1 : 0) | ((n & 1) ? 2 : 0) | ((n & 2) ? 4 : 0);
    }
}

static void csi(u8 c)
{
    u8 i, n = pv[0];

    switch (c) {
    case 'H':
    case 'f':
        order('=');
        emit(0x20 + (pv[0] ? pv[0] - 1 : 0));
        emit(0x20 + (pv[1] ? pv[1] - 1 : 0));
        break;
    case 'J':
        order(n == 2 ? '*' : 'Y');
        break;
    case 'K':
        order('T');
        break;
    case 'L':
    case 'M':
        for (i = n ? n : 1; i; i--)
            order(c == 'L' ? 'E' : 'R');
        break;
    case 'm':
        for (i = 0; i <= np; i++)
            sgr(pv[i]);
        setattr();
        break;
    }                                   /* (the other sequences, e.g. the scroll region r, are dropped) */
}

/* the sub CPU draws a kanji (Shift_JIS in the text, kanji ROM) as dots into the graphic planes: the graphic display and the palette must be on.
 * The console takes Shift_JIS after the command $37 with the parameter $FF (found by tracing F-BASIC; F-BASIC sets $00). */
void bsp_con_init(void)
{
    u8 *p = SHARED;
    u8 i;

    for (i = 0; i < 8; i++)
        FM_IO(0xFD98 + i) = i;          /* palette: the colour code is the colour */
    FM_IO(0xFDA0) |= 0x0F;              /* text and graphic display on */
    while (FM_SUBSTAT & 0x80)
        ;
    FM_SUBSTAT = 0x80;
    while (!(FM_SUBSTAT & 0x80))
        ;
    p[1] = 0;
    p[2] = 0x37;
    p[3] = 0xFF;
    FM_SUBSTAT = 0;
}

void bsp_con_put(u8 c)
{
    switch (st) {
    case 0:
        if (c == 0x1B)
            st = 1;
        else
            emit(c);
        return;
    case 1:
        if (c == '[') {
            st = 2;
            np = 0;
            pv[0] = pv[1] = pv[2] = 0;
        } else
            st = 0;
        return;
    default:
        if (c >= '0' && c <= '9')
            pv[np] = pv[np] * 10 + (c - '0');
        else if (c == ';') {
            if (np < 2)
                np++;
        } else if (c >= 0x40) {
            st = 0;
            csi(c);
        }
        return;
    }
}
