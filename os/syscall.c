#include "syscall.h"
#include "console.h"
#include "defs.h"
#include "loader.h"
#include "syscall_ids.h"
#include "timer.h"
#include "trap.h"
#include "taskinfo.h"
#include "string.h"
#include "fcntl.h"

#include <unistd.h>

uint64 console_write(uint64 va, uint64 len)
{
	struct proc *p = curr_proc();
	char str[MAX_STR_LEN];
	int size = copyinstr(p->pagetable, str, va, MIN(len, MAX_STR_LEN));
	tracef("write size = %d", size);
	for (int i = 0; i < size; ++i) {
		console_putchar(str[i]);
	}
	return len;
}

uint64 console_read(uint64 va, uint64 len)
{
	struct proc *p = curr_proc();
	char str[MAX_STR_LEN];
	tracef("read size = %d", len);
	for (int i = 0; i < len; ++i) {
		int c = consgetc();
		str[i] = c;
	}
	copyout(p->pagetable, va, str, len);
	return len;
}

uint64 sys_write(int fd, uint64 va, uint64 len)
{
	if (fd < 0 || fd > FD_BUFFER_SIZE)
		return -1;
	struct proc *p = curr_proc();
	struct file *f = p->files[fd];
	if (f == NULL) {
		errorf("invalid fd %d\n", fd);
		return -1;
	}
	switch (f->type) {
	case FD_STDIO:
		return console_write(va, len);
	case FD_INODE:
		return inodewrite(f, va, len);
	default:
		panic("unknown file type %d\n", f->type);
	}
}

