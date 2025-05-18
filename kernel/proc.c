#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"

struct cpu cpus[NCPU];

struct proc proc[NPROC];

struct proc *initproc;

int nextpid = 1;
struct spinlock pid_lock;

extern void forkret(void);
static void freeproc(struct proc *p);

extern char trampoline[]; // trampoline.S

// helps ensure that wakeups of wait()ing
// parents are not lost. helps obey the
// memory model when using p->parent.
// must be acquired before any p->lock.
struct spinlock wait_lock;

// Allocate a page for each process's kernel stack.
// Map it high in memory, followed by an invalid
// guard page.
void
proc_mapstacks(pagetable_t kpgtbl)
{
  struct proc *p;
  
  for(p = proc; p < &proc[NPROC]; p++) {
    char *pa = kalloc();
    if(pa == 0)
      panic("kalloc");
    uint64 va = KSTACK((int) (p - proc));  // 在TRAMPOLINE下面
    //PGSIZE 而不是2*PASIZE.因为保护页不会映射到物理内存，所以不会浪费物理内存，只是占据了虚拟地址空间的一段靠后的地址。 
    kvmmap(kpgtbl, va, (uint64)pa, PGSIZE, PTE_R | PTE_W); 

  }
}

// initialize the proc table.
// 为每个进程指定内核栈 ，不是分配. 因为kvminit的时候已经分配
// 分页后调用procinit
void
procinit(void)
{
  struct proc *p;
  
  initlock(&pid_lock, "nextpid");  // 这个时候已经开启了paging MMU  全局变量在paging 之前 就已经分配
  initlock(&wait_lock, "wait_lock");
  for(p = proc; p < &proc[NPROC]; p++) {
      initlock(&p->lock, "proc");
      p->state = UNUSED;
      p->kstack = KSTACK((int) (p - proc)); // paging 后是内核态虚拟地址。 
  }
}

// Must be called with interrupts disabled,
// to prevent race with process being moved
// to a different CPU.
int
cpuid()
{
  int id = r_tp();
  return id;
}

// 下面这两个函数要注意看7.4章节 这两个函数的执行要禁用中断

// Return this CPU's cpu struct.
// Interrupts must be disabled.
struct cpu*
mycpu(void)
{
  int id = cpuid();
  struct cpu *c = &cpus[id];
  return c;
}

// Return the current struct proc *, or zero if none.
struct proc*
myproc(void)
{
  push_off();
  struct cpu *c = mycpu();
  struct proc *p = c->proc;
  pop_off();
  return p;
}

int
allocpid()
{
  int pid;
  
  acquire(&pid_lock);
  pid = nextpid;
  nextpid = nextpid + 1;
  release(&pid_lock);

  return pid;
}

