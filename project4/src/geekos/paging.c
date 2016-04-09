/*
 * Paging (virtual memory) support
 * Copyright (c) 2003, Jeffrey K. Hollingsworth <hollings@cs.umd.edu>
 * Copyright (c) 2003,2004 David H. Hovemeyer <daveho@cs.umd.edu>
 * $Revision: 1.55 $
 * 
 * This is free software.  You are permitted to use,
 * redistribute, and modify it as specified in the file "COPYING".
 */

#include <geekos/string.h>
#include <geekos/int.h>
#include <geekos/idt.h>
#include <geekos/kthread.h>
#include <geekos/kassert.h>
#include <geekos/screen.h>
#include <geekos/mem.h>
#include <geekos/malloc.h>
#include <geekos/gdt.h>
#include <geekos/segment.h>
#include <geekos/user.h>
#include <geekos/vfs.h>
#include <geekos/crc32.h>
#include <geekos/paging.h>

/* ----------------------------------------------------------------------
 * Public data
 * ---------------------------------------------------------------------- */

/* ----------------------------------------------------------------------
 * Private functions/data
 * ---------------------------------------------------------------------- */

#define SECTORS_PER_PAGE (PAGE_SIZE / SECTOR_SIZE)

static int numPagingDiskPages;
static struct Paging_Device * pagingDevice;
/*
 * flag to indicate if debugging paging code
 */
int debugFaults = 0;
#define Debug(args...) if (debugFaults) Print(args)
extern	ulong_t g_numTicks;		//全局节拍计数器（在timer.c中定义）
extern	struct Page* g_pageList;		//页面结构表（在mem.c中定义）
extern	int unsigned s_numPages;	//物理页面的总数（在mem.c中定义）

void checkPaging()
{
  unsigned long reg=0;
  __asm__ __volatile__( "movl %%cr0, %0" : "=a" (reg));
  Print("Paging on ? : %d\n", (reg & (1<<31)) != 0);
}


/*
 * Print diagnostic information for a page fault.
 */
static void Print_Fault_Info(uint_t address, faultcode_t faultCode)
{
    extern uint_t g_freePageCount;

    Print("Pid %d, Page Fault received, at address %x (%d pages free)\n",
        g_currentThread->pid, address, g_freePageCount);
    if (faultCode.protectionViolation)
        Print ("   Protection Violation, ");
    else
        Print ("   Non-present page, ");
    if (faultCode.writeFault)
        Print ("Write Fault, ");
    else
        Print ("Read Fault, ");
    if (faultCode.userModeFault)
        Print ("in User Mode\n");
    else
        Print ("in Supervisor Mode\n");
}

/*
 * Handler for page faults.
 * You should call the Install_Interrupt_Handler() function to
 * register this function as the handler for interrupt 14.
 */

/* ----------------------------------------------------------------------
 * Public functions
 * ---------------------------------------------------------------------- */


/*
 * Initialize virtual memory by building page tables建立初始内存页目录和页表
 * for the kernel and physical memory.
 */
