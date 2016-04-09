/*
 * ELF executable loading
 * Copyright (c) 2003, Jeffrey K. Hollingsworth <hollings@cs.umd.edu>
 * Copyright (c) 2003, David H. Hovemeyer <daveho@cs.umd.edu>
 * $Revision: 1.14 $
 * 
 * This is free software.  You are permitted to use,
 * redistribute, and modify it as specified in the file "COPYING".
 */

#ifndef GEEKOS_ELF_H
#define GEEKOS_ELF_H


/*
 * ELF header at the beginning of the executable.
	详细信息 http://www.sco.com/developers/gabi/1998-04-29/ch4.eheader.html
	elfHeader是由编译器编译后自动生成的，我们只需要了解其结构
 */
typedef struct {
    unsigned  char	ident[16];
	/*
	EI_MAG0			0	File identification  文件标识
	EI_MAG1			1	File identification
	EI_MAG2			2	File identification
	EI_MAG3			3	File identification
		EI_MAG0-4表示该文件为ELF文件
	EI_CLASS		4	File class
		标示文件类型32/64	
		Name			Value	Meaning
		ELFCLASSNONE	0		Invalid class
		ELFCLASS32		1		32-bit objects
		ELFCLASS64		2		64-bit objects
	EI_DATA			5	Data encoding
		指定（对象文件的容器和包含在对象文件区段）所使用的数据结构的编码
		Name		Value	Meaning
		ELFDATANONE	0	Invalid data encoding
		ELFDATA2LSB	1	小端模式
		ELFDATA2MSB	2	大端模式
	EI_VERSION		6	File version  
		字节e_ident[EI_VERSION]指定ELF头的版本号。目前，此值必须EV_CURRENT，如上所述为e_version。
	EI_OSABI		7	Operating system/ABI identification
		标识该对象目标的操作系统和ABI（应用程序二进制接口）
	EI_ABIVERSION	8	ABI version
		标识对象所针对的ABI的版本
	EI_PAD			9	Start of padding bytes
		此值标志着在e_ident中未使用的字节的开始。这些字节被保留并设置为零；读取目标文件的程序应忽略它们。
	EI_NIDENT		16	Size of e_ident[]
	*/
    unsigned  short	type;
	/*标识对象的文件类型。
		ET_NONE		0		No file type
		ET_REL		1		Relocatable file
		ET_EXEC		2		Executable file
		ET_DYN		3		Shared object file
		ET_CORE		4		Core file
		ET_LOOS		0xfe00	Operating system-specific
		ET_HIOS		0xfeff	Operating system-specific
		ET_LOPROC	0xff00	Processor-specific
		ET_HIPROC	0xffff	Processor-specific
	*/
    unsigned  short	machine;
	//指定文件所需要的处理器架构
    unsigned  int	version;
    unsigned  int	entry;
	//该成员给出了该系统将控制权转移的虚拟地址，从而启动进程。也就是启动该进程的地址
    unsigned  int	phoff;
	//该成员保存程序头表的文件中的字节偏移量.也就是elfHeader后phoff字节为programHeader的起始地址
    unsigned  int	sphoff;
	//该成员保存section表的文件中的字节偏移量。section是编译时需要的，segment是加载时需要的
    unsigned  int	flags;
    unsigned  short	ehsize;
    unsigned  short	phentsize;
    unsigned  short	phnum;
	//该成员保存程序头表项的个数
    unsigned  short	shentsize;
    unsigned  short	shnum;
    unsigned  short	shstrndx;
} elfHeader;

/*
 * An entry in the ELF program header table.
 * This describes a single segment of the executable.
 */
typedef struct {
    unsigned  int   type;
    unsigned  int   offset;
	//segment首地址
    unsigned  int   vaddr;
	//在内存中的虚拟地址
    unsigned  int   paddr;
	//物理地址
    unsigned  int   fileSize;
	//在文件中的segment的大小
    unsigned  int   memSize;
	//在内存中segment的大小
    unsigned  int   flags;
    unsigned  int   alignment;
} programHeader;

/*
 * Bits in flags field of programHeader.
 * These describe memory permissions required by the segment.
 */
#define PF_R	0x4	 /* Pages of segment are readable. */
#define PF_W	0x2	 /* Pages of segment are writable. */
#define PF_X	0x1	 /* Pages of segment are executable. */

/*
 * A segment of an executable.
 * It specifies a region of the executable file to be loaded
 * into memory.
 */
typedef struct  {
    ulong_t offsetInFile;	 /* Offset of segment in executable file */
    ulong_t lengthInFile;	 /* Length of segment data in executable file */
    ulong_t startAddress;	 /* Start address of segment in user memory */
    ulong_t sizeInMemory;	 /* Size of segment in memory */
    int protFlags;		 /* VM protection flags; combination of VM_READ,VM_WRITE,VM_EXEC */
}Exe_Segment;

/*
 * Maximum number of executable segments we allow.
 * Normally, we only need a code segment and a data segment.
 * Recent versions of gcc (3.2.3) seem to produce 3 segments.
 */
#define EXE_MAX_SEGMENTS 3

/*
 * A struct concisely representing all information needed to
 * load an execute an executable.
 */
struct Exe_Format {
    Exe_Segment segmentList[EXE_MAX_SEGMENTS]; /* Definition of segments */
    int numSegments;		/* Number of segments contained in the executable */
    ulong_t entryAddr;	 	/* Code entry point address */
};

int Parse_ELF_Executable(char *exeFileData, ulong_t exeFileLength,
    struct Exe_Format *exeFormat);

#endif  /* GEEKOS_ELF_H */