// Look in the process table for an UNUSED proc.
// If found, initialize state required to run in the kernel,
// and return with p->lock held.
// If there are no free procs, or a memory allocation fails, return 0.
static struct proc*
allocproc(void)
{
  struct proc *p;

  for(p = proc; p < &proc[NPROC]; p++) {
    acquire(&p->lock);
    if(p->state == UNUSED) {
      goto found;
    } else {
      release(&p->lock);
    }
  }
  return 0;

found:
  p->pid = allocpid();
  p->state = USED;

  // Allocate a trapframe page.
  if((p->trapframe = (struct trapframe *)kalloc()) == 0){
    freeproc(p);
    release(&p->lock);
    return 0;
  }

  // An empty user page table.
  p->pagetable = proc_pagetable(p);  // pagetable  但是这个函数会为trampoline和trapframe映射虚拟地址
  if(p->pagetable == 0){
    freeproc(p);
    release(&p->lock);
    return 0;
  }

  // Set up new context to start executing at forkret,
  // which returns to user space.
  memset(&p->context, 0, sizeof(p->context));

  // forkret 第一次会帮助 进程回到用户态. 帮助释放p->lock
  // 进程从用户态回到内核态,是通过系统调用或者中断(定时中断)
  // 进程后面如果在内核态发生了调度, 现在想回到用户态,但是这时候ra不是forkret怎么办?
  // 答案: 这个时候ra虽然不是forkret的地址,也是在uservec, usertrap,usertrapret,useret这些函数的某个地址.它后续用最后的
  // useret的 sret指令返回到用户态
  // 所以总结下: p->context->ra 只会是 forkret,uservec, usertrap,usertrapret,useret 这几个函数或这个几个函数里面的函数的地址!!!

  // p->context.ra存储函数调用后的返回地址（即 call 指令下一条指令的地址）
  // swtch保存了ra寄存器，它保存了swtch应该返回的地址。现在，swtch从新的上下文中恢复寄存器，
  // 新的上下文中保存着前一次swtch所保存的寄存器值。当swtch返回时，它返回到被恢复的ra寄存器所指向的指令，
  // 也就是当这个进程第一次被执行时,就会执行forkret ,forkret会进入到用户态
  // a0 寄存器和 ra寄存器的区别举例:
  // int val = fun(); c语言代码
  // call fun; fun的栈中最后会是ret指令  汇编指令
  // mov a0, a1[0]; a1[0]就是val        汇编指令
  // 则 call fun后都ret指令执行,就会把返回值赋值给a0, 然后ra寄存器就是存的mov指令(call的下一条指令)
  p->context.ra = (uint64)forkret; 
  // 分页后调用procinit 给kstack指定了虚拟地址
  p->context.sp = p->kstack + PGSIZE;  // 由于是空的 所以要加PAGSIZE

  return p;
}

// free a proc structure and the data hanging from it,
// including user pages.
// p->lock must be held.
static void
freeproc(struct proc *p)
{
  if(p->trapframe)
    kfree((void*)p->trapframe);
  p->trapframe = 0;
  if(p->pagetable)
    proc_freepagetable(p->pagetable, p->sz);
  p->pagetable = 0;
  p->sz = 0;
  p->pid = 0;
  p->parent = 0;
  p->name[0] = 0;
  p->chan = 0;
  p->killed = 0;
  p->xstate = 0;
  p->state = UNUSED;
}

// Create a user page table for a given process, with no user memory,
// but with trampoline and trapframe pages.
pagetable_t
proc_pagetable(struct proc *p)
{
  pagetable_t pagetable;

  // An empty page table.
  pagetable = uvmcreate();
  if(pagetable == 0)
    return 0;

  // map the trampoline code (for system call return)
  // at the highest user virtual address.
  // only the supervisor uses it, on the way
  // to/from user space, so not PTE_U.
  // pagetable是用户的proc，所以是用户态的虚拟地址
  if(mappages(pagetable, TRAMPOLINE, PGSIZE,
              (uint64)trampoline, PTE_R | PTE_X) < 0){
    uvmfree(pagetable, 0);
    return 0;
  }

  // map the trapframe page just below the trampoline page, for
  // trampoline.S.   
  // pagetable是用户的proc，所以是用户态的虚拟地址。  p->trapframe是kalloc分出来的
  if(mappages(pagetable, TRAPFRAME, PGSIZE,
              (uint64)(p->trapframe), PTE_R | PTE_W) < 0){
    uvmunmap(pagetable, TRAMPOLINE, 1, 0);
    uvmfree(pagetable, 0);
    return 0;
  }

  return pagetable;
}

// Free a process's page table, and free the
// physical memory it refers to.
void
proc_freepagetable(pagetable_t pagetable, uint64 sz)
{
  uvmunmap(pagetable, TRAMPOLINE, 1, 0);
  uvmunmap(pagetable, TRAPFRAME, 1, 0);
  uvmfree(pagetable, sz);
}

// a user program that calls exec("/init")
// assembled from ../user/initcode.S
// od -t xC ../user/initcode
uchar initcode[] = {
  0x17, 0x05, 0x00, 0x00, 0x13, 0x05, 0x45, 0x02,
  0x97, 0x05, 0x00, 0x00, 0x93, 0x85, 0x35, 0x02,
  0x93, 0x08, 0x70, 0x00, 0x73, 0x00, 0x00, 0x00,
  0x93, 0x08, 0x20, 0x00, 0x73, 0x00, 0x00, 0x00,
  0xef, 0xf0, 0x9f, 0xff, 0x2f, 0x69, 0x6e, 0x69,
  0x74, 0x00, 0x00, 0x24, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00
};

