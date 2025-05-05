#ifndef __ASSEMBLER__

// callee 就是说 callee可能会改变这个寄存器的值
// caller 就是说 caller可能会改变这个寄存器的值

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
// sstatus（监督模式状态寄存器）：类似于mstatus，但用于监督模式。sstatus中的SIE位控制设备中断是否被启用，如果内核清除SIE，RISC-V将推迟设备中断，直到内核设置SIE。SPP位表示trap是来自用户模式还是supervisor模式，并控制sret返回到什么模式。
// sepc（监督异常程序计数器）：保存监督模式下的异常地址。当trap发生时，RISC-V会将程序计数器保存在这里（因为PC会被stvec覆盖）。sret（从trap中返回）指令将sepc复制到pc中。内核可以写sepc来控制sret的返回到哪里
// scause（监督异常原因寄存器）：保存监督模式下的异常原因。RISC -V在这里放了一个数字，描述了trap的原因。
// stvec（监督陷阱向量基址寄存器）：指向监督模式异常处理程序的入口地址。内核在这里写下trap处理程序的地址；RISC-V跳转到这里来处理trap。
// satp（监督地址转换与保护寄存器）：用于监督模式下的虚拟内存管理，控制页表基地址。  kvminithart会用它
// sscratch
// （这个寄存器的用处会在实现线程时起到作用，目前仅了解即可）
//  在用户态，sscratch 保存内核栈的地址；在内核态，sscratch 的值为 0。
//  为了能够执行内核态的中断处理流程，仅有一个入口地址是不够的。中断处理流程很可能需要使用栈，而程序当前的用户栈是不安全的。因此，我们还需要一个预设的安全的栈空间，存放在这里。
//  在内核态中，sp 可以认为是一个安全的栈空间，sscratch 便不需要保存任何值。此时将其设为 0，可以在遇到中断时通过 sscratch 中的值判断中断前程序是否处于内核态。
// https://rcore-os.cn/rCore-Tutorial-deploy/docs/lab-1/guide/part-2.html

// 特殊功能寄存器
// 除了上述常见的寄存器，还有一些针对特定功能的寄存器，比如计时器、性能计数器等：
// time：存储处理器的当前时间戳。
// cycle：记录处理器周期数。
// instret：记录执行的指令数。
// 寄存器的操作与管理


// which hart (core) is this?
static inline uint64
r_mhartid()
{
  uint64 x;
  asm volatile("csrr %0, mhartid" : "=r" (x) );
  return x;
}

// Machine Status Register, mstatus

#define MSTATUS_MPP_MASK (3L << 11) // previous mode.
#define MSTATUS_MPP_M (3L << 11)
#define MSTATUS_MPP_S (1L << 11)
#define MSTATUS_MPP_U (0L << 11)
#define MSTATUS_MIE (1L << 3)    // machine-mode interrupt enable.

static inline uint64
r_mstatus()
{
  uint64 x;
  asm volatile("csrr %0, mstatus" : "=r" (x) );
  return x;
}

static inline void 
w_mstatus(uint64 x)
{
  asm volatile("csrw mstatus, %0" : : "r" (x));
}

// machine exception program counter, holds the
// instruction address to which a return from
// exception will go.
static inline void 
w_mepc(uint64 x)
{
  asm volatile("csrw mepc, %0" : : "r" (x));
}

// Supervisor Status Register, sstatus

#define SSTATUS_SPP (1L << 8)  // Previous mode, 1=Supervisor, 0=User
#define SSTATUS_SPIE (1L << 5) // Supervisor Previous Interrupt Enable
#define SSTATUS_UPIE (1L << 4) // User Previous Interrupt Enable
#define SSTATUS_SIE (1L << 1)  // Supervisor Interrupt Enable
#define SSTATUS_UIE (1L << 0)  // User Interrupt Enable

static inline uint64
r_sstatus()
{
  uint64 x;
  asm volatile("csrr %0, sstatus" : "=r" (x) );
  return x;
}

static inline void 
w_sstatus(uint64 x)
{
  asm volatile("csrw sstatus, %0" : : "r" (x));
}