void Init_VM(struct Boot_Info *bootInfo)
{
    /*
     * Hints:
     * - Build kernel page directory and page tables
     * - Call Enable_Paging() with the kernel page directory
     * - Install an interrupt handler for interrupt 14,
    （1）建立内核页目录表和页表；
	（2）调用Enable_Paging函数使分页机制有效；
	（3）加入一个缺页中断处理程序，并注册其中断号为14。
     *   page fault
     * - Do not map a page at address 0; this will help trap
     *   null pointer references
     */
    //TODO("Build initial kernel page directory and page tables");
    g_kernel_pde=(pde_t *)Alloc_Page();	//为内核页目录分配一页空间，Alloc_Page(mem.c)
	KASSERT(g_kernel_pde!=NULL);	// g_kernel_pde不能为空
	int whole_pages = bootInfo -> memSizeKB/4;		//计算物理内存的页数, bootInfo->memSizeKB为内存大小
	//计算内核页目录需要页目录项的数目以保证映射到所有的物理内存页
	int kernel_pde_entries=whole_pages/NUM_PAGE_DIR_ENTRIES+(whole_pages%NUM_PAGE_DIR_ENTRIES==0 ? 0:1);
	memset(g_kernel_pde,0,PAGE_SIZE);		//将页中所有的位清0
	//取页目录项中第0个页表中的第0页
	pte_t* cur_pte = (pte_t *)((g_kernel_pde->pageTableBaseAddr) << 12);
	for(i=0;i<kernel_pde_entries-1;i++)			//对于每一个内核页目录的页目录项
	{
		cur_pde_entry->present=1;				//为1表示相应页在内存中
		cur_pde_entry->flags=VM_WRITE;		//状态为可写
		cur_pde_entry->globalPage=1;			//是全局页
		cur_pte=(pte_t *)Alloc_Page();			//分配一页
		KASSERT(cur_pte!=NULL);				// cur_pte不能为空
		memset(cur_pte,0,PAGE_SIZE);			//将当前页表中所有的位清0
		cur_pde_entry->pageTableBaseAddr=(uint_t)cur_pte>>12;	//页表在内存中的基地址
		for(j=0;j<NUM_PAGE_TABLE_ENTRIES;j++)//1024次循环
		{
			cur_pte->present=1;				//为1表示相应页在内存中
			cur_pte->flags=VM_WRITE;			//状态为可写
			cur_pte->globalPage=1;				//是全局页
			cur_pte->pageBaseAddr=mem_addr>>12; //页在内存中的基地址
			cur_pte++;						//对于每一个页表项
			mem_addr+=PAGE_SIZE;			//每次循环内存地址都自增一个页大小
		}
		cur_pde_entry++;						//对于每一个页目录项
	}
	//初始化最后一个页目录表项和对应的页表，注意页表中的页表项不一定足够1024
	cur_pde_entry->present=1;					//为1表示相应页在内存中
	cur_pde_entry->flags=VM_WRITE;			//状态为可写
	cur_pde_entry->globalPage=1;				//是全局页
	cur_pte=(pte_t *)Alloc_Page();				//分配一页
	KASSERT(cur_pte!=NULL);					// cur_pte不能为空
	memset(cur_pte,0,PAGE_SIZE);				//将当前页表中所有的位清0
	cur_pde_entry->pageTableBaseAddr=(uint_t)cur_pte>>12; //页表在内存中的基地址
	int last_pagetable_num;						//余下的不足1024项的页表数
	last_pagetable_num=whole_pages%NUM_PAGE_TABLE_ENTRIES;//总页数%1024
	//注意当last_pagetable_num=0时，意味着最后一个页目录项对应的页表是满的，
	//就是说页表中1024个页表项都指向一个有效的页。
	if(last_pagetable_num==0)					//当last_pagetable_num=0时
	{
		last_pagetable_num=NUM_PAGE_TABLE_ENTRIES;
	}
	//为余下的页表项建立第二级映射
	for(j=0;j<last_pagetable_num;j++)
	{
		cur_pte->present=1;					//为1表示相应页在内存中
		cur_pte->flags=VM_WRITE;				//状态为可写
		cur_pte->globalPage=1;					//是全局页
		cur_pte->pageBaseAddr=mem_addr>>12;	//页表在内存中的基地址
		cur_pte++;							//对于余下的每一个页表项
		mem_addr+=PAGE_SIZE;				//每次循环内存地址都自增一个页大小
	}
	
	//内存第0页设置为不存在，用于NULL指针异常检测
	cur_pte->present=0;
	Enable_Paging(g_kernel_pde);	//使系统的分页机制有效
	Install_Interrupt_Handle(14,Page_Fault_Handler);		 //加入缺页中断处理程序
}

/**
 * Initialize paging file data structures.初始化页面文件数据结构
 * All filesystems should be mounted before this function
 * is called, to ensure that the paging file is available.
（1）调用Get_Paging_Device函数来获取一个已经注册了的分页设备；
（2）计算磁盘中的页面数，页面数=总扇区数/一页占用的扇区数；
（3）调用Create_Bit_Set函数为磁盘中的每一页设置标志位。
 */