// Set up first user process.
void
userinit(void)
{
  struct proc *p;

  p = allocproc();
  initproc = p;
  
  // allocate one user page and copy initcode's instructions
  // and data into it.
  uvmfirst(p->pagetable, initcode, sizeof(initcode));
  p->sz = PGSIZE;

  // prepare for the very first "return" from kernel to user.
  p->trapframe->epc = 0;      // user program counter //返回用户态的时候就会执行从epc开始的指令
  p->trapframe->sp = PGSIZE;  // user stack pointer // 跟initcode.sS 共享一页？

  safestrcpy(p->name, "initcode", sizeof(p->name));
  p->cwd = namei("/");

  p->state = RUNNABLE;

  release(&p->lock);
}

// Grow or shrink user memory by n bytes.
// Return 0 on success, -1 on failure.
int
growproc(int n)
{
  uint64 sz;
  struct proc *p = myproc();

  sz = p->sz;
  if(n > 0){
    if((sz = uvmalloc(p->pagetable, sz, sz + n, PTE_W)) == 0) {
      return -1;
    }
  } else if(n < 0){
    sz = uvmdealloc(p->pagetable, sz, sz + n);
  }
  p->sz = sz;
  return 0;
}

// Create a new process, copying the parent.
// Sets up child kernel stack to return as if from fork() system call.
int
fork(void)
{
  int i, pid;
  struct proc *np;
  struct proc *p = myproc();

  // Allocate process.
  if((np = allocproc()) == 0){
    return -1;
  }

  // Copy user memory from parent to child.
  if(uvmcopy(p->pagetable, np->pagetable, p->sz) < 0){
    freeproc(np);
    release(&np->lock);
    return -1;
  }
  np->sz = p->sz;

  // copy saved user registers.
  *(np->trapframe) = *(p->trapframe);

  // Cause fork to return 0 in the child.
  //  如果是在child进程里， fork返回的是a0的值 也就是0
  // 当child进程在cpu中执行,返回用户态,a0就会成为c函数fork的返回值
  // 这里用a0是因为这个是在parent的内核态fork函数中,如果是child的内核态也可以return 0；
  np->trapframe->a0 = 0;

  // increment reference counts on open file descriptors.
  for(i = 0; i < NOFILE; i++)
    if(p->ofile[i])
      np->ofile[i] = filedup(p->ofile[i]);
  np->cwd = idup(p->cwd);

  safestrcpy(np->name, p->name, sizeof(p->name));

  pid = np->pid;

  release(&np->lock);

  acquire(&wait_lock);
  np->parent = p;
  release(&wait_lock);

  acquire(&np->lock);
  np->state = RUNNABLE;
  release(&np->lock);
  
  // parent 进程 FORK返回子进程的PID。如果swtch切到这个进程,返回用户态c函数fork调用 a0 就会是这个pid（编译器编译的时候默认就会这样编）
  // 由于fork当前处于parent进程的内核态，直接return 即可
  return pid; 
}
// Pass p's abandoned children to init.
// Caller must hold wait_lock.
void
reparent(struct proc *p)
{
  struct proc *pp;

  for(pp = proc; pp < &proc[NPROC]; pp++){
    if(pp->parent == p){
      pp->parent = initproc;
      wakeup(initproc);
    }
  }
}

// Exit the current process.  Does not return.
// An exited process remains in the zombie state
// until its parent calls wait().
void
exit(int status)
{
  struct proc *p = myproc();

  if(p == initproc)
    panic("init exiting");

  // Close all open files.
  for(int fd = 0; fd < NOFILE; fd++){
    if(p->ofile[fd]){
      struct file *f = p->ofile[fd];
      fileclose(f);
      p->ofile[fd] = 0;
    }
  }

  begin_op();
  iput(p->cwd);
  end_op();
  p->cwd = 0;

  acquire(&wait_lock);

  // Give any children to init.
  reparent(p);

  // Parent might be sleeping in wait().
  wakeup(p->parent);
  
  acquire(&p->lock);

  p->xstate = status;
  p->state = ZOMBIE;

  release(&wait_lock);

  // Jump into the scheduler, never to return.
  sched();
  panic("zombie exit");
}