// Supervisor Interrupt Pending
static inline uint64
r_sip()
{
  uint64 x;
  asm volatile("csrr %0, sip" : "=r" (x) );
  return x;
}

static inline void 
w_sip(uint64 x)
{
  asm volatile("csrw sip, %0" : : "r" (x));
}

// Supervisor Interrupt Enable
#define SIE_SEIE (1L << 9) // external
#define SIE_STIE (1L << 5) // timer
#define SIE_SSIE (1L << 1) // software
static inline uint64
r_sie()
{
  uint64 x;
  asm volatile("csrr %0, sie" : "=r" (x) );
  return x;
}

static inline void 
w_sie(uint64 x)
{
  asm volatile("csrw sie, %0" : : "r" (x));
}

// Machine-mode Interrupt Enable
#define MIE_STIE (1L << 5)  // supervisor timer
static inline uint64
r_mie()
{
  uint64 x;
  asm volatile("csrr %0, mie" : "=r" (x) );
  return x;
}

static inline void 
w_mie(uint64 x)
{
  asm volatile("csrw mie, %0" : : "r" (x));
}

// supervisor exception program counter, holds the
// instruction address to which a return from
// exception will go.
static inline void 
w_sepc(uint64 x)
{
  asm volatile("csrw sepc, %0" : : "r" (x));
}

static inline uint64
r_sepc()
{
  uint64 x;
  asm volatile("csrr %0, sepc" : "=r" (x) );
  return x;
}

// Machine Exception Delegation
static inline uint64
r_medeleg()
{
  uint64 x;
  asm volatile("csrr %0, medeleg" : "=r" (x) );
  return x;
}

static inline void 
w_medeleg(uint64 x)
{
  asm volatile("csrw medeleg, %0" : : "r" (x));
}

// Machine Interrupt Delegation
static inline uint64
r_mideleg()
{
  uint64 x;
  asm volatile("csrr %0, mideleg" : "=r" (x) );
  return x;
}

static inline void 
w_mideleg(uint64 x)
{
  asm volatile("csrw mideleg, %0" : : "r" (x));
}

// Supervisor Trap-Vector Base Address
// low two bits are mode.
static inline void 
w_stvec(uint64 x)
{
  asm volatile("csrw stvec, %0" : : "r" (x));
}

static inline uint64
r_stvec()
{
  uint64 x;
  asm volatile("csrr %0, stvec" : "=r" (x) );
  return x;
}

// Supervisor Timer Comparison Register
static inline uint64
r_stimecmp()
{
  uint64 x;
  // asm volatile("csrr %0, stimecmp" : "=r" (x) );
  asm volatile("csrr %0, 0x14d" : "=r" (x) );
  return x;
}

static inline void 
w_stimecmp(uint64 x)
{
  // asm volatile("csrw stimecmp, %0" : : "r" (x));
  asm volatile("csrw 0x14d, %0" : : "r" (x));
}

// Machine Environment Configuration Register
static inline uint64
r_menvcfg()
{
  uint64 x;
  // asm volatile("csrr %0, menvcfg" : "=r" (x) );
  asm volatile("csrr %0, 0x30a" : "=r" (x) );
  return x;
}

static inline void 
w_menvcfg(uint64 x)
{
  // asm volatile("csrw menvcfg, %0" : : "r" (x));
  asm volatile("csrw 0x30a, %0" : : "r" (x));
}

// Physical Memory Protection
static inline void
w_pmpcfg0(uint64 x)
{
  asm volatile("csrw pmpcfg0, %0" : : "r" (x));
}

static inline void
w_pmpaddr0(uint64 x)
{
  asm volatile("csrw pmpaddr0, %0" : : "r" (x));
}

// use riscv's sv39 page table scheme.
#define SATP_SV39 (8L << 60)

#define MAKE_SATP(pagetable) (SATP_SV39 | (((uint64)pagetable) >> 12))

// supervisor address translation and protection;
// holds the address of the page table.
static inline void 
w_satp(uint64 x)
{
  asm volatile("csrw satp, %0" : : "r" (x));
}