void Init_Paging(void)
{
    //TODO("Initialize paging file data structures");
    pagingDevice=Get_Paging_Device();			//获取一个分页设备
    Print("The execution of function GetPaging_Device has been finished!\n");
	if(pagingDevice==NULL)					//如果获取的分页设备为空
	{
		Print("can not find pagefile\n");
		return;
	}
	//计算磁盘中的页面数，页面数=总扇区数/一页占用的扇区数
	static int numPagingDiskPages = pagingDevice->numSectors / SECTORS_PER_PAGE;
	Print("Now the value of numPagingDiskPages is %d.\n",numPagingDiskPages);
	//为pagefile中每一页设置标志位
	Bitmap=Create_Bit_Set(numPagingDiskPages);
	Print("The execution of function Init_Paging has been finished!\n");
}

/**
 * Find a free bit of disk on the paging file for this page.为页面文件分配磁盘块
 * Interrupts must be disabled.
 * @return index of free page sized chunk of disk space in
 *   the paging file, or -1 if the paging file is full
 */
int Find_Space_On_Paging_File(void)
{
    KASSERT(!Interrupts_Enabled());
    return Find_First_Free_Bit(Bitmap, numPagingDiskPages);    //寻找并分配空间
    //TODO("Find free page in paging file");
}

/**
 * Free a page-sized chunk of disk space in the paging file.为页面文件释放磁盘块
 * Interrupts must be disabled.
 * @param pagefileIndex index of the chunk of disk space
 */
void Free_Space_On_Paging_File(int pagefileIndex)
{
    KASSERT(!Interrupts_Enabled());				//先屏蔽中断
	// pagefileIndex必须大于等于0且必须小于磁盘中的页面数
	KASSERT(0 <= pagefileIndex && pagefileIndex < numPagingDiskPages);
	Clear_Bit(Bitmap,pagefileIndex);				//释放空间
    //TODO("Free page in paging file");
}

/**
 * Write the contents of given page to the indicated block	向内存页写入数据
 * of space in the paging file.
 * @param paddr a pointer to the physical memory of the page
 * @param vaddr virtual address where page is mapped in user memory
 * @param pagefileIndex the index of the page sized chunk of space
 *   in the paging file
 */
void Write_To_Paging_File(void *paddr, ulong_t vaddr, int pagefileIndex)
{
    struct Page *page = Get_Page((ulong_t) paddr);
    KASSERT(!(page->flags & PAGE_PAGEABLE)); /* Page must be locked! */
	KASSERT((page->flags & PAGE_LOCKED)); 
	// pagefileIndex必须大于等于0且必须小于磁盘中的页面数
	if(0<=pagefileIndex && pagefileIndex<numPagingDiskPages)
	{
		int i;
		for(i=0;i<SECTORS_PER_PAGE;i++)		//对于每一页的扇区
		{
			Block_Write						//调用Block_Write实现写的功能
			(	
				pagingDevice->dev,
				pagefileIndex*SECTORS_PER_PAGE+ i + (pagingDevice->startSector),
				paddr+i*SECTOR_SIZE
			);   			
		}
		Set_Bit(Bitmap,pagefileIndex); //给该页面分配空间(在bitset.c中定义)
	}
	else
	{
		Print("Write_To_Paging_File: pagefileIndex out of range!\n");
		Exit(-1);
	}
    //TODO("Write page data to paging file");
}

/**
 * Read the contents of the indicated block    读取内存页的数据
 * of space in the paging file into the given page.
 * @param paddr a pointer to the physical memory of the page
 * @param vaddr virtual address where page will be re-mapped in
 *   user memory
 * @param pagefileIndex the index of the page sized chunk of space
 *   in the paging file
 */
