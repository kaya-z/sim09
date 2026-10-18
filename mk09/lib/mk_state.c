/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 YUKARI Semiconductor Devices */
/* lib/mk_state.c - part of the MK09 C library (see mklib.h); one object per group so that a program links only what it uses */
#include "types.h"
#include "mklib.h"
#include "mkstr.h"

u8 mk_in_h = 0xFF, mk_out_h = 0xFF;    /* FS handles of a redirected standard input / output (0xFF: the console) */
int argc_;
char *argv_[10];
