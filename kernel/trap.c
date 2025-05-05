#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"

// 当需要执行trap时，RISC-V硬件对所有的trap类型（除定时器中断外）进行以下操作：

// 1. 如果该trap是设备中断，且`sstatus` **SIE**位为0，则不执行以下任何操作。
// 2. 通过清除SIE来禁用中断。
// 3. 复制`pc`到`sepc`。
// 4. 将当前模式（用户态或特权态）保存在`sstatus`的**SPP**位。
// 5. 在`scause`设置该次trap的原因。
// 6. 将模式转换为特权态。
// 7. 将`stvec`复制到`pc`。
// 8. 从新的`pc`开始执行。

// 当uservec启动时，所有32个寄存器都包含被中断的代码所拥有的值。但是uservec需要能够修改一些寄存器，以便设置satp和生成保存寄存器的地址。RISC-V通过sscratch寄存器提供了帮助。uservec开始时的csrrw指令将a0和sscratch的内容互换。现在用户代码的a0被保存了；uservec有一个寄存器（a0）可以使用；a0包含了内核之前放在sscratch中的值。
// uservec的下一个任务是保存用户寄存器。在进入用户空间之前，内核先设置sscratch指向该进程的trapframe，这个trapframe可以保存所有用户寄存器（kernel/proc.h:44）。因为satp仍然是指用户页表，所以uservec需要将trapframe映射到用户地址空间中。当创建每个进程时，xv6为进程的trapframe分配一页内存，并将它映射在用户虚拟地址TRAPFRAME，也就是TRAMPOLINE的下面。进程的p->trapframe也指向trapframe，不过是指向它的物理地址[1]，这样内核可以通过内核页表来使用它。
// 因此，在交换a0和sscratch后，a0将指向当前进程的trapframe。uservec将在trapframe保存全部的寄存器，包括从sscratch读取的a0。
// trapframe包含指向当前进程的内核栈、当前CPU的hartid、usertrap的地址和内核页表的地址的指针，uservec将这些值设置到相应的寄存器中，并将satp切换到内核页表和刷新TLB，然后调用usertrap。
// usertrap的作用是确定trap的原因，处理它，然后返回（kernel/ trap.c:37）。如上所述，它首先改变stvec，这样在内核中发生的trap将由kernelvec处理。它保存了sepc（用户PC），这也是因为usertrap中可能会有一个进程切换，导致sepc被覆盖。如果trap是系统调用，syscall会处理它；如果是设备中断，devintr会处理；否则就是异常，内核会杀死故障进程。usertrap会把用户pc加4，因为RISC-V在执行系统调用时，会留下指向ecall指令的程序指针[2]。在退出时，usertrap检查进程是否已经被杀死或应该让出CPU（如果这个trap是一个定时器中断）。

struct spinlock tickslock;
uint ticks;

extern char trampoline[], uservec[], userret[];

// in kernelvec.S, calls kerneltrap().
void kernelvec();

extern int devintr();

void
trapinit(void)
{
  initlock(&tickslock, "time");
}

// set up to take exceptions and traps while in the kernel.
void
trapinithart(void)
{
  w_stvec((uint64)kernelvec);
}

//
// handle an interrupt, exception, or system call from user space.
// called from trampoline.S
// 用户态陷阱处理程序
void
usertrap(void)
{
  int which_dev = 0;

  if((r_sstatus() & SSTATUS_SPP) != 0)
    panic("usertrap: not from user mode");

  // send interrupts and exceptions to kerneltrap(),
  // since we're now in the kernel.
  w_stvec((uint64)kernelvec);

  struct proc *p = myproc();
  
  // save user program counter.
  p->trapframe->epc = r_sepc(); // 异常发生时会把异常之前的地址存在sepc
  
  if(r_scause() == 8){
    // system call

    if(killed(p)) // 如果程序已经被杀死了
      exit(-1);   // 释放进程资源，把状态改为ZOMBIE

    // sepc points to the ecall instruction,
    // but we want to return to the next instruction.
    p->trapframe->epc += 4;  // 系统调用用的是ecall。 我们希望系统调用返回的时候执行的是ecall的下一条指令

    // an interrupt will change sepc, scause, and sstatus,
    // so enable only now that we're done with those registers.
    intr_on();

    syscall();  // 调用对应的系统调用
  } else if((which_dev = devintr()) != 0){  // 定时器和外设中断
    // ok
  } else {
    // 程序发生了异常（比如除0） 将程序标记为killed 
    printf("usertrap(): unexpected scause 0x%lx pid=%d\n", r_scause(), p->pid);
    printf("            sepc=0x%lx stval=0x%lx\n", r_sepc(), r_stval());
    setkilled(p);
  }

  if(killed(p))
    exit(-1);  // 释放进程资源 标记状态为 zombie，调用进程切换

  // give up the CPU if this is a timer interrupt.
  if(which_dev == 2)  // 如果是定时器中断，要切换下进程
    yield();

  usertrapret();  //  处理完了 返回到用户态
}

