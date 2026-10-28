/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 YUKARI Semiconductor Devices */
/* pastok.h - the tokens of the Pascal compiler: pas0 reads the text and writes them, pas1 reads them (docs/PASCAL.md).
 * A token is a byte (a punctuation character stands for itself), then its data:
 *   T_NUM: the low word and the high word (two bytes each, high byte first)   T_ID, T_STR, T_REAL: a length byte, then the characters (a name is in lower case)
 *   TK_LINE: the line number (a word) of the tokens that follow     TK_STACK: the size of the stack (a word), from {$M n}     TK_FILE: the name of the file (length, characters) */
#ifndef PASTOK_H
#define PASTOK_H
enum {
    T_EOF = 0, TK_FILE = 124, TK_STACK = 125, TK_LINE = 126, T_NUM = 128, T_ID, T_STR, T_REAL, T_ASSIGN, T_LE, T_GE, T_NE, T_DOTDOT,
    K_AND, K_ARRAY, K_BEGIN, K_CASE, K_CONST, K_DIV, K_DO, K_DOWNTO, K_ELSE, K_END, K_FILE, K_FOR, K_FORWARD, K_FUNCTION, K_GOTO, K_IF, K_IN, K_LABEL,
    K_MOD, K_NIL, K_NOT, K_OF, K_OR, K_PACKED, K_PROCEDURE, K_PROGRAM, K_RECORD, K_REPEAT, K_SET, K_THEN, K_TO, K_TYPE, K_UNTIL, K_VAR, K_WHILE, K_WITH,
    K_XOR, K_SHL, K_SHR
};
#endif
