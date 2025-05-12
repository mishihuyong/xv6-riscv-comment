//
// low-level driver routines for 16550a UART.
//

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"

// 连接关系:键盘 -> uart -> 屏幕

// 流层图 (uart是双工,且跟进程解耦)::

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
                    

// the UART control registers are memory-mapped
// at address UART0. this macro returns the
// address of one of the registers.
#define Reg(reg) ((volatile unsigned char *)(UART0 + (reg)))

// the UART control registers.
// some have different meanings for
// read vs write.
// see http://byterunner.com/16550.html
#define RHR 0                 // receive holding register (for input bytes)
#define THR 0                 // transmit holding register (for output bytes)
#define IER 1                 // interrupt enable register
#define IER_RX_ENABLE (1<<0)
#define IER_TX_ENABLE (1<<1)
#define FCR 2                 // FIFO control register
#define FCR_FIFO_ENABLE (1<<0)
#define FCR_FIFO_CLEAR (3<<1) // clear the content of the two FIFOs
#define ISR 2                 // interrupt status register
#define LCR 3                 // line control register
#define LCR_EIGHT_BITS (3<<0)
#define LCR_BAUD_LATCH (1<<7) // special mode to set baud rate
#define LSR 5                 // line status register
#define LSR_RX_READY (1<<0)   // input is waiting to be read from RHR
#define LSR_TX_IDLE (1<<5)    // THR can accept another character to send

#define ReadReg(reg) (*(Reg(reg)))
#define WriteReg(reg, v) (*(Reg(reg)) = (v))

// the transmit output buffer.
struct spinlock uart_tx_lock;
#define UART_TX_BUF_SIZE 32
char uart_tx_buf[UART_TX_BUF_SIZE]; // 与具体进程解耦不属于任何进程：环形缓冲队列 判空：uart_tx_w == uart_tx_r, 判满：uart_tx_w == uart_tx_r+32
uint64 uart_tx_w; // write next to uart_tx_buf[uart_tx_w % UART_TX_BUF_SIZE]
uint64 uart_tx_r; // read next from uart_tx_buf[uart_tx_r % UART_TX_BUF_SIZE]

extern volatile int panicked; // from printf.c

void uartstart();

void
uartinit(void)
{
  // disable interrupts.
  WriteReg(IER, 0x00);

  // special mode to set baud rate.
  WriteReg(LCR, LCR_BAUD_LATCH);

  // LSB for baud rate of 38.4K.
  WriteReg(0, 0x03);

  // MSB for baud rate of 38.4K.
  WriteReg(1, 0x00);

  // leave set-baud mode,
  // and set word length to 8 bits, no parity.
  WriteReg(LCR, LCR_EIGHT_BITS);

  // reset and enable FIFOs.
  WriteReg(FCR, FCR_FIFO_ENABLE | FCR_FIFO_CLEAR);

  // enable transmit and receive interrupts.
  WriteReg(IER, IER_TX_ENABLE | IER_RX_ENABLE);

  initlock(&uart_tx_lock, "uart");
}

// add a character to the output buffer and tell the
// UART to start sending if it isn't already.
// blocks if the output buffer is full.
// because it may block, it can't be called
// from interrupts; it's only suitable for use
// by write().
void
uartputc(int c)
{
  acquire(&uart_tx_lock);

  // 内核出现故障，就死循环，程序失去响应
  if(panicked){
    for(;;)
      ;
  }

  // 缓存区满了，让进程休眠在uart_tx_r这个channel上
  while(uart_tx_w == uart_tx_r + UART_TX_BUF_SIZE){
    // buffer is full.
    // wait for uartstart() to open up space in the buffer.
    sleep(&uart_tx_r, &uart_tx_lock);
  }
  // 没满，放入缓存区，告知UART 准备发送
  uart_tx_buf[uart_tx_w % UART_TX_BUF_SIZE] = c; // 放入缓存
  uart_tx_w += 1;
  uartstart();// 驱动uart芯片发送数据
  release(&uart_tx_lock);
}


// alternate version of uartputc() that doesn't 
// use interrupts, for use by kernel printf() and
// to echo characters. it spins waiting for the uart's
// output register to be empty.
void
uartputc_sync(int c)
{
  push_off();

  // 内核故障，死循环不再响应
  if(panicked){
    for(;;)
      ;
  }

  // wait for Transmit Holding Empty to be set in LSR.
  while((ReadReg(LSR) & LSR_TX_IDLE) == 0)
    ;
  WriteReg(THR, c);

  pop_off();
}

// if the UART is idle, and a character is waiting
// in the transmit buffer, send it.
// caller must hold uart_tx_lock.
// called from both the top- and bottom-half.
// 驱动uart芯片向外发送数据
// usartstart 被两个地方调用：
// 1） top： uartputc函数 是用户和内核可以调用的接口
// 2） bottom： uartintr 中断

// 异步发送
void
uartstart()
{
  while(1){
    if(uart_tx_w == uart_tx_r){
      // transmit buffer is empty.
      ReadReg(ISR);
      return;
    }
    // 有字符等待发送，但是上次的发送还没完成 
    if((ReadReg(LSR) & LSR_TX_IDLE) == 0){
      // the UART transmit holding register is full,
      // so we cannot give it another byte.
      // it will interrupt when it's ready for a new byte.
      // uart thr寄存器还是满的 不能给它另外一个byte。等它准备好的时候主动发起中断
      return;
    }
    
    int c = uart_tx_buf[uart_tx_r % UART_TX_BUF_SIZE];
    uart_tx_r += 1;
    
    // maybe uartputc() is waiting for space in the buffer.
    // 唤醒之前在uart_tx_r地址上进行睡眠等待的锁
    // 也就是将进程状态该为running，从而进入调度队列；
    // 对应uartputc中的sleep（由于uart_tx_buf缓冲区满被睡眠了，现在不满就可以开始工作了）
    wakeup(&uart_tx_r);
    
    WriteReg(THR, c);
  }
}

// read one input character from the UART.
// return -1 if none is waiting.
// 读取uart寄存器的字符
int
uartgetc(void)
{
  if(ReadReg(LSR) & 0x01){
    // input data is ready.
    return ReadReg(RHR);
  } else {
    return -1;
  }
}

// handle a uart interrupt, raised because input has
// arrived, or the uart is ready for more output, or
// both. called from devintr().

// 两种情况会触发uartintr: 1) 输入通道rx满(键盘有数据输入). 2)输出通道tx为空
void
uartintr(void)
{
  // read and process incoming characters.
  // 读取字符,通道rx为满的中断
  while(1){
    int c = uartgetc();
    if(c == -1)
      break;
    // 将字符放入console的缓冲区,并回显字符
    consoleintr(c);
  }

  // send buffered characters.
  // 异步发送,对应tx为空的中断
  acquire(&uart_tx_lock);
  uartstart();
  release(&uart_tx_lock);
}
