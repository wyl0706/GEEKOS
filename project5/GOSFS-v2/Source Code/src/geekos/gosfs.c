/*
 * GeekOS file system
 * Copyright (c) 2004, David H. Hovemeyer <daveho@cs.umd.edu>
 * $Revision: 1.54 $
 *
 * This is free software.  You are permitted to use,
 * redistribute, and modify it as specified in the file "COPYING".
 */

#include <limits.h>
#include <geekos/errno.h>
#include <geekos/kassert.h>
#include <geekos/screen.h>
#include <geekos/malloc.h>
#include <geekos/string.h>
#include <geekos/bitset.h>
#include <geekos/synch.h>
#include <geekos/bufcache.h>
#include <geekos/gosfs.h>
#include <geekos/user.h>
#include <geekos/stdio.h>

int debugGOSFS = 0;
#define Debug(args...) if (debugGOSFS) Print(args)

/* ----------------------------------------------------------------------
 * Private data and functions
 * ---------------------------------------------------------------------- */
static struct FS_Buffer_Cache * gosfsBufferCache;
static struct VNode_List vnodeList;
static struct File *stdIn, *stdOut;
//static struct GOSFS_Inode * currentDir;

#define FIND_INODEBLOCK_AND_INODEOFFSET(bitPos,blockNum,inodeOffset)	\
do {						\
    blockNum = bitPos / GOSFS_DIR_ENTRIES_PER_BLOCK + 2;			\
    inodeOffset = bitPos % GOSFS_DIR_ENTRIES_PER_BLOCK;				\
} while (0)

#define FIND_BLOCK_NUM(filePoz, blockNum, blockOffset) \
do {						\
	blockNum = filePoz / GOSFS_FS_BLOCK_SIZE;				\
	blockOffset = filePoz % GOSFS_FS_BLOCK_SIZE;				\
}while(0)

#define FIND_SEC_IND_BLOCK_NUM(filePoz, blockNum, blockOffset) \
do {						\
	blockNum = filePoz / GOSFS_NUM_PTRS_PER_BLOCK;				\
	blockOffset = filePoz % GOSFS_NUM_PTRS_PER_BLOCK;				\
}while(0)

void Init_GOSFS_BootSector(struct FS_Buffer * gosfsBootSector)
{
	memset(gosfsBootSector->data, '\0', GOSFS_FS_BLOCK_SIZE);

	return ;
}

void Init_GOSFS_Instance(struct GOSFS_Instance * gosInstance, struct FS_Buffer * gosSuperBlock, struct Block_Device * dev)
{
	int i;
	gosInstance->magic = GOSFS_MAGIC;
	gosInstance->numLogicBlocks = GOSFS_NUM_BLOCK;
	gosInstance->numInodes = GOSFS_NUM_INODE;
	gosInstance->dev = dev;
	gosInstance->firstDataBlock = GOSFS_FIRST_DATA_BLOCK; // this is 102;
	
	memset(gosInstance->inodeBitmapVector, '\0', GOSFS_NUM_INODE_BITMAP_BYTES);
	gosInstance->inodeBitmapVector[0] = 3; // 0000 0011 first two nodes(0, 1) in use
	for(i = GOSFS_NUM_INODE_BITMAP_INUSE -1; i < GOSFS_NUM_INODE_BITMAP_BYTES; i++)
		gosInstance->inodeBitmapVector[i] = 0xff; // 11111111, in use
	
	memset(gosInstance->blockBitmapVector, '\0', GOSFS_NUM_BLOCK_BITMAP_BYTES);
	// ERROR:gosInstance->blockBitmapVector[0] = 1; // 0000 0001 first block(num 102) is for the root dir
	for(i = GOSFS_NUM_BLOCK_BITMAP_INUSE -1; i < GOSFS_NUM_BLOCK_BITMAP_BYTES; i++)
		gosInstance->blockBitmapVector[i] = 0xff; // 11111111 in use

	return;
}

void Init_GOSFS_Rootdir(struct GOSFS_Dir_Entry * rootDirEntry)
{
	int i = 0;
	Debug("	%x\n", (int)rootDirEntry);
	rootDirEntry->size = 0; // no file in root 
	char *rootname = "d";
	strcpy(rootDirEntry->filename, rootname);
	Debug("	filename:%x\n", (int)((rootDirEntry->filename)));
	rootDirEntry->flags = GOSFS_DIRENTRY_ISDIRECTORY | GOSFS_DIRENTRY_USED;
	for(i = 0; i < GOSFS_NUM_BLOCK_PTRS; i++)
		rootDirEntry->blockList[i] = 0;
	// ERROR: inode has no block until the last blockList is used;
	//rootDirEntry->blockList[0] = GOSFS_FIRST_DATA_BLOCK;

	return;
}

int Init_GOSFS_Dir_Entry(struct GOSFS_Dir_Entry * dirEntry, char* filename, ulong_t flags)
{
	int i;
	int fnamelen;
	
	if ((fnamelen = strlen(filename)) >= GOSFS_FILENAME_MAX)
		return ENAMETOOLONG;
	strcpy(dirEntry->filename, filename);
	Debug("  name:%s\n", dirEntry->filename);
	dirEntry->flags = flags;
	dirEntry->size = 0; // at first the size of the file is 0
	for(i = 0; i < GOSFS_DIR_ENTRIES_PER_BLOCK; i++)
		dirEntry->blockList[i] = 0; // no block is allocated to the file

	return 0;
}

// Allocate an inode from superBlock and initialize it
int Allocate_Inode(struct Mount_Point * mountPoint, struct GOSFS_Dir_Entry **pDirEntry, char * filename, ulong_t flags)
{
	struct GOSFS_Superblock *gosSuperBlock = (struct GOSFS_Superblock *)(mountPoint->fsData);
	struct GOSFS_Dir_Block *dirBlock;
	struct GOSFS_Dir_Entry *dirEntry = NULL;
	struct FS_Buffer *nodeBuffer;
	int inode, inodeBlock, inodeOffset;
	
	// see if superblock is locked and lock it
	Mutex_Lock(&gosSuperBlock->lock);

	// find free inode on disk and allocate
	inode = Find_First_Free_Bit(&(gosSuperBlock->gfsInstance.inodeBitmapVector), GOSFS_NUM_INODE);
	Set_Bit(&(gosSuperBlock->gfsInstance.inodeBitmapVector), inode);
	FIND_INODEBLOCK_AND_INODEOFFSET(inode, inodeBlock, inodeOffset);
	int rc = Get_FS_Buffer(gosfsBufferCache, inodeBlock, &nodeBuffer);
	if(rc < 0) return rc;
	dirBlock = (struct GOSFS_Dir_Block *)nodeBuffer->data;
	Init_GOSFS_Dir_Entry(&(dirBlock->entryTable[inodeOffset]), filename, flags);
	// ERROR: useless old struct field dirBlock->numExistEntry++;
	
	// allocate inode in mem
	dirEntry = (struct GOSFS_Dir_Entry *)Malloc(sizeof(struct GOSFS_Dir_Entry));
	if (dirEntry == NULL)
		return ENOMEM;
	memcpy(dirEntry, &(dirBlock->entryTable[inodeOffset]), sizeof(struct GOSFS_Dir_Entry));

	// write back the inode immediately
	Modify_FS_Buffer(gosfsBufferCache, nodeBuffer);
	Sync_FS_Buffer(gosfsBufferCache, nodeBuffer);
	Release_FS_Buffer(gosfsBufferCache, nodeBuffer);

	// write back the superblock later
	gosSuperBlock->dirty = true;

	Mutex_Unlock(&(gosSuperBlock->lock));

	*pDirEntry = dirEntry;

	return inode;
}

// get an existing Dir_Entry from disk
int Get_Exist_Inode(int inodeNum, struct GOSFS_Dir_Entry** pDirEntry)
{
	struct FS_Buffer *inodeBuf;
	struct GOSFS_Dir_Entry *dirEntry;
	struct GOSFS_Dir_Block *srcBlock;
	int inodeBlock, inodeOffset;

	// allocate the space of dirEntry
	dirEntry = (struct GOSFS_Dir_Entry *)Malloc(sizeof(struct GOSFS_Dir_Entry));
	if (dirEntry == NULL) return ENOMEM;

	// if inodeNum exist, find it and load it into dirEntry
	FIND_INODEBLOCK_AND_INODEOFFSET(inodeNum, inodeBlock, inodeOffset);
	int rc = Get_FS_Buffer(gosfsBufferCache, inodeBlock, &inodeBuf);
	if (rc < 0) return rc;
	srcBlock = (struct GOSFS_Dir_Block*)(inodeBuf->data);
	if(&srcBlock->entryTable[inodeNum] == NULL)
		return ENOTFOUND;
	memcpy(dirEntry, &(srcBlock->entryTable[inodeOffset]), sizeof(struct GOSFS_Dir_Entry));
	Release_FS_Buffer(gosfsBufferCache, inodeBuf);

	// We don't konw if it is a deleted inode
	// see code in Delete_GOSFS_Inode to get more information
	// an inode in inode block will only be marked as GOSFS_DIRENTRY_OLD during its deletion
	// so if we didn't check this flag, we may use a deleted inode
	if (dirEntry->flags & GOSFS_DIRENTRY_OLD)
	{
		*pDirEntry = NULL;
		return ENOTFOUND;
	}
	
	Debug("  exist:%s\n", dirEntry->filename);
	*pDirEntry = dirEntry;

	return 0;
}

// This function can be used in two ways:
// 	Init a newly created GOSFS_Dir_Entry on disk and init GOSFS_Inode
//	Load an exist GOSFS_Dir_Entry on disk and init GOSFS_Inode
// Watch out the parameters, some of them are used in different ways
struct GOSFS_Inode * Init_GOSFS_Inode(struct GOSFS_Dir_Entry *dirEntry, int inodeNum)
{
	struct GOSFS_Inode *gosInode;
	// struct GOSFS_Dir_Entry *dirEntry;

	// Allocate space for gosInode
	gosInode = (struct GOSFS_Inode *)Malloc(sizeof(struct GOSFS_Inode));
	
	// Initialize gosInode
	memcpy(&(gosInode->dirEntry), dirEntry, sizeof(struct GOSFS_Dir_Entry));
	gosInode->inodeNumber = inodeNum;
	gosInode->icount = 0;
	Mutex_Init(&(gosInode->lock));
	gosInode->iseek = 0;
	gosInode->dirty = false;
	Cond_Init(&gosInode->cond);

	return gosInode;	
}