//
// return to user space
//
void
usertrapret(void)
{
  struct proc *p = myproc();

  // we're about to switch the destination of traps from
  // kerneltrap() to usertrap(), so turn off interrupts until
  // we're back in user space, where usertrap() is correct.
  intr_off();

  // send syscalls, interrupts, and exceptions to uservec in trampoline.S
  uint64 trampoline_uservec = TRAMPOLINE + (uservec - trampoline);
  w_stvec(trampoline_uservec); // 设置 异常入口  trampoline_uservec 为uservec的虚拟地址

  // set up trapframe values that uservec will need when
  // the process next traps into the kernel.
  // 设置异常处理信息
  p->trapframe->kernel_satp = r_satp();         // kernel page table
  p->trapframe->kernel_sp = p->kstack + PGSIZE; // process's kernel stack
  p->trapframe->kernel_trap = (uint64)usertrap; // trampoline.s 的uservec 会无条件跳转到这个函数
  p->trapframe->kernel_hartid = r_tp();         // hartid for cpuid()

  // set up the registers that trampoline.S's sret will use
  // to get to user space.
  
  // set S Previous Privilege mode to User.
  unsigned long x = r_sstatus();
  x &= ~SSTATUS_SPP; // clear SPP to 0 for user mode   清除用户态标志
  x |= SSTATUS_SPIE; // enable interrupts in user mode
  w_sstatus(x);

  // set S Exception Program Counter to the saved user pc.
  w_sepc(p->trapframe->epc); // 设置执行sret后 恢复到正常的用户态的下一条执行指令

  // tell trampoline.S the user page table to switch to.
  uint64 satp = MAKE_SATP(p->pagetable);

  // jump to userret in trampoline.S at the top of memory, which 
  // switches to the user page table, restores user registers,
  // and switches to user mode with sret.
  uint64 trampoline_userret = TRAMPOLINE + (userret - trampoline);
  ((void (*)(uint64))trampoline_userret)(satp);   // a0是satp的值 就是进程的用户态页表地址
}

// interrupts and exceptions from kernel code go here via kernelvec,
// on whatever the current kernel stack is.
void 
kerneltrap()
{
  int which_dev = 0;
  uint64 sepc = r_sepc();
  uint64 sstatus = r_sstatus();
  uint64 scause = r_scause();
  
  if((sstatus & SSTATUS_SPP) == 0)
    panic("kerneltrap: not from supervisor mode");
  if(intr_get() != 0)
    panic("kerneltrap: interrupts enabled");

  if((which_dev = devintr()) == 0){
    // interrupt or trap from an unknown source
    printf("scause=0x%lx sepc=0x%lx stval=0x%lx\n", scause, r_sepc(), r_stval());
    panic("kerneltrap");
  }

  // give up the CPU if this is a timer interrupt.
  if(which_dev == 2 && myproc() != 0)
    yield();

  // the yield() may have caused some traps to occur,
  // so restore trap registers for use by kernelvec.S's sepc instruction.
  w_sepc(sepc);
  w_sstatus(sstatus);
}

void
clockintr()
{
  if(cpuid() == 0){
    acquire(&tickslock);
    ticks++;
    wakeup(&ticks);
    release(&tickslock);
  }

  // ask for the next timer interrupt. this also clears
  // the interrupt request. 1000000 is about a tenth
  // of a second.
  w_stimecmp(r_time() + 1000000);
}

// check if it's an external interrupt or software interrupt,
// and handle it.
// returns 2 if timer interrupt,  // 定时器的中断
// 1 if other device,  // 外部设备中断
// 0 if not recognized. // 没有被认可为中断
int
devintr()
{
  uint64 scause = r_scause();

  if(scause == 0x8000000000000009L){
    // this is a supervisor external interrupt, via PLIC.

    // irq indicates which device interrupted.
    int irq = plic_claim();

    if(irq == UART0_IRQ){
      uartintr();
    } else if(irq == VIRTIO0_IRQ){
      virtio_disk_intr();
    } else if(irq){
      printf("unexpected interrupt irq=%d\n", irq);
    }

    // the PLIC allows each device to raise at most one
    // interrupt at a time; tell the PLIC the device is
    // now allowed to interrupt again.
    if(irq)
      plic_complete(irq);

    return 1;
  } else if(scause == 0x8000000000000005L){
    // timer interrupt.
    clockintr();
    return 2;
  } else {
    return 0;
  }
}

