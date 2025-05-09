#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "defs.h"

//
// the riscv Platform Level Interrupt Controller (PLIC).
//

// 外部设备中断也叫全局中断 用plic
// 本地设备中断（软件中断和定时器中断）用clint

void
plicinit(void)
{
  // set desired IRQ priorities non-zero (otherwise disabled).
  // 优先级都为低优先级（可以无需响应）PLIC规定谁的设备号小 谁先响应，所以虚拟磁盘优先级高
  *(uint32*)(PLIC + UART0_IRQ*4) = 1; // uart 中断 优先级为1 
  *(uint32*)(PLIC + VIRTIO0_IRQ*4) = 1;
}

void
plicinithart(void)
{
  int hart = cpuid();
  
  // set enable bits for this hart's S-mode
  // for the uart and virtio disk.
  *(uint32*)PLIC_SENABLE(hart) = (1 << UART0_IRQ) | (1 << VIRTIO0_IRQ);

  // set this hart's S-mode priority threshold to 0.
  *(uint32*)PLIC_SPRIORITY(hart) = 0; // 阈值为0 会响应所有中断
}

// ask the PLIC what interrupt we should serve.
int
plic_claim(void)
{
  int hart = cpuid();
  int irq = *(uint32*)PLIC_SCLAIM(hart);
  return irq;
}

// tell the PLIC we've served this IRQ.
void
plic_complete(int irq)
{
  int hart = cpuid();
  *(uint32*)PLIC_SCLAIM(hart) = irq;
}