void Init_Directory_Block(struct GOSFS_Dir_Block * dirBlock)
{
	int i;
	struct GOSFS_Dir_Entry * pEntry = dirBlock->entryTable;
	for(i = 0; i < GOSFS_DIR_ENTRIES_PER_BLOCK; i++)
	{
		pEntry = NULL;
		pEntry++;
	}
	// ERROR: old struct field dirBlock->numExistEntry = 0; // at the beginning the is no entry at all
}

void Init_VNode_List()
{
	Clear_VNode_List(&vnodeList);
}


// release direct block according to it's block number
int Release_Block(struct Mount_Point *mountPoint, ulong_t blockNum)
{
	struct GOSFS_Superblock *gosfsSuperBlock;
	struct FS_Buffer *blockBuf;
	int rc = 0;

	gosfsSuperBlock = (struct GOSFS_Superblock *)mountPoint->fsData;

	// fetch the superblock
	Mutex_Lock(&gosfsSuperBlock->lock);

	// get the block to release
	rc = Get_FS_Buffer(gosfsBufferCache, blockNum, &blockBuf);
	if (rc < 0) { Mutex_Unlock(&gosfsSuperBlock->lock); return rc;}
	blockBuf->flags |= FS_BUFFER_OLD;
	Release_FS_Buffer(gosfsBufferCache, blockBuf);

	// modify the superblock
	Set_Bit(gosfsSuperBlock->gfsInstance.blockBitmapVector, blockNum);

	gosfsSuperBlock->dirty = true;

	Mutex_Unlock(&gosfsSuperBlock->lock);

	return rc;
}


// release first indirect block's entries according to it's block numbers and the first indirect block
int Release_First_Indirect_Block(struct Mount_Point *mountPoint, ulong_t blockNum)
{
	struct GOSFS_Indirect_Block *indBlock;
	struct FS_Buffer *blockBuf;
	int i, rc;

	i = 0;
	// Fetch the first indirect block
	rc = Get_FS_Buffer(gosfsBufferCache, blockNum, &blockBuf);
	if (rc < 0) return rc;
	indBlock = (struct GOSFS_Indirect_Block *)blockBuf->data;

	// release the entries
	for(i = 0; i < GOSFS_NUM_PTRS_PER_BLOCK; i++)
	{
		if(indBlock->blockNumber[i] > 0)
		{
			rc = Release_Block(mountPoint, indBlock->blockNumber[i]);
			if (rc < 0) { Release_FS_Buffer(gosfsBufferCache, blockBuf); return rc; }
		}
	}

	Release_FS_Buffer(gosfsBufferCache, blockBuf);
	Release_Block(mountPoint, blockNum); // release the block

	
	return rc;
}


// release the second indirect block's entries and the block
int Release_Second_Indirect_Block(struct Mount_Point *mountPoint, ulong_t blockNum)
{
	struct GOSFS_Indirect_Block *indBlock;
	struct FS_Buffer *blockBuf;
	int i, rc;

	i = rc = 0;

	// Fetch the first indirect block
	rc = Get_FS_Buffer(gosfsBufferCache, blockNum, &blockBuf);
	if (rc < 0) return rc;
	indBlock = (struct GOSFS_Indirect_Block *)blockBuf->data;


	// release the entries
	for(i = 0; i < GOSFS_NUM_PTRS_PER_BLOCK; i++)
	{
		if(indBlock->blockNumber[i] > 0)
		{
			rc = Release_First_Indirect_Block(mountPoint, indBlock->blockNumber[i]);
			if (rc < 0) { Release_FS_Buffer(gosfsBufferCache, blockBuf); return rc; }
		}
	}

	Release_FS_Buffer(gosfsBufferCache, blockBuf);
	Release_Block(mountPoint, blockNum); // release the block

	
	return rc;

}


// Delete Inode
int Delete_GOSFS_Inode(struct Mount_Point *mountPoint, ulong_t inodeNum)
{
	struct GOSFS_Superblock *gosfsSuperBlock;
	struct FS_Buffer *nodeBuf;
	struct GOSFS_Dir_Block *nodeBlock = NULL;
	int inodeBlock, inodeOffset;
	int rc = 0;

	gosfsSuperBlock = (struct GOSFS_Superblock *)mountPoint->fsData;

	// First release the inode in inode block
	FIND_INODEBLOCK_AND_INODEOFFSET(inodeNum, inodeBlock, inodeOffset);
	rc = Get_FS_Buffer(gosfsBufferCache, inodeBlock, &nodeBuf);
	if (rc < 0) goto done;
	nodeBlock = (struct GOSFS_Dir_Block *)nodeBuf->data;
	nodeBlock->entryTable[inodeOffset].flags &= GOSFS_DIRENTRY_OLD;
	Modify_FS_Buffer(gosfsBufferCache, nodeBuf);
	Sync_FS_Buffer(gosfsBufferCache, nodeBuf);
	Release_FS_Buffer(gosfsBufferCache, nodeBuf);
	
	Mutex_Lock(&gosfsSuperBlock->lock);

	// Then set the bit in inode bitmap
	Set_Bit(gosfsSuperBlock->gfsInstance.inodeBitmapVector, inodeNum);
	gosfsSuperBlock->dirty = true;

	Mutex_Unlock(&gosfsSuperBlock->lock);


done:
	return rc;
}

// Zero given block in buffer;
// as we allocate, write, then release one block, this block may be dirty with expired data
// next time we allocate this block to other program, the dirty data still there;
// this may cause some unexpected problems.
// we use this in Allocate_Block only
// for speed reason, we only clear the block in mem(i.e, the buffer).
int Clear_Block(ulong_t blockNum)
{
	int i = 0;
	struct GOSFS_Indirect_Block *indBlock;
	struct FS_Buffer *blockBuf;

	int rc = Get_FS_Buffer(gosfsBufferCache, blockNum, &blockBuf);
	if(rc < 0) goto done;
	indBlock = (struct GOSFS_Indirect_Block*)blockBuf->data;
	for(i = 0; i < GOSFS_NUM_PTRS_PER_BLOCK; i++)
		indBlock->blockNumber[i] = 0;

done:
	Release_FS_Buffer(gosfsBufferCache, blockBuf);
	return rc;
}

// Allocate a direct block
// set blockNumEntry to be the block number allocated from disk
int Allocate_Block(struct Mount_Point *mountPoint, ulong_t *blockNumEntry)
{
	if(*blockNumEntry > 0) // if allocated already, don't have to allocate a new block
		return *blockNumEntry;

	struct GOSFS_Superblock *gosfsSuperBlock;
	int blockBit;
	gosfsSuperBlock = (struct GOSFS_Superblock *)mountPoint->fsData;
	
	Mutex_Lock(&gosfsSuperBlock->lock);
	// First find block bit to allocate
	blockBit = Find_First_Free_Bit(gosfsSuperBlock->gfsInstance.blockBitmapVector, GOSFS_NUM_BLOCK);

	// allocate block in file
	*blockNumEntry = blockBit + GOSFS_FIRST_DATA_BLOCK;

	// update superblock
	Set_Bit(gosfsSuperBlock->gfsInstance.blockBitmapVector, blockBit);
	gosfsSuperBlock->dirty = true;
	
	Mutex_Unlock(&gosfsSuperBlock->lock);

	Debug("blockNum:%d, blockBit:%d\n", (int)(*blockNumEntry), (int)blockBit);
	Clear_Block(*blockNumEntry);
	return *blockNumEntry;
}


// 
int Allocate_First_Indirect_Block(struct Mount_Point *mountPoint, struct GOSFS_Inode *iNode, ulong_t blockNum)
{
	struct GOSFS_Dir_Entry *dirEntry = &iNode->dirEntry;
	int fIndBlock;
	if(dirEntry->blockList[8] == 0)
	{
		fIndBlock = Allocate_Block(mountPoint, &dirEntry->blockList[8]);
		iNode->dirty = true;
	}
	else
		fIndBlock = dirEntry->blockList[8];

	struct GOSFS_Indirect_Block *IndBlock;
	struct FS_Buffer *blockBuf;
	int rc;
	
	rc = Get_FS_Buffer(gosfsBufferCache, fIndBlock, &blockBuf);
	IndBlock = (struct GOSFS_Indirect_Block *)blockBuf->data;
	if(IndBlock->blockNumber[blockNum] == 0)
	{	rc = Allocate_Block(mountPoint, &IndBlock->blockNumber[blockNum]);
		blockBuf->flags |= FS_BUFFER_DIRTY; // write back later sometime
	}else
		rc = IndBlock->blockNumber[blockNum];
	Release_FS_Buffer(gosfsBufferCache, blockBuf);

	return rc;		
}

int Allocate_Second_Indirect_Block(struct Mount_Point *mountPoint, struct GOSFS_Inode *iNode, ulong_t blockNum)
{
	struct GOSFS_Dir_Entry *dirEntry = &iNode->dirEntry;
	int sIndBlock;
	if(dirEntry->blockList[9] > 0)
		sIndBlock = dirEntry->blockList[9];
	else
	{
		Debug(" !Allocate second ind block.\n");
		sIndBlock = Allocate_Block(mountPoint, &dirEntry->blockList[9]);
		iNode->dirty = true;
	}
		

	struct GOSFS_Indirect_Block *IndBlock = NULL;
	struct FS_Buffer *blockBuf;
	int sIndNum, fIndNum;
	int sIndBlockNum = 0;
	int rc;

	Debug(" !Allocate first ind block.\n");
	FIND_SEC_IND_BLOCK_NUM(blockNum, sIndNum, fIndNum);
	rc = Get_FS_Buffer(gosfsBufferCache, sIndBlock, &blockBuf);
	Print("sIndNum:%d, fIndNum:%d\n",sIndNum, fIndNum);
	if(rc < 0) return rc;
	IndBlock = (struct GOSFS_Indirect_Block *)blockBuf->data;

	if(IndBlock->blockNumber[sIndNum] == 0)
	{
		sIndBlockNum = Allocate_Block(mountPoint, &IndBlock->blockNumber[sIndNum]);
		blockBuf->flags |= FS_BUFFER_DIRTY;
	}else{
	 Debug("already have first ind block:%d\n", (int)IndBlock->blockNumber[sIndNum]);
	 sIndBlockNum = IndBlock->blockNumber[sIndNum];
	}
	Release_FS_Buffer(gosfsBufferCache, blockBuf);

	Debug(" !Allocate direct block.\n");
	rc = Get_FS_Buffer(gosfsBufferCache, sIndBlockNum, &blockBuf);
	IndBlock = (struct GOSFS_Indirect_Block *)blockBuf->data;
	if(IndBlock->blockNumber[fIndNum] == 0)
	{
		sIndBlockNum = Allocate_Block(mountPoint, &IndBlock->blockNumber[fIndNum]);
		blockBuf->flags |= FS_BUFFER_DIRTY;
	}else{
		sIndBlockNum = IndBlock->blockNumber[fIndNum];
	}
	Release_FS_Buffer(gosfsBufferCache, blockBuf);

	return sIndBlockNum;
}


