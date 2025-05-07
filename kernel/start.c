#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "defs.h"

void main();
void timerinit();

// entry.S needs one stack per CPU.   // S 和M模式都用的内核栈。 
__attribute__ ((aligned (16))) char stack0[4096 * NCPU]; // stack0在bss段，内核自己的栈一直就用这个


// entry.S jumps here in machine mode on stack0.
// entry.s start.c 是每个CPU 都会执行的操作  到了main函数，每个cpu执行的路径就不一样了 只有CPU0 才会做初始化 其他cpu只是开启paging
void
start()
{
  // set M Previous Privilege mode to Supervisor, for mret.
  // 设置 s模式标志，也就是系统模式。 调用mret 才会生效
  unsigned long x = r_mstatus();
  x &= ~MSTATUS_MPP_MASK;
  x |= MSTATUS_MPP_S;
  w_mstatus(x);

  // set M Exception Program Counter to main, for mret.
  // requires gcc -mcmodel=medany
  w_mepc((uint64)main);    // 设置异常的返回的下一跳地址，调用mret后 ip指向这个main函数

  // disable paging for now.
  w_satp(0);   // 设置页表根  

  // delegate all interrupts and exceptions to supervisor mode.
  w_medeleg(0xffff);   // 将m模式的异常交给 s模式
  w_mideleg(0xffff);   // 将m模式的中断交给 s模式
  w_sie(r_sie() | SIE_SEIE | SIE_STIE | SIE_SSIE);  // 设置 系统的 时钟中断，软件中断，使能

  // configure Physical Memory Protection to give supervisor mode
  // access to all of physical memory.
  w_pmpaddr0(0x3fffffffffffffull);   // 设置页表起始地址
  w_pmpcfg0(0xf);                    // 设置页表的权限是  有效，rwx

  // ask for clock interrupts.
  timerinit();   // 初始化时钟硬件

  // keep each CPU's hartid in its tp register, for cpuid().
  int id = r_mhartid();   // 获取hardid   假设是2个cpu 2个cpu都同时走entry，start的流层？? 应该是每个cpu都走一遍entry和start的流程
  w_tp(id); // 设置这个给后 tp寄存器 就是cpuid了

  // switch to supervisor mode and jump to main().
  asm volatile("mret");   // 用了这个后 就切换到s模式
}

// ask each hart to generate timer interrupts.
// 定时器中断必须在机器模式下才能使用
// xv6 会将定时器中断转成软中断
void
timerinit()
{
  // enable supervisor-mode timer interrupts.
  w_mie(r_mie() | MIE_STIE);
  
  // enable the sstc extension (i.e. stimecmp).
  w_menvcfg(r_menvcfg() | (1L << 63)); 
  
  // allow supervisor to use stimecmp and time.
  w_mcounteren(r_mcounteren() | 2);
  
  // ask for the very first timer interrupt.
  w_stimecmp(r_time() + 1000000);
}
