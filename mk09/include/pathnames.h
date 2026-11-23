/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 YUKARI Semiconductor Devices */
/*
 * pathnames.h - the names of the files that the system and the programs know (docs/V2_PLAN.md, stage 1)
 *
 * 1.x: the names are not case sensitive (FS makes them upper case), and the images hold upper case names: the paths are written in upper case.
 * 2.0 (MK09_V2): the names are case sensitive, and the basic form is lower case: the same paths in lower case (the images are made with lower case names).
 * Every place that opens a system file takes the name from here, so that a 1.x build is the same as before.
 */
#ifndef MK_PATHNAMES_H
#define MK_PATHNAMES_H

#ifdef MK09_V2
#define PN_BIN      "/bin/"
#define PN_EXT_MX   ".mx"
#define PN_SH_MX    "/bin/sh.mx"
#define PN_LOGIN_MX "/bin/login.mx"
#define PN_NO_SH    "MK09: no sh.mx\r\n"
#define PN_LIB      "/lib"
#define PN_LIB_     "/lib/"
#define PN_CLIB_OBJ "clib.obj"
#define PN_RT_OBJ   "rt.obj"
#define PN_CLIB_IDX "clib.idx"
#define PN_RT_IDX   "rt.idx"
#define PN_START_S  "start.s"
#define PN_CC1_MSG  "/lib/cc1.msg"
#define PN_PAS1_MSG "/lib/pas1.msg"
#define PN_INC_     "/inc/"
#define PN_RC_SH    "/etc/rc.sh"
#define PN_TTYS     "/etc/ttys"
#define PN_PIPE0    "/tmp/pipe0.tmp"      /* (/tmp is for everybody: a user cannot make files in /) */
#define PN_PIPE1    "/tmp/pipe1.tmp"
#define PN_SKK_DIC  "/skk/skk.dic"
#define PN_USER_DIC "/skk/user.dic"
#else
#define PN_BIN      "/BIN/"
#define PN_EXT_MX   ".MX"
#define PN_SH_MX    "/BIN/SH.MX"
#define PN_LOGIN_MX "/BIN/LOGIN.MX"
#define PN_NO_SH    "MK09: no SH.MX\r\n"
#define PN_LIB      "/LIB"
#define PN_LIB_     "/LIB/"
#define PN_CLIB_OBJ "CLIB.OBJ"
#define PN_RT_OBJ   "RT.OBJ"
#define PN_CLIB_IDX "CLIB.IDX"
#define PN_RT_IDX   "RT.IDX"
#define PN_START_S  "START.S"
#define PN_CC1_MSG  "/LIB/CC1.MSG"
#define PN_PAS1_MSG "/LIB/PAS1.MSG"
#define PN_INC_     "/INC/"
#define PN_RC_SH    "/ETC/RC.SH"
#define PN_TTYS     "/ETC/TTYS"
#define PN_PIPE0    "/PIPE0.TMP"
#define PN_PIPE1    "/PIPE1.TMP"
#define PN_SKK_DIC  "/SKK/SKK.DIC"
#define PN_USER_DIC "/SKK/USER.DIC"
#endif

#endif
