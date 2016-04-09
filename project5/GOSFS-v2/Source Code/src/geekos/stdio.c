/*
 * Standerd in/out file
 * 
 * Written by Eric Bai <evilby@163.com>
 * 2006.5
 * version 1.0
 *
 * This is free software.  You are permitted to use,
 * redistribute, and modify it as specified in the file "COPYING".
 */

#include <limits.h>
#include <geekos/errno.h>
#include <geekos/kassert.h>
#include <geekos/screen.h>
#include <geekos/keyboard.h>
#include <geekos/malloc.h>
#include <geekos/string.h>
#include <geekos/bitset.h>
#include <geekos/synch.h>
#include <geekos/bufcache.h>
#include <geekos/stdio.h>
#include <geekos/gosfs.h>
#include <geekos/user.h>


// This function is taken directly from <geekos/conio.h>
// only modified slightly
static bool s_echo = true;
int Read_Line(char* buf, ulong_t bufSize)
{
    char *ptr = buf;
    int n = 0;
    Keycode k;
    bool done = false;
    int startrow = 0, startcol = 0;
    Get_Cursor(&startrow, &startcol);
    /*Print("Start column is %d\n", startcol); */

    bufSize--;
    do {
	k = Wait_For_Key();
	if ((k & KEY_SPECIAL_FLAG) || (k & KEY_RELEASE_FLAG))
	    continue;

	k &= 0xff;
	if (k == '\r')
	    k = '\n';

	if (k == ASCII_BS) {
	    if (n > 0) {
		char last = *(ptr - 1);
		int newcol = startcol;
		int i;

		/* Back up in line buffer */
		--ptr;
		--n;

		if (s_echo) {
		    /*
		     * Figure out what the column position of the last
		     * character was
		     */
		    for (i = 0; i < n; ++i) {
			char ch = buf[i];
			if (ch == '\t') {
			    int rem = newcol % TABWIDTH;
			    newcol += (rem == 0) ? TABWIDTH : (TABWIDTH - rem);
			} else {
			    ++newcol;
			}
		    }

		    /* Erase last character */
		    if (last != '\t')
			last = ' ';
		    Put_Cursor(startrow, newcol);
		    Put_Char(last);
		    Put_Cursor(startrow, newcol);
		}
	    }
	    continue;
	}

	if (s_echo)
	    Put_Char(k);

	if (k == '\n')
	    done = true;

	if (n < bufSize) {
	    *ptr++ = k;
	    ++n;
	}
    }
    while (!done);

    *ptr = '\0';

    return n;
}


// Read from keyboard, at most len characters
static int StdInput_Read(struct File *file, void *buf, ulong_t len)
{
	struct GOSFS_Inode *stdioINode = (struct GOSFS_Inode *)file->fsData;
	Mutex_Lock(&stdioINode->lock);
	int ret =  Read_Line(buf, len);
	Mutex_Unlock(&stdioINode->lock);

	return ret;
}


struct File_Ops s_stdInputFileOps = {
    0, // FStat,
    &StdInput_Read,
    0, // Write,
    0, // Seek,
    0, // Close,
    0, /* Read_Entry */
};


// simply print all the characters in the buffer out to the screen
static int StdOutput_Write(struct File *file, void *buf, ulong_t len)
{
	struct GOSFS_Inode *stdioINode = (struct GOSFS_Inode *)file->fsData;
	Mutex_Lock(&stdioINode->lock);

	int buflen = len;
	char *bufend = strchr(buf, '\0');
	char *output = (char *)Malloc(len + 1);
	if(output == NULL){ 
		buflen = ENOMEM;
		goto done;
	}
	
	if(bufend == 0)
	{	
		memcpy(output, buf, len);
		output[len] = '\0';
	}
	else
	{
		buflen = (int)(bufend-(char *)buf);
		memcpy(output, buf, buflen);
		output[buflen] = '\0';
	}

	Print("%s", output);
	Free(output);

done:
	Mutex_Unlock(&stdioINode->lock);
	return buflen;
}

struct File_Ops s_stdOutputFileOps = {
    0, // FStat,
    0, // Read,
    &StdOutput_Write,
    0, // Seek,
    0, // Close,
    0, /* Read_Entry */
};

struct File* Init_StdIn()
{
	struct GOSFS_Inode *stdioINode;
	struct File *stdIn = NULL;

	stdioINode = (struct GOSFS_Inode *)Malloc(sizeof(struct GOSFS_Inode));
	if (stdioINode == NULL)
		return NULL;
	//strcpy(stdioINode->name, "stdin");
	stdioINode->inodeNumber = -1;
	Mutex_Init(&stdioINode->lock);
	stdIn = Allocate_File(&s_stdInputFileOps, 0, 0, stdioINode, 0, 0);

	return stdIn;
}

struct File* Init_Stdout()
{
	struct GOSFS_Inode *stdioINode;
	struct File* stdOut = NULL;
	
	stdioINode = (struct GOSFS_Inode *)Malloc(sizeof(struct GOSFS_Inode));
	if (stdioINode == NULL)
		return NULL;
	//strcpy(stdioINode->name, "stdout");
	stdioINode->inodeNumber = -1;
	Mutex_Init(&stdioINode->lock);
	stdOut = Allocate_File(&s_stdOutputFileOps, 0, 0, stdioINode, 0, 0);

	return stdOut;
}

