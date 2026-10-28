/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 YUKARI Semiconductor Devices */
/*
 * reboot - write the files, then start the system again, as at power on.
 *
 * The system modules have no room for a new call, so this program does it itself: it copies a small routine to $0200 (the sector buffer of the boot ROM, free after the
 * boot), which turns the interrupts off, clears the memory (the system keeps the state of its tasks in variables that are zero at power on) and jumps to the reset vector:
 * the boot ROM reads the kernel and the servers from the card again.
 *
 *     orcc #$50 / lds #$0100 / clear $0110-$01FF, $0300-$DFFF and $E200-$FEFF / ldx $FFFE / jmp ,x
 */
#include "types.h"
#include "mklib.h"

static const u8 routine[] = {
    0x1A, 0x50, 0x10, 0xCE, 0x01, 0x00, 0x8E, 0x01, 0x10, 0x6F, 0x80, 0x8C, 0x02, 0x00, 0x26, 0xF9,
    0x8E, 0x03, 0x00, 0x6F, 0x80, 0x8C, 0xE0, 0x00, 0x26, 0xF9, 0x8E, 0xE2, 0x00, 0x6F, 0x80, 0x8C,
    0xFF, 0x00, 0x26, 0xF9, 0xBE, 0xFF, 0xFE, 0x6E, 0x84
};

int main(void)
{
    u8 i;
    u8 *p = (u8 *)0x0200;
    sync();
    for (i = 0; i < sizeof(routine); i++) p[i] = routine[i];
    ((void (*)(void))0x0200)();
    return 0;
}
