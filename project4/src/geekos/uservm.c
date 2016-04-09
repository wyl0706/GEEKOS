/*
 * Paging-based user mode implementation
 * Copyright (c) 2003,2004 David H. Hovemeyer <daveho@cs.umd.edu>
 * $Revision: 1.50 $
 * 
 * This is free software.  You are permitted to use,
 * redistribute, and modify it as specified in the file "COPYING".
 */

#include <geekos/int.h>
#include <geekos/mem.h>
#include <geekos/paging.h>
#include <geekos/malloc.h>
#include <geekos/string.h>
#include <geekos/argblock.h>
#include <geekos/kthread.h>
#include <geekos/range.h>
#include <geekos/vfs.h>
#include <geekos/user.h>

/* ----------------------------------------------------------------------
 * Private functions
 * ---------------------------------------------------------------------- */

// TODO: Add private functions
/* ----------------------------------------------------------------------
 * Public functions
 * ---------------------------------------------------------------------- */

/*
 * Destroy a User_Context object, including all memory
 * and other resources allocated within it.
 */

bool Free_Pages_User_Process(pde_t * page_dir)
{
    pde_t * pdir;                   //页目录指针，临时变量
    bool flag=Begin_Int_Atomic();   //开始原始中断标志（关中断）
    if(page_dir==NULL)          //若用户进程的页目录为空则直接返回
    {
        return true;
    }
    for(pdir=page_dir+NUM_PAGE_DIR_ENTRIES/2; pdir < page_dir+
       NUM_PAGE_DIR_ENTRIES; pdir++)    //从进程的第一个页目录到最后一个
    {
        pte_t * ptable;
        pte_t * ptable_first;
        if(!pdir->present)  //如果页目录不在内存中
        {
            continue;
        }
        ptable_first=(pte_t*) (pdir->pageTableBaseAddr << 12);//基址按位左移12位
        for(ptable=ptable_first; ptable<ptable_first+//从进程的第一个页表到最后一个
           NUM_PAGE_TABLE_ENTRIES; ptable++)        
        {
            if(ptable->present) //如果相应页表在内存中则释放其占用的内存空间
            {
                Free_Page( (void*) (ptable->pageBaseAddr << 12));
            } 
            else if(ptable->kernelInfo==KINFO_PAGE_ON_DISK) //当页在pagefile中
            {
                //pte_t结构中的pageBaseAddr指示了页在pagefile中的位置
                Free_Space_On_Paging_File(ptable->pageBaseAddr);    //(7.1.5已定义)            
            }
        }
        Free_Page(ptable_first);                //释放第一个页表占用的内存空间
    }
    Free_Page(page_dir);                    //释放页目录所占的物理内存空间
    End_Int_Atomic(flag);                   //结束原始中断（开中断）
    return true;
}


