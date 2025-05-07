# Context switch
#
#   void swtch(struct context *old, struct context *new);
# 
# Save current registers in old. Load from new.	

# Swtch知道struct context中每个寄存器字段的偏移量。它不保存pc。相反，swtch保存了ra寄存器，它保存了swtch应该返回的地址。现在，swtch从新的上下文中恢复寄存器，新的上下文中保存着前一次swtch所保存的寄存器值。当swtch返回时，它返回到被恢复的ra寄存器所指向的指令，也就是新线程之前调用swtch的指令。此外，它还会返回新线程的堆栈。
# 在我们的例子中，sched调用swtch切换到cpu->scheduler，即CPU调度器的上下文。这个上下文已经被scheduler对swtch的调用所保存(kernel/proc.c:475)。当我们跟踪的swtch返回时，它不是返回到sched而是返回到scheduler，它的栈指针指向当前CPU的调度器栈。
.globl swtch
swtch:
        sd ra, 0(a0)  # 将 寄存器 的值存入 a0 old
        sd sp, 8(a0)
        sd s0, 16(a0)
        sd s1, 24(a0)
        sd s2, 32(a0)
        sd s3, 40(a0)
        sd s4, 48(a0)
        sd s5, 56(a0)
        sd s6, 64(a0)
        sd s7, 72(a0)
        sd s8, 80(a0)
        sd s9, 88(a0)
        sd s10, 96(a0)
        sd s11, 104(a0)

        ld ra, 0(a1)   # 寄存器 用a1（new）的值   // ra可能是forkret. 函数执行完好 就会执行ra里面的指令
        ld sp, 8(a1)
        ld s0, 16(a1)
        ld s1, 24(a1)
        ld s2, 32(a1)
        ld s3, 40(a1)
        ld s4, 48(a1)
        ld s5, 56(a1)
        ld s6, 64(a1)
        ld s7, 72(a1)
        ld s8, 80(a1)
        ld s9, 88(a1)
        ld s10, 96(a1)
        ld s11, 104(a1)
        
        ret

	