// Wait for a child process to exit and return its pid.
// Return -1 if this process has no children.
int
wait(uint64 addr)
{
  struct proc *pp;
  int havekids, pid;
  struct proc *p = myproc();

  acquire(&wait_lock);

  for(;;){
    // Scan through table looking for exited children.
    havekids = 0;
    for(pp = proc; pp < &proc[NPROC]; pp++){
      if(pp->parent == p){
        // make sure the child isn't still in exit() or swtch().
        acquire(&pp->lock);

        havekids = 1;
        if(pp->state == ZOMBIE){
          // Found one.
          pid = pp->pid;
          if(addr != 0 && copyout(p->pagetable, addr, (char *)&pp->xstate,
                                  sizeof(pp->xstate)) < 0) {
            release(&pp->lock);
            release(&wait_lock);
            return -1;
          }
          freeproc(pp);
          release(&pp->lock);
          release(&wait_lock);
          return pid;
        }
        release(&pp->lock);
      }
    }

    // No point waiting if we don't have any children.
    if(!havekids || killed(p)){
      release(&wait_lock);
      return -1;
    }
    
    // Wait for a child to exit.
    sleep(p, &wait_lock);  //DOC: wait-sleep
  }
}

// Per-CPU process scheduler.
// Each CPU calls scheduler() after setting itself up.
// Scheduler never returns.  It loops, doing:
//  - choose a process to run.
//  - swtch to start running that process.
//  - eventually that process transfers control
//    via swtch back to the scheduler.
// 详细细节 在book的7.3 Code: Scheduling 说的很清楚

// scheduler()是调度器执行进程, sched()是放弃进程的执行
// 大致流程是: 进程在中断或系统调用通过trap进入内核态(这个时候会保存sepc到trapframe->epc),
// 通过sched 放弃执行,存ra到p->context,交给scheduler,
// scheduler将cpu交给进程, 
// 进程通过ra寄存器回到trap,trap使用trapframe->epc的位置回到用户态指令执行
// 这样就通过sched scheduler 实现了循环.  trap 实现了用户态和内核态的循环切换