void Destroy_User_Context(struct User_Context* userContext)
{
    /*
     * Hints:
     * - Free all pages, page tables, and page directory for
     *   the process (interrupts must be disabled while you do this,
     *   otherwise those pages could be stolen by other processes)
     * - Free semaphores, files, and other resources used
     *   by the process
     */
    //TODO("Destroy User_Context data structure after process exits");
     Free_Segment_Descriptor(userContext->ldtDescriptor);   //释放占用的LDT
    Set_PDBR(g_kernel_pde);                         //重新设置CR3
    if(userContext->pageDir!=NULL)      //释放用户进程页面所占用的内存空间
    {
        Free_Pages_User_Process(userContext->pageDir);
    }
    userContext->pageDir = 0;
    Free(userContext);                  //释放userContext本身占用的内存
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
     * - This will be similar to the same function in userseg.c
     * - Determine space requirements for code, data, argument block,
     *   and stack
     * - Allocate pages for above, map them into user address
     *   space (allocating page directory and page tables as needed)
     * - Fill in initial stack pointer, argument block address,
     *   and code entry point fields in User_Context
     (1）计算用户内存空间的大小：用户内存空间的大小 = 代码段或数据段中最高地址（对齐到页大小） + 堆栈段大小 + 参数块大小；
    （2）创建用户上下文，根据用户内存空间大小开辟用户内存空间，并初始化之；
    （3）将文件缓冲区中的段装入新建的用户内存空间中；
    （4）利用GeekOS中相应函数构造参数块；
    （5）初始化代码段入口地址entryAddr（注意：不是代码段的起始偏移地址），参数块偏移地址argBlockAddr，用户堆栈栈底偏移地址stackPointAddr；
    （6）初始化ldt相关数据，步骤为：①调用Allocate_Segment_Descriptor函数新建一个LDT描述符；②调用Init_LDT_Descriptor函数初始化新建立的LDT描述符；③调用Selector函数新建一个LDT选择子；④调用Init_Code_Segment_
    Descriptor函数新建一个代码段描述符；⑤调用Init_Data_Segment_Descriptor函数新建一个数据段描述符；⑥调用Selector新建一个数据段选择子；⑦调用Selector新建一个代码段选择子。
     */
    //TODO("Load user program into address space");
    struct User_Context *uContext;     //用户上下文
    uContext=Create_User_Context();     //创建用户上下文
    //-----首先处理pUserContext中涉及分段机制的选择子，描述符等结构-----
    uContext->ldtDescriptor=Allocate_Segment_Descriptor();  //步骤（6）①
    if(uContext->ldtDescriptor==NULL)
    {
        Print("allocate segment descriptor fail\n");    
        return  -1;
    }
    Init_LDT_Descriptor(uContext->ldtDescriptor,uContext->ldt,NUM_USER_LDT_ENTRIES);  //步骤（6）②
    uContext->ldtSelector=Selector(USER_PRIVILEGE,true,Get_Descriptor_Index(uContext->ldtDescriptor));     //步骤（6）③
    //注意：在GeekOS的分页机制下，用户地址空间默认从线性地址2G开始
    Init_Code_Segment_Descriptor(&uContext->ldt[0],USER_VM_START,USER_VM_LEN/PAGE_SIZE,USER_PRIVILEGE); //步骤（6）④
    Init_Data_Segment_Descriptor(&uContext->ldt[1],USER_VM_START,USER_VM_LEN/PAGE_SIZE,USER_PRIVILEGE); //步骤（6）⑤
    uContext->csSelector=Selector(USER_PRIVILEGE,false,0); //步骤（6）⑥
    uContext->dsSelector=Selector(USER_PRIVILEGE,false,1); //步骤（6）⑦
    //-----其次处理分页涉及的数据-----
    pde_t * pageDirectory;              //页目录指针
    pageDirectory=(pde_t *)Alloc_Page();    //为其分配一页
    if(pageDirectory==NULL)         //无可分配的页
    {
        Print("no more page!\n");
        return -1;
    }
    //将页目录表项中所有的位清0
    memset(pageDirectory,0,PAGE_SIZE);          
    //将内核页目录复制到用户态进程的页目录中
    memcpy(pageDirectory,g_kernel_pde,PAGE_SIZE);
    //将用户态进程对应高2G线性地址的页目录表项置为0
    //用户态进程中高2G的线性地址在GeekOS中为用户空间
    memset(pageDirectory+512,0,PAGE_SIZE/2);
    uContext->pageDir=pageDirectory;    //当前用户进程的页目录
    int i;                              //用于控制循环次数
    int res;                            //返回值（临时变量）
    uint_t  startAddress=0;         //可执行文件的起始地址
    uint_t  sizeInMemory=0;         //可执行文件在内存中的大小
    uint_t  offsetInFile=0;             //可执行文件中的段偏移
    uint_t  lengthInFile=0;         //可执行文件中的段数据长度
    for(i=0; i<exeFormat->numSegments; i++) //对于每一段
    {
        startAddress = exeFormat->segmentList[i].startAddress;      //起始地址
        sizeInMemory = exeFormat->segmentList[i].sizeInMemory;  //内存中的大小
        offsetInFile = exeFormat->segmentList[i].offsetInFile;          //段偏移
        lengthInFile = exeFormat->segmentList[i].lengthInFile;      //段数据长度
        //如果超出用户虚拟地址空间的长度
        if(startAddress + sizeInMemory < USER_VM_LEN)   
        {
                //在GeekOS中用户地址空间默认从线性地址2G开始
            res=Alloc_User_Page(pageDirectory,startAddress+USER_VM_START,sizeInMemory);      //给用户进程分配页面
            if(res!=0)                              //若分配失败
            {
                return -1;
            }
            //将存储在缓冲区中的段信息读入到线性地址对应的页中
            res=Copy_User_Page(pageDirectory,startAddress+USER_VM_START,exeFileData+offsetInFile,lengthInFile);
            if(res!=true)                           //若读入失败
            {
                return -1;
            }
        }   
        else    
        {
            Print("startAddress+sizeInMemory > 2GB in Load_User_Program\n");
            return -1;
        }
    }
    //-----再次处理参数块与堆栈块-----
    uint_t  args_num;   //参数块数目
    uint_t  stack_addr; //堆栈块地址
    uint_t  arg_addr;       //参数块地址
    ulong_t arg_size;       //参数块大小
    Get_Argument_Block_Size(command, &args_num, &arg_size); //获取参数块大小
    if(arg_size > PAGE_SIZE)    //如果参数块大小大于页面大小
    {
        Print("Argument Block too big for one PAGE_SIZE\n");
        return -1;
    }
    //分配参数块所需页
    arg_addr=Round_Down_To_Page(USER_VM_LEN-arg_size);  //与4K边界对齐
    char* block_buffer=Malloc(arg_size);    //分配参数块大小的内存
    KASSERT(block_buffer!=NULL);    //不允许为空
    Format_Argument_Block(block_buffer,args_num,arg_addr,command);//格式化参数块
    res=Alloc_User_Page(pageDirectory, arg_addr+USER_VM_START, arg_size);
    if(res!=0)                          //如果分配参数块所需页失败
    {
        return -1;
    }
    res=Copy_User_Page(pageDirectory, arg_addr+USER_VM_START, block_buffer,arg_size);
    if(res!=true)
    {
        return -1;
    }
    Free(block_buffer);                 //释放内存
    //分配堆栈所需页
    stack_addr=USER_VM_LEN-Round_Up_To_Page(arg_size)-DEFAULT_STACK_SIZE;
    res=Alloc_User_Page(pageDirectory,stack_addr+USER_VM_START,DEFAULT_STACK_SIZE);
    if(res!=0)                          //如果分配堆栈所需页失败
    {
        return -1;
    }
    //-----最后处理pUserContext的剩余信息-----
    uContext->entryAddr = exeFormat->entryAddr;
    uContext->argBlockAddr = arg_addr;
    uContext->size = USER_VM_LEN;
    uContext->stackPointerAddr = arg_addr;
    *pUserContext=uContext;
    return 0;
}

/*
 * Copy data from user buffer into kernel buffer.用户缓冲区中将数据复制到内核缓冲
 * Returns true if successful, false otherwise.
 */
bool Copy_From_User(void* destInKernel, ulong_t srcInUser, ulong_t numBytes)
{
    /*
     * Hints:
     * - Make sure that user page is part of a valid region
     *   of memory
     * - Remember that you need to add 0x80000000 to user addresses
     *   to convert them to kernel addresses, because of how the
     *   user code and data segments are defined
     * - User pages may need to be paged in from disk before being accessed.
     * - Before you touch (read or write) any data in a user
     *   page, **disable the PAGE_PAGEABLE bit**.
     *
     * Be very careful with race conditions in reading a page from disk.
     * Kernel code must always assume that if the struct Page for
     * a page of memory has the PAGE_PAGEABLE bit set,
     * IT CAN BE STOLEN AT ANY TIME.  The only exception is if
     * interrupts are disabled; because no other process can run,
     * the page is guaranteed not to be stolen.
     */
    struct User_Context* userContext=g_currentThread->userContext; //当前进程上下文
    void* user_lin_addr=(void*)(USER_VM_START)+srcInUser;       //用户线性地址
    if((srcInUser+numBytes) < userContext->size)    //若内核缓冲区的空间足够大
    {
        memcpy(destInKernel, user_lin_addr, numBytes); //将数据从用户复制到内核
        return true;
    }
    return false;
    //TODO("Copy user data to kernel buffer");
}

/*
 * Copy data from kernel buffer into user buffer.内核缓冲区中将数据复制到用户缓冲区
 * Returns true if successful, false otherwise.
 */
bool Copy_To_User(ulong_t destInUser, void* srcInKernel, ulong_t numBytes)
{
    /*
     * Hints:
     * - Same as for Copy_From_User()
     * - Also, make sure the memory is mapped into the user
     *   address space with write permission enabled
     */
    struct User_Context* userContext=g_currentThread->userContext;//当前进程上下文
    void* user_lin_addr = (void*)(USER_VM_START) + destInUser;  //用户线性地址
    if((destInUser+numBytes) < userContext->size)   //若用户缓冲区的空间足够大
    {
        memcpy(user_lin_addr, srcInKernel ,numBytes);//将数据从内核复制到用户
        return true;
    }
    return false;
    //TODO("Copy kernel data to user buffer");
}

/*
 * Switch to user address space.装载相应页目录和LDT来切换到一个用户地址空间
 */
void Switch_To_Address_Space(struct User_Context *userContext)
{
    /*
     * - If you are still using an LDT to define your user code and data
     *   segments, switch to the process's LDT
     * - 
     */
    //TODO("Switch_To_Address_Space() using paging");
    if(userContext == 0)
    {
        Print("the userContext is NULL!\n");
        return;
    }
    Set_PDBR(userContext->pageDir); //设置CR3寄存器为当前用户进程的页目录
    Load_LDTR(userContext->ldtSelector);// 将用户ldt选择子导入ldt寄存器
}


