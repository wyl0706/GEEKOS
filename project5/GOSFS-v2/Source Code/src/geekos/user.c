/*
 * Common user mode functions
 * Copyright (c) 2001,2003,2004 David H. Hovemeyer <daveho@cs.umd.edu>
 * $Revision: 1.50 $
 * 
 * This is free software.  You are permitted to use,
 * redistribute, and modify it as specified in the file "COPYING".
 */

#include <geekos/errno.h>
#include <geekos/ktypes.h>
#include <geekos/kassert.h>
#include <geekos/int.h>
#include <geekos/mem.h>
#include <geekos/malloc.h>
#include <geekos/kthread.h>
#include <geekos/vfs.h>
#include <geekos/tss.h>
#include <geekos/user.h>

/*
 * This module contains common functions for implementation of user
 * mode processes.
 */
static int lprogdebug = 0;
/*
 * Associate the given user context with a kernel thread.
 * This makes the thread a user process.
 */
void Attach_User_Context(struct Kernel_Thread* kthread, struct User_Context* context)
{
    KASSERT(context != 0);
    kthread->userContext = context;

    Disable_Interrupts();

    /*
     * We don't actually allow multiple threads
     * to share a user context (yet)
     */
    KASSERT(context->refCount == 0);

    ++context->refCount;
    Enable_Interrupts();
}

/*
 * If the given thread has a user context, detach it
 * and destroy it.  This is called when a thread is
 * being destroyed.
 */
void Detach_User_Context(struct Kernel_Thread* kthread)
{
    struct User_Context* old = kthread->userContext;

    kthread->userContext = 0;

    if (old != 0) {
	int refCount;
	if(Interrupts_Enabled())
		Disable_Interrupts();
        --old->refCount;
	refCount = old->refCount;
	Enable_Interrupts();

	/*Print("User context refcount == %d\n", refCount);*/
        if (refCount == 0)
            Destroy_User_Context(old);
    }
}

/*
 * Spawn a user process.
 * Params:
 *   program - the full path of the program executable file
 *   command - the command, including name of program and arguments
 *   pThread - reference to Kernel_Thread pointer where a pointer to
 *     the newly created user mode thread (process) should be
 *     stored
 * Returns:
 *   The process id (pid) of the new process, or an error code
 *   if the process couldn't be created.  Note that this function
 *   should return ENOTFOUND if the reason for failure is that
 *   the executable file doesn't exist.
 */
int Spawn(const char *program, const char *command, struct Kernel_Thread **pThread)
{
    /*
     * Hints:
     * - Call Read_Fully() to load the entire executable into a memory buffer
     * - Call Parse_ELF_Executable() to verify that the executable is
     *   valid, and to populate an Exe_Format data structure describing
     *   how the executable should be loaded
     * - Call Load_User_Program() to create a User_Context with the loaded
     *   program
     * - Call Start_User_Thread() with the new User_Context
     *
     * If all goes well, store the pointer to the new thread in
     * pThread and return 0.  Otherwise, return an error code.
     */
    //TODO("Spawn a process by reading an executable from a filesystem");
     char *exeFileData = 0;
     ulong_t exeFileLength;
     struct Exe_Format exeFormat;
     struct User_Context * pUserContext = NULL;
     struct Kernel_Thread* pkthread;
     int rc;

     if (lprogdebug)
     {
        Print("Reading %s...\n", program);
     }

     if ( (rc = Read_Fully(program, (void**) &exeFileData, &exeFileLength)) != 0)
     {
        Print("Read_Fully failed to read %s from disk\n", program);
        goto fail;
     }

     if (lprogdebug)
     {  
       Print("Read_Fully OK\n");
     }

  if ((rc = Parse_ELF_Executable(exeFileData, exeFileLength, &exeFormat)) != 0)
    {
      Print("Parse_ELF_Executable failed\n");
      goto fail;
    }

  if (lprogdebug)
    { 
      Print("Parse_ELF_Executable OK\n");
    }


  if ( (rc = Load_User_Program(exeFileData, exeFileLength,
		&exeFormat, command,
		&pUserContext)) != 0)
    {
      Print("Load_User_Program failed\n");
      goto fail;
    }

  if ((pkthread = Start_User_Thread(pUserContext, false)) == NULL) //ERROR detached should be false
  {
  	Print("Start_User_Thread Failed!\n");
	rc = -1;
	goto fail;
  }
  
    /*
     * User program has been loaded, so we can free the
     * executable file data now.
     */
    Free(exeFileData);
    exeFileData = 0;

  /* If we arrived here, everything was fine and the program exited */
  //Print("If you see this you're happy\n");
  *pThread = pkthread;
  if(pThread == NULL)
	Print("wrong!\n");

  return pkthread->pid;
  // Exit(0); as this is not a thread, we don't exit but return.


fail:

    /* We failed; release any allocated memory */
    //if(Interrupts_Enabled())
    	Disable_Interrupts();

    if(pUserContext != NULL && pUserContext->memory != NULL)
        Free(pUserContext->memory);

    Enable_Interrupts();

    return rc;
}

/*
 * If the given thread has a User_Context,
 * switch to its memory space.
 *
 * Params:
 *   kthread - the thread that is about to execute
 *   state - saved processor registers describing the state when
 *      the thread was interrupted
 */
void Switch_To_User_Context(struct Kernel_Thread* kthread, struct Interrupt_State* state)
{
    /*
     * Hint: Before executing in user mode, you will need to call
     * the Set_Kernel_Stack_Pointer() and Switch_To_Address_Space()
     * functions.
     */
    //TODO("Switch to a new user address space, if necessary");
    	 Set_Kernel_Stack_Pointer((ulong_t)((kthread->stackPage)+PAGE_SIZE));
	// ERROR: only user thread need this function
	if(Interrupts_Enabled())
		Disable_Interrupts();
	if (kthread->userContext != 0)
	{
		//Print("SuC: %d ldt: %d\n", (int)(kthread->userContext), 
		//	(int)));
		//Set_Kernel_Stack_Pointer((ulong_t)(kthread->esp));
		Switch_To_Address_Space(kthread->userContext);
		//Print("Switch to Address Space!\n");
		//Print("jump to %d\n", (int)(kthread->userContext->entryAddr));
		//Dump_Interrupt_State(state);
		//KASSERT(0);
	}
	Enable_Interrupts();

}

