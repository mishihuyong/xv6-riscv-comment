// Saved registers for kernel context switches.
// 这些寄存器就是函数调用要用到的寄存器
struct context {
  // 总结下:ra只会是 forkret,uservec, usertrap,usertrapret,useret 这几个函数或这几个函数里面的函数的地址!!!
  // 所以 ra 用来保存swtch后执行的指令, 其最终会让进程回到用户态.因为如前所述,它在trap中
  uint64 ra;  // 在执行子程序调用之前，ra设置为子程序的返回地址，通常为“pc + 4”
  uint64 sp;  // 栈顶

  // callee-saved
  uint64 s0;
  uint64 s1;
  uint64 s2;
  uint64 s3;
  uint64 s4;
  uint64 s5;
  uint64 s6;
  uint64 s7;
  uint64 s8;
  uint64 s9;
  uint64 s10;
  uint64 s11;
};

// Per-CPU state.
struct cpu {
  struct proc *proc;          // The process running on this cpu, or null. 如果已经在执行 就将其=0
  struct context context;     // swtch() here to enter scheduler().  存储被切换出去的进程上下文
  int noff;                   // Depth of push_off() nesting. 嵌套关中断的次数
  int intena;                 // Were interrupts enabled before push_off()?   // = 1，说明在push_off之前 中断在启用状态
};

extern struct cpu cpus[NCPU];

// per-process data for the trap handling code in trampoline.S.
// sits in a page by itself just under the trampoline page in the
// user page table. not specially mapped in the kernel page table.
// uservec in trampoline.S saves user registers in the trapframe,
// then initializes registers from the trapframe's
// kernel_sp, kernel_hartid, kernel_satp, and jumps to kernel_trap.
// usertrapret() and userret in trampoline.S set up
// the trapframe's kernel_*, restore user registers from the
// trapframe, switch to the user page table, and enter user space.
// the trapframe includes callee-saved user registers like s0-s11 because the
// return-to-user path via usertrapret() doesn't return through
// the entire kernel call stack.
struct trapframe {
  /*   0 */ uint64 kernel_satp;   // kernel page table
  /*   8 */ uint64 kernel_sp;     // top of process's kernel stack
  /*  16 */ uint64 kernel_trap;   // usertrap()
  /*  24 */ uint64 epc;           // saved user program counter
  /*  32 */ uint64 kernel_hartid; // saved kernel tp
  /*  40 */ uint64 ra;  // 在执行子程序调用之前，ra设置为子程序的返回地址，即 call 指令下一条指令的地址
  /*  48 */ uint64 sp;  // 栈顶
  /*  56 */ uint64 gp;
  /*  64 */ uint64 tp;  // hardid
  /*  72 */ uint64 t0;
  /*  80 */ uint64 t1;
  /*  88 */ uint64 t2;
  /*  96 */ uint64 s0;
  /* 104 */ uint64 s1;
  /* 112 */ uint64 a0;   // 1)存放arg0  2)retval 用于存储函数返回的数据结果 比如 addi a0,zero,42; ret 返回42
  /* 120 */ uint64 a1;   // arg1
  /* 128 */ uint64 a2;
  /* 136 */ uint64 a3;
  /* 144 */ uint64 a4;
  /* 152 */ uint64 a5;
  /* 160 */ uint64 a6;
  /* 168 */ uint64 a7;
  /* 176 */ uint64 s2;
  /* 184 */ uint64 s3;
  /* 192 */ uint64 s4;
  /* 200 */ uint64 s5;
  /* 208 */ uint64 s6;
  /* 216 */ uint64 s7;
  /* 224 */ uint64 s8;
  /* 232 */ uint64 s9;
  /* 240 */ uint64 s10;
  /* 248 */ uint64 s11;
  /* 256 */ uint64 t3;
  /* 264 */ uint64 t4;
  /* 272 */ uint64 t5;
  /* 280 */ uint64 t6;
};

enum procstate { UNUSED, USED, SLEEPING, RUNNABLE, RUNNING, ZOMBIE };

// Per-process state
struct proc {
  struct spinlock lock;

  // p->lock must be held when using these:
  enum procstate state;        // Process state
  void *chan;                  // If non-zero, sleeping on chan
  int killed;                  // If non-zero, have been killed
  int xstate;                  // Exit status to be returned to parent's wait
  int pid;                     // Process ID

  // wait_lock must be held when using this:
  struct proc *parent;         // Parent process

  // these are private to the process, so p->lock need not be held.
  // 为什么kstack 用的是内核态（kernel)虚拟地址？？ 因为为栈守护页留下了空间。 为了做栈保护
  uint64 kstack;               // Virtual address of kernel stack  // 由于栈是向下的，kstack是栈顶 kbp - 4096 = kstack。但实际上它是空的
  uint64 sz;                   // Size of process memory (bytes)

  // pagetable 内核中它是物理地址 但是va==pa
  pagetable_t pagetable;       // User page table  // 每个进程有自己的独立页表。 有自己独立的用户栈（exec的时候创建）和独立的内核栈（内核初始化的时候创建proc_mapstacks）
  // trapframe是物理地址 它也有有用户态（user）虚拟地址  trampoline 也有用户态虚拟地址 也有物理地址
  // 进程的p->trapframe也指向trapframe，不过是指向它的物理地址（来自kalloc分配）会映射到虚拟地址TRAPFRAME，
  // 这样内核可以通过内核页表来使用它。
  struct trapframe *trapframe; // data page for trampoline.S
  struct context context;      // swtch() here to run process
  struct file *ofile[NOFILE];  // Open files
  struct inode *cwd;           // Current directory
  char name[16];               // Process name (debugging)
};


// 用户进程的布局 虚拟地址：User memory layout.  【用户态虚拟地址】
// Address zero first:
//   text
//   original data and bss
//   fixed-size stack
//   expandable heap
//   ...
//   TRAPFRAME (p->trapframe, used by the trampoline)
//   TRAMPOLINE (the same page as in the kernel)

// 内核布局 基于虚拟地址                         【内核态虚拟地址】
// Address zero first:
//  0
//  clint // 本地设备中断（软件中断和定时器中断
//  plic  // 外部设备中断
//  uarto
//  virtio disk
//  kernel text
//  kernel data  全局变量和BSS。其包含stack0，也就是内核自己的栈
//  free memory... 
//  各个进程的内核栈proc[i]->kstack
//  TRAMPOLINE

// 内核布局 基于物理地址                         【内核态物理地址】
// Address zero first:                    
//  0
//  clint // 本地设备中断（软件中断和定时器中断
//  plic // 外部设备中断
//  uarto
//  virtio disk
//  kernel text //包含trampoline
//  kernel data  其包含stack0. 也就是内核的自己栈也在这 
//  内核页表和各个进程的内核栈  // 因为是最先用kalloc分配的
//  RAM
//  unused
