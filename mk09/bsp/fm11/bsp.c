/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 YUKARI Semiconductor Devices */
/* bsp.c - FM-11 board: the tick (PTM) and power are handled by the kernel in assembler (arch/m6809/kernel.s); only stubs here */
#include "bsp.h"

void bsp_timer_start(void) { }
u8 bsp_timer_fired(void) { return 0; }
void bsp_power_off(void) { for (;;) ; }
