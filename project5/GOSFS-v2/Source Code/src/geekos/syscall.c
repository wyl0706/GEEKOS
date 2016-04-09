/*
 * System call handlers
 * Copyright (c) 2003, Jeffrey K. Hollingsworth <hollings@cs.umd.edu>
 * Copyright (c) 2003,2004 David Hovemeyer <daveho@cs.umd.edu>
 * $Revision: 1.59 $
 * 
 * This is free software.  You are permitted to use,
 * redistribute, and modify it as specified in the file "COPYING".
 */

#include <geekos/syscall.h>
#include <geekos/errno.h>
#include <geekos/kthread.h>
#include <geekos/int.h>
#include <geekos/elf.h>
#include <geekos/malloc.h>
#include <geekos/screen.h>
#include <geekos/keyboard.h>
#include <geekos/string.h>
#include <geekos/user.h>
#include <geekos/timer.h>
#include <geekos/vfs.h>

/*
 * Null system call.
 * Does nothing except immediately return control back
 * to the interrupted user program.
 * Params:
 *  state - processor registers from user mode
 *
 * Returns:
 *   always returns the value 0 (zero)
 */
static int Sys_Null(struct Interrupt_State* state)
{
    return 0;
}

/*
 * Exit system call.
 * The interrupted user process is terminated.
 * Params:
 *   state->ebx - process exit code
 * Returns:
 *   Never returns to user mode!
 */
static int Sys_Exit(struct Interrupt_State* state)
{
    // TODO("Exit system call");
    Exit((int)state->ebx);
    return 0;
}

/*
 * Print a string to the console.
 * Params:
 *   state->ebx - user pointer of string to be printed
 *   state->ecx - number of characters to print
 * Returns: 0 if successful, -1 if not
 */
static int Sys_PrintString(struct Interrupt_State* state)
{
    //TODO("PrintString system call");
    	char * kStr = (char *)Malloc((int)(state->ecx + 1));
	if(Copy_From_User(kStr, (state->ebx), (int)(state->ecx + 1)) == false)
		return 0;
	Put_String(kStr);
	//Print("%d\n", (int)state->ecx);
	Free(kStr);
	//Enable_Interrupts();
	return (int)state->ecx;
}

/*
 * Get a single key press from the console.
 * Suspends the user process until a key press is available.
 * Params:
 *   state - processor registers from user mode
 * Returns: the key code
 */
static int Sys_GetKey(struct Interrupt_State* state)
{
    //TODO("GetKey system call");
    return Wait_For_Key();
}

/*
 * Set the current text attributes.
 * Params:
 *   state->ebx - character attributes to use
 * Returns: always returns 0
 */
static int Sys_SetAttr(struct Interrupt_State* state)
{
    //TODO("SetAttr system call");
    	Set_Current_Attr(state->ebx);

	return 0;
}

/*
 * Get the current cursor position.
 * Params:
 *   state->ebx - pointer to user int where row value should be stored
 *   state->ecx - pointer to user int where column value should be stored
 * Returns: 0 if successful, -1 otherwise
 */
static int Sys_GetCursor(struct Interrupt_State* state)
{
    //TODO("GetCursor system call");
    	ulong_t row = (ulong_t)(g_currentThread->userContext->memory + state->ebx);
	ulong_t col = (ulong_t)(g_currentThread->userContext->memory + state->ecx);
	
	if(row > (ulong_t)(g_currentThread->userContext->memory +
		g_currentThread->userContext->size) ||
		col > (ulong_t)(g_currentThread->userContext->memory +
		g_currentThread->userContext->size))
		return -1;

	Get_Cursor((int *)row, (int *)col);
	//Print("get cursor\n");

	return 0;

}

/*
 * Set the current cursor position.
 * Params:
 *   state->ebx - new row value
 *   state->ecx - new column value
 * Returns: 0 if successful, -1 otherwise
 */
static int Sys_PutCursor(struct Interrupt_State* state)
{
    // TODO("PutCursor system call");
	//Print("put cursor\n");
	if( Put_Cursor((int)(state->ebx), (int)(state->ecx)) )
		return 0;
	else
		return -1;
}

