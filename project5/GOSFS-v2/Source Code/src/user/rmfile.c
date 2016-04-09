/*
 * rmfile - Delete a normal file(text file)
 * Copyright (c) 2006 Eric Bai <evilby@163.com>
 * $Revision: 1.0 $
 *
 * This is free software.  You are permitted to use,
 * redistribute, and modify it as specified in the file "COPYING".
 */

#include <conio.h>
#include <process.h>
#include <fileio.h>

int main(int argc, char *argv[])
{
    int rc;

    if (argc != 2) {
        Print("Usage: rmfile <file>\n");
	Exit(1);
    }

    rc = Delete(argv[1]);
    if (rc != 0)
	Print("Could not delete file: %s\n", Get_Error_String(rc));

    return !(rc == 0);
}