// Get first indirect Block; this function is for supportment of GOSFS_Read
int Get_First_Indirect_Block(struct GOSFS_Dir_Entry* dirEntry, ulong_t directBlock)
{
	if(dirEntry->blockList[8] == 0)
		return ENOBLOCK;

	struct GOSFS_Indirect_Block *dirBlock;
	struct FS_Buffer *blockBuf;
	ulong_t fIndNum;
	//int fIndBlock = directBlock - GOSFS_NUM_DIRECT_BLOCKS;
	int rc = Get_FS_Buffer(gosfsBufferCache, dirEntry->blockList[8], &blockBuf);
	dirBlock = (struct GOSFS_Indirect_Block *)blockBuf->data;
	fIndNum = dirBlock->blockNumber[directBlock];

	if(fIndNum == 0)
	{
		rc = ENOBLOCK;
		goto done;
	}

	rc = fIndNum;
	
done:

	Release_FS_Buffer(gosfsBufferCache, blockBuf);
	return rc;
}

int Get_Second_Indirect_Block(struct GOSFS_Dir_Entry* dirEntry, ulong_t directBlock)
{
	Debug("read sec ind block.\n");
	if(dirEntry->blockList[9] == 0)
		return ENOBLOCK;

	struct GOSFS_Indirect_Block *dirBlock;
	struct FS_Buffer *blockBuf;
	int sIndNum, sIndOffset;
	int sIndBlock, fIndBlock;
	// First get second indirect block
	//int sIndBlockNum = directBlock - GOSFS_NUM_DIRECT_BLOCKS - GOSFS_NUM_PTRS_PER_BLOCK;
	FIND_SEC_IND_BLOCK_NUM(directBlock, sIndNum, sIndOffset);
	int rc = Get_FS_Buffer(gosfsBufferCache, dirEntry->blockList[9], &blockBuf);
	if(rc < 0)  goto done;
	dirBlock = (struct GOSFS_Indirect_Block *)blockBuf->data;
	sIndBlock = dirBlock->blockNumber[sIndNum];
	if(sIndBlock == 0)
	{
		rc = ENOBLOCK;
		goto done;
	}
	Release_FS_Buffer(gosfsBufferCache, blockBuf);

	// Find first indirect block
	rc = Get_FS_Buffer(gosfsBufferCache, sIndBlock, &blockBuf);
	if(rc < 0) goto done;
	dirBlock = (struct GOSFS_Indirect_Block *)blockBuf->data;
	fIndBlock = dirBlock->blockNumber[sIndOffset];
	if(fIndBlock == 0)  
		rc = ENOBLOCK;
	rc = fIndBlock;
done:
	Release_FS_Buffer(gosfsBufferCache, blockBuf); 
	return rc;
}

/* ----------------------------------------------------------------------
 * Implementation of VFS operations
 * ---------------------------------------------------------------------- */

/*
 * Get metadata for given file.
 */
static int GOSFS_FStat(struct File *file, struct VFS_File_Stat *stat)
{
	struct GOSFS_Inode *iNode;
	iNode = (struct GOSFS_Inode *)file->fsData;
	
	stat->isDirectory = iNode->dirEntry.flags & GOSFS_DIRENTRY_ISDIRECTORY ? true : false;
	stat->isSetuid = iNode->dirEntry.flags & GOSFS_DIRENTRY_SETUID ? true : false;
	stat->size = iNode->dirEntry.size;

	return 0;
}

/*
 * Read data from current position in file.
 */
// Notice: Because seek may let the filePos point to random position,
// we have to check the data before start to read, to make sure there exist the data;
// if there is no data, or not enough data compared with numBytes, return ENODATA

static int GOSFS_Read(struct File *file, void *buf, ulong_t numBytes)
{
	Debug("gosfs read:\n");
	if(!(file->mode & O_READ)) return EUNSUPPORTED;
	if(sizeof(buf) < 0 || buf == NULL || numBytes<= 0)
	{ Debug("invalid pra.\n");	return EINVALID;}
//ERROR:if((file->filePos + numBytes) > file->endPos || file->filePos > file->endPos)
//	{ Print("no data.\n");	return ENODATA;}
	if(file->endPos == 0 || file->filePos >= file->endPos) // Empty file
		return ENOBLOCK;

	struct FS_Buffer *blockBuf;
	struct GOSFS_Dir_Block *dirBlock;
	struct GOSFS_Inode *iNode;
	struct GOSFS_Dir_Entry *dirEntry;
	struct Mount_Point *mountPoint;
	struct GOSFS_Superblock *gosfsSuperBlock;
	int blockNum, blockOffset;
	int inodeBlock, inodeOffset;
	int readSize, readBytes;
	int readBlock;
	int rc;
	char *pbuf = (char *)buf;
	char *pblock;
	
	// Init
	iNode = (struct GOSFS_Inode *)file->fsData;
	dirEntry = &iNode->dirEntry;
	mountPoint = file->mountPoint;
	gosfsSuperBlock = (struct GOSFS_Superblock*)mountPoint->fsData;
	blockBuf = NULL;
	dirBlock = NULL;
	blockNum = blockOffset = inodeBlock = inodeOffset = readSize = readBytes = readBlock = 0;
	rc = 0;

	Debug("start reading:\n");
	while(numBytes > 0)
	{
		FIND_BLOCK_NUM(file->filePos, blockNum, blockOffset);
		if(blockNum >= GOSFS_NUM_DIRECT_BLOCKS)
		{
			if(blockNum >= GOSFS_NUM_DIRECT_BLOCKS + GOSFS_NUM_PTRS_PER_BLOCK &&
				blockNum < GOSFS_NUM_TOTAL_BLOCKS)
				readBlock = Get_Second_Indirect_Block(dirEntry, blockNum-(GOSFS_NUM_DIRECT_BLOCKS + GOSFS_NUM_PTRS_PER_BLOCK));
			else if(blockNum < (GOSFS_NUM_DIRECT_BLOCKS +GOSFS_NUM_PTRS_PER_BLOCK))
				readBlock = Get_First_Indirect_Block(dirEntry, blockNum-GOSFS_NUM_DIRECT_BLOCKS);
			else{
				rc = ENOBLOCK;
				break;
			}
		}
		else if(blockNum >= 0 && dirEntry->blockList[blockNum] == 0)
		{
			rc = ENOBLOCK;
			break;
		}
		else
			readBlock = dirEntry->blockList[blockNum];

		Debug("readblock:%d\n", readBlock);
		// check if the block is allocated
		Mutex_Lock(&gosfsSuperBlock->lock);
		rc = Is_Bit_Set(gosfsSuperBlock->gfsInstance.blockBitmapVector, readBlock - GOSFS_FIRST_DATA_BLOCK);
		if(rc == 0)
			return EOLDBLOCK;
		Mutex_Unlock(&gosfsSuperBlock->lock);

		// Read the block
		rc = Get_FS_Buffer(gosfsBufferCache, readBlock, &blockBuf);
		if(rc < 0) return rc;
		pblock = (char *)blockBuf->data;
		pblock += blockOffset;
		readSize = numBytes >= (GOSFS_FS_BLOCK_SIZE-blockOffset) ? (GOSFS_FS_BLOCK_SIZE-blockOffset) : numBytes;
		memcpy(pbuf, pblock, readSize);
		Debug("readsize:%d\n", readSize);
		// update relevent information
		numBytes -= readSize;
		//pblock += readSize;
		pbuf += readSize;
		file->filePos += readSize;
		readBytes +=readSize;
		Release_FS_Buffer(gosfsBufferCache, blockBuf);
	}


	if(rc == ENOBLOCK && readBytes == 0)
		readBytes = rc;

	Debug("gosfs read buf:%s\n", (char*)buf);
	return readBytes;
}

/*
 * Write data to current position in file.
 */