// 进程切换的流层图如下：
// ecall进入内核态 -> uservec-usertrap -> yield:sched
// -> scheduler（可能是任意进程） -> 执行ra的地址 -> usertrapret-userret回到用户态 -> 执行sepc的地址（第一次就是forkret）
void
scheduler(void)
{
  struct proc *p;
  struct cpu *c = mycpu();

  c->proc = 0;
  for(;;){
    // The most recent process to run may have had interrupts
    // turned off; enable them to avoid a deadlock if all
    // processes are waiting. 
    // 最近运行的进程可能关闭了中断(硬件默认进入trap就关闭了中断),为了避免所有进程等待进入死锁打开中断
    // usertrapret调用sret回到用户态的时候就打开了中断,
    // 这个地方难:
    // 1:一个进程使用uartputc因为uart缓存区满而sleep, 它需要等一个uartstart读取让出缓冲区空间来唤醒这个进程
    // 只有允许中断,才能在uartintr中调用uartstart来清除部分空间.来将这个进程唤醒. 否则这个进程就永远sleep了
    // 2: 内核的时钟中断会唤醒清空空间的进程吗?kerneltrap在调用yield的时候会判断是不是内核线程在执行(if(which_dev == 2 && myproc() != 0)),
    // 如果是进程的内核态才会调用yield,如果是内核线程则不会调用yield
    
    // 可以参考 https://cloud.tencent.com.cn/developer/article/2337467
//     首先，所有的进程切换过程都发生在内核中，所有的acquire，switch，release都发生在内核代码而不是用户代码。实际上XV6允许在执行内核代码时触发中断，如果你查看trap.c中的代码你可以发现，如果XV6正在执行内核代码时发生了定时器中断，中断处理程序会调用yield函数并出让CPU。
// 但是在之前的课程中我们讲过acquire函数在等待锁之前会关闭中断，否则的话可能会引起死锁（注，详见10.8），所以我们不能在等待锁的时候处理中断。所以如果你查看XV6中的acquire函数，你可以发现函数中第一件事情就是关闭中断，之后再“自旋”等待锁释放。你或许会想，为什么不能先“自旋”等待锁释放，再关闭中断？因为这样会有一个短暂的时间段锁被持有了但是中断没有关闭，在这个时间段内的设备的中断处理程序可能会引起死锁。
// 所以不幸的是，当我们在自旋等待锁释放时会关闭中断，进而阻止了定时器中断并且阻止了进程P2将CPU出让回给进程P1。
// 死锁是如何避免的？

// 在XV6中，死锁是通过禁止在线程切换的时候加锁来避免的。
// XV6禁止在调用switch函数时，获取除了p->lock以外的其他锁。如果你查看sched函数的代码，里面包含了一些检查代码来确保除了p->lock以外线程不持有其他锁。所以上面会产生死锁的代码在XV6中是不合法的并被禁止的。
    intr_on();

    int found = 0;
    for(p = proc; p < &proc[NPROC]; p++) {

      // 1)这个加锁会关闭该CPU的中断. 
      // 2)被这个CPU的scheduler函数锁住后,由于proc[NPROC]是全局的,也就是所有CPU共享,其他cpu的scheduler就没法继续执行了
      // 除非这个锁被释放??? 除了第一次运行进程的时候快速通过forkret释放了锁,其他情况下也就是进程在一个时间片里,如果其他cpu的scheduler的for循环正好在这个进程上就被停住了??
      // 为啥不用try lock失败了接着走后面的
      // 需要等yield->sched回来后释放了锁才能并发.被停在这个lock的进程其他CPU才会运行. 这个并发性不太好
      acquire(&p->lock); 
      if(p->state == RUNNABLE) {
        // Switch to chosen process.  It is the process's job
        // to release its lock and then reacquire it
        // before jumping back to us.
        p->state = RUNNING;
        c->proc = p;  // 唯一给proc赋有效值的地方

        // 注意第一次执行进程的时候ra 是 forkret，forkret 会调用usertrapret 返回用户空间

        //执行这个进程 , 将之前的寄存器也就是scheduler()函数的上下文环境存放到c->context
        swtch(&c->context, &p->context);  
        // swtch执行完后就会执行ra开始的指令.其会通过userret的sret回到用户态,继续执行
        // trapframe->epc里面的用户态指令. 
        // 而执行e->proc=0 要等到时钟中断yield之类的trap,通过函数schd()函数返回才会执行
        // 这时proc已经不在cpu中了  所以为0

        // Process is done running for now.
        // It should have changed its p->state before coming back.
        // schd()函数回到这里
        c->proc = 0;  
        found = 1;
      }
      release(&p->lock);  // 跨进程（也可能是本进程）释放yield 里面获取的锁， 因为swtch换了执行路径等schd()函数回来 p已经换了
    }
    if(found == 0) {
      // nothing to run; stop running on this core until an interrupt.
      intr_on();
      asm volatile("wfi");  // 提示cpu可以进入低功耗状态
    }
  }
}



// 第1次执行进程:
// ecall -> fork -> exec p1 ->
// scheduler: acquire p1-> swtch( -> p1) -> 执行ra里面的forkret -> release p1  ->
// userret 执行用户态程序 ->被中断打断
// yield： acquire p1-> swtch(-> scheduler) -> 回到 scheduler里的swtch的下一行代码：执行release p1

// 后面第1+n(n=0,1..)次执行进程
// 最大的不同是调用scheduler切换到p1进程不会从forkret开始执行unlock p1. 
// 而是执行yield: swtch的下一行代码：release。因为yield调用swtch切换到scheduler后，ra就是yield:swtch的下一行代码，回来就从ra开始。