/*
 * Create a new user process.
 * Params:
 *   state->ebx - user address of name of executable
 *   state->ecx - length of executable name
 *   state->edx - user address of command string
 *   state->esi - length of command string
 * Returns: pid of process if successful, error code (< 0) otherwise
 */
static int Sys_Spawn(struct Interrupt_State* state)
{
    // TODO("Spawn system call");
	struct Kernel_Thread* pkthread = NULL;
	if(state->ecx > VFS_MAX_PATH_LEN)
	{
		Print("path TOO LONG!\n");
		return ENAMETOOLONG;
	} 
	ulong_t program = (ulong_t)(g_currentThread->userContext->memory + state->ebx);
	ulong_t command = (ulong_t)(g_currentThread->userContext->memory + state->edx);
	Enable_Interrupts(); // Enable the Interrupts!
	// Print("Sys_Spawn started exeName:%s, cmd:%s\n", (char*)program, (char*)command);
	pkthread->pid = Spawn((char*)program, (char*)command, &pkthread);
	return pkthread->pid;
}

/*
 * Wait for a process to exit.
 * Params:
 *   state->ebx - pid of process to wait for
 * Returns: the exit code of the process,
 *   or error code (< 0) on error
 */
static int Sys_Wait(struct Interrupt_State* state)
{
    // TODO("Wait system call");
	struct Kernel_Thread * kwthread;
	kwthread = Lookup_Thread((int)(state->ebx));
	Enable_Interrupts();
	return Join(kwthread);
}

/*
 * Get pid (process id) of current thread.
 * Params:
 *   state - processor registers from user mode
 * Returns: the pid of the current thread
 */
static int Sys_GetPID(struct Interrupt_State* state)
{
    //TODO("GetPID system call");
	return g_currentThread->pid;
}

/*
 * Set the scheduling policy.
 * Params:
 *   state->ebx - policy,
 *   state->ecx - number of ticks in quantum
 * Returns: 0 if successful, -1 otherwise
 */
static int Sys_SetSchedulingPolicy(struct Interrupt_State* state)
{
    TODO("SetSchedulingPolicy system call");
}

/*
 * Get the time of day.
 * Params:
 *   state - processor registers from user mode
 *
 * Returns: value of the g_numTicks global variable
 */
static int Sys_GetTimeOfDay(struct Interrupt_State* state)
{
    TODO("GetTimeOfDay system call");
}

/*
 * Create a semaphore.
 * Params:
 *   state->ebx - user address of name of semaphore
 *   state->ecx - length of semaphore name
 *   state->edx - initial semaphore count
 * Returns: the global semaphore id
 */
static int Sys_CreateSemaphore(struct Interrupt_State* state)
{
    TODO("CreateSemaphore system call");
}

/*
 * Acquire a semaphore.
 * Assume that the process has permission to access the semaphore,
 * the call will block until the semaphore count is >= 0.
 * Params:
 *   state->ebx - the semaphore id
 *
 * Returns: 0 if successful, error code (< 0) if unsuccessful
 */
static int Sys_P(struct Interrupt_State* state)
{
    TODO("P (semaphore acquire) system call");
}

/*
 * Release a semaphore.
 * Params:
 *   state->ebx - the semaphore id
 *
 * Returns: 0 if successful, error code (< 0) if unsuccessful
 */
static int Sys_V(struct Interrupt_State* state)
{
    TODO("V (semaphore release) system call");
}

/*
 * Destroy a semaphore.
 * Params:
 *   state->ebx - the semaphore id
 *
 * Returns: 0 if successful, error code (< 0) if unsuccessful
 */
static int Sys_DestroySemaphore(struct Interrupt_State* state)
{
    TODO("DestroySemaphore system call");
}

/*
 * Mount a filesystem.
 * Params:
 * state->ebx - contains a pointer to the Mount_Syscall_Args structure
 *   which contains the block device name, mount prefix,
 *   and filesystem type
 *
 * Returns:
 *   0 if successful, error code if unsuccessful
 */
