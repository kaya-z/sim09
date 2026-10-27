/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 YUKARI Semiconductor Devices */
/* types.h - fixed size integer types (CMOC: char 8, int 16, long 32 bits) */
#ifndef YSD_TYPES_H
#define YSD_TYPES_H
typedef unsigned char  u8;
typedef signed char    s8;
typedef unsigned int   u16;
typedef int            s16;
typedef unsigned long  u32;
typedef long           s32;
#ifndef NULL
#define NULL ((void *)0)
#endif
#endif