// scheduler 里面最开始执行 acquire，其是在，yield里release的：
// yield(void)                                  scheduler()                                    yield(void)   
// {                                            {                                              {
//   struct proc *p = myproc();                   for(proc[NPROC])                               struct proc *p = myproc(); 
//   acquire(&p->lock);       ------                acquire(&p->lock);    --------------         acquire(&p->lock);       
//   p->state = RUNNABLE;          |                p->state = RUNNING;                |         p->state = RUNNABLE; 
//   sched();                      |                swtch(&c->context, &p->context);   |         sched();
//   release(&p->lock);            ----->>          release(&p->lock);                 ------>>  release(&p->lock);  
// }                                            }                                              }
// 如上图所示假设这种场景：
//  1.最开始 p11 调用yield(acquire p11)退出执行进入scheduler， proc[NPROC]数组下一个进程是P22, acquire p22, 切换到p22执行
//  2.由于P22不是第1次执行，就会执行yield:sched(从这里把P22切换出去的)的下一行ra寄存器的release p22
//  3.等p22 被中断打断执行yield (acquire p22) 就是步骤1。 死循环

// Switch to scheduler.  Must hold only p->lock
// and have changed proc->state. Saves and restores
// intena because intena is a property of this
// kernel thread, not this CPU. It should
// be proc->intena and proc->noff, but that would
// break in the few places where a lock is held but
// there's no process.

// 该函数放弃进程的执行,将执行上下文改回到 scheduler()函数的环境
// 要求禁用中断 因为:调用cpuid和mycpu时，需要禁用中断)
void
sched(void)
{
  int intena;
  struct proc *p = myproc();  // 获取当前的进程

  if(!holding(&p->lock))  // 判断当前cpu是否得到了这个进程
    panic("sched p->lock");
  if(mycpu()->noff != 1)  // 除了持有进程的锁，其他的睡眠锁，等等都不允许持有，否则会死锁！！！
    panic("sched locks");
  if(p->state == RUNNING)  // 当前进程是runing状态 
    panic("sched running"); // 不应该调用sched？？
  if(intr_get())  // 如果开启了中断
    panic("sched interruptible");

  intena = mycpu()->intena;

  // 放弃当前的进程的执行，将上下文环境(就是当前进程的寄存器)存放在p->contexts下一次循环可能会用 
  // 当前进程的context切换到 scheduler()函数的上下文

  // 解释mycpu()->context为啥是scheduler()的上下文:
  // 在内核的初始化的时候(main.c) 每个cpu 都会死循环执行scheduler()函数.也就是进入得到工作状态.只有被中断才能打断
  // 这个时候如果有进程进入runable状态. 会执行 swtch(c->context, p->context),也就是将进程改为running执行进程(就是把进程的上下文环境放在当前的cpu上). 
  // 这时将scheduler的环境存在cpu上下文中. 
  swtch(&p->context, &mycpu()->context); 
  mycpu()->intena = intena;
}

// 要求禁用中断调用cpuid和mycpu时，需要禁用中断)!!!!!!!!!!
// Give up the CPU for one scheduling round.
// 让当前进程放弃在cpu中执行进入scheduler()循环，但是还是runnable 所以进入下个循环会被重新执行。
void
yield(void)
{
  struct proc *p = myproc();
  acquire(&p->lock); // 获取锁 在scheduler 中释放
  p->state = RUNNABLE;
  sched();
  // 前面sched换了执行路径，等scheduler回到这里，releae 释放的是scheduler里面的进程的锁
  release(&p->lock); // 跨进程（也可能是本进程）释放 scheduler中获取的锁 不是yield里面的这个锁。因为sched换了执行路径
}

// A fork child's very first scheduling by scheduler()
// will swtch to forkret.
// 有一种情况是调度器对swtch的调用没有以sched结束。当一个新进程第一次被调度时，
// 它从forkret开始。forkret的存在是为了释放p->lock；
// 否则，新进程需要从usertrapret开始。
void
forkret(void)
{
  static int first = 1;

  // Still holding p->lock from scheduler.

  // 解锁!!  第一次执行进程,ra是forket会解锁, 这样会出现多个cpu并发执行
  // 不同进程的情况.
  // 但是后面随着进程的执行,ra不是forkret后怎么办????
  release(&myproc()->lock);

  if (first) {
    // File system initialization must be run in the context of a
    // regular process (e.g., because it calls sleep), and thus cannot
    // be run from main().
    fsinit(ROOTDEV);

    first = 0;
    // ensure other cores see first=0.
    __sync_synchronize();
  }

  usertrapret(); // 注册用户态的异常处理:w_stvec(trampoline_uservec); 
}