static int Sys_Mount(struct Interrupt_State *state)
{
    int rc = 0;
    struct VFS_Mount_Request *args = 0;

    /* Allocate space for VFS_Mount_Request struct. */
    if ((args = (struct VFS_Mount_Request *) Malloc(sizeof(struct VFS_Mount_Request))) == 0) {
	rc = ENOMEM;
	goto done;
    }

    /* Copy the mount arguments structure from user space. */
    if (!Copy_From_User(args, state->ebx, sizeof(struct VFS_Mount_Request))) {
	rc = EINVALID;
	goto done;
    }

    /*
     * Hint: use devname, prefix, and fstype from the args structure
     * and invoke the Mount() VFS function.  You will need to check
     * to make sure they are correctly nul-terminated.
     */
    Enable_Interrupts();
    rc = Mount(args->devname, args->prefix, args->fstype);
    Disable_Interrupts();

done:
    if (args != 0) Free(args);

    return rc;
}

/*
 * Open a file.
 * Params:
 *   state->ebx - address of user string containing path of file to open
 *   state->ecx - length of path
 *   state->edx - file mode flags
 *
 * Returns: a file descriptor (>= 0) if successful,
 *   or an error code (< 0) if unsuccessful
 */
static int Sys_Open(struct Interrupt_State *state)
{
	char *path = NULL;
	uint_t pathLen;
	int mode = (int)state->edx;
	int rc = 0;

	pathLen = state->ecx;
	if(pathLen > (VFS_MAX_PATH_LEN + 1))  { rc = ENAMETOOLONG; goto done; }
	path = (char *)Malloc(pathLen+1);
	if(path == NULL) { rc = ENOMEM; goto done;}
	if (!Copy_From_User(path, (ulong_t)state->ebx, (ulong_t)pathLen))
	{	rc = EUNSPECIFIED; goto done;}
	path[pathLen] = '\0';
	struct File *file = NULL;
	Enable_Interrupts();
	//Print("sys open path:%s\n", path);
	//while(1);
	rc = Open(path, mode, &file);
	//Print("sys open thread:%x, file:%x\n", (int)g_currentThread,(int)file);
	Disable_Interrupts();
	Print("sys_open:%d, %x\n", rc, (int)file);
done:
	if(path != NULL)
		Free(path);
	
	return rc;
}

/*
 * Open a directory.
 * Params:
 *   state->ebx - address of user string containing path of directory to open
 *   state->ecx - length of path
 *
 * Returns: a file descriptor (>= 0) if successful,
 *   or an error code (< 0) if unsuccessful
 */
static int Sys_OpenDirectory(struct Interrupt_State *state)
{
	char *path = NULL;
	uint_t pathLen;
	int rc = 0;

	pathLen = state->ecx;
	if(pathLen > (VFS_MAX_PATH_LEN + 1))  { rc = ENAMETOOLONG; goto done; }
	path = (char *)Malloc(pathLen + 1);
	if(path == NULL) { rc = ENOMEM; goto done;}
	if (!Copy_From_User(path, (ulong_t)state->ebx, (ulong_t)pathLen))
	{	rc = EUNSPECIFIED; goto done;}
	path[pathLen] = '\0';

	struct File *dir;
	Enable_Interrupts();
	rc = Open_Directory(path, &dir);
	Disable_Interrupts();

done:
	if(path != NULL)
		Free(path);

	return rc;
}

/*
 * Close an open file or directory.
 * Params:
 *   state->ebx - file descriptor of the open file or directory
 * Returns: 0 if successful, or an error code (< 0) if unsuccessful
 */
static int Sys_Close(struct Interrupt_State *state)
{
	//Print("close file.\n");
	if(g_currentThread->userContext == 0)
		return EUNSUPPORTED;

	if(state->ebx >= USER_MAX_FILES)
		return EUNSUPPORTED;

	if(g_currentThread->userContext->fileList[state->ebx] == NULL)
		return EINVALID;
	struct File *file = g_currentThread->userContext->fileList[state->ebx];
	Enable_Interrupts();
	int rc = Close(file);
	Disable_Interrupts();

	return rc;
}

