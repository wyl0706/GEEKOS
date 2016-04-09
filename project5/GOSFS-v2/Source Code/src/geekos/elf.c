/*
 * ELF executable loading
 * Copyright (c) 2003, Jeffrey K. Hollingsworth <hollings@cs.umd.edu>
 * Copyright (c) 2003, David H. Hovemeyer <daveho@cs.umd.edu>
 * $Revision: 1.29 $
 * 
 * This is free software.  You are permitted to use,
 * redistribute, and modify it as specified in the file "COPYING".
 */

#include <geekos/errno.h>
#include <geekos/kassert.h>
#include <geekos/ktypes.h>
#include <geekos/screen.h>  /* for debug Print() statements */
#include <geekos/pfat.h>
#include <geekos/malloc.h>
#include <geekos/string.h>
#include <geekos/user.h>
#include <geekos/fileio.h>
#include <geekos/elf.h>


/**
 * From the data of an ELF executable, determine how its segments
 * need to be loaded into memory.
 * @param exeFileData buffer containing the executable file
 * @param exeFileLength length of the executable file in bytes
 * @param exeFormat structure describing the executable's segments
 *   and entry address; to be filled in
 * @return 0 if successful, < 0 on error
 */
int Parse_ELF_Executable(char *exeFileData, ulong_t exeFileLength,
    struct Exe_Format *exeFormat)
{
    //TODO("Parse an ELF executable image");
    Print("Start Parsing Elf!\n");
    elfHeader * pelfh = (elfHeader *)(exeFileData);
    unsigned int phoff = pelfh->phoff;
    // unsigned short phentsize = pelfh->phentsize;
    unsigned short phnum = pelfh->phnum;
    int i;

    if(pelfh->type != 2)
    {
	Print("this is not an executable file!\n");	
	return -1;
    }

    if(pelfh->machine != 3)
    {
	Print("can not run on 80x06!\n");
	return -1;
    }

    exeFormat->entryAddr = pelfh->entry;
    exeFormat->numSegments = phnum;
    
    programHeader * ph = (programHeader*)(exeFileData+phoff);
    for(i = 0; i < phnum; i++)
    {
	exeFormat->segmentList[i].offsetInFile = ph->offset;
	exeFormat->segmentList[i].lengthInFile = ph->fileSize;
	exeFormat->segmentList[i].startAddress = ph->vaddr;
	exeFormat->segmentList[i].sizeInMemory = ph->memSize;
	exeFormat->segmentList[i].protFlags = ph->flags;
	ph++;
    }

    return 0;

}

