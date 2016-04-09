/*
 * GeekOS file system
 * Copyright (c) 2003, Jeffrey K. Hollingsworth <hollings@cs.umd.edu>
 * Copyright (c) 2004, David H. Hovemeyer <daveho@cs.umd.edu>
 * $Revision: 1.19 $
 *
 * Modified by Eric Bai <evilby@163.com>
 * 2005. 5
 *
 * This is free software.  You are permitted to use,
 * redistribute, and modify it as specified in the file "COPYING".
 */

#ifndef GEEKOS_GOSFS_H
#define GEEKOS_GOSFS_H

#include <geekos/blockdev.h>
#include <geekos/fileio.h>
#include <geekos/vfs.h>
#include <geekos/user.h> // ERROR: import User_Context
//#include <geekos/kthread.h> //ERROR: import Thread_Queue
#include <geekos/synch.h> // ERROR: import Condition

/* Number of disk sectors per filesystem block. */
#define GOSFS_SECTORS_PER_FS_BLOCK	8

/* Size of a filesystem block in bytes. */
#define GOSFS_FS_BLOCK_SIZE		(GOSFS_SECTORS_PER_FS_BLOCK*SECTOR_SIZE)

/* Flags bits for directory entries. */
#define GOSFS_DIRENTRY_USED		0x01	/* Directory entry is in use. */
#define GOSFS_DIRENTRY_ISDIRECTORY	0x02	/* Directory entry refers to a subdirectory. */
#define GOSFS_DIRENTRY_SETUID		0x04	/* File executes using uid of file owner. */
#define GOSFS_DIRENTRY_OLD			0x08

#define GOSFS_FILENAME_MAX		127	/* Maximum filename length. */

#define GOSFS_NUM_DIRECT_BLOCKS		8	/* Number of direct blocks in dir entry. */
#define GOSFS_NUM_INDIRECT_BLOCKS	1	/* Number of singly-indirect blocks in dir entry. */
#define GOSFS_NUM_2X_INDIRECT_BLOCKS	1	/* Number of doubly-indirect blocks in dir entry. */
#define GOSFS_NUM_TOTAL_BLOCKS	\
	GOSFS_NUM_DIRECT_BLOCKS + 	\
	GOSFS_NUM_PTRS_PER_BLOCK*GOSFS_NUM_INDIRECT_BLOCKS +	\
	GOSFS_NUM_PTRS_PER_BLOCK*GOSFS_NUM_PTRS_PER_BLOCK*GOSFS_NUM_2X_INDIRECT_BLOCKS

/* Number of block pointers that can be stored in a single filesystem block. */
#define GOSFS_NUM_PTRS_PER_BLOCK	(GOSFS_FS_BLOCK_SIZE / sizeof(ulong_t))

/* Total number of block pointers in a directory entry. */
#define GOSFS_NUM_BLOCK_PTRS \
    (GOSFS_NUM_DIRECT_BLOCKS+GOSFS_NUM_INDIRECT_BLOCKS+GOSFS_NUM_2X_INDIRECT_BLOCKS)

#define GOSFS_NUM_DIR_ENTRY GOSFS_NUM_BLOCK_PTRS
/* Number of directory entries that fit in a filesystem block. */
#define GOSFS_DIR_ENTRIES_PER_BLOCK	(GOSFS_FS_BLOCK_SIZE / sizeof(struct GOSFS_Dir_Entry))
#define GOSFS_MAGIC		0x78330000

/*
 * A directory entry.
 * It is strongly recommended that you don't change this struct.
 */
struct GOSFS_Dir_Entry {
    ulong_t size;				/* Size of file. */
    ulong_t flags;				/* Flags: used, isdirectory, setuid. */
    char filename[GOSFS_FILENAME_MAX+1];	/* Filename (including space for nul-terminator). */
    ulong_t blockList[GOSFS_NUM_DIR_ENTRY];	/* Pointers to direct, indirect, and doubly-indirect blocks. */
    struct VFS_ACL_Entry acl[VFS_MAX_ACL_ENTRIES];/* List of ACL entries; first is for the file's owner. */
};

// this is the inode's block structure
struct GOSFS_Dir_Block{
	struct GOSFS_Dir_Entry entryTable[GOSFS_DIR_ENTRIES_PER_BLOCK];
	// ERROR: useless field uint_t numExistEntry;
};

// indirect block structure
struct GOSFS_Indirect_Block{
	ulong_t blockNumber[GOSFS_NUM_PTRS_PER_BLOCK];
};

struct GOSFS_Inode{
	struct GOSFS_Dir_Entry  dirEntry;
	uint_t inodeNumber;
	uint_t icount;
	struct Mutex lock;
	ulong_t iseek;
	ulong_t dirty;
	// ERROR: struct Thread_Queue waitQueue;
	struct Condition cond;		/*!< Condition: waiting for a buffer. */
};

// ERROR: my definitions
#define GOSFS_NUM_INODE_BITMAP_INUSE 225
#define GOSFS_NUM_INODE_BITMAP_BYTES 256
#define GOSFS_NUM_BLOCK_BITMAP_INUSE 1011
#define GOSFS_NUM_BLOCK_BITMAP_BYTES 1024
#define GOSFS_NUM_INODE 1800
#define GOSFS_NUM_BLOCK 8192
#define GOSFS_NUM_INODE_IN_BLOCK 100

#define GOSFS_SUPER_BLOCK_NUM 1
#define GOSFS_FIRST_DATA_BLOCK 102
#define GOSFS_ROOT_INODE_NUM 1

struct GOSFS_Instance{
	ulong_t magic;
	ulong_t numLogicBlocks;
	uint_t numInodes;
	ulong_t firstDataBlock;
	struct Block_Device * dev;
	uchar_t inodeBitmapVector[GOSFS_NUM_INODE_BITMAP_BYTES];
	uchar_t blockBitmapVector[GOSFS_NUM_BLOCK_BITMAP_BYTES];
};

struct GOSFS_Superblock{
	struct GOSFS_Instance gfsInstance;
	// ERROR:struct Thread_Queue waitQueue;
	struct Condition cond;		/*!< Condition: waiting for a buffer. */
	struct Mutex lock;
	ulong_t flags;
	uchar_t dirty;
};

void Init_GOSFS(void);
int Init_Stdio(void);
void Init_User_Stdio(struct User_Context *userContext);

#endif