static inline uint64
r_satp()
{
  uint64 x;
  asm volatile("csrr %0, satp" : "=r" (x) );
  return x;
}

// Supervisor Trap Cause
static inline uint64
r_scause()
{
  uint64 x;
  asm volatile("csrr %0, scause" : "=r" (x) );
  return x;
}

// Supervisor Trap Value
static inline uint64
r_stval()
{
  uint64 x;
  asm volatile("csrr %0, stval" : "=r" (x) );
  return x;
}

// Machine-mode Counter-Enable
static inline void 
w_mcounteren(uint64 x)
{
  asm volatile("csrw mcounteren, %0" : : "r" (x));
}

static inline uint64
r_mcounteren()
{
  uint64 x;
  asm volatile("csrr %0, mcounteren" : "=r" (x) );
  return x;
}

// machine-mode cycle counter
static inline uint64
r_time()
{
  uint64 x;
  asm volatile("csrr %0, time" : "=r" (x) );
  return x;
}

// enable device interrupts
static inline void
intr_on()
{
  w_sstatus(r_sstatus() | SSTATUS_SIE);
}

// disable device interrupts
// 清除SIE标志 关闭中断
static inline void
intr_off()
{
  w_sstatus(r_sstatus() & ~SSTATUS_SIE);
}

// are device interrupts enabled?
// =1 说明开启了中断， =0 说明推迟了中断
static inline int
intr_get()
{
  uint64 x = r_sstatus();
  return (x & SSTATUS_SIE) != 0;
}

static inline uint64
r_sp()
{
  uint64 x;
  asm volatile("mv %0, sp" : "=r" (x) );
  return x;
}

// read and write tp, the thread pointer, which xv6 uses to hold
// this core's hartid (core number), the index into cpus[].
static inline uint64
r_tp()
{
  uint64 x;
  asm volatile("mv %0, tp" : "=r" (x) );
  return x;
}

static inline void 
w_tp(uint64 x)
{
  asm volatile("mv tp, %0" : : "r" (x));
}

static inline uint64
r_ra()
{
  uint64 x;
  asm volatile("mv %0, ra" : "=r" (x) );
  return x;
}

// flush the TLB.
static inline void
sfence_vma()
{
  // the zero, zero means flush all TLB entries.
  asm volatile("sfence.vma zero, zero");
}

typedef uint64 pte_t;
typedef uint64 *pagetable_t; // 512 PTEs

#endif // __ASSEMBLER__

#define PGSIZE 4096 // bytes per page
#define PGSHIFT 12  // bits of offset within a page

#define PGROUNDUP(sz)  (((sz)+PGSIZE-1) & ~(PGSIZE-1))
#define PGROUNDDOWN(a) (((a)) & ~(PGSIZE-1))

#define PTE_V (1L << 0) // valid
#define PTE_R (1L << 1)
#define PTE_W (1L << 2)
#define PTE_X (1L << 3)
#define PTE_U (1L << 4) // user can access

// shift a physical address to the right place for a PTE.
#define PA2PTE(pa) ((((uint64)pa) >> 12) << 10) // >>12 去掉4096  <<10 给flags 留除空间

#define PTE2PA(pte) (((pte) >> 10) << 12) // >> 10 去掉flags的数据, << 12 4096 就是PPN 加上页内偏移就是真实物理地址了

#define PTE_FLAGS(pte) ((pte) & 0x3FF)

// extract the three 9-bit page table indices from a virtual address.
#define PXMASK          0x1FF // 9 bits
#define PXSHIFT(level)  (PGSHIFT+(9*(level)))
#define PX(level, va) ((((uint64) (va)) >> PXSHIFT(level)) & PXMASK) // 根据level 算出其在页表目录基地址的偏移 

// one beyond the highest possible virtual address.
// MAXVA is actually one bit less than the max allowed by
// Sv39, to avoid having to sign-extend virtual addresses
// that have the high bit set.
#define MAXVA (1L << (9 + 9 + 9 + 12 - 1))
