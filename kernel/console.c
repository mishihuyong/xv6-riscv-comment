//
// Console input and output, to the uart.
// Reads are line at a time.
// Implements special input characters:
//   newline -- end of line
//   control-h -- backspace
//   control-u -- kill line
//   control-d -- end of file
//   control-p -- print process list
//

#include <stdarg.h>

#include "types.h"
#include "param.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "fs.h"
#include "file.h"
#include "memlayout.h"
#include "riscv.h"
#include "defs.h"
#include "proc.h"

// console 虽然是一个软件，但被mknod 注册为一个设备

// 1. 字符串的显示: char -> display
// printf 会调用putc -> write -> sys_write ->filewrite ->consolewrite->uartputc->uartstart->屏幕
// filewrite 根据类型选择处理, 我们通过mknod 注册了console 所以会调用consolewrite
// consolewrite 将数据拷贝到内核态然后调用uartputc放入uart输出缓存区
// uart 调用uartstart 输出到TSR寄存器,然后硬件将信息从TSR发出去,最终通过qemu的模拟屏幕收到信息

// 2. 字符怎么被从键盘读取的:  keboard -> char
// gets ->read -sys_read->fileread -> consoleread
// 由于stdin,stdout,stderr在第一个进程都被重定向console设备
// 所以read 就是从console设备读取字符. console设备怎么得到键盘敲入的字符呢?
// 答案:
// 连接关系:键盘 -> uart -> 屏幕
// 每次按一个键 , uart 产生一个中断,外部设备由plic 路由(claim/complete机制),devintr来接管
// 调用uartintr : 先调用uartgetc获取键盘传给uart的字符. 
// 然后调用consoleintr,将字符放入console缓冲区(read->consoleread就可以从这里读取字符了!!!),使用consputc同步实时回显到屏幕

// 流层图 (uart是双工)::

// 输入: 敲键盘-> usertrap->devintr->uartintr-> uart硬设备:tx缓存 ->consoleintr -> console软设备: console缓存(并同步实时回显) -> sh调用gets读取缓存就得到用户态的键盘输入

// 输出: 用户态printf -> usertrap->sys_write -> consolwrite-> console软设备:console缓存 -> uartputc -> uart硬设备:tx缓存  -> (1) uartstart异步显示 -> 屏幕
//                                                                                                        键盘输入场景: -> (2) consoleintr -> consputc->uartputc_sync同步显示 -> 屏幕

// 从上面可以看出 console这个软设备作为uart的影子设备,将其模拟成文件,供用户态程序使用read,write 来IO
//     uart只有发送带缓存，响应键盘中断uartintr是不带缓存直接读取的，然后uartintr调用consoleintr写入console的缓存区
//     console 是写是没有带缓存，是直接写道uart的发送区.  console的read 也是读取的缓存区
//    
//
//              display            shell:read
//               ^ ^                   ^
//               | |buff               | buff
//               uart  -----> buff console(filesys)
//                ^    <----------     ^
//                |                    |
//            keyboard            shell:write
                    


#define BACKSPACE 0x100
#define C(x)  ((x)-'@')  // Control-x

//
// send one character to the uart.
// called by printf(), and to echo input characters,
// but not from write().
// 将字符发给uart,其不经过缓存区,直接给uart来发送
// consputc 和consolewrite 都是向屏幕发送字符
// consolewrite 是读取用户态的字符异步发送
// consputc是内核态同步发送,延时低
void
consputc(int c)
{
  if(c == BACKSPACE){
    // if the user typed backspace, overwrite with a space.
    uartputc_sync('\b'); uartputc_sync(' '); uartputc_sync('\b');
  } else {
    uartputc_sync(c);
  }
}

struct {
  struct spinlock lock;
  
  // input
#define INPUT_BUF_SIZE 128
  char buf[INPUT_BUF_SIZE]; // 环形缓存队列。判空：r=w; 判满：w = r+128
  uint r;  // Read index
  uint w;  // Write index
  uint e;  // Edit index
} cons; //  与具体进程解耦不属于任何进程

