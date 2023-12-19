#include "types.h"
#include "defs.h"
#include "param.h"
//#include "fs.h" 
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"
#include "x86.h"
#include "traps.h"
#include "spinlock.h"
//#include "file.h"

// Interrupt descriptor table (shared by all CPUs).
struct gatedesc idt[256];
extern uint vectors[];  // in vectors.S: array of 256 entry pointers
struct spinlock tickslock;
uint ticks;

struct file {
  enum { FD_NONE, FD_PIPE, FD_INODE } type;
  int ref; // reference count
  char readable;
  char writable;
  struct pipe *pipe;
  struct inode *ip;
  uint off;
};

void
tvinit(void)
{
  int i;

  for(i = 0; i < 256; i++)
    SETGATE(idt[i], 0, SEG_KCODE<<3, vectors[i], 0);
  SETGATE(idt[T_SYSCALL], 1, SEG_KCODE<<3, vectors[T_SYSCALL], DPL_USER);

  initlock(&tickslock, "time");
}

void
idtinit(void)
{
  lidt(idt, sizeof(idt));
}

int
handle_page_fault(struct trapframe *tf) {
  struct proc *curproc = myproc();
  uint fault_addr = rcr2(); 
  int is_write = tf->err & 2;  // 0이면 read, 1이면 write
  struct mmap_area *area = 0;
  
  // 3번: mmap 영역을 확인합니다.
  for (int i = 0; i < 64; i++) {
    if (mmap_areas[i].addr <= fault_addr && (mmap_areas[i].addr + mmap_areas[i].length) > fault_addr) {
      area = &mmap_areas[i];
      break;
    }
  }
  // 3번: 주소에 해당하는 mmap_area가 없는 경우
  if (!area) {
      return -1;
  }
  // 4번: 쓰기 금지된 mmap_area에 쓰기 시도하는 경우
  if (is_write && !(area->prot & PROT_WRITE)) {
      return -1;
  }



  char *mem;
  mem = kalloc();
  if (mem == 0) {
    return -1; // 메모리 할당 실패
  }
  memset(mem, 0, PGSIZE);

  // uint offst = fault_addr - area->addr;
  // int r = 0;

// 3. If it is file mapping, read file into the physical page with offset
  if (!(area->flags & MAP_ANONYMOUS) && area->f->ip != 0) {
    // ilock(area->f->ip);
    // int curroff = (area->offset + offst) % PGSIZE;
    // r = readi(area->f->ip, mem, area->offset + offst - curroff, PGSIZE);
    // iunlock(area->f->ip);
    fileread(area->f, mem, PGSIZE);
  }
  // if (r<0){
  // }
  // 페이지 테이블 만들기 및 적절하게 채우기
  int pte_flags = PTE_U | (is_write ? PTE_W : 0);
  if (mappages(curproc->pgdir, (void *)fault_addr, PGSIZE, V2P(mem), pte_flags) < 0) {
      kfree(mem);
      myproc()->killed = 1;
  }
  return 0;
}

//PAGEBREAK: 41
void
trap(struct trapframe *tf)
{
  if(tf->trapno == T_SYSCALL){
    if(myproc()->killed)
      exit();
    myproc()->tf = tf;
    syscall();
    if(myproc()->killed)
      exit();
    return;
  }

  switch(tf->trapno){
  case T_IRQ0 + IRQ_TIMER:
    if(cpuid() == 0){
      acquire(&tickslock);
      ticks++;
      wakeup(&ticks);
      release(&tickslock);
    }
    lapiceoi();
    break;
  case T_IRQ0 + IRQ_IDE:
    ideintr();
    lapiceoi();
    break;
  case T_IRQ0 + IRQ_IDE+1:
    // Bochs generates spurious IDE1 interrupts.
    break;
  case T_IRQ0 + IRQ_KBD:
    kbdintr();
    lapiceoi();
    break;
  case T_IRQ0 + IRQ_COM1:
    uartintr();
    lapiceoi();
    break;
  case T_IRQ0 + 7:
  case T_IRQ0 + IRQ_SPURIOUS:
    cprintf("cpu%d: spurious interrupt at %x:%x\n",
            cpuid(), tf->cs, tf->eip);
    lapiceoi();
    break;

  case T_PGFLT:
    handle_page_fault(tf);
    break;

  //PAGEBREAK: 13
  default:
    if(myproc() == 0 || (tf->cs&3) == 0){
      // In kernel, it must be our mistake.
      cprintf("unexpected trap %d from cpu %d eip %x (cr2=0x%x)\n",
              tf->trapno, cpuid(), tf->eip, rcr2());
      panic("trap");
    }
    // In user space, assume process misbehaved.
    cprintf("pid %d %s: trap %d err %d on cpu %d "
            "eip 0x%x addr 0x%x--kill proc\n",
            myproc()->pid, myproc()->name, tf->trapno,
            tf->err, cpuid(), tf->eip, rcr2());
    myproc()->killed = 1;
  }

  // Force process exit if it has been killed and is in user space.
  // (If it is still executing in the kernel, let it keep running
  // until it gets to the regular system call return.)
  if(myproc() && myproc()->killed && (tf->cs&3) == DPL_USER)
    exit();

  // Force process to give up CPU on clock tick.
  // If interrupts were on while locks held, would need to check nlock.
  if(myproc() && myproc()->state == RUNNING &&
     tf->trapno == T_IRQ0+IRQ_TIMER)
    yield();

  // Check if the process has been killed since we yielded
  if(myproc() && myproc()->killed && (tf->cs&3) == DPL_USER)
    exit();
}
