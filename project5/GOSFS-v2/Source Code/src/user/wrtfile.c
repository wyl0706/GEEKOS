/*
 * wrtfile - Write at most one line into a normal file(text file)
 * Copyright (c) 2006 Eric Bai <evilby@163.com>
 * $Revision: 1.0 $
 *
 * This is free software.  You are permitted to use,
 * redistribute, and modify it as specified in the file "COPYING".
 */

#include <conio.h>
#include <process.h>
#include <fileio.h>
#define LINE_SIZE 79

int main(int argc, char *argv[])
{
    int rc = 0;
    int fd = 0;
    char buf[LINE_SIZE+1];
    if (argc != 2) {
        Print("Usage: wrtfile <file>\n");
	Exit(1);
    }

    fd = Open(argv[1], O_WRITE);
    if (fd < 0)
    {
	Print("Could not open file: %s\n", Get_Error_String(rc));
	Exit(1);
    }
    Print("fd:%d\n", fd);

    //Write something into the file
    Read_Line(buf, sizeof(buf));
    rc = Write(fd, buf, sizeof(buf));
    if(rc < 0)
    {
	Print("Could not write to file: %s\n", Get_Error_String(rc));
	Close(fd);
	Exit(1);
    }

    Close(fd);

    return rc;
}
