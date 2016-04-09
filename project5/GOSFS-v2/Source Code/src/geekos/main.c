/*
 * GeekOS C code entry point
 * Copyright (c) 2001,2003,2004 David H. Hovemeyer <daveho@cs.umd.edu>
 * Copyright (c) 2003, Jeffrey K. Hollingsworth <hollings@cs.umd.edu>
 * Copyright (c) 2004, Iulian Neamtiu <neamtiu@cs.umd.edu>
 * $Revision: 1.51 $
 * 
 * This is free software.  You are permitted to use,
 * redistribute, and modify it as specified in the file "COPYING".
 */

#include <geekos/bootinfo.h>
#include <geekos/string.h>
#include <geekos/screen.h>
#include <geekos/mem.h>
#include <geekos/crc32.h>
#include <geekos/tss.h>
#include <geekos/int.h>
#include <geekos/kthread.h>
#include <geekos/trap.h>
#include <geekos/timer.h>
#include <geekos/keyboard.h>
#include <geekos/dma.h>
#include <geekos/ide.h>
#include <geekos/floppy.h>
#include <geekos/pfat.h>
#include <geekos/vfs.h>
#include <geekos/user.h>
#include <geekos/paging.h>
#include <geekos/gosfs.h>


/*
 * Define this for a self-contained boot floppy
 * with a PFAT filesystem.  (Target "fd_aug.img" in
 * the makefile.)
 */
/*#define FD_BOOT*/

#ifdef FD_BOOT
#  define ROOT_DEVICE "fd0"
#  define ROOT_PREFIX "a"
#else
#  define ROOT_DEVICE "ide0"
#  define ROOT_PREFIX "c"
#endif

#define INIT_PROGRAM "/" ROOT_PREFIX "/shell.exe"



static void Mount_Root_Filesystem(void);
static void Spawn_Init_Process(void);


/*
 * Kernel C code entry point.
 * Initializes kernel subsystems, mounts filesystems,
 * and spawns init process.
 */
void Main(struct Boot_Info* bootInfo)
{
    Init_BSS();
    Init_Screen();
    Init_Mem(bootInfo);
    Init_CRC32();
    Init_TSS();
    Init_Interrupts();
    //Init_VM(bootInfo);
    Init_Scheduler();
    Init_Traps();
    Init_Timer();
    Init_Keyboard();
    Init_DMA();
    Init_Floppy();
    Init_IDE();
    Init_PFAT();
    Init_GOSFS();
	
    Mount_Root_Filesystem();

    Set_Current_Attr(ATTRIB(BLACK, GREEN|BRIGHT));
    Print("Welcome to GeekOS!\n");
    Set_Current_Attr(ATTRIB(BLACK, GRAY));

	// for testing gosfs
	int rc = Format("ide1", "gosfs");
	if(rc == 0)
	{
		Print("format ide1 with gosfs.\n");
		rc = Mount("ide1", "d", "gosfs");
		if(rc == 0)
			Print("gosfs mounted.\n");
		else
			Print("gosfs Failed Mounting: %d\n", rc);
	}
	else
		Print("gosfs formatted failed: %d\n", rc);
/*
	rc = Create_Directory("/d/a");
	if(rc != 0)
		Print("Failed create dir: %d\n", rc);
	else
		Print("Create dir successfull!\n");
	
	rc = Create_Directory("/d/a/b");
	if(rc != 0)
		Print("Failed create dir: %d\n", rc);
	else
		Print("Create dir successfull!\n");

	rc = Create_Directory("/d/a/b/c");
	if(rc != 0)
		Print("Failed create dir: %d\n", rc);
	else
		Print("Create dir successfull!\n");

	struct File *file1, *file2, *file3;
	rc = Open_Directory("/d/a/b", &file1);
	if(file1 != NULL) Print("dir open rc:%d\n", rc);

	rc = Open_Directory("/d/a/b/c", &file2);
	if(file2 != NULL) Print("dir open rc:%d\n", rc);

	rc = Open_Directory("/d/a/b/c", &file3);
	if(file3 != NULL) Print("dir open rc:%d\n", rc);
	Print("file3:%x\n", (int)file3);

	Close(file2);
	Close(file3);

	Print("  file closed.\n");

	rc = Delete("/d/a/b/c/motherfuckerfile");


	struct File *file4;
	rc = Open("/d/a/b/file", O_CREATE|O_WRITE, &file4);
	Print("rc from open:%d\n", rc);
	Print("---------------------------\n");

	struct File *file5;
	rc = Open("/d/a/b/file", O_READ|O_WRITE, &file5);
	Print("rc from open:%d\n", rc);
	Print("file5:%x\n", (int)file5);
	Print("file5 write:%x\n", (int)file5->ops->Write);

	Print("file4:%x\n", (int)file4);
	Print("file4 pos:%d\n", (int)file4->filePos);
	Print("file4 write:%x\n", (int)file4->ops->Write);
	char buf[4] = "abc";
	char buf1[4];
	Seek(file4, 5000000);
	Write(file4, buf, 4);
	Print("filepos:%d\n", (int)file4->filePos);
	Print("fileend:%d\n", (int)file4->endPos);
	Seek(file4, 5000000);
	Print("---------------------read\n");
	rc = Read(file4, buf1, 4);static int tWriteReread(int howManyKBs, char const * fileName)
	Print("buf:%s, rc:%d\n", buf1, rc);
	Close(file4);
	Close(file5);
	//Delete("/d/a/b/file");
*/


    Spawn_Init_Process();

    /* Now this thread is done. */
    Exit(0);
}



static void Mount_Root_Filesystem(void)
{
    if (Mount(ROOT_DEVICE, ROOT_PREFIX, "pfat") != 0)
	Print("Failed to mount /" ROOT_PREFIX " filesystem\n");
    else
	Print("Mounted /" ROOT_PREFIX " filesystem!\n");

    // ERROR: Init_Paging(); //we will handle this later
}






static void Spawn_Init_Process(void)
{
	// TODO("Spawn the init process");
	const char * program = "/c/shell.exe";
	const char * command = ""; //ÃüÁîÖÁÉÙ°üº¬Ò»žö¿ÉÖŽÐÐÎÄŒþÃû
	struct Kernel_Thread * pThread = NULL;

	
	// ERROR
	// if (rc = Tlocal_Create(&key, Detach_User_Context) != 0)
	// {
	//	Print("Failed in Tlocal_Create!\n");
	// }
	//-----------------------------------------------------------
	

	if(Spawn(program, command, &pThread) < 0)
		Print("Spawn Failed!\n");
	else if (pThread == NULL)
		Print("pThread is NULL!\n");
	else
		; //Print("See This is the end of Spawn_Init_Process.Thread is %u\n", (int)pThread);
 
}