/*
 * Delete a file.
 * Params:
 *   state->ebx - address of user string containing path to delete
 *   state->ecx - length of path
 *
 * Returns: 0 if successful, error code (< 0) if unsuccessful
 */
static int Sys_Delete(struct Interrupt_State *state)
{
	uint_t pathLen = state->ecx;
	if(pathLen > (VFS_MAX_PATH_LEN + 1))
		return ENAMETOOLONG;

	int rc = 0;
	char *path = (char *)Malloc(pathLen + 1);
	if(path == NULL) { rc = ENOMEM; goto done;}
	if(!Copy_From_User(path, (ulong_t)state->ebx, (ulong_t)pathLen))
	{	rc = EUNSPECIFIED; goto done;}
	path[pathLen] = '\0';

	Enable_Interrupts();
	rc = Delete(path);
	Disable_Interrupts();
	

done:
	if(path != NULL)
		Free(path);

	return rc;
}

/*
 * Read from an open file.
 * Params:
 *   state->ebx - file descriptor to read from
 *   state->ecx - user address of buffer to read into
 *   state->edx - number of bytes to read
 *
 * Returns: number of bytes read, 0 if end of file,
 *   or error code (< 0) on error
 */
static int Sys_Read(struct Interrupt_State *state)
{
	if(g_currentThread->userContext == 0) return EUNSUPPORTED;


	if(state->ebx >= g_currentThread->userContext->fileCount) return EUNSUPPORTED;
	struct File *file = g_currentThread->userContext->fileList[state->ebx];
	if(file == 0) return EINVALID;

	int rc = 0;
	char * buf = (char *)Malloc(state->edx);
	if(buf == NULL)	{ rc = ENOMEM; goto done;}

	Enable_Interrupts();	
	rc = Read(file, buf, (ulong_t)state->edx);
	Disable_Interrupts();
	//Print("sys_read:%s\n", buf);
	if(rc < 0) goto done;
	//Print("sys read buf:%s\n", buf);	

	if(!Copy_To_User((ulong_t)state->ecx, buf, (ulong_t)rc))
		rc = EUNSPECIFIED;


done:
	if(buf != NULL)
		Free(buf);
	return rc;
}

/*
 * Read a directory entry from an open directory handle.
 * Params:
 *   state->ebx - file descriptor of the directory
 *   state->ecx - user address of struct VFS_Dir_Entry to copy entry into
 * Returns: 0 if successful, error code (< 0) if unsuccessful
 */
static int Sys_ReadEntry(struct Interrupt_State *state)
{
	struct VFS_Dir_Entry *vDirEntry;
	if(state->ebx >= USER_MAX_FILES) return EUNSUPPORTED;
	struct File *file = g_currentThread->userContext->fileList[state->ebx];
	if(file == 0) return EINVALID;
	int rc = 0;
	vDirEntry = (struct VFS_Dir_Entry *)Malloc(sizeof(struct VFS_Dir_Entry));
	if(vDirEntry == NULL) {rc = ENOMEM; goto done;}

	Enable_Interrupts();
	//Print("before readentry.\n");
	rc = Read_Entry(file, vDirEntry);
	//Print("after readentry.\n");
	Disable_Interrupts();
	if(rc < 0) goto done;

	if(!Copy_To_User((ulong_t)state->ecx, vDirEntry, sizeof(struct VFS_Dir_Entry)))
		rc = EUNSPECIFIED;

done:
	if(vDirEntry != NULL)
	{ //Print("free vDirEntry.rc:%d\n", rc); 
		Free(vDirEntry); 
	}
	return rc;
}

/*
 * Write to an open file.
 * Params:
 *   state->ebx - file descriptor to write to
 *   state->ecx - user address of buffer get data to write from
 *   state->edx - number of bytes to write
 *
 * Returns: number of bytes written,
 *   or error code (< 0) on error
 */
