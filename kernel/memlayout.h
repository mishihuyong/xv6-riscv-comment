// Physical memory layout

// qemu -machine virt is set up like this,
// based on qemu's hw/riscv/virt.c:
//
// 00001000 -- boot ROM, provided by qemu
// 02000000 -- CLINT
// 0C000000 -- PLIC
// 10000000 -- uart0 
// 10001000 -- virtio disk 
// 80000000 -- boot ROM jumps here in machine mode
//             -kernel loads the kernel here
// unused RAM after 80000000.

// the kernel uses physical memory thus:
// 80000000 -- entry.S, then kernel text and data
// end -- start of kernel page allocation area
// PHYSTOP -- end RAM used by the kernel

// qemu puts UART registers here in physical memory.
#define UART0 0x10000000L
#define UART0_IRQ 10

// virtio mmio interface
#define VIRTIO0 0x10001000
#define VIRTIO0_IRQ 1

// qemu puts platform-level interrupt controller (PLIC) here.
#define PLIC 0x0c000000L
#define PLIC_PRIORITY (PLIC + 0x0)
#define PLIC_PENDING (PLIC + 0x1000)
#define PLIC_SENABLE(hart) (PLIC + 0x2080 + (hart)*0x100)
#define PLIC_SPRIORITY(hart) (PLIC + 0x201000 + (hart)*0x2000)
#define PLIC_SCLAIM(hart) (PLIC + 0x201004 + (hart)*0x2000)

// the kernel expects there to be RAM
// for use by the kernel and user pages
// from physical address 0x80000000 to PHYSTOP.
#define KERNBASE 0x80000000L
#define PHYSTOP (KERNBASE + 128*1024*1024)

// map the trampoline page to the highest address,
// in both user and kernel space.
#define TRAMPOLINE (MAXVA - PGSIZE)

// map kernel stacks beneath the trampoline,
// each surrounded by invalid guard pages.
#define KSTACK(p) (TRAMPOLINE - ((p)+1)* 2*PGSIZE)

// User memory layout.
// Address zero first:
//   text
//   original data and bss
//   fixed-size stack
//   expandable heap
//   ...
//   TRAPFRAME (p->trapframe, used by the trampoline)
//   TRAMPOLINE (the same page as in the kernel)
#define TRAPFRAME (TRAMPOLINE - PGSIZE)   // 这个地址只在进程的用户态的时候有，其他情况下没有这个东西


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
//  clint   // 本地中断（软件中断和定时器中断）
//  plic    // 外部设备中断
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
//  plic  // 外部全局中断
//  uarto
//  virtio disk
//  kernel text //包含trampoline
//  kernel data  其包含stack0. 也就是内核的自己栈也在这 
//  内核页表和各个进程的内核栈  // 因为是最先用kalloc分配的
//  RAM
//  unused

