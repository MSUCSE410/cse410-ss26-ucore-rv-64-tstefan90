#include "syscall.h"
#include "defs.h"
#include "loader.h"
#include "syscall_ids.h"
#include "timer.h"
#include "trap.h"
#include "taskinfo.h"
#include "vm.h"

uint64 sys_write(int fd, uint64 va, uint len)
{
	debugf("sys_write fd = %d va = %x, len = %d", fd, va, len);
	if (fd != STDOUT)
		return -1;
	struct proc *p = curr_proc();
	char str[MAX_STR_LEN];
	int size = copyinstr(p->pagetable, str, va, MIN(len, MAX_STR_LEN));
	debugf("size = %d", size);
	for (int i = 0; i < size; ++i) {
		console_putchar(str[i]);
	}
	return size;
}

__attribute__((noreturn)) void sys_exit(int code)
{
	exit(code);
	__builtin_unreachable();
}

uint64 sys_sched_yield()
{
	yield();
	return 0;
}

uint64 sys_gettimeofday(TimeVal *val, int _tz) // TODO: implement sys_gettimeofday in pagetable. (VA to PA)
{
	// get physical address of timeval from page table
	uint64 pa = useraddr(curr_proc()->pagetable, (uint64)val);
	// YOUR CODE
	
	// pointer to physical address of timeval
	TimeVal *physical_time = (TimeVal*)pa;

	// set values of timeval at physical address
	uint64 cycle = get_cycle();
	physical_time->sec = cycle / CPU_FREQ;
	physical_time->usec = (cycle % CPU_FREQ) * 1000000 / CPU_FREQ;

	return 0;
}

// TODO: add support for mmap and munmap syscall.
// hint: read through docstrings in vm.c. Watching CH4 video may also help.
// Note the return value and PTE flags (especially U,X,W,R)
/*
* LAB1: you may need to define sys_task_info here
*/
int sys_task_info(TaskInfo *ti)
{
	//find physical address of ti from current process's page table
	uint64 pa = useraddr(curr_proc()->pagetable, (uint64)ti);

	// pointer to physical address of task
	TaskInfo* physical_task = (TaskInfo*)pa;

	struct proc *p = curr_proc();

	// set values of taskinfo at physical address

	// set process state
	switch (p->state) {
	case RUNNING:
		physical_task->status = Running;
		break;	
	case RUNNABLE:
		physical_task->status = Ready;
		break;
	case UNUSED:
		physical_task->status = Exited;
		break;
	default:	
		physical_task->status = UnInit;
		break;
	}	

	// set syscall times at syscall id
	for (int i = 0; i < MAX_SYSCALL_NUM; ++i)	
		physical_task->syscall_times[i] = p->syscall_times[i];
	

	// set task info time
	uint64 cycle = get_cycle();
	
	physical_task->time = (cycle - p->start_time) * 1000 / CPU_FREQ;

	return 0;
}

uint64 sys_mmap(uint64 start, uint64 len, int port, int flag, int fd)
{
	if (len == 0) //if length is 0, return directly
		return 0;
	if (len > (1024 * 1024 * 1024))  // 1 GB upper limit
    	return -1;
 

	// 0x7 is 00000111
	// if read, write, and execute perms are all 0, port is invalid
	if ((port & 0x7) == 0) 
		return -1;
	// if the bits above the first 3 have values, port is invalid
	if ((port & ~0x7) != 0)
		return -1;

	//ensure start is page aligned
	//in other words, make sure the start of the virtual address is at the start of the page
	if (start % PGSIZE != 0)
		return -1;
	

	uint64 va = start;
	//get the virtual end address of the mapping, round up the next page if needed
	// since its page aligned
	uint64 end = PGROUNDUP(start + len);

	int perm = PTE_U; //start with user bit

	// 0x1 = 00000001 - bit 0
	if (port & 0x1) 
		perm |= PTE_R; //add on read bit if port has it
	// 0x2 = 00000010 - bit 1
	if (port & 0x2) // add on write bit if port has it
		perm |= PTE_W;
	// 0x4 = 00000100 - bit 2
	if (port & 0x4) //add on executable bit if port has it
		perm |= PTE_X;

	struct proc *p = curr_proc();

	// iterate through pages to see if an address is already mapped
	// if so, return with an error
	for (uint64 add = va; add < end; add += PGSIZE) 
	{
		// walk the address in the current pagetable 
		if (walkaddr(p->pagetable, add) != 0)
    		return -1;
	}

	// map the pages
	for (uint64 add = va; add < end; add += PGSIZE) 
	{
		//allocate one page in physical memory
		// store the pointer in pa
		void *pa = kalloc(); 

		// if the pointer is null, return error
		if (pa == 0) 
			return -1;

		//set an entire page to 0, clear leftover data
		memset(pa, 0, PGSIZE);

		// map pages using given function
		int status = mappages(p->pagetable, add, PGSIZE, (uint64)pa, perm);

		// if map pages failed, return error
		if (status != 0)
			return -1;
	}

	//return success if all error checks pass
	return 0;
}

uint64 sys_munmap(uint64 start, uint64 len)
{
	//if len is 0, return diurectly
	if (len == 0)
        return 0;
	if (len > (1024 * 1024 * 1024))  // 1 GB upper limit
    	return -1;

	//ensure start is page aligned
    if (start % PGSIZE != 0)
        return -1;

    uint64 va  = start;
    uint64 end = PGROUNDUP(start + len);

    struct proc *p = curr_proc();

	//make sure page is mapped before unmapping
    for (uint64 a = va; a < end; a += PGSIZE) 
	{
        if (walkaddr(p->pagetable, a) == 0)
            return -1;
    }

	//unmap using given function
    uvmunmap(p->pagetable, va, (end - va) / PGSIZE, 1);

	//return success
    return 0;
}
extern char trap_page[];

void syscall()
{
	struct trapframe *trapframe = curr_proc()->trapframe;
	int id = trapframe->a7, ret;
	uint64 args[6] = { trapframe->a0, trapframe->a1, trapframe->a2,
			   trapframe->a3, trapframe->a4, trapframe->a5 };
	tracef("syscall %d args = [%x, %x, %x, %x, %x, %x]", id, args[0],
	       args[1], args[2], args[3], args[4], args[5]);
	/*
	* LAB1: you may need to update syscall counter for task info here
	*/
	curr_proc()->syscall_times[id]++;
	switch (id) {
	case SYS_write:
		ret = sys_write(args[0], args[1], args[2]);
		break;
	case SYS_exit:
		sys_exit(args[0]);
		// __builtin_unreachable();
	case SYS_sched_yield:
		ret = sys_sched_yield();
		break;
	case SYS_gettimeofday:
		ret = sys_gettimeofday((TimeVal *)args[0], args[1]);
		break;
	/*
	* LAB1: you may need to add SYS_taskinfo case here
	*/
	case SYS_task_info:
		ret = sys_task_info((TaskInfo *)args[0]);
		break;
	case SYS_getpid:
		ret = curr_proc()->pid;
		break;
	case SYS_mmap:
		ret = sys_mmap(args[0], args[1], args[2], args[3], args[4]);
		break;
	case SYS_munmap:
		ret = sys_munmap(args[0], args[1]);
		break;
	default:
		ret = -1;
		errorf("unknown syscall %d", id);
	}
	trapframe->a0 = ret;
	tracef("syscall ret %d", ret);
}