// Write from current file position;
// Consider the fact that one block is 4KB and is big enough
// so we won't allocate more than one block each time we need a block
// we decrease the total bytes after write one block of bytes 
// if the value left still above zero, we need to allocate a new block
//static int wc = 0;
static int GOSFS_Write(struct File *file, void *buf, ulong_t numBytes)
{
	// First do a little check
	Debug("start of GOSFS_Write:%s\n", (char*)buf);
	if(sizeof(buf) < 0 || buf == NULL || numBytes<= 0)
		return EINVALID;
	if(!(file->mode & O_WRITE))
	{
		Debug("file %x mode %x\n", (int)file, (int)file->mode);
		return EUNSUPPORTED;
	}

	/*test
	int i;
	char *buffer = (char *)buf;
	for(i = 0; i < 100; i++)
		if(buffer[i] != i)
		{
			Print("ERROR BEFORE WRITING:buf[%d]=%d\n", buffer[i], i);
			while(1);
		}
	*/

	struct FS_Buffer *blockBuf;
	struct GOSFS_Dir_Block *dirBlock;
	struct GOSFS_Inode *iNode;
	struct GOSFS_Dir_Entry *dirEntry;
	struct Mount_Point *mountPoint;
	int blockNum, blockOffset, writeBlock;
	int inodeBlock, inodeOffset;
	int writeSize, writeBytes;
	int rc;
	char *pbuf = (char *)buf;
	char *pblock;

	// Init
	iNode = (struct GOSFS_Inode *)file->fsData;
	dirEntry = &iNode->dirEntry;
	mountPoint = file->mountPoint;
	blockBuf = NULL;
	dirBlock = NULL;
	blockNum = blockOffset = writeBlock = inodeBlock = inodeOffset = writeSize = writeBytes = 0;
	rc = 0;
	
	Debug("start writing:\n");
	while(numBytes > 0)
	{
		FIND_BLOCK_NUM(file->filePos, blockNum, blockOffset);
		Debug("blockNum:%d, blockOff:%d\n", blockNum, blockOffset);
		// allocate the block
		if(blockNum >= GOSFS_NUM_DIRECT_BLOCKS)
		{
			if(blockNum >= GOSFS_NUM_DIRECT_BLOCKS + GOSFS_NUM_PTRS_PER_BLOCK &&
				blockNum < GOSFS_NUM_TOTAL_BLOCKS)
				writeBlock = Allocate_Second_Indirect_Block(mountPoint, iNode, blockNum-(GOSFS_NUM_DIRECT_BLOCKS + GOSFS_NUM_PTRS_PER_BLOCK));
			else if(blockNum < (GOSFS_NUM_DIRECT_BLOCKS +GOSFS_NUM_PTRS_PER_BLOCK))
				writeBlock = Allocate_First_Indirect_Block(mountPoint, iNode, blockNum-GOSFS_NUM_DIRECT_BLOCKS);
			else
				return ENOBLOCK;
		}
		else if(blockNum >= 0 && dirEntry->blockList[blockNum] == 0)
		{
			writeBlock = Allocate_Block(mountPoint, &dirEntry->blockList[blockNum]);
			iNode->dirty = true;
			Debug("direct block.\n");
		}
		else
			writeBlock = dirEntry->blockList[blockNum];
		

		Debug("writeBlock:%d\n", writeBlock);
		//if(writeBlock == 109) wc++;
		// update relevent structures: GOSFS_Inode, GOSFS_Dir_Entry on disk
		if(iNode->dirty == true)
		{
			FIND_INODEBLOCK_AND_INODEOFFSET(iNode->inodeNumber, inodeBlock, inodeOffset);
			rc = Get_FS_Buffer(gosfsBufferCache, inodeBlock, &blockBuf);
			if (rc < 0) return rc;
			dirBlock = (struct GOSFS_Dir_Block *)blockBuf->data;
			memcpy(&dirBlock->entryTable[inodeOffset], dirEntry, sizeof(struct GOSFS_Dir_Entry));
			Modify_FS_Buffer(gosfsBufferCache, blockBuf);
			Sync_FS_Buffer(gosfsBufferCache, blockBuf);
			Release_FS_Buffer(gosfsBufferCache, blockBuf);

			iNode->dirty = false;
		}
		
		// Write buf to block
		writeSize = numBytes >= (GOSFS_FS_BLOCK_SIZE-blockOffset) ? (GOSFS_FS_BLOCK_SIZE-blockOffset) : numBytes;
		// get the target block and write
		rc = Get_FS_Buffer(gosfsBufferCache, writeBlock, &blockBuf);
		if (rc < 0) return rc;
		pblock = (char*)blockBuf->data;
		pblock += blockOffset;
		Debug("copy to block:%d\n", writeBlock);
		memcpy(pblock, pbuf, writeSize);

		// update relevent data
		numBytes -= writeSize;
		writeBytes += writeSize;
		file->filePos += writeSize;
		if(file->filePos > file->endPos) file->endPos = file->filePos;
		pbuf += writeSize;
		Modify_FS_Buffer(gosfsBufferCache, blockBuf); // just modify ,but don't have to write back immediately
		Release_FS_Buffer(gosfsBufferCache, blockBuf);
	}

	// update the size of the file
	dirEntry->size += writeBytes;
	FIND_INODEBLOCK_AND_INODEOFFSET(iNode->inodeNumber, inodeBlock, inodeOffset);
	rc = Get_FS_Buffer(gosfsBufferCache, inodeBlock, &blockBuf);
	if (rc < 0) return rc;
	dirBlock = (struct GOSFS_Dir_Block *)blockBuf->data;
	memcpy(&dirBlock->entryTable[inodeOffset], dirEntry, sizeof(struct GOSFS_Dir_Entry));
	Modify_FS_Buffer(gosfsBufferCache, blockBuf);
	Sync_FS_Buffer(gosfsBufferCache, blockBuf);
	Release_FS_Buffer(gosfsBufferCache, blockBuf);

	// Check the write content
	
	return writeBytes;
}

/*
 * Seek to a position in file.
 */
static int GOSFS_Seek(struct File *file, ulong_t pos)
{
	if (pos >= 0)
	    file->filePos = pos;
	else
		return EINVALID;

	Debug("seek pos: %d\n", (int)file->filePos);
    return file->filePos;
}

/*
 * Close a file.
 */
static int GOSFS_Close(struct File *file)
{
	// i don't know if there is any difference at present
	struct GOSFS_Inode *iNode = NULL;
	struct File *vNode = NULL;
	int i = 0;
	iNode = (struct GOSFS_Inode *)file->fsData;
	if(iNode->inodeNumber == GOSFS_ROOT_INODE_NUM)
	{	
		Debug("root dir.\n");
		file->filePos = 0;	
		return EUNSUPPORTED;
	}

	if(g_currentThread->userContext != 0)
	{
		for(i = 0; i < USER_MAX_FILES; i++)
		{
			if(g_currentThread->userContext->fileList[i] == file)
			{
				if(file == 0) { i = ENOTFOUND; break;}
					
				Print("	Close opened file.\n");
				file->filePos = 0;// clear file pos
				iNode = (struct GOSFS_Inode *)file->fsData;
				iNode->icount--;
				g_currentThread->userContext->fileList[i] = 0;
				Debug("close file rc:%d\n", g_currentThread->userContext->fileCount);
				g_currentThread->userContext->fileCount--;
				break;
			}
		}

		if(i == USER_MAX_FILES) { i = ENOTFOUND; }
	}
	else if(g_currentThread->userContext == 0){ // kernel thread
		vNode = Get_Front_Of_VNode_List(&vnodeList);
		while(vNode != 0)
		{
			if(vNode == file)
			{
				iNode = (struct GOSFS_Inode *)file->fsData;
				if(iNode->icount > 0) iNode->icount--;
				vNode->filePos = 0;
				Debug("	Close opened file.\n");
				break;
			}
			vNode = Get_Next_In_VNode_List(vNode);
		}
	}
	
	Debug("	In close file pos:%d\n", (int)file->filePos);
	return i;
}


/*static*/ struct File_Ops s_gosfsFileOps = {
    &GOSFS_FStat,
    &GOSFS_Read,
    &GOSFS_Write,
    &GOSFS_Seek,
    &GOSFS_Close,
    0, /* Read_Entry */
};

/*
 * Stat operation for an already open directory.
 */
static int GOSFS_FStat_Directory(struct File *dir, struct VFS_File_Stat *stat)
{
	Print("gosfs fstat dir.\n");
	struct GOSFS_Inode *iNode;
	iNode = (struct GOSFS_Inode *)dir->fsData;
	if(!(iNode->dirEntry.flags & GOSFS_DIRENTRY_ISDIRECTORY))
		return ENOTDIR;

	stat->isDirectory = 1;
	stat->size = iNode->dirEntry.size;

	return 0;
}

/*
 * Directory Close operation.
 */
static int GOSFS_Close_Directory(struct File *dir)
{
	// struct GOSFS_Inode *iNode, *dirNode;
	return GOSFS_Close(dir);
}

/*
 * Read a directory entry from an open directory.
 */
// The entry to be read is defined by dir->filePos
// so after we read one entry, we have to increase the filePos by one
static int GOSFS_Read_Entry(struct File *dir, struct VFS_Dir_Entry *entry)
{
	struct GOSFS_Inode *iNode = NULL;
	struct GOSFS_Dir_Entry *dirEntry = NULL;
	int rc = 0;
	int i = 0;
	if(dir->filePos == GOSFS_NUM_DIR_ENTRY) // Read beyond max number of entries
	{
		rc = 1; //no more entry to read
		dir->filePos = 0;
		goto failed;
	}

	Debug("No.%d entry.\n" , (int)dir->filePos);
	
	iNode = (struct GOSFS_Inode *)dir->fsData;
	if(dir->filePos == 0)
		for(i = dir->filePos; i < GOSFS_NUM_DIR_ENTRY; i++)
		{
			if(iNode->dirEntry.blockList[i] != 0)
			{ dir->filePos = i; break; }
		}
	if(i == GOSFS_NUM_DIR_ENTRY)
	{ rc = ENOTFOUND; goto failed; }
	
	rc = Get_Exist_Inode(iNode->dirEntry.blockList[dir->filePos], &dirEntry);
	if(rc < 0) { Print("failed reading entry.\n"); goto failed;}
	strcpy(entry->name, dirEntry->filename);
	Debug("entry name:%s, entry:%x\n", entry->name, (int)dirEntry);
	Debug("direntry:no.%d, %s\n", (int)iNode->dirEntry.blockList[dir->filePos], dirEntry->filename);
	entry->stats.isDirectory = (dirEntry->flags & GOSFS_DIRENTRY_ISDIRECTORY) ? true:false;
	entry->stats.isSetuid = (dirEntry->flags & GOSFS_DIRENTRY_SETUID) ? true : false;
	entry->stats.size = dirEntry->size;
	// ERROR:dir->filePos++;
	i = dir->filePos + 1;
	while(i < GOSFS_NUM_DIR_ENTRY )
	{
		if(iNode->dirEntry.blockList[i] != 0)
		{
			dir->filePos = i;
			break;
		}

		i++;
	}

	if(i == GOSFS_NUM_DIR_ENTRY)
		dir->filePos = i;


failed:
	if(dirEntry != NULL)
		Free(dirEntry);

	return rc;
}

/*static*/ struct File_Ops s_gosfsDirOps = {
    &GOSFS_FStat_Directory,
    0, /* Read */
    0, /* Write */
    &GOSFS_Seek, //ERROR;fixed
    &GOSFS_Close_Directory,
    &GOSFS_Read_Entry,
};

// Create a normal file
// then open it
static int GOSFS_Create_File(struct Mount_Point *mountPoint, const char *path, int mode, struct File **pFile)
{
	// First of all check if the user can afford another opened file
	if (g_currentThread->userContext != 0)
	{
		if(g_currentThread->userContext->fileCount == USER_MAX_FILES)
			return EMFILE;
	}

	Print("gosfs create file:\n");
	struct GOSFS_Dir_Entry * tempEntry, *newEntry, *wasteEntry;
	struct GOSFS_Inode * iNode = NULL;
	struct FS_Buffer *nodeBuf;
	struct GOSFS_Dir_Block *dirBlock;
	struct File *vNode, *fileNode = NULL;
	const char *suffix;
	char * pPath;
	char prefix[GOSFS_FILENAME_MAX + 1];
	void* tempPath;
	bool found = false;
	bool packPath = false;
	int inodeBlock, inodeOffset, inodeNum, fDirNum;
	int i, rc;

	tempEntry = newEntry = wasteEntry = NULL;
	inodeBlock = inodeOffset = inodeNum = fDirNum = i = rc = 0; //all init as zero, or there will be warnings
	tempPath = Malloc(strlen(path));
	if (tempPath == NULL)
		return ENOMEM;
	pPath = tempPath;
	strcpy(pPath, path);

	rc = Get_Exist_Inode(GOSFS_ROOT_INODE_NUM, &tempEntry);
	fDirNum = GOSFS_ROOT_INODE_NUM; // ERROR: default is root dir

	//--------------------------------------------
	// First find father dir
	while((packPath = Unpack_Path(pPath, prefix, &suffix)) == true)
	{
		if(strcmp(suffix, "/") == 0)
		{
			Debug(" suffix err.\n");
			break;
		}
		if (tempEntry->size == 0 || !(tempEntry->flags & GOSFS_DIRENTRY_ISDIRECTORY)) // empty dir
		{
			Debug("	size:%d\n", (int)tempEntry->size);
			Debug("	flags:%d\n", (int)tempEntry);
			Print("	Empty Dir or No Dir.\n");
			rc = ENOTFOUND;
			goto failed;
		}
		// start searching
		for (i = 0; i < tempEntry->size; i++)
		{
			Get_Exist_Inode(tempEntry->blockList[i], &newEntry);
			if(strcmp(prefix, newEntry->filename) == 0)
			{
				fDirNum = tempEntry->blockList[i];
				Free(tempEntry);
				tempEntry = newEntry;
				found = true;
				break;
			}
			// not found in this round
			Free(newEntry);
		}
		
		// check if we found target dir
		if(found)
			found = false;
		else
		{
			Print("	not in this directory:%s", tempEntry->filename);
			rc = ENOTFOUND;
			goto failed;
		}
		strcpy(pPath, suffix); 
	}
	// now we've got the father dir;
	//if(packPath == false)
		

	Debug("	father dir:%s\n", tempEntry->filename);
	Debug("	target file:%s\n", prefix);
	// check if we can afford a new file
	// check if the user can afford another opened file
	if(tempEntry->size >= USER_MAX_FILES)
	{
		rc = EMFILE;
		goto failed;
	}



	//----------------------------------
	// make sure it has a unique name
	if(tempEntry->size > 0)
	for (i = 0; i < tempEntry->size; i++)
	{
		Get_Exist_Inode(tempEntry->blockList[i], &newEntry);
		if(strcmp(prefix, newEntry->filename) == 0)
		{
			//fDirNum = tempEntry->blockList[i];
			//Free(tempEntry);
			//tempEntry = newEntry;
			found = true;
			break;
		}
		// not found in this round
		Free(newEntry);
	}

	if(found == true)
	{
		rc = EEXIST;
		Free(newEntry);
		goto failed;
	}

	//--------------------------------------
	// start creating file-GOSFS_Dir_Entry
	inodeNum = Allocate_Inode(mountPoint, &wasteEntry, prefix, 0);
	if(inodeNum < 0) //not successfully allocated
	{
		rc = inodeNum;
		Free(wasteEntry);
		goto failed;
	}
	Print("	inodeNum:%d\n", inodeNum);

	// create the in-mem structures-GOSFS_Inode
	iNode = Init_GOSFS_Inode(wasteEntry, inodeNum);
	if (iNode == NULL)
	{
		Debug("	Failed init iNode.\n");
		goto failed;
	}
	iNode->icount++;

	// now we've got the GOSFS_Inode; create File(VNode);
	// filepos = endpos = 0
	Debug("	Allocate File.\n");
		vNode = Allocate_File(&s_gosfsFileOps, 0, 0, iNode, mode, mountPoint);
	if (vNode == NULL)
	{
		Debug("	Failed allocating vNode.\n");
		Free(iNode);
		goto failed;
	}
	else{
		Debug("vNode:%x\n", (int)vNode);
		Debug("vNode->pos:%d\n", (int)vNode->filePos);
		Debug("vNode->ops->write:%x\n", (int)vNode->ops->Write);
		fileNode = vNode;
	}

	Add_To_Back_Of_VNode_List(&vnodeList, vNode);

	//--------------------------------------
	// at last update the father Dir;
	// first check if the father Dir remains in the list
	Debug(" UpDate father Dir.\n");
	vNode = Get_Front_Of_VNode_List(&vnodeList);
	while(vNode != NULL)
	{
		Print("	test.\n");
		iNode = (struct GOSFS_Inode *)vNode->fsData;
		if(fDirNum == iNode->inodeNumber)
		{
			Debug("	Father Dir already in list.\n");
			for(i = 0; i < GOSFS_DIR_ENTRIES_PER_BLOCK; i++)
			{	if(iNode->dirEntry.blockList[i] == 0) break; }
			iNode->dirEntry.blockList[i] = inodeNum;
			iNode->dirEntry.size++;
			iNode->dirty = true; // although we set the dirty bit, we still have to write back right now
			break;
		}

		vNode = Get_Next_In_VNode_List(vNode);
	}

	// Write back the changed father inode
	FIND_INODEBLOCK_AND_INODEOFFSET(fDirNum, inodeBlock, inodeOffset);
	rc = Get_FS_Buffer(gosfsBufferCache, inodeBlock, &nodeBuf);
	if (rc < 0) return rc;
	dirBlock = (struct GOSFS_Dir_Block *)nodeBuf->data;
	for(i = 0; i < GOSFS_DIR_ENTRIES_PER_BLOCK; i++)
	{	if(dirBlock->entryTable[inodeOffset].blockList[i] == 0) break; }
	dirBlock->entryTable[inodeOffset].blockList[i] = inodeNum;
	dirBlock->entryTable[inodeOffset].size++;
	Debug("	size:%d\n", (int)dirBlock->entryTable[inodeOffset].size);
	Modify_FS_Buffer(gosfsBufferCache, nodeBuf);
	Sync_FS_Buffer(gosfsBufferCache, nodeBuf);
	Release_FS_Buffer(gosfsBufferCache, nodeBuf);


	//--------------------------
	// add to the user list if the caller is a user thread
	if (g_currentThread->userContext != 0)
	{
		for(i = 0; i < GOSFS_NUM_DIR_ENTRY; i++)
		{ if(g_currentThread->userContext->fileList[i] == 0) break; }
		g_currentThread->userContext->fileList[i] = fileNode;
		rc = i;
		Debug("gosfs create rc:%d\n", rc);
		g_currentThread->userContext->fileCount++;
	}

	*pFile = fileNode;
	Free(tempEntry); // father dir
	Free(tempPath); 


	Print(" finished.\n");
	return rc;


failed:
	Print("failed.\n");	
			Free(tempPath);
			Free(tempEntry);
			return rc;
}


/*
 * Create a directory named by given path.
 */
// ERROR: fixed
static int GOSFS_Create_Directory(struct Mount_Point *mountPoint, const char *path)
{
	struct GOSFS_Inode *iNode = NULL;
	struct GOSFS_Dir_Entry * newEntry, *tempEntry = 0, *wasteEntry = 0;
	struct FS_Buffer * nodeBuf;
	struct GOSFS_Dir_Block * dirBlock = NULL;
	struct File* vNode = NULL;
	int inodeBlock, inodeOffset, inodeNum, fDirNum;
	const char *suffix;
	char * pPath;
	char prefix[GOSFS_FILENAME_MAX + 1];
	void* tempPath;
	
	int i=0, rc;
	bool found = false;
	inodeBlock = inodeOffset = inodeNum = 0;
	fDirNum = GOSFS_ROOT_INODE_NUM;

	// first get rootDir from mountPoint
	//VNode = Get_Front_Of_VNode_List(&vnodeList);
	
	// path can be from root Dir, or current Dir, even only one name;
	// we start searching from the current given VNode's dir block
	Get_Exist_Inode(fDirNum, &newEntry);

	//Print("	bufBlock:%d\n", (int)nodeBuf->fsBlockNum);
	Debug("newEntry->name:%s\n", newEntry->filename);
	//Print("%s\n", dirBlock->entryTable[inodeOffset].filename);
	
	// This is slow :-(
	tempPath = Malloc(strlen(path));
	if (tempPath == NULL)
		return ENOMEM;
	pPath = tempPath;
	strcpy(pPath, path);
	// enter the loop; if there is one prefix, there must be one sub Dir
	Debug("	pPath:%s\n", pPath);
	if(strcmp(pPath, "/") == 0)
	{ Free(tempPath); Free(newEntry); return ENOTFOUND; }
	while(Unpack_Path(pPath, prefix, &suffix))
	{	
		if(strcmp(suffix, "/") == 0) 
		{
			Debug(" suffix err.\n");
			break;
		}
		// this is an empty dir  or this is not a dir
		//Print("	prefix:%s\n", prefix);
		//Print("	suffix:%s\n", suffix);
		if (newEntry->size == 0 || !(newEntry->flags & GOSFS_DIRENTRY_ISDIRECTORY))
		{
			Debug("	newEntry->size:%d,name:%s\n", (int)(newEntry->size),newEntry->filename);
			Debug("	newEntry->flags:%d\n", (int)(newEntry->flags & GOSFS_DIRENTRY_ISDIRECTORY));
			Print("	not found.\n");
			Free(pPath);
			Free(newEntry);
			return ENOTFOUND;
		}
		// compare every entry with prefix to see if there is one sub-dir matched;
		for(i= 0; i < newEntry->size; i++)
		{
			Get_Exist_Inode(newEntry->blockList[i], &tempEntry);
			Debug("	prefix:%s, filename:%s\n", prefix, tempEntry->filename);
			if(strcmp(prefix, tempEntry->filename) == 0)
			{	
				found = true;
				fDirNum = newEntry->blockList[i];
				Free(newEntry);
				newEntry = tempEntry;
				Print("	found!\n");
				break;
			}
			Free(tempEntry);
			
		}
		
		if (found)
		{
			Debug("	found one; go on to the next.\n");
			found = false;
		}
		else // we can't find even one matching name in this dir
		{
			Free(tempPath);
			Free(newEntry);
			return ENOTFOUND;
		}
		strcpy(pPath, suffix); // go on with the next sub-path
	}
	
	// Now we've got destEntry
	// check if we can afford a new Inode
	if (newEntry->size == GOSFS_NUM_BLOCK_PTRS)
	{
		// no more space in blockList
		Free(tempPath);
		Free(newEntry);
		return EMFILE;
	}


	// first Allocate a new Inode from SuperBlock
	inodeNum = Allocate_Inode(mountPoint, &wasteEntry, prefix, GOSFS_DIRENTRY_ISDIRECTORY);
	if(inodeNum < 0) //not successfully allocated
	{
		Free(tempPath);
		Free(newEntry);
		return inodeNum;
	}
	Debug("	inodeNum:%d\n", inodeNum);

	// then update the father Dir
	// Check to see if we've opened it and it still remained in the vnodeList
	vNode = Get_Front_Of_VNode_List(&vnodeList);
	while(vNode != NULL)
	{
		iNode = (struct GOSFS_Inode *)vNode->fsData;
		if(iNode->inodeNumber == fDirNum)
		{
			Debug("	Father Dir already in list.\n");
			for(i = 0; i < GOSFS_DIR_ENTRIES_PER_BLOCK; i++)
			{	if(iNode->dirEntry.blockList[i] == 0) break; }
			iNode->dirEntry.blockList[i] = inodeNum;
			iNode->dirEntry.size++;
			iNode->dirty = true; // although we set the dirty bit, we still have to write back right now
			break;
		}

		vNode = Get_Next_In_VNode_List(vNode);
	}

	// update father dir
	FIND_INODEBLOCK_AND_INODEOFFSET(fDirNum, inodeBlock, inodeOffset);
	rc = Get_FS_Buffer(gosfsBufferCache, inodeBlock, &nodeBuf);
	if (rc < 0) return rc;
	dirBlock = (struct GOSFS_Dir_Block *)nodeBuf->data;
	memcpy(newEntry, &(dirBlock->entryTable[inodeOffset]), sizeof(struct GOSFS_Dir_Entry));
	for(i = 0; i < GOSFS_DIR_ENTRIES_PER_BLOCK; i++)
	{	if(dirBlock->entryTable[inodeOffset].blockList[i] == 0) break; }
	dirBlock->entryTable[inodeOffset].blockList[i] = inodeNum;	
	dirBlock->entryTable[inodeOffset].size++;
	Debug("	size:%d\n", (int)dirBlock->entryTable[inodeOffset].size);
	Modify_FS_Buffer(gosfsBufferCache, nodeBuf);
	Sync_FS_Buffer(gosfsBufferCache, nodeBuf);
	Release_FS_Buffer(gosfsBufferCache, nodeBuf);
	// Init destEntry's sub entry in datablock
	//////////newEntry->blockList?????
	Free(newEntry); // this entry is not used, so release it
	Free(tempPath);
	Free(wasteEntry);
	// ERROR: dirBlock->numExistEntry++;
	Print(" finished.\n");
	return 0;
}



