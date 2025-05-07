#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "syscall.h"
#include "defs.h"

// Fetch the uint64 at addr from the current process.
int
fetchaddr(uint64 addr, uint64 *ip)
{
  struct proc *p = myproc();
  if(addr >= p->sz || addr+sizeof(uint64) > p->sz) // both tests needed, in case of overflow
    return -1;
  if(copyin(p->pagetable, (char *)ip, addr, sizeof(*ip)) != 0)
    return -1;
  return 0;
}

// Fetch the nul-terminated string at addr from the current process.
// Returns length of string, not including nul, or -1 for error.
// 一些系统调用传递指针作为参数，而内核必须使用这些指针来读取或写入用户内存。
// 例如，exec系统调用会向内核传递一个指向用户空间中的字符串的指针数组。
// 这些指针带来了两个挑战。首先，用户程序可能是错误的或恶意的，
// 可能会传递给内核一个无效的指针或一个旨在欺骗内核访问内核内存而不是用户内存的指针。
// 第二，xv6内核页表映射与用户页表映射不一样，所以内核不能使用普通指令从用户提供的地址加载或存储。

// 内核实现了安全地将数据复制到用户提供的地址或从用户提供的地址复制数据的函数。例如fetchstr


// 是用户态传给内核的字符串的指针,现在它自己已经被转成内核态虚拟地址了,但是它指向的是用户态页面的地址
int
fetchstr(uint64 addr, char *buf, int max)
{
  struct proc *p = myproc();
  // copyinstr（kernel/vm.c:406）将用户页表pagetable中的虚拟地址srcva复制到dst，需指定最大复制字节数。
  // 它使用walkaddr（调用walk函数）在软件中模拟分页硬件的操作，以确定srcva的物理地址pa0。
  // walkaddr（kernel/vm.c:95）检查用户提供的虚拟地址是否是进程用户地址空间的一部分，
  // 所以程序不能欺骗内核读取其他内存。类似的函数copyout，可以将数据从内核复制到用户提供的地址。
  if(copyinstr(p->pagetable, buf, addr, max) < 0)
    return -1;
  return strlen(buf);
}


// 因为用户代码调用系统调用的包装函数，参数首先会存放在寄存器中，
// 这是C语言存放参数的惯例位置。内核trap代码将用户寄存器保存到当前进程的trap frame中，
// 内核代码可以在那里找到它们。函数argint、argaddr和argfd从trap frame中以整数、指针或
// 文件描述符的形式检索第n个系统调用参数。它们都调用argraw来获取保存的用户寄存器
static uint64
argraw(int n)
{
  struct proc *p = myproc();
  switch (n) {
  case 0:
    return p->trapframe->a0;
  case 1:
    return p->trapframe->a1;
  case 2:
    return p->trapframe->a2;
  case 3:
    return p->trapframe->a3;
  case 4:
    return p->trapframe->a4;
  case 5:
    return p->trapframe->a5;
  }
  panic("argraw");
  return -1;
}

// Fetch the nth 32-bit system call argument.
void
argint(int n, int *ip)
{
  *ip = argraw(n);
}

// Retrieve an argument as a pointer.
// Doesn't check for legality, since
// copyin/copyout will do that.
void
argaddr(int n, uint64 *ip)
{
  *ip = argraw(n);
}

// Fetch the nth word-sized system call argument as a null-terminated string.
// Copies into buf, at most max.
// Returns string length if OK (including nul), -1 if error.
// 将程的用户态的trapframe->a[N]存的字符串的指针,将其所指向的用户态字符串,拷贝到内核里面的 buf中去
int
argstr(int n, char *buf, int max)
{
  uint64 addr;
  argaddr(n, &addr);  // addr 就是进程的trapframe->a[N]
  return fetchstr(addr, buf, max);
}

// Prototypes for the functions that handle system calls.
extern uint64 sys_fork(void);
extern uint64 sys_exit(void);
extern uint64 sys_wait(void);
extern uint64 sys_pipe(void);
extern uint64 sys_read(void);
extern uint64 sys_kill(void);
extern uint64 sys_exec(void);
extern uint64 sys_fstat(void);
extern uint64 sys_chdir(void);
extern uint64 sys_dup(void);
extern uint64 sys_getpid(void);
extern uint64 sys_sbrk(void);
extern uint64 sys_sleep(void);
extern uint64 sys_uptime(void);
extern uint64 sys_open(void);
extern uint64 sys_write(void);
extern uint64 sys_mknod(void);
extern uint64 sys_unlink(void);
extern uint64 sys_link(void);
extern uint64 sys_mkdir(void);
extern uint64 sys_close(void);

// An array mapping syscall numbers from syscall.h
// to the function that handles the system call.
static uint64 (*syscalls[])(void) = {
[SYS_fork]    sys_fork,
[SYS_exit]    sys_exit,
[SYS_wait]    sys_wait,
[SYS_pipe]    sys_pipe,
[SYS_read]    sys_read,
[SYS_kill]    sys_kill,
[SYS_exec]    sys_exec,
[SYS_fstat]   sys_fstat,
[SYS_chdir]   sys_chdir,
[SYS_dup]     sys_dup,
[SYS_getpid]  sys_getpid,
[SYS_sbrk]    sys_sbrk,
[SYS_sleep]   sys_sleep,
[SYS_uptime]  sys_uptime,
[SYS_open]    sys_open,
[SYS_write]   sys_write,
[SYS_mknod]   sys_mknod,
[SYS_unlink]  sys_unlink,
[SYS_link]    sys_link,
[SYS_mkdir]   sys_mkdir,
[SYS_close]   sys_close,
};

void
syscall(void)
{
  int num;
  struct proc *p = myproc();

  num = p->trapframe->a7;  // 系统号的约定存放位置
  if(num > 0 && num < NELEM(syscalls) && syscalls[num]) {
    // Use num to lookup the system call function for num, call it,
    // and store its return value in p->trapframe->a0
    // 将系统调用的值放在a0中,随着 usertrapret,userret返回给用户态的调用
    // 比如在用户态程序中写的  a = exec(xx); 则a的值就会是这个a0寄存器的值
    p->trapframe->a0 = syscalls[num]();
  } else {
    printf("%d %s: unknown sys call %d\n",
            p->pid, p->name, num);
    p->trapframe->a0 = -1;
  }
}
