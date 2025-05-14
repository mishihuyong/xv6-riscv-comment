        # qemu -kernel loads the kernel at 0x80000000
        # and causes each hart (i.e. CPU) to jump there.
        # kernel.ld causes the following code to
        # be placed at 0x80000000.

        # 这个文件就是定义各个cpu的内核栈stack0 然后跳转到start.c
.section .text
.global _entry
_entry: 
        #  M mode 下设置内核栈，每个cpu一个4096字节的栈
        # set up a stack for C.
        # stack0 is declared in start.c,
        # with a 4096-byte stack per CPU.
        # sp = stack0 + (hartid * 4096)
        la sp, stack0    # sp = stack0
        li a0, 1024*4    # a0 = 4096
        csrr a1, mhartid #  a1 = mhardid  多个cpu同时启动？？是的  应该是每个cpu都走一遍entry和start的流程
        addi a1, a1, 1   # a1 += 1;  
        mul a0, a0, a1   # a0 *= a1   
        add sp, sp, a0   # sp = stack0 + 4096 * (hardid + 1)  由于栈是空所以+1
        # jump to start() in start.c
        call start
spin:
        j spin