//
// user write()s to the console go here.
//conwputc 和consolewrite 都是向屏幕发送字符
// consolewrite 是读取用户态的字符异步发送
// consputc是内核态同步发送,延时低
int
consolewrite(int user_src, uint64 src, int n)
{
  int i;

  for(i = 0; i < n; i++){
    char c;
    if(either_copyin(&c, user_src, src+i, 1) == -1)
      break;
    // 放入uart输出缓冲区
    // 并用uartstart驱动uart芯片尝试向外发送
    uartputc(c);
  }

  return i;
}

//
// user read()s from the console go here.
// copy (up to) a whole input line to dst.
// user_dist indicates whether dst is a user
// or kernel address.
// 从console缓冲区读取数据，并拷贝到用户态
int
consoleread(int user_dst, uint64 dst, int n)
{
  uint target;
  int c;
  char cbuf;

  target = n;
  acquire(&cons.lock);
  while(n > 0){
    // wait until interrupt handler has put some
    // input into cons.buffer.
    // 在中断响应函数美誉将字符放入cons.buffer之前休眠等待
    // 也就是console缓冲区没有字符，进程先睡眠
    while(cons.r == cons.w){ // 判空操作
      if(killed(myproc())){
        release(&cons.lock);
        return -1;
      }
      sleep(&cons.r, &cons.lock);
    }

    c = cons.buf[cons.r++ % INPUT_BUF_SIZE];

    // 如果是Ctrl+D 
    
    if(c == C('D')){  // end-of-file
      // 是否已经读取了一部分数据
      // 如果是，则本次读取结束，保留ctrl+D在缓冲区中
      // 如果ctrl+D是本次读取的第一个字符，则结束并丢掉 ctrl+D

      if(n < target){
        // Save ^D for next time, to make sure
        // caller gets a 0-byte result.
        
        // 回退读指针，这样下一次调用者就会得到一个0字节的结果
        cons.r--;
      }
      break;
    }

    // copy the input byte to the user-space buffer.
    cbuf = c;
    if(either_copyout(user_dst, dst, &cbuf, 1) == -1)
      break;

    dst++;
    --n;

    if(c == '\n'){
      // a whole line has arrived, return to
      // the user-level read().
      break;
    }
  }
  release(&cons.lock);

  return target - n;
}

//
// the console input interrupt handler.
// uartintr() calls this for input character.
// do erase/kill processing, append to cons.buf,
// wake up consoleread() if a whole line has arrived.
// 将字符放入console的缓冲区,并调用给你consputc回显字符
void
consoleintr(int c)
{
  acquire(&cons.lock);

  switch(c){
  case C('P'):  // Print process list.
    procdump();
    break;
  case C('U'):  // Kill line.
    while(cons.e != cons.w &&
          cons.buf[(cons.e-1) % INPUT_BUF_SIZE] != '\n'){
      cons.e--;
      consputc(BACKSPACE);
    }
    break;
  case C('H'): // Backspace
  case '\x7f': // Delete key
    if(cons.e != cons.w){
      cons.e--;
      consputc(BACKSPACE);
    }
    break;
  default:
    if(c != 0 && cons.e-cons.r < INPUT_BUF_SIZE){
      c = (c == '\r') ? '\n' : c;

      // echo back to the user.
      consputc(c);

      // store for consumption by consoleread().
      cons.buf[cons.e++ % INPUT_BUF_SIZE] = c;

      if(c == '\n' || c == C('D') || cons.e-cons.r == INPUT_BUF_SIZE){
        // wake up consoleread() if a whole line (or end-of-file)
        // has arrived.
        cons.w = cons.e;
        wakeup(&cons.r);
      }
    }
    break;
  }
  
  release(&cons.lock);
}

void
consoleinit(void)
{
  initlock(&cons.lock, "cons");

  uartinit();

  // connect read and write system calls
  // to consoleread and consolewrite.

  // xv6 只支持了 CONSOLE这一种设备
  devsw[CONSOLE].read = consoleread;
  devsw[CONSOLE].write = consolewrite;
}