// Atomically release lock and sleep on chan.
// Reacquires lock when awakened.
// 放弃当前进程的执行
void
sleep(void *chan, struct spinlock *lk)
{
  struct proc *p = myproc();
  
  // Must acquire p->lock in order to
  // change p->state and then call sched.
  // Once we hold p->lock, we can be
  // guaranteed that we won't miss any wakeup
  // (wakeup locks p->lock),
  // so it's okay to release lk.

  acquire(&p->lock);  //DOC: sleeplock1
  release(lk);

  // Go to sleep.
  p->chan = chan;
  p->state = SLEEPING;

  sched();

  // Tidy up.
  p->chan = 0;

  // Reacquire original lock.
  release(&p->lock);
  acquire(lk);
}

// Wake up all processes sleeping on chan. 将所有睡眠的进程唤醒
// Must be called without any p->lock.
void
wakeup(void *chan)
{
  struct proc *p;

  for(p = proc; p < &proc[NPROC]; p++) {
    if(p != myproc()){
      acquire(&p->lock);
      if(p->state == SLEEPING && p->chan == chan) {
        p->state = RUNNABLE;
      }
      release(&p->lock);
    }
  }
}

// Kill the process with the given pid.
// The victim won't exit until it tries to return
// to user space (see usertrap() in trap.c).
int
kill(int pid)
{
  struct proc *p;

  for(p = proc; p < &proc[NPROC]; p++){
    acquire(&p->lock);
    if(p->pid == pid){
      p->killed = 1;
      if(p->state == SLEEPING){
        // Wake process from sleep().
        p->state = RUNNABLE;
      }
      release(&p->lock);
      return 0;
    }
    release(&p->lock);
  }
  return -1;
}


// 只有在系统调用kill和 usertrap异常处理才会标记killed，
void
setkilled(struct proc *p)
{
  acquire(&p->lock);
  p->killed = 1;
  release(&p->lock);
}

int
killed(struct proc *p)
{
  int k;
  
  acquire(&p->lock);
  k = p->killed;
  release(&p->lock);
  return k;
}

// Copy to either a user address, or kernel address,
// depending on usr_dst.
// Returns 0 on success, -1 on error.
int
either_copyout(int user_dst, uint64 dst, void *src, uint64 len)
{
  struct proc *p = myproc();
  if(user_dst){
    return copyout(p->pagetable, dst, src, len);
  } else {
    memmove((char *)dst, src, len);
    return 0;
  }
}

// Copy from either a user address, or kernel address,
// depending on usr_src.
// Returns 0 on success, -1 on error.
int
either_copyin(void *dst, int user_src, uint64 src, uint64 len)
{
  struct proc *p = myproc();
  if(user_src){
    return copyin(p->pagetable, dst, src, len);
  } else {
    memmove(dst, (char*)src, len);
    return 0;
  }
}

// Print a process listing to console.  For debugging.
// Runs when user types ^P on console.
// No lock to avoid wedging a stuck machine further.
void
procdump(void)
{
  static char *states[] = {
  [UNUSED]    "unused",
  [USED]      "used",
  [SLEEPING]  "sleep ",
  [RUNNABLE]  "runble",
  [RUNNING]   "run   ",
  [ZOMBIE]    "zombie"
  };
  struct proc *p;
  char *state;

  printf("\n");
  for(p = proc; p < &proc[NPROC]; p++){
    if(p->state == UNUSED)
      continue;
    if(p->state >= 0 && p->state < NELEM(states) && states[p->state])
      state = states[p->state];
    else
      state = "???";
    printf("%d %s %s", p->pid, state, p->name);
    printf("\n");
  }
}