/*
 * Open a directory named by given path.
 */
static int GOSFS_Open_Directory(struct Mount_Point *mountPoint, const char *path, struct File **pDir)
{
	// First of all check if the user can afford another opened file
	if (g_currentThread->userContext != 0)
	{
		if(g_currentThread->userContext->fileCount == USER_MAX_FILES)
			return EMFILE;
	}

	struct GOSFS_Dir_Entry * tempEntry, *newEntry;
	struct GOSFS_Inode * iNode = NULL;
	struct File *vNode;
	const char *suffix;
	char * pPath;
	char prefix[GOSFS_FILENAME_MAX + 1];
	void* tempPath;
	bool found = false;
	int i, rc, inodeNum = GOSFS_ROOT_INODE_NUM;

	i = rc = 0;
	tempPath = Malloc(strlen(path));
	if (tempPath == NULL)
		return ENOMEM;
	pPath = tempPath;
	strcpy(pPath, path);

	rc = Get_Exist_Inode(GOSFS_ROOT_INODE_NUM, &tempEntry);

	while(Unpack_Path(pPath, prefix, &suffix))
	{
		if(strcmp(pPath, "/") == 0)
		{
			Debug(" suffix err.\n");
			break;
		}
		if (tempEntry->size == 0 || !(tempEntry->flags & GOSFS_DIRENTRY_ISDIRECTORY)) // empty dir
		{
			Debug("	size:%d\n", (int)tempEntry->size);
			Debug("	flags:%d\n", (int)tempEntry);
			Print("	Empty Dir or No Dir.\n");
			rc = ENOTFOUND;
			goto failed;
		}
		// start searching
		for (i = 0; i < tempEntry->size; i++)
		{
			Get_Exist_Inode(tempEntry->blockList[i], &newEntry);
			if(strcmp(prefix, newEntry->filename) == 0)
			{
				inodeNum = tempEntry->blockList[i];
				Free(tempEntry);
				tempEntry = newEntry;
				found = true;
				break;
			}
			// not found in this round
			Free(newEntry);
		}
		
		// check if we found target dir
		if(found)
			found = false;
		else
		{
			Print("	not in this directory:%s", tempEntry->filename);
			rc = ENOTFOUND;
			goto failed;
		}
		strcpy(pPath, suffix); 
	}


	// now we've got the Dir_Entry; create the GOSFS_Inode, then the VNode;
	Debug("	target dir:%s\n", tempEntry->filename);

	
	// Check to see if we've opened it and it still remained in the vnodeList
	vNode = Get_Front_Of_VNode_List(&vnodeList);
	while(vNode != NULL)
	{
		iNode = (struct GOSFS_Inode *)vNode->fsData;
		if(inodeNum == iNode->inodeNumber)
		{
			Debug("	VNode already in list:%d, %x\n", (int)inodeNum, (int)vNode);
			iNode->icount++;
			goto done;
		}

		vNode = Get_Next_In_VNode_List(vNode);
	}


	// Not in vnodeList, create a new one and insert it into the list
	iNode = Init_GOSFS_Inode(tempEntry, inodeNum);
	if (iNode == NULL)
	{
		Debug("	Failed init iNode.\n");
		goto failed;
	}
	iNode->icount++;

	// now we've got the GOSFS_Inode; create File(VNode);
	// filepos : next dir to be read
	// endpos: total dir numbers in this dir
	// we don't have mode for dir right now, so all dirs can be opened
	vNode = Allocate_File(&s_gosfsDirOps, 0, tempEntry->size, iNode, 0, mountPoint);
	if (vNode == NULL)
	{
		Debug("	Failed allocating vNode.\n");
		Free(iNode);
		goto failed;
	}

	Add_To_Back_Of_VNode_List(&vnodeList, vNode);


done:
	// we're sure that we can afford another opened dir; see the top of the function
	if (g_currentThread->userContext != 0)
	{
			for(i = 0; i < GOSFS_NUM_DIR_ENTRY; i++)
			{ if(g_currentThread->userContext->fileList[i] == 0) break; }
			g_currentThread->userContext->fileList[i] = vNode;
			rc = i;
			Debug("gosfs open dir rc:%d\n", rc);
			g_currentThread->userContext->fileCount++;
	}

	*pDir = vNode;
	Free(tempPath);
	Free(tempEntry);
	
	return rc;


	
failed:
			Free(tempPath);
			Free(tempEntry);
			return rc;
}


// Almost the same as GOSFS_Open_Directory
static int GOSFS_Open_File(struct Mount_Point *mountPoint, const char *path, int mode, struct File **pFile)
{
	// First of all check if the user can afford another opened file
	if (g_currentThread->userContext != 0)
	{
		if(g_currentThread->userContext->fileCount == USER_MAX_FILES)
			return EMFILE;
	}

	struct GOSFS_Dir_Entry * tempEntry, *newEntry;
	struct GOSFS_Inode * iNode = NULL;
	struct File *vNode;
	const char *suffix;
	char * pPath;
	char prefix[GOSFS_FILENAME_MAX + 1];
	void* tempPath;
	bool found = false;
	int i, rc, inodeNum = 0;

	i = rc = 0;
	tempPath = Malloc(strlen(path));
	if (tempPath == NULL)
		return ENOMEM;
	pPath = tempPath;
	strcpy(pPath, path);

	rc = Get_Exist_Inode(GOSFS_ROOT_INODE_NUM, &tempEntry);
	Debug("gosfs open path:%s\n", path);
	if(strcmp(path, "/") == 0)
	{
		rc = ENOTFOUND;
		Print("can't open /d by using open file.\n");
		goto failed;
	}
	//while(1);
	// First locate the father directory
	while(Unpack_Path(pPath, prefix, &suffix))
	{
		if(strcmp(pPath, "/") == 0)
		{
			Debug(" suffix err.\n");
			break;
		}
		if (tempEntry->size == 0 || !(tempEntry->flags & GOSFS_DIRENTRY_ISDIRECTORY)) // empty dir
		{
			Debug("	size:%d\n", (int)tempEntry->size);
			Debug("	flags:%d\n", (int)tempEntry->flags);
			Print("	Empty Dir or No Dir:%s\n", tempEntry->filename);
			rc = ENOTFOUND;
			goto failed;
		}
		// start searching
		for (i = 0; i < tempEntry->size; i++)
		{
			Get_Exist_Inode(tempEntry->blockList[i], &newEntry);
			Debug("prefix:%s, newEntry:%s\n", prefix, newEntry->filename);
			if(strcmp(prefix, newEntry->filename) == 0 && !(newEntry->flags & GOSFS_DIRENTRY_ISDIRECTORY))
			{
				inodeNum = tempEntry->blockList[i];
				Free(tempEntry);
				tempEntry = newEntry;
				found = true;
				break;
			}
			// not found in this round
			Free(newEntry);
		}
		
		// check if we found target dir
		if(found)
			found = false;
		else
		{
			Print("	not in this directory:%s\n", newEntry->filename);
			rc = ENOTFOUND;
			goto failed;
		}
		strcpy(pPath, suffix); 
	}


	// now we've got the Dir_Entry; create the GOSFS_Inode, then the VNode;
	Debug("	target file:%s\n", tempEntry->filename);
	
	// Check to see if we've opened it and it still remained in the vnodeList
	vNode = Get_Front_Of_VNode_List(&vnodeList);
	while(vNode != NULL)
	{
		iNode = (struct GOSFS_Inode *)vNode->fsData;
		if(inodeNum == iNode->inodeNumber)
		{
			Debug("	VNode already in list:no.%d,%x\n", (int)inodeNum, (int)vNode);
			Debug("	VNode name:%s\n", iNode->dirEntry.filename);
			Print("vNode->Write:%x\n", (int)vNode->ops->Write);
			vNode->mode = mode;
			iNode->icount++;
			
			goto done;
		}

		vNode = Get_Next_In_VNode_List(vNode);
	}


	// Not in vnodeList, create a new one and insert it into the list
	iNode = Init_GOSFS_Inode(tempEntry, inodeNum);
	if (iNode == NULL)
	{
		Print("	Failed init iNode.\n");
		goto failed;
	}
	iNode->icount++;

	// now we've got the GOSFS_Inode; create File(VNode);
	// filepos : next dir to be read
	// endpos: total dir numbers in this dir
	// we don't have mode for dir right now, so all dirs can be opened
	vNode = Allocate_File(&s_gosfsFileOps, 0, tempEntry->size, iNode, mode, mountPoint);
	if (vNode == NULL)
	{
		Debug("	Failed allocating vNode.\n");
		Free(iNode);
		goto failed;
	}

	Add_To_Back_Of_VNode_List(&vnodeList, vNode);


done:
	// we're sure that we can afford another opened dir; see the top of the function
	if (g_currentThread->userContext != 0)
	{
			for(i = 0; i < GOSFS_NUM_DIR_ENTRY; i++)
			{ if(g_currentThread->userContext->fileList[i] == 0) break; }
			g_currentThread->userContext->fileList[i] = vNode;
			rc = i;
			Debug("gosfs open rc:%d\n", rc);
			g_currentThread->userContext->fileCount++;
	}

	Debug("done in vNode:%x\n", (int)vNode);
	*pFile = vNode;
	Print("in pfile:%x\n", (int)(*pFile));
	Free(tempPath);
	Free(tempEntry);
	
	return rc;


	
failed:
			Free(tempPath);
			Free(tempEntry);
			return rc;
}

/*
 * Open a file named by given path.
 */
// check mode
// mode is defined in fileio.h
// if mode is O_CREATE, then we will create a new file
// here, the mode defines how a file would be operated
// for example, if we pass the mode O_READ | O_WRITE, then this file can be both read and written
// if we only pass O_WRITE, then this file can only be written but not read; this may sound rediculous
static int GOSFS_Open(struct Mount_Point *mountPoint, const char *path, int mode, struct File **pFile)
{
	// First check if we can open another file
	if(g_currentThread->userContext != 0)
		if(g_currentThread->userContext->fileCount == USER_MAX_FILES)
			return EMFILE;


	int rc = 0;
	// We have to choose: create or open
	if(mode & O_CREATE) // means we have to create a file
	{
		Debug("Create file:\n");
		rc = GOSFS_Create_File(mountPoint, path, mode &(~O_CREATE), pFile);
	}
	else{ // open exist file
		Debug("Open file:\n");
		rc = GOSFS_Open_File(mountPoint, path, mode, pFile);
	}


	return rc;
}


/*
 * Delete a directory named by given path.
 */
static int GOSFS_Delete(struct Mount_Point *mountPoint, const char *path)
{
       
    	struct GOSFS_Dir_Entry * tempEntry, *newEntry;
	struct GOSFS_Inode *tempNode;
	struct FS_Buffer *nodeBuf = NULL;
	struct GOSFS_Dir_Block *dirBlock = NULL;
	struct GOSFS_Inode *iNode = NULL;
	struct File *vNode;
	const char *suffix;
	char * pPath;
	char prefix[GOSFS_FILENAME_MAX + 1];
	void* tempPath;
	bool found = false;
	int i, rc, inodeNum, inodeBlock, inodeOffset;
	int fDirNum = 0;
	
	i = rc = inodeNum = inodeBlock = inodeOffset = 0;
	tempPath = Malloc(strlen(path));
	if (tempPath == NULL)
		return ENOMEM;
	pPath = tempPath;
	strcpy(pPath, path);

	// First check if  the path is exist
	rc = Get_Exist_Inode(GOSFS_ROOT_INODE_NUM, &tempEntry);
	fDirNum = GOSFS_ROOT_INODE_NUM;
	
	Debug("path:%s\n", pPath);
	if (strcmp(path, "/") == 0)
	{
		rc = EUNSUPPORTED;
		goto failed;
	}

	// Find Father dir first
	while(Unpack_Path(pPath, prefix, &suffix))
	{
		if(strcmp(suffix, "/") == 0)
		{
			Debug(" suffix err.\n");
			break;
		}
		if (tempEntry->size == 0 || !(tempEntry->flags & GOSFS_DIRENTRY_ISDIRECTORY)) // empty dir
		{
			Debug("	size:%d\n", (int)tempEntry->size);
			Debug("	flags:%d\n", (int)tempEntry);
			Print("	Empty Dir or No Dir.\n");
			rc = ENOTFOUND;
			goto failed;
		}
		// start searching
		for (i = 0; i < tempEntry->size; i++)
		{
			Get_Exist_Inode(tempEntry->blockList[i], &newEntry);
			if(strcmp(prefix, newEntry->filename) == 0)
			{
				fDirNum = tempEntry->blockList[i];
				Free(tempEntry);
				tempEntry = newEntry;
				found = true;
				break;
			}
			// not found in this round
			Free(newEntry);
		}
		
		// check if we found target dir
		if(found)
			found = false;
		else
		{
			Print("	not in this directory:%s", tempEntry->filename);
			rc = ENOTFOUND;
			goto failed;
		}
		strcpy(pPath, suffix); 
	}

	// First check if unpack path is failed
	// we only fail the unpack path when the path is invalid
	// !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!

	// now we've got the Dir_Entry; delete the GOSFS_Inode, then the VNode;
	Debug("	father dir:%s\n", tempEntry->filename);
	Debug("	delete file:%s\n", prefix);

	//----------------------------------
	// make sure it has a unique name
	if(tempEntry->size > 0)
	for (i = 0; i < GOSFS_NUM_DIR_ENTRY; i++)
	{
		if(tempEntry->blockList[i] == 0)continue;
		Get_Exist_Inode(tempEntry->blockList[i], &newEntry);
		if(strcmp(prefix, newEntry->filename) == 0)
		{
			//fDirNum = tempEntry->blockList[i];
			//Free(tempEntry);
			//tempEntry = newEntry;
			found = true;
			inodeNum = tempEntry->blockList[i];
			break;
		}
		// not found in this round
		Free(newEntry);
	}
	else
	{
		rc = ENOTFOUND;
		goto failed;
	}

	if(found == false)
	{
		rc = EDELETION;
		//Free(newEntry);
		goto failed;
	}

	Debug(" target found:%s, inodeNo:%d\n", newEntry->filename, inodeNum);

	// check if this is a directory and it's not empty
	if(newEntry->flags & GOSFS_DIRENTRY_ISDIRECTORY && newEntry->size >0)
	{	rc = EDELETION; 
		Free(newEntry);
		goto failed;
	}

	// If it is an exist entry, see if we've opened it
	vNode = Get_Front_Of_VNode_List(&vnodeList);
	while(vNode != NULL)
	{
		tempNode= (struct GOSFS_Inode *)vNode->fsData;
		Debug("	in list:%s\n", tempNode->dirEntry.filename);
		if(tempNode->inodeNumber == inodeNum)
		{
			// Found one in vnodeList, free it after check
			if (tempNode->icount > 0) //still opened
			{
				Print("	Can't delete Opened file.Ref:%d\n", (int)tempNode->icount);
				rc = EDELETION;
				Free(newEntry);
				goto failed;
			}
			else if(tempNode->icount == 0) // no refference
			{
				Debug("	VNode has no refference.\n");
				Remove_From_VNode_List(&vnodeList, vNode);
				Free(tempNode);
				Free(vNode);
				break; // Free in mem vNode;
			}
		}

		vNode = Get_Next_In_VNode_List(vNode);
	}


	// Not opened, we can delete it
	// First delete the blocks of the file if this file is a normal one
	if(!(newEntry->flags & GOSFS_DIRENTRY_ISDIRECTORY))
	{
		Debug("	This is a normal file.\n");
		// release the direct blocks
		for(i = 0; i < GOSFS_NUM_DIRECT_BLOCKS; i++)
		{
			if(newEntry->blockList[i] > 0)
			{
				Debug("release direct block.\n");
				Release_Block(mountPoint, newEntry->blockList[i]);
			}
		}

		// release the first indirect block
		if(newEntry->blockList[8] > 0)
		{
			Debug("release first ind block.\n");
			Release_First_Indirect_Block(mountPoint, newEntry->blockList[8]);
			// Release_Block(mountPoint, tempEntry->blockList[8]);
		}

		// release the second indirect block
		if(newEntry->blockList[9] > 0)
		{
			Debug("release sec ind block.\n");
			Release_Second_Indirect_Block(mountPoint, newEntry->blockList[9]);
			// Release_Block(mountPoint, tempEntry->blockList[9]);
		}
	}


	// Now we can release the iNode
	rc = Delete_GOSFS_Inode(mountPoint, inodeNum);
	if(rc < 0) goto failed;
	Free(newEntry); // release the target dir

	//--------------------------------------
	// at last update the father Dir;
	// first check if the father Dir remains in the list
	Debug(" UpDate father Dir.\n");
	vNode = Get_Front_Of_VNode_List(&vnodeList);
	while(vNode != NULL)
	{
		Debug("	test.\n");
		iNode = (struct GOSFS_Inode *)vNode->fsData;
		if(iNode->inodeNumber == fDirNum)
		{
			Debug("	Father Dir already in list.\n");
			for(i = 0; i < GOSFS_DIR_ENTRIES_PER_BLOCK; i++)
			{ if(iNode->dirEntry.blockList[i] == inodeNum) break; }
			iNode->dirEntry.blockList[i] = 0;
			iNode->dirEntry.size--;
			iNode->dirty = true; // although we set the dirty bit, we still have to write back right now
			break;
		}

		vNode = Get_Next_In_VNode_List(vNode);
	}

	// Write back the changed father inode
	FIND_INODEBLOCK_AND_INODEOFFSET(fDirNum, inodeBlock, inodeOffset);
	rc = Get_FS_Buffer(gosfsBufferCache, inodeBlock, &nodeBuf);
	if (rc < 0) return rc;
	dirBlock = (struct GOSFS_Dir_Block *)nodeBuf->data;
	for(i = 0; i < GOSFS_DIR_ENTRIES_PER_BLOCK; i++)
	{ if(dirBlock->entryTable[inodeOffset].blockList[i] == inodeNum) break; }
	dirBlock->entryTable[inodeOffset].blockList[i] = 0;
	dirBlock->entryTable[inodeOffset].size--;
	Debug("	size:%d\n", (int)dirBlock->entryTable[inodeOffset].size);
	Modify_FS_Buffer(gosfsBufferCache, nodeBuf);
	Sync_FS_Buffer(gosfsBufferCache, nodeBuf);
	Release_FS_Buffer(gosfsBufferCache, nodeBuf);




	Print("	Dir_Entry deleted.\n");

failed:

	Free(tempPath);
	Free(tempEntry);

	return rc;
}

/*
 * Get metadata (size, permissions, etc.) of file named by given path.
 */
