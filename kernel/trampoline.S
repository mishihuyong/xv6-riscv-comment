        #
        # low-level code to handle traps from user space into
        # the kernel, and returns from kernel to user.
        #
        # the kernel maps the page holding this code  
        # at the same virtual address (TRAMPOLINE)
        # in user and kernel space so that it continues
        # to work when it switches page tables.
        # kernel.ld causes this code to start at 
        # a page boundary.
        #

        # TRAMPOLINE 在内核态和用户态的虚拟地址一样，这样切换了页表也能工作

#include "riscv.h"
#include "memlayout.h"

# uservec 就是将用户态的寄存器保存到p->trapframe,将页表切换到内核页表，然后跳转到usertrap函数执行
# userret 就是将 用户态的寄存器还原，将页表切换到进程的用户态页表。然后模式返回到用户态

# 由于这两段代码会在用户页和内核页表之间相互切换。所以 TRAMPOLINE在用户态和内核态的地址要一样，否则没法工作

.section trampsec
.globl trampoline
.globl usertrap
trampoline:
.align 4
.globl uservec
uservec:    
	#
        # trap.c sets stvec to point here, so   将stvec（S模式下的trap处理程序的地址）设置为uservec
        # traps from user space start here,     但是是S模式下用户态的页表
        # in supervisor mode, but with a
        # user page table.
        #

        # save user a0 in sscratch so
        # a0 can be used to get at TRAPFRAME.
        csrw sscratch, a0   # 先把a0 临时存放在sscratch寄存器. 后面要用a0传参

        # each process has a separate p->trapframe memory area,
        # but it's mapped to the same virtual address
        # (TRAPFRAME) in every process's user page table.
        li a0, TRAPFRAME    # a0 指向 TRAPFRAME的地址
        #  将用户态的寄存器 存在 trapframe中
        # save the user registers in TRAPFRAME
        sd ra, 40(a0)
        sd sp, 48(a0)
        sd gp, 56(a0)
        sd tp, 64(a0)
        sd t0, 72(a0)
        sd t1, 80(a0)
        sd t2, 88(a0)
        sd s0, 96(a0)
        sd s1, 104(a0)
        sd a1, 120(a0)
        sd a2, 128(a0)
        sd a3, 136(a0)
        sd a4, 144(a0)
        sd a5, 152(a0)
        sd a6, 160(a0)
        sd a7, 168(a0)
        sd s2, 176(a0)
        sd s3, 184(a0)
        sd s4, 192(a0)
        sd s5, 200(a0)
        sd s6, 208(a0)
        sd s7, 216(a0)
        sd s8, 224(a0)
        sd s9, 232(a0)
        sd s10, 240(a0)
        sd s11, 248(a0)
        sd t3, 256(a0)
        sd t4, 264(a0)
        sd t5, 272(a0)
        sd t6, 280(a0)

	# save the user a0 in p->trapframe->a0
        csrr t0, sscratch
        sd t0, 112(a0)  # 给 trapfram的a0 赋值 之前a0存在sscratch中

        # 用户态的寄存器前面都保存了  现在这些寄存器都可以随便使用了

        # initialize kernel stack pointer, from p->trapframe->kernel_sp
        ld sp, 8(a0)

        # make tp hold the current hartid, from p->trapframe->kernel_hartid
        ld tp, 32(a0)

        # load the address of usertrap(), from p->trapframe->kernel_trap
        ld t0, 16(a0)

        # fetch the kernel page table address, from p->trapframe->kernel_satp.
        ld t1, 0(a0)

        # wait for any previous memory operations to complete, so that
        # they use the user page table.
        sfence.vma zero, zero  # 等待内存操作完成

        # install the kernel page table.
        csrw satp, t1   # 切换到内核态的页表

        # flush now-stale user entries from the TLB.
        sfence.vma zero, zero

        # jump to usertrap(), which does not return
        jr t0   # t0寄存器指向 trapframe->kernel_trap  就是 usertrap。  无条件跳转到kernel_trap（usertrap）

.globl userret
userret:
        # userret(pagetable)
        # called by usertrapret() in trap.c to
        # switch from kernel to user.
        # a0: user page table, for satp.

        # switch to the user page table.
        sfence.vma zero, zero

        csrw satp, a0  #  usertrapret(void) 最后会调用((void (*)(uint64))trampoline_userret)(satp);  a0就是arg0 也是a0就是satp的值 就是进程的用户态页表地址
        sfence.vma zero, zero

        # TRAPFRAME 只作为内核态的虚拟地址
        li a0, TRAPFRAME   # a0作为形参0用完后，在这里 指向内核态虚拟地址TRAPFRAME 这样下面的ld代码就可以换运哟工会图埃的寄存器了

        # restore all but a0 from TRAPFRAME   # 恢复用户态的寄存器
        ld ra, 40(a0)
        ld sp, 48(a0)
        ld gp, 56(a0)
        ld tp, 64(a0)
        ld t0, 72(a0)
        ld t1, 80(a0)
        ld t2, 88(a0)
        ld s0, 96(a0)
        ld s1, 104(a0)
        ld a1, 120(a0)
        ld a2, 128(a0)
        ld a3, 136(a0)
        ld a4, 144(a0)
        ld a5, 152(a0)
        ld a6, 160(a0)
        ld a7, 168(a0)
        ld s2, 176(a0)
        ld s3, 184(a0)
        ld s4, 192(a0)
        ld s5, 200(a0)
        ld s6, 208(a0)
        ld s7, 216(a0)
        ld s8, 224(a0)
        ld s9, 232(a0)
        ld s10, 240(a0)
        ld s11, 248(a0)
        ld t3, 256(a0)
        ld t4, 264(a0)
        ld t5, 272(a0)
        ld t6, 280(a0)

	# restore user a0
        ld a0, 112(a0)   #  恢复用户态的a0
        
        # return to user mode and user pc.
        # usertrapret() set up sstatus and sepc.  # 返回到U模式 就是用户态
        sret
