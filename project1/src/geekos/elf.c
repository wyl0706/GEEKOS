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
 /*
	struct Exe_Format {
    struct Exe_Segment segmentList[EXE_MAX_SEGMENTS];  Definition of segments 
    int numSegments;		 Number of segments contained in the executable 
    ulong_t entryAddr;	 	 Code entry point address 
 */
 
int Parse_ELF_Executable(char *exeFileData, ulong_t exeFileLength,
    struct Exe_Format *exeFormat)//要将exeFileData的信息赋给exeFormat
{
    int i;
	elfHeader * elfh = (elfHeader*) exeFileData;//ELF文件头
	programHeader * proh = (programHeader*) (exeFileData + elfh->phoff);
	struct Exe_Segment * seg = exeFormat->segmentList;//将ELF各个段的信息赋值给Exe_Segment类型的数组
	for(i = elfh->phnum; i > 0; i--)
	{
		seg->offsetInFile = proh->offset;
		seg->lengthInFile = proh->fileSize;
		seg->startAddress = proh->vaddr;
		seg->sizeInMemory = proh->memSize;
		seg->protFlags = proh->flags;
		seg++;
		proh++;
	}
	exeFormat->numSegments = elfh->phnum;
	exeFormat->entryAddr = elfh->entry;
	
	return 0;
}