static int Sys_Write(struct Interrupt_State *state)
{
	if(state->ebx >= g_currentThread->userContext->fileCount) return EUNSUPPORTED;
	//Print("ebx:%d\n", (int)state->ebx);
	struct File *file = g_currentThread->userContext->fileList[state->ebx];
	if(file == 0) return EINVALID;
	int rc = 0;
	
	void *buf = Malloc((ulong_t)state->edx);
	if(buf == NULL){ rc = ENOMEM; goto done;}
	if(!Copy_From_User(buf, (ulong_t)state->ecx, (ulong_t)state->edx))
		rc = EUNSPECIFIED;	
	//Print("sys write: %s\n", (char*)buf);
	Enable_Interrupts();
	//Print("sys write thread:%x,file:%x\n", (int)g_currentThread,(int)file);	
	rc = Write(file, buf, (ulong_t)state->edx);
	Disable_Interrupts();
	if(rc < 0) goto done;


done:
	if(buf != NULL)
		Free(buf);
	return rc;
}

/*
 * Get file metadata.
 * Params:
 *   state->ebx - address of user string containing path of file
 *   state->ecx - length of path
 *   state->edx - user address of struct VFS_File_Stat object to store metadata in
 *
 * Returns: 0 if successful, error code (< 0) if unsuccessful
 */
static int Sys_Stat(struct Interrupt_State *state)
{
	uint_t pathLen = state->ecx;
	if(pathLen > (VFS_MAX_PATH_LEN + 1))
		return ENAMETOOLONG;

	struct VFS_File_Stat *vFileStat = NULL;
	int rc = 0;
	char *path = (char *)Malloc(pathLen + 1);
	if(path == NULL)
	{	rc = ENOMEM; goto done;}
	vFileStat =(struct VFS_File_Stat *)Malloc(sizeof(struct VFS_File_Stat));
	if(vFileStat == NULL)
	{	rc = ENOMEM; goto done;}

	if(!Copy_From_User(path, (ulong_t)state->ebx, (ulong_t)pathLen))
	{	rc = EUNSPECIFIED; goto done;}
	path[pathLen] = '\0';

	Enable_Interrupts();
	rc = Stat(path, vFileStat);
	Disable_Interrupts();
	if(rc < 0) goto done;

	if(!Copy_To_User((ulong_t)state->edx, vFileStat, (ulong_t)sizeof(struct VFS_File_Stat)))
	{	rc = EUNSPECIFIED; goto done; }


done:
	if(path != NULL)
		Free(path);
	if(vFileStat != NULL)
		Free(vFileStat);

	return rc;
}

/*
 * Get metadata of an open file.
 * Params:
 *   state->ebx - file descriptor to get metadata for
 *   state->ecx - user address of struct VFS_File_Stat object to store metadata in
 *
 * Returns: 0 if successful, error code (< 0) if unsuccessful
 */
static int Sys_FStat(struct Interrupt_State *state)
{
	if(state->ebx >= USER_MAX_FILES) return EUNSUPPORTED;
	struct File *file = g_currentThread->userContext->fileList[state->ebx];
	struct VFS_File_Stat *vFileStat;
	int rc = 0;

	vFileStat = (struct VFS_File_Stat *)Malloc(sizeof(struct VFS_File_Stat));
	if(vFileStat == NULL)
	{	rc = ENOMEM; goto done;}

	Enable_Interrupts();
	rc = FStat(file, vFileStat);
	Disable_Interrupts();

	rc = Copy_To_User((ulong_t)state->ecx, vFileStat, (ulong_t)sizeof(struct VFS_File_Stat));
	if(rc) rc = 0;

done:
	if(vFileStat != NULL)
		Free(vFileStat);
	return rc;
}

/*
 * Change the access position in a file
 * Params:
 *   state->ebx - file descriptor 
 *   state->ecx - position in file
 *
 * Returns: 0 if successful, error code (< 0) if unsuccessful
 */
static int Sys_Seek(struct Interrupt_State *state)
{
	if(state->ebx >= USER_MAX_FILES) return EUNSUPPORTED;
	struct File *file = g_currentThread->userContext->fileList[state->ebx];

	Enable_Interrupts();
	int rc = Seek(file, (ulong_t)state->ecx);
	Disable_Interrupts();

	return rc;
}