uint64 sys_read(int fd, uint64 va, uint64 len)
{
	if (fd < 0 || fd > FD_BUFFER_SIZE)
		return -1;
	struct proc *p = curr_proc();
	struct file *f = p->files[fd];
	if (f == NULL) {
		errorf("invalid fd %d\n", fd);
		return -1;
	}
	switch (f->type) {
	case FD_STDIO:
		return console_read(va, len);
	case FD_INODE:
		return inoderead(f, va, len);
	default:
		panic("unknown file type %d\n", f->type);
	}
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

uint64 sys_getpid()
{
	return curr_proc()->pid;
}

uint64 sys_getppid()
{
	struct proc *p = curr_proc();
	return p->parent == NULL ? IDLE_PID : p->parent->pid;
}

uint64 sys_clone()
{
	debugf("fork!");
	return fork();
}

static inline uint64 fetchaddr(pagetable_t pagetable, uint64 va)
{
	uint64 *addr = (uint64 *)useraddr(pagetable, va);
	return *addr;
}

uint64 sys_exec(uint64 path, uint64 uargv)
{
	struct proc *p = curr_proc();
	char name[MAX_STR_LEN];
	copyinstr(p->pagetable, name, path, MAX_STR_LEN);
	uint64 arg;
	static char strpool[MAX_ARG_NUM][MAX_STR_LEN];
	char *argv[MAX_ARG_NUM];
	int i;
	for (i = 0; uargv && (arg = fetchaddr(p->pagetable, uargv));
	     uargv += sizeof(char *), i++) {
		copyinstr(p->pagetable, (char *)strpool[i], arg, MAX_STR_LEN);
		argv[i] = (char *)strpool[i];
	}
	argv[i] = NULL;
	return exec(name, (char **)argv);
}

uint64 sys_wait(int pid, uint64 va)
{
	struct proc *p = curr_proc();
	int *code = (int *)useraddr(p->pagetable, va);
	return wait(pid, code);
}

uint64 sys_spawn(uint64 path)
{
    struct proc *p = curr_proc();
    char name[MAX_STR_LEN];

    copyinstr(p->pagetable, name, path, MAX_STR_LEN);

    return spawn(name);
}
uint64 sys_set_priority(long long prio) 
{
	struct proc *p = curr_proc();
	if (prio < 2 || prio > __INT_MAX__) // check if priority is valid
	{
		return -1;
	}
	p->priority = prio;
	p->pass = BIG_STRIDE / prio;
    return prio;
}

uint64 sys_openat(uint64 va, uint64 omode, uint64 _flags)
{
	struct proc *p = curr_proc();
	char path[200];
	copyinstr(p->pagetable, path, va, 200);
	return fileopen(path, omode);
}

uint64 sys_close(int fd)
{
	if (fd < 0 || fd > FD_BUFFER_SIZE)
		return -1;
	struct proc *p = curr_proc();
	struct file *f = p->files[fd];
	if (f == NULL) {
		errorf("invalid fd %d", fd);
		return -1;
	}
	fileclose(f);
	p->files[fd] = 0;
	return 0;
}

int sys_fstat(int fd,uint64 stat)
{
    if (fd < 0 || fd > FD_BUFFER_SIZE)
        return -1;

    struct proc *p = curr_proc();
    struct file *f = p->files[fd];
    if (f == NULL || f->type != FD_INODE)
        return -1;

    struct inode *ip = f->ip;
    ivalid(ip);

    struct Stat st;
    st.dev   = 0;
    st.ino   = ip->inum;
    st.mode  = (ip->type == T_DIR) ? DIR : FILE;
    st.nlink = ip->nlink;
    memset(st.pad, 0, sizeof(st.pad));

    // Write Stat struct back to user space VA
    if (copyout(p->pagetable, stat, (char *)&st, sizeof(st)) < 0)
        return -1;

    return 0;
}

int sys_linkat(int olddirfd, char *oldpath, int newdirfd, char *newpath, unsigned int flags)
{
    // olddirfd, newdirfd, flags ignored per spec

    // Reject same-name link (only defined error case)
    if (strncmp(oldpath, newpath, DIRSIZ) == 0)
        return -1;

    // Look up the existing file's inode
    struct inode *ip = namei(oldpath);
    if (ip == 0)
        return -1;
    ivalid(ip);

    // Don't allow hard-linking directories
    if (ip->type == T_DIR) {
        iput(ip);
        return -1;
    }

    // Add a new dirent in root_dir pointing to the same inode number
    struct inode *dp = root_dir();
    ivalid(dp);
    if (dirlink(dp, newpath, ip->inum) < 0) {
        iput(dp);
        iput(ip);
        return -1;
    }
    iput(dp);

    // Increment nlink and write back to disk
    ip->nlink++;
    iupdate(ip);
    iput(ip);
    return 0;
}

int sys_unlinkat(int dirfd, uint64 name, uint64 flags)
{
    // dirfd and flags ignored per spec

    // Copy the path string from user space (same pattern as sys_openat)
    struct proc *p = curr_proc();
    char path[200];
    copyinstr(p->pagetable, path, name, 200);

    // Get root directory inode
    struct inode *dp = root_dir();
    ivalid(dp);

    // Find the dirent for this name, get its offset in the directory
    uint off;
    struct inode *ip = dirlookup(dp, path, &off);
    if (ip == 0) {
        iput(dp);
        return -1;  // file does not exist
    }
    ivalid(ip);

    // Zero out the dirent on disk to remove the directory entry
    struct dirent de;
    memset(&de, 0, sizeof(de));
    if (writei(dp, 0, (uint64)&de, off, sizeof(de)) != sizeof(de)) {
        iput(ip);
        iput(dp);
        return -1;
    }
    iput(dp);

    // Decrement link count and persist
    ip->nlink--;
    iupdate(ip);

    // If no links remain, free all data blocks and mark inode free
    if (ip->nlink == 0) {
        itrunc(ip);
        ip->type = 0;
        iupdate(ip);
    }

    iput(ip);
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

	switch (id) {
	case SYS_write:
		ret = sys_write(args[0], args[1], args[2]);
		break;
	case SYS_read:
		ret = sys_read(args[0], args[1], args[2]);
		break;
	case SYS_openat:
		ret = sys_openat(args[0], args[1], args[2]);
		break;
	case SYS_close:
		ret = sys_close(args[0]);
		break;
	case SYS_exit:
		sys_exit(args[0]);
		// __builtin_unreachable();
	case SYS_sched_yield:
		ret = sys_sched_yield();
		break;
	case SYS_gettimeofday:
		ret = sys_gettimeofday((TimeVal*)args[0], args[1]);
		break;
	case SYS_task_info:
		ret = sys_task_info((TaskInfo *)args[0]);
		break;
	case SYS_mmap:
		ret = sys_mmap(args[0], args[1], args[2], args[3], args[4]);
		break;
	case SYS_munmap:
		ret = sys_munmap(args[0], args[1]);
		break;
	case SYS_getpid:
		ret = sys_getpid();
		break;
	case SYS_getppid:
		ret = sys_getppid();
		break;
	case SYS_clone: // SYS_fork
		ret = sys_clone();
		break;
	case SYS_execve:
		ret = sys_exec(args[0], args[1]);
		break;
	case SYS_wait4:
		ret = sys_wait(args[0], args[1]);
		break;
	case SYS_fstat:
	    ret = sys_fstat(args[0],args[1]);
		break;
	case SYS_linkat: {
		struct proc *p = curr_proc();
		char oldpath[200];
		char newpath[200];
		copyinstr(p->pagetable, oldpath, args[1], 200);
		copyinstr(p->pagetable, newpath, args[3], 200);
		ret = sys_linkat(args[0], oldpath, args[2], newpath, args[4]);
		break;
}
	case SYS_unlinkat:
	    ret = sys_unlinkat(args[0],args[1],args[2]);
		break;
	case SYS_spawn:
		console_putchar('S');
		ret = sys_spawn(args[0]);
		break;
	case SYS_setpriority:
		ret = sys_set_priority(args[0]);
		break;
	default:
		ret = -1;
		errorf("unknown syscall %d", id);
	}
	trapframe->a0 = ret;
	tracef("syscall ret %d", ret);
}
