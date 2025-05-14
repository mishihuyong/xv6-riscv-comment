# Initial process that execs /init.
# This code runs in user space.

#include "syscall.h"

# 全局变量char init[] = "/init\0";
# 全局变量char *argv[] = { init, 0 };
# 执行 init.c
# 循环调用exit。为啥是循环

# exec(init, argv)
.globl start
start:
        la a0, init  # 下面的init字符串
        la a1, argv  # 下面定义argv： inti， 0
        li a7, SYS_exec
        ecall  # 系统调用 SYS_exit

# for(;;) exit();   
exit:
        li a7, SYS_exit
        ecall # 系统调用 SYS_exit
        jal exit

# char init[] = "/init\0";
init:
  .string "/init\0"

# char *argv[] = { init, 0 };
.p2align 2
argv:
  .quad init
  .quad 0
