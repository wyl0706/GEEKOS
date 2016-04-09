/*
 * Standard input/output header file
 * 
 * Written by Eric Bai <evilby@163.com>
 * Revision: 1.0
 *
 * This is free software.  You are permitted to use,
 * redistribute, and modify it as specified in the file "COPYING".
 */

#ifndef GEEKOS_STDIO_H
#define GEEKOS_STDIO_H

#include <geekos/gosfs.h>


#define GOS_STDINPUT_FILE 0
#define GOS_STDOUTPUT_FILE 1


struct File* Init_StdIn(void);
struct File* Init_Stdout(void);

// ERROR: void Init_Stdio(struct User_Context *userContext);
#endif