void Read_From_Paging_File(void *paddr, ulong_t vaddr, int pagefileIndex, pte_t * entry)
{
    struct Page *page = Get_Page((ulong_t) paddr);
	page->flags = page->flags & ~PAGE_PAGEABLE;
    KASSERT(!(page->flags & PAGE_PAGEABLE)); /* Page must be locked! */
	// pagefileIndex必须大于等于0且必须小于磁盘中的页面数
	if(0 <= pagefileIndex && pagefileIndex < numPagingDiskPages)
	{
		int i;
		for(i=0;i<SECTORS_PER_PAGE;i++)	//对于每一页的扇区
		{
			Block_Read					//调用Block_Read实现读的功能
			(
				pagingDevice->dev,
				pagefileIndex*SECTORS_PER_PAGE+i+(pagingDevice->startSector),
				paddr+i*SECTOR_SIZE
			);   			
		}
		Clear_Bit(Bitmap,pagefileIndex);   	//释放已分配给该页面的空间
	}
	else
	{
		Print("Read_From_Paging_File: pagefileIndex out of range!\n");
	}         
	page->vaddr = vaddr;	//把vaddr赋给该页映射到的用户空间虚地址
	page->entry = entry;	//把entry赋给该页页表的入口
	page->flags |= PAGE_PAGEABLE;    //该页可以被换出
    //TODO("Read page data from paging file");
}
/*
Update_Clock用于修改clock的值
页面数据结构Page的clock域
*/
void Update_Clock() 
{	
	int i;	
	for( i = 0; i < s_numPages; i++) //对于每一个物理页面
	{
		//如果当前页面可以被换出且找到入口且已被分配
		if(g_pageList[i].flags & PAGE_PAGEABLE && g_pageList[i].entry && 
		   g_pageList[i].flags & PAGE_ALLOCATED)
		{
			if(g_pageList[i].entry->accesed) 		//如果入口已被访问
			{				
				g_pageList[i].clock = g_numTicks;	//修改clock值
				g_pageList[i].entry->accesed = 0;		//accessed清零
			}	
		}	
	}	
}

void Page_Fault_Handler(struct Interrupt_State* state)
{
    ulong_t address;
    faultcode_t faultCode;
    KASSERT(!Interrupts_Enabled());
	Update_Clock();						//调用Update_Clock函数修改clock值
    address = Get_Page_Fault_Address();		//获取引起缺页中断的地址
    Debug("Page fault @%lx\n", address);		//打印引起缺页中断的地址
    faultCode = *((faultcode_t *) &(state->errorCode)); //获取错误代码
	//当前进程的用户上下文
	struct User_Context* userContext = g_currentThread->userContext;
	//写错误，缺页情况为堆栈生长到新页，即第一种缺页情况
	if(faultCode.writeFault)
	{	
		int res;
	    res=Alloc_User_Page(userContext->pageDir,Round_Down_To_Page(address),PAGE_SIZE);		//分配一个新页进程继续
		if(res==-1)							//若分配失败
		{
			Exit(-1);							//终止用户进程
		}
		return ;
	}
	//读错误，分两种缺页情况，即第二种和第三种缺页情况
	else
	{	
		//先找到虚拟地址对应的页表项
		ulong_t page_dir_addr=address >> 22;		//页目录地址
		ulong_t page_addr=(address << 10) >> 22;	//页地址
		pde_t * page_dir_entry=(pde_t*)userContext->pageDir+page_dir_addr;//目录入口
		pte_t * page_entry= NULL;				//页表的入口
		if(page_dir_entry->present)				//如果相应页在内存中
		{
			page_entry=(pte_t*)((page_dir_entry->pageTableBaseAddr) << 12);
			page_entry+=page_addr;
		}
		else									//否则相应页不在内存中(在磁盘中)
		{	
			//非法地址访问的缺页情况，即第三种缺页情况
			Print_Fault_Info(address,faultCode);	//打印出错信息
			Exit(-1);							//终止用户进程
		}
		if(page_entry->kernelInfo!=KINFO_PAGE_ON_DISK)
		{
			//非法地址访问的缺页情况，即第三种缺页情况
			Print_Fault_Info(address,faultCode);	//打印出错信息
			Exit(-1);							//终止用户进程
		}
		//因为页保存在磁盘pagefile引起的缺页，即第二种缺页情况
		int pagefile_index = page_entry->pageBaseAddr;
		void * paddr=Alloc_Pageable_Page(page_entry,Round_Down_To_Page(address));
		if(paddr==NULL)				//无可分配的新页
		{
			Print("no more page\n");		//无可分配的新页
			Exit(-1);					//终止用户进程
		}
		*((uint_t*)page_entry)=0;
		page_entry->present=1;
		page_entry->flags=VM_WRITE | VM_READ | VM_USER;
		page_entry->globalPage = 0;
		page_entry->pageBaseAddr = (ulong_t)paddr>>12;
		Enable_Interrupts();						//开中断
		Read_From_Paging_File(paddr,Round_Down_To_Page(address), 
                      pagefile_index,page_entry); //从page file读入需要的页并继续
		Disable_Interrupts();						//关中断
		Free_Space_On_Paging_File(pagefile_index); 	//释放页面文件中的空间
		return ;
	}
}