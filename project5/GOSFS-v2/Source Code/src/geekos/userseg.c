/*
 * Segmentation-based user mode implementation
 * Copyright (c) 2001,2003 David H. Hovemeyer <daveho@cs.umd.edu>
 * $Revision: 1.23 $
 * 
 * This is free software.  You are permitted to use,
 * redistribute, and modify it as specified in the file "COPYING".
 */

#include <geekos/ktypes.h>
#include <geekos/kassert.h>
#include <geekos/defs.h>
#include <geekos/mem.h>
#include <geekos/string.h>
#include <geekos/malloc.h>
#include <geekos/int.h>
#include <geekos/gdt.h>
#include <geekos/segment.h>
#include <geekos/tss.h>
#include <geekos/kthread.h>
#include <geekos/argblock.h>
#include <geekos/user.h>
#include <geekos/errno.h>
#include <geekos/gosfs.h> // ERROR: init standard I/O

/* ----------------------------------------------------------------------
 * Variables
 * ---------------------------------------------------------------------- */

#define DEFAULT_USER_STACK_SIZE 8192

int debugUserseg= 0;
#define Debug(args...) if (debugUserseg) Print(args)

/* ----------------------------------------------------------------------
 * Private functions
 * ---------------------------------------------------------------------- */


/*
 * Create a new user context of given size
 */

/* TODO: Implement
static struct User_Context* Create_User_Context(ulong_t size)
*/

// userSpaceÊÇÖžÏòÓÃ»§ÄÚŽæµÄµØÖ· 
// static void * userSpace;

static bool Validate_User_Memory(struct User_Context* userContext,
    ulong_t userAddr, ulong_t bufSize)
{
    ulong_t avail;

    if (userAddr >= userContext->size)
        return false;

    avail = userContext->size - userAddr;
    if (bufSize > avail)
        return false;

    return true;
}

/* ----------------------------------------------------------------------
 * Public functions
 * ---------------------------------------------------------------------- */

/*
 * Destroy a User_Context object, including all memory
 * and other resources allocated within it.
 */
void Destroy_User_Context(struct User_Context* userContext)
{
    /*
     * Hints:
     * - you need to free the memory allocated for the user process
     * - don't forget to free the segment descriptor allocated
     *   for the process's LDT
     */
	struct Segment_Descriptor *desc= &userContext->ldt[0]; // cs
	Init_Null_Segment_Descriptor(desc);
	desc = &userContext->ldt[1]; //ds
	Init_Null_Segment_Descriptor(desc);
	desc = userContext->ldtDescriptor; //ldt in gdt
	Init_Null_Segment_Descriptor(desc);
	desc = &userContext->ldt[2];//ss in gdt
	Init_Null_Segment_Descriptor(desc);
	
	Free_Segment_Descriptor(userContext->ldtDescriptor);
	Debug("free ldtdescriptor.\n");
	Free(userContext->memory);
	Free(userContext);
    
}

/*
 * Load a user executable into memory by creating a User_Context
 * data structure.
 * Params:
 * exeFileData - a buffer containing the executable to load
 * exeFileLength - number of bytes in exeFileData
 * exeFormat - parsed ELF segment information describing how to
 *   load the executable's text and data segments, and the
 *   code entry point address
 * command - string containing the complete command to be executed:
 *   this should be used to create the argument block for the
 *   process
 * pUserContext - reference to the pointer where the User_Context
 *   should be stored
 *
 * Returns:
 *   0 if successful, or an error code (< 0) if unsuccessful
 */
int Load_User_Program(char *exeFileData, ulong_t exeFileLength,
    struct Exe_Format *exeFormat, const char *command,
    struct User_Context **pUserContext)
{
    /*
     * Hints:
     * - Determine where in memory each executable segment will be placed
     * - Determine size of argument block and where it memory it will
     *   be placed
     * - Copy each executable segment into memory
     * - Format argument block in memory
     * - In the created User_Context object, set code entry point
     *   address, argument block address, and initial kernel stack pointer
     *   address
     */
        struct User_Context * userContext;
	//static struct Argument_Block * argBlock;
	//void * argBlock;
	struct Segment_Descriptor * userDesc;
	ulong_t memsize, maxva = 0;
	unsigned numArgs;
	ulong_t argBlockSize;
	int i;

	// Allocate space for userContext
	userContext = (struct User_Context *)Malloc(sizeof(struct User_Context));
	if(userContext == NULL)
	{
		Print("not enough space for userContext!\n");
		return -1;
	}


 	for (i = 0; i < exeFormat->numSegments; ++i) {
		struct Exe_Segment *segment = &exeFormat->segmentList[i];
		ulong_t topva = segment->startAddress + segment->sizeInMemory; 
    
   		if (topva > maxva)
      		maxva = topva;
	}
	memsize = Round_Up_To_Page(maxva);
	userContext->argBlockAddr = memsize;
	Get_Argument_Block_Size(command, &numArgs, &argBlockSize);
	Debug("there are %d args\nargblocksize is %d \n", numArgs, (int)argBlockSize);
	//argBlock = Malloc(argBlockSize);
	memsize += Round_Up_To_Page(argBlockSize);
	int stackaddr = memsize;
        memsize += DEFAULT_USER_STACK_SIZE; 
	userContext->size = memsize;

	// --------------------------------------------------
	userContext->memory = (char *)Malloc(userContext->size);
	if(userContext->memory == NULL)
	{
		Print("Failed Allocating Memory!\n");
		return EUNSPECIFIED; // -1
	}
	memset((char *)(userContext->memory), '\0', memsize);

	// ldt, ss
	userContext->ldtDescriptor = Allocate_Segment_Descriptor();
	Init_LDT_Descriptor(userContext->ldtDescriptor, userContext->ldt, 3);
	userContext->ldtSelector = Selector(0, true, Get_Descriptor_Index(userContext->ldtDescriptor));
	Debug("ldt:%d\n", (int)(userContext->ldtSelector));

	// ERROR:
	userDesc = Allocate_Segment_Descriptor();
	userContext->ldt[0] = * userDesc;
	Free_Segment_Descriptor(userDesc);
	
	userDesc = Allocate_Segment_Descriptor();
	userContext->ldt[1] = *userDesc;
	Free_Segment_Descriptor(userDesc);

	userDesc = Allocate_Segment_Descriptor();
	userContext->ldt[2] = *userDesc;
	Free_Segment_Descriptor(userDesc);


	Init_Code_Segment_Descriptor( 
		&(userContext->ldt[0]), 
		(ulong_t)(userContext->memory), 
		userContext->size / PAGE_SIZE, 
		3);
	userContext->csSelector = Selector( 3, false, 
		&(userContext->ldt[0]) - userContext->ldt );

	Init_Data_Segment_Descriptor( 
		&(userContext->ldt[1]), 
		(ulong_t)(userContext->memory), 
		userContext->size / PAGE_SIZE, 
		3);
	userContext->dsSelector = Selector( 3, false, 
		&(userContext->ldt[1]) - userContext->ldt );

	Init_Data_Segment_Descriptor( 
		&(userContext->ldt[2]), 
		stackaddr, 
		userContext->size / PAGE_SIZE, // ERROR
		3);
	userContext->ssSelector = Selector( 3, false, 
		&(userContext->ldt[2]) - userContext->ldt );
	Debug("cs: %u\nds: %u\nss: %u\n", (int)(userContext->csSelector), (int)(userContext->dsSelector), (int)(userContext->ssSelector));



	for (i = 0; i < exeFormat->numSegments; ++i) {
		struct Exe_Segment *segment = &exeFormat->segmentList[i];

       	memcpy((void *)(userContext->memory + segment->startAddress),
    	   		(void *)exeFileData + segment->offsetInFile,
	   		segment->lengthInFile);
       }


	userContext->entryAddr = exeFormat->entryAddr;
	userContext->stackPointerAddr = (ulong_t)(userContext->size); //ERROR!! Initial Kernel Stack?
	// ERROR why can't this work? userContext.stackPointerAddr = DEFAULT_USER_STACK_SIZE;
	userContext->refCount = 0;
	Debug("entry: %u\n", (int)(userContext->entryAddr));
	//------------------------------------------------
	//ERROR
	Format_Argument_Block((char *)(userContext->memory + userContext->argBlockAddr), numArgs, 
		(ulong_t)(userContext->argBlockAddr),
		 command);

	// init user filelist and standard i/o file
	for(i = 0; i < USER_MAX_FILES; i++)
		userContext->fileList[i] = NULL;
	userContext->fileCount = 0;
	Init_User_Stdio(userContext);
	
	*pUserContext = userContext;

	return 0; // successfully load user program
}

/*
 * Copy data from user memory into a kernel buffer.
 * Params:
 * destInKernel - address of kernel buffer
 * srcInUser - address of user buffer
 * bufSize - number of bytes to copy
 *
 * Returns:
 *   true if successful, false if user buffer is invalid (i.e.,
 *   doesn't correspond to memory the process has a right to
 *   access)
 */
bool Copy_From_User(void* destInKernel, ulong_t srcInUser, ulong_t bufSize)
{
    /*
     * Hints:
     * - the User_Context of the current process can be found
     *   from g_currentThread->userContext
     * - the user address is an index relative to the chunk
     *   of memory you allocated for it
     * - make sure the user buffer lies entirely in memory belonging
     *   to the process
     */
    // TODO("Copy memory from user buffer to kernel buffer");
    bool isVal = Validate_User_Memory(
		g_currentThread->userContext, 
		srcInUser,bufSize); /* delete this; keeps gcc happy */
	if(!isVal) return false;

	ulong_t userInKernel = (ulong_t)g_currentThread->userContext->memory + srcInUser;
	memcpy(destInKernel, (void*)userInKernel, bufSize);
	
	return true;
}

/*
 * Copy data from kernel memory into a user buffer.
 * Params:
 * destInUser - address of user buffer
 * srcInKernel - address of kernel buffer
 * bufSize - number of bytes to copy
 *
 * Returns:
 *   true if successful, false if user buffer is invalid (i.e.,
 *   doesn't correspond to memory the process has a right to
 *   access)
 */
bool Copy_To_User(ulong_t destInUser, void* srcInKernel, ulong_t bufSize)
{
    /*
     * Hints: same as for Copy_From_User()
     */
    //TODO("Copy memory from kernel buffer to user buffer");
	bool isVal = Validate_User_Memory(g_currentThread->userContext, destInUser, bufSize);
	if(!isVal) return false;
	
	ulong_t destInKernel = (ulong_t)(g_currentThread->userContext->memory + destInUser);
	memcpy((void*)destInKernel, srcInKernel, bufSize);

	return true;
}

/*
 * Switch to user address space belonging to given
 * User_Context object.
 * Params:
 * userContext - the User_Context
 */
void Switch_To_Address_Space(struct User_Context *userContext)
{
    /*
     * Hint: you will need to use the lldt assembly language instruction
     * to load the process's LDT by specifying its LDT selector.
     */
    // TODO("Switch to user address space using segmentation/LDT");

	ushort_t ldtr = userContext->ldtSelector;
	// Print("ldt %d\n", (int)(userContext->ldtSelector));
	// KASSERT(0);
	Load_LDTR(ldtr);
	/* Load the task register */
	
	// KASSERT(0);
}