/*
 * Create directory
 * Params:
 *   state->ebx - address of user string containing path of new directory
 *   state->ecx - length of path
 *
 * Returns: 0 if successful, error code (< 0) if unsuccessful
 */
static int Sys_CreateDir(struct Interrupt_State *state)
{
	uint_t pathLen = state->ecx;
	if(pathLen > (VFS_MAX_PATH_LEN + 1))
		return ENAMETOOLONG;

	char *path = (char *)Malloc(pathLen + 1);
	int rc = 0;
	if(path == NULL)
	{	rc = ENOMEM; goto done;}

	if(!Copy_From_User(path, (ulong_t)state->ebx, (ulong_t)pathLen))
	{	rc = EUNSPECIFIED; goto done;}
	path[pathLen] = '\0';

	Enable_Interrupts();
	rc = Create_Directory(path);
	Disable_Interrupts();
	
done:
	if(path != NULL)
		Free(path);
	
	return rc;
}

/*
 * Flush filesystem buffers
 * Params: none 
 * Returns: 0 if successful, error code (< 0) if unsuccessful
 */
static int Sys_Sync(struct Interrupt_State *state)
{

	Enable_Interrupts();
	int rc = Sync();
	Disable_Interrupts();

	return rc;
}
/*
 * Format a device
 * Params:
 *   state->ebx - address of user string containing device to format
 *   state->ecx - length of device name string
 *   state->edx - address of user string containing filesystem type 
 *   state->esi - length of filesystem type string

 * Returns: 0 if successful, error code (< 0) if unsuccessful
 */
static int Sys_Format(struct Interrupt_State *state)
{
	uint_t devLen = state->ecx;
	if(devLen > (BLOCKDEV_MAX_NAME_LEN + 1))
		return ENAMETOOLONG;
	uint_t fsLen = state->esi;
	if(fsLen > (BLOCKDEV_MAX_NAME_LEN + 1))
		return ENAMETOOLONG;

	int rc = 0;
	char* devName = (char*)Malloc(devLen + 1);
	if(devName == NULL)
	{	rc = ENOMEM; goto done;}

	char* fsName = (char*)Malloc(fsLen + 1);
	if(fsName == NULL)
	{	rc = ENOMEM; goto done;}

	if(!Copy_From_User(devName, (ulong_t)state->ebx, (ulong_t)devLen))
	{	rc = EUNSPECIFIED; goto done;}
	devName[devLen] = '\0';
	
	if(!Copy_From_User(fsName, (ulong_t)state->edx, (ulong_t)fsLen))
	{	rc = EUNSPECIFIED; goto done;}
	fsName[fsLen] = '\0';

	Enable_Interrupts();	
	rc = Format(devName, fsName);
	Disable_Interrupts();

done:
	if(devName == NULL)
		Free(devName);
	if(fsName == NULL)
		Free(fsName);

	return rc;
}


/*
 * Global table of system call handler functions.
 */
const Syscall g_syscallTable[] = {
    Sys_Null,
    Sys_Exit,
    Sys_PrintString,
    Sys_GetKey,
    Sys_SetAttr,
    Sys_GetCursor,
    Sys_PutCursor,
    Sys_Spawn,
    Sys_Wait,
    Sys_GetPID,
    /* Scheduling and semaphore system calls. */
    Sys_SetSchedulingPolicy,
    Sys_GetTimeOfDay,
    Sys_CreateSemaphore,
    Sys_P,
    Sys_V,
    Sys_DestroySemaphore,
    /* File I/O system calls. */
    Sys_Mount,
    Sys_Open,
    Sys_OpenDirectory,
    Sys_Close,
    Sys_Delete,
    Sys_Read,
    Sys_ReadEntry,
    Sys_Write,
    Sys_Stat,
    Sys_FStat,
    Sys_Seek,
    Sys_CreateDir,
    Sys_Sync,
    Sys_Format,
};

/*
 * Number of system calls implemented.
 */
const int g_numSyscalls = sizeof(g_syscallTable) / sizeof(Syscall);
