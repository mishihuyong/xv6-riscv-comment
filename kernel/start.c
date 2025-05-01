#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "defs.h"

void main();
void timerinit();

// entry.S needs one stack per CPU.   // S 和M模式都用的内核栈。 
__attribute__ ((aligned (16))) char stack0[4096 * NCPU]; // stack0在bss段，内核自己的栈一直就用这个


// 寄存器	ABI 名称	描述	Saver
// x0	zero	零值	-
// x1	ra	返回地址	Caller
// x2	sp	堆栈指针	Callee
// x3	gp	全局指针	-
// x4	tp	线程指针	-
// x5	t0	临时/备用链接寄存器	Caller
// x6-x7	t1-t2	临时寄存器	Caller
// x8	s0/fp	保存寄存器/帧指针	Callee
// x9	s1/gp	保存寄存器/全局指针	Callee
// x10-x11	a0-a1	函数参数/返回值	Caller
// x12-x17	a2-a7	函数参数	Caller
// x18-x27	s2-s11	保存寄存器	Callee
// x28-x31	t3-t6	临时寄存器	Caller

// CSR：

// 处理器信息相关寄存器
// misa	Machine ISA Register	机器模式指令集架构寄存器，可以查看处理器的位宽，以及所支持的扩展
// mvendorid	Machine Vendor ID Register	机器模式供应商编号寄存器
// marchid	Machine Architecture ID Register	机器模式架构编号寄存器
// mimpid	Machine Implementation ID Register	机器模式硬件实现编号寄存器
// mhartid	Hart ID Register	Hart编号寄存器
// 中断配置相关寄存器		
// mie	Machine Interrupt Enable Registers	机器模式中断使能寄存器，维护处理器的中断使能状态
// mip	Machine Interrupt Pending Registers	机器模式中断等待寄存器，记录当前的中断请求
// mtvec	Machine Trap-Vector Base-Address Register	机器模式异常入口基地址寄存器，低2位可以设置为直接模式或向量模式
// 异常处理相关寄存器		
// mcause	Machine Cause Register	机器模式异常原因寄存器
// mtval	Machine Trap Value Register	又名mbadaddr, 机器模式异常值寄存器，存放当前自陷相关的额外信息，如地址异常的故障地址、非法指令异常的指令，发生其他异常时其值为 0
// mstatus	Machine Status Registers	机器模式状态寄存器
// mepc	Machine Exception Program Counter	机器模式异常PC寄存器，指向发生异常的指令
// 性能统计相关		
// mtime	Machine Timer Registers	机器模式计时寄存器
// mcycle	Cycle counter	cycle 计数器（rv32 寄存器值为32位，需配合mcycleh使用，rv64 寄存器值为64位）
// mcycleh	Cycle counter	cycle 计数器高32位(rv64 无)

// 监督模式下的 CSR：
// sstatus（监督模式状态寄存器）：类似于mstatus，但用于监督模式。
// sepc（监督异常程序计数器）：保存监督模式下的异常地址。
// scause（监督异常原因寄存器）：保存监督模式下的异常原因。
// stvec（监督陷阱向量基址寄存器）：指向监督模式异常处理程序的入口地址。
// satp（监督地址转换与保护寄存器）：用于监督模式下的虚拟内存管理，控制页表基地址。  kvminithart会用它
stvec：内核在这里写下trap处理程序的地址；RISC-V跳转到这里来处理trap。
sepc：当trap发生时，RISC-V会将程序计数器保存在这里（因为PC会被stvec覆盖）。sret（从trap中返回）指令将sepc复制到pc中。内核可以写sepc来控制sret的返回到哪里。
scause：RISC -V在这里放了一个数字，描述了trap的原因。
sscratch：内核在这里放置了一个值，在trap处理程序开始时可以方便地使用。
sstatus：sstatus中的SIE位控制设备中断是否被启用，如果内核清除SIE，RISC-V将推迟设备中断，直到内核设置SIE。SPP位表示trap是来自用户模式还是supervisor模式，并控制sret返回到什么模式。

sscratch
（这个寄存器的用处会在实现线程时起到作用，目前仅了解即可）
在用户态，sscratch 保存内核栈的地址；在内核态，sscratch 的值为 0。
为了能够执行内核态的中断处理流程，仅有一个入口地址是不够的。中断处理流程很可能需要使用栈，而程序当前的用户栈是不安全的。因此，我们还需要一个预设的安全的栈空间，存放在这里。
在内核态中，sp 可以认为是一个安全的栈空间，sscratch 便不需要保存任何值。此时将其设为 0，可以在遇到中断时通过 sscratch 中的值判断中断前程序是否处于内核态。

https://rcore-os.cn/rCore-Tutorial-deploy/docs/lab-1/guide/part-2.html

// 特殊功能寄存器
// 除了上述常见的寄存器，还有一些针对特定功能的寄存器，比如计时器、性能计数器等：
// time：存储处理器的当前时间戳。
// cycle：记录处理器周期数。
// instret：记录执行的指令数。
// 寄存器的操作与管理

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