static int GOSFS_Stat(struct Mount_Point *mountPoint, const char *path, struct VFS_File_Stat *stat)
{
	struct GOSFS_Dir_Entry * tempEntry, *newEntry;
	//struct GOSFS_Inode * iNode = NULL;
	//struct File *vNode;
	const char *suffix;
	char * pPath;
	char prefix[GOSFS_FILENAME_MAX + 1];
	void* tempPath;
	bool found = false;
	int i, rc, inodeNum = 0;

	i = rc = 0;
	tempPath = Malloc(strlen(path));
	if (tempPath == NULL)
		return ENOMEM;
	pPath = tempPath;
	strcpy(pPath, path);

	rc = Get_Exist_Inode(GOSFS_ROOT_INODE_NUM, &tempEntry);

	// First locate the father directory
	while(Unpack_Path(pPath, prefix, &suffix))
	{
		if(strcmp(pPath, "/") == 0)
		{
			Debug(" suffix err.\n");
			break;
		}
		if (tempEntry->size == 0 || !(tempEntry->flags & GOSFS_DIRENTRY_ISDIRECTORY)) // empty dir
		{
			Debug("	size:%d\n", (int)tempEntry->size);
			Debug("	flags:%d\n", (int)tempEntry);
			Print("	Empty Dir or No Dir.\n");
			rc = ENOTFOUND;
			goto failed;
		}
		// start searching
		for (i = 0; i < tempEntry->size; i++)
		{
			Get_Exist_Inode(tempEntry->blockList[i], &newEntry);
			if(strcmp(prefix, newEntry->filename) == 0)
			{
				inodeNum = tempEntry->blockList[i];
				Free(tempEntry);
				tempEntry = newEntry;
				found = true;
				break;
			}
			// not found in this round
			Free(newEntry);
		}
		
		// check if we found target dir
		if(found)
			found = false;
		else
		{
			Print("	not in this directory:%s", tempEntry->filename);
			rc = ENOTFOUND;
			goto failed;
		}
		strcpy(pPath, suffix); 
	}


	// now we've got the Dir_Entry; create the GOSFS_Inode, then the VNode;
	Debug("	target file:%s\n", tempEntry->filename);

	// Make the assignment of the VFS_File_Stat structure
	stat->isDirectory = tempEntry->flags & GOSFS_DIRENTRY_ISDIRECTORY ? true : false;
	stat->size = tempEntry->size;
	stat->isSetuid = tempEntry->flags & GOSFS_DIRENTRY_SETUID ? true : false;

failed: 

	Free(tempEntry);
	Free(tempPath);
	return rc;
}

/*
 * Synchronize the filesystem data with the disk
 * (i.e., flush out all buffered filesystem data).
 */
static int GOSFS_Sync(struct Mount_Point *mountPoint)
{
	// First sync inode blocks and data blocks
	int rc = Sync_FS_Buffer_Cache(gosfsBufferCache);
	if(rc < 0) return rc;

	// Then sync superblock
	struct GOSFS_Superblock *gosfsSuperBlock;
	struct FS_Buffer *blockBuf;
	gosfsSuperBlock = (struct GOSFS_Superblock *)mountPoint->fsData;
	if (gosfsSuperBlock->dirty == true)
	{
		rc = Get_FS_Buffer(gosfsBufferCache, GOSFS_SUPER_BLOCK_NUM, &blockBuf);
		if(rc < 0) goto done;
		memcpy(blockBuf->data, &gosfsSuperBlock->gfsInstance, sizeof(struct GOSFS_Instance));
		Modify_FS_Buffer(gosfsBufferCache, blockBuf);
		Sync_FS_Buffer(gosfsBufferCache, blockBuf);
	}


done:
	Release_FS_Buffer(gosfsBufferCache, blockBuf);

	return rc;
}

/*static*/ struct Mount_Point_Ops s_gosfsMountPointOps = {
    &GOSFS_Open, //open existed file or create new file
    &GOSFS_Create_Directory,
    &GOSFS_Open_Directory,
    &GOSFS_Stat,
    &GOSFS_Sync,
    &GOSFS_Delete,
};


// Write the first block as all '0's--this is the bootSector
// Initialize the FS_Buffer_Cache structure
// get a FS_Buffer, and write the superBlock in to this Buffer
// write SuperBlock back to disk
// Initialize the first inode--root Dir
// return 0 if successfull
static int GOSFS_Format(struct Block_Device * blockDev)
{
	// first we create a Buffer Cache for GOSFS;
	struct Block_Device* dev = blockDev;
	uint_t fsBlockSize = GOSFS_FS_BLOCK_SIZE;
	int rc = 0;
	
	gosfsBufferCache = Create_FS_Buffer_Cache(dev, fsBlockSize); // now we get a FS_Buffer_Cache struct

	// then we create the all zero bootSector if nessessary;
	struct FS_Buffer * gosBootSector, *gosSuperBlock, *gosRootDir;
	struct GOSFS_Dir_Block * rootDir;
	struct GOSFS_Superblock *gosSB;
	struct GOSFS_Dir_Entry *rootEntry;
	
	// first check if it is a formatted gosfs
	if ((rc = Get_FS_Buffer(gosfsBufferCache, GOSFS_SUPER_BLOCK_NUM, &gosSuperBlock) )< 0)
		return rc;

	gosSB = (struct GOSFS_Superblock *)(gosSuperBlock->data);
	struct GOSFS_Instance * gosInstance = (struct GOSFS_Instance * )(&gosSB->gfsInstance);
	Print("	gosInstance->magic: %x\n", (int)(gosInstance->magic));
	if(gosInstance->magic != GOSFS_MAGIC || 1) // if the fs is unformatted
	{
		// Initialize bootSector
		if ((rc = Get_FS_Buffer(gosfsBufferCache, 0, &gosBootSector)) < 0)
			return rc; // now we've read the num 0 block into mem

		Init_GOSFS_BootSector(gosBootSector);
		Modify_FS_Buffer(gosfsBufferCache, gosBootSector);
		Sync_FS_Buffer(gosfsBufferCache, gosBootSector);
		Release_FS_Buffer(gosfsBufferCache, gosBootSector);


		// Initialize SuperBlock
		Init_GOSFS_Instance(gosInstance, gosSuperBlock, dev);
		Modify_FS_Buffer(gosfsBufferCache, gosSuperBlock);
		Sync_FS_Buffer(gosfsBufferCache, gosSuperBlock);
		Release_FS_Buffer(gosfsBufferCache, gosSuperBlock);

		// Initialize the first and second inode(in fact only the second inode is used, so...)
		if ((rc = Get_FS_Buffer(gosfsBufferCache, 2, &gosRootDir)) < 0)
			return rc;
		rootDir = (struct GOSFS_Dir_Block *)(gosRootDir->data);
		rootEntry = &(rootDir->entryTable[1]);
		//strcpy(rootDir->entryTable[1].filename, "fuck");
		Print("	%x, %x\n", (int)&rootDir->entryTable, (int)&rootDir->entryTable[0]);
		Print("	%x\n", (int)&rootDir->entryTable[1]);
		Init_GOSFS_Rootdir(&rootDir->entryTable[1]); //this is the root Dir
		Print("	root:%s\n", rootDir->entryTable[1].filename);
		Print("	root size:%d\n", (int)rootDir->entryTable[1].size);
		Modify_FS_Buffer(gosfsBufferCache, gosRootDir);
		Sync_FS_Buffer(gosfsBufferCache, gosRootDir);
		Release_FS_Buffer(gosfsBufferCache, gosRootDir);

		//ERROR: nitialize the first block
		//if((rc = Get_FS_Buffer(gosfsBufferCache, GOSFS_FIRST_DATA_BLOCK, &gosRootBlock)) < 0)
		//	return rc;
		//rootBlock = (struct GOSFS_Dir_Block *)(gosRootBlock->data);
		//Init_Directory_Block(rootBlock);
		//Modify_FS_Buffer(gosfsBufferCache, gosRootBlock);
		//Sync_FS_Buffer(gosfsBufferCache, gosRootBlock);
		//Release_FS_Buffer(gosfsBufferCache, gosRootBlock);
	}
	else
		Release_FS_Buffer(gosfsBufferCache, gosSuperBlock);

	// it's been formatted
	return rc;	
}

// Initialize structures needed for GOSFS; including VNode list, Mount_Point, GOSFS_Superblock,
// 	currentDir, etc.
static int GOSFS_Mount(struct Mount_Point *mountPoint)
{
	//first, read the superBlock-on disk into mem
	Print("start Mounting gosfs.\n");
	struct FS_Buffer * gosfsInstance;
	struct GOSFS_Inode *rootDirInode;
	struct File* rootDirVNode;
	struct GOSFS_Dir_Entry *rootEntry;
	// struct GOSFS_Dir_Entry *dirEntry;
	
	Print("fetching superblock.\n");
	int rc = Get_FS_Buffer(gosfsBufferCache, 1, &gosfsInstance);
	if(rc < 0) return rc;
	Print(" allocating superblock in mem.\n");
	struct GOSFS_Superblock * gosfsSuperBlock = (struct GOSFS_Superblock *)Malloc(sizeof(struct GOSFS_Superblock));
	if(gosfsSuperBlock == NULL)
		return ENOMEM;
	// get GOSFS_Instance
	Print("	building GOSFS_Superblock.\n");
	memcpy( &(gosfsSuperBlock->gfsInstance), (struct GOSFS_Instance *)(gosfsInstance->data), sizeof(struct GOSFS_Instance));
	Print("	gfsInstance->magic: %x\n", (int)(gosfsSuperBlock->gfsInstance.magic));
	// finish the initialization of the rest of GOSFS_Superblock
	gosfsSuperBlock->flags = 0;
	gosfsSuperBlock->dirty = 0;
	Mutex_Init(&(gosfsSuperBlock->lock));
	Cond_Init(&(gosfsSuperBlock->cond));
	Release_FS_Buffer(gosfsBufferCache, gosfsInstance);

	// second, initialize VNodeList
	Init_VNode_List();

	// init part of the mountPoint
	mountPoint->fsData = gosfsSuperBlock;
	mountPoint->ops = &s_gosfsMountPointOps;

	// init the rootDirVNode;
	Get_Exist_Inode(GOSFS_ROOT_INODE_NUM, &rootEntry);
	rootDirInode = Init_GOSFS_Inode(rootEntry, GOSFS_ROOT_INODE_NUM);
	rootDirVNode = Allocate_File(&s_gosfsDirOps, 0, 0, rootDirInode, 0, mountPoint);
	Print("root ops:(%x)\n", (int)rootDirVNode);
	Print("read entry:%x\n", (int)rootDirVNode->ops->Read_Entry);
	Print("close:%x\n", (int)rootDirVNode->ops->Close);
	Add_To_Back_Of_VNode_List(&vnodeList, rootDirVNode);	

	// init the standard input/output file
	Init_Stdio();
	
	return rc;
}

static struct Filesystem_Ops s_gosfsFilesystemOps = {
    &GOSFS_Format,
    &GOSFS_Mount,
};

/* ----------------------------------------------------------------------
 * Public functions
 * ---------------------------------------------------------------------- */

void Init_GOSFS(void)
{
    Register_Filesystem("gosfs", &s_gosfsFilesystemOps);
}


int Init_Stdio()
{
	stdIn = Init_StdIn();
	stdOut = Init_Stdout();

	Print("stdIn:%x, stdIn op:%x\n", (int)stdIn, (int)stdIn->ops->Read);
	Print("stdOut:%x, stdOut op:%x\n", (int)stdOut, (int)stdOut->ops->Write);

	Add_To_Back_Of_VNode_List(&vnodeList, stdIn);
	Add_To_Back_Of_VNode_List(&vnodeList, stdOut);

	return 0;
}

void Init_User_Stdio(struct User_Context *userContext)
{
	userContext->fileList[0] = stdIn;
	userContext->fileList[1] = stdOut;
	userContext->fileCount += 2;
}

