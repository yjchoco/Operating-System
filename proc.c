#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "x86.h"
#include "proc.h"
#include "spinlock.h"
#include "math.h"
#include "biguint.h"

struct {
  struct spinlock lock;
  struct proc proc[NPROC];
} ptable;

static struct proc *initproc;

int nextpid = 1;
extern void forkret(void);
extern void trapret(void);

static void wakeup1(void *chan);

const int weight[40] = {
  /*0~9*/ 88761, 71755, 56483, 46273, 36291, 29154, 23254, 18705, 14949, 11916,
  /*10~19*/ 9548, 7620, 6100, 4904, 3906, 3121, 2501, 1991, 1586, 1277,
  /*20~29*/ 1024, 820, 655, 526, 423, 335, 272, 215, 172, 137,
  /*30~39*/ 110, 87, 70, 56, 45, 36, 29, 23, 18, 15
};
extern uint ticks;
extern uint total_ticks;


int compare_biguint(BigUInt a, BigUInt b) { 
    if (a.high > b.high) {
        return 1;
    } else if (a.high < b.high) {
        return -1;
    } else { // a->high == b->high
        if (a.low > b.low) {
            return 1;
        } else if (a.low < b.low) {
            return -1;
        } else {
            return 0; // a->low == b->low
        }
    }
}

BigUInt minvruntime(){   
  struct proc *p;
  BigUInt min_vrt;
  min_vrt.high = 0xFFFFFFFF;
  min_vrt.low = 0xFFFFFFFF;

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++) {
      if(p->state == RUNNABLE && compare_biguint(p->vruntime, min_vrt) < 0) {
          min_vrt = p->vruntime;
      }
  }
  // If no process in RUNNABLE state, return 0
  if (min_vrt.high == 0xFFFFFFFF && min_vrt.low == 0xFFFFFFFF) {
      BigUInt zeroValue = {0, 0};
      return zeroValue;
  }
  return min_vrt;
}

BigUInt subtract_value(BigUInt num, BigUInt value) { ///ppp
    BigUInt rturn;
    rturn.low = num.low - value.low;
    rturn.high = num.high - value.high;

    if (num.low < 0) { // 오버플로우 발생
        num.low += 10000000;
        num.high--;  
    }
    return rturn;
    }

void
pinit(void)
{
  initlock(&ptable.lock, "ptable");
}

// Must be called with interrupts disabled
int
cpuid() {
  return mycpu()-cpus;
}


// Must be called with interrupts disabled to avoid the caller being
// rescheduled between reading lapicid and running through the loop.
struct cpu*
mycpu(void)
{
  int apicid, i;
  
  if(readeflags()&FL_IF)
    panic("mycpu called with interrupts enabled\n");
  
  apicid = lapicid();
  // APIC IDs are not guaranteed to be contiguous. Maybe we should have
  // a reverse map, or reserve a register to store &cpus[i].
  for (i = 0; i < ncpu; ++i) {
    if (cpus[i].apicid == apicid)
      return &cpus[i];
  }
  panic("unknown apicid\n");
}

// Disable interrupts so that we are not rescheduled
// while reading proc from the cpu structure
struct proc*
myproc(void) {
  struct cpu *c;
  struct proc *p;
  pushcli();
  c = mycpu();
  p = c->proc;
  popcli();
  return p;
}

//PAGEBREAK: 32
// Look in the process table for an UNUSED proc.
// If found, change state to EMBRYO and initialize
// state required to run in the kernel.
// Otherwise return 0.
static struct proc*
allocproc(void)
{
  struct proc *p;
  char *sp;

  acquire(&ptable.lock);

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    if(p->state == UNUSED)
      goto found;

  release(&ptable.lock);
  return 0;

found:
  p->state = EMBRYO;
  p->pid = nextpid++;

  // 초기값 초기화 코드
  p->nice = 20;
  p->vruntime.high = 0;
  p->vruntime.low = 0;
  p->runtime = 0; 
  p->weight = weight[p->nice];
  p->timeslice = 0; 

  release(&ptable.lock);

  // Allocate kernel stack.
  if((p->kstack = kalloc()) == 0){
    p->state = UNUSED;
    return 0;
  }
  sp = p->kstack + KSTACKSIZE;

  // Leave room for trap frame.
  sp -= sizeof *p->tf;
  p->tf = (struct trapframe*)sp;

  // Set up new context to start executing at forkret,
  // which returns to trapret.
  sp -= 4;
  *(uint*)sp = (uint)trapret;

  sp -= sizeof *p->context;
  p->context = (struct context*)sp;
  memset(p->context, 0, sizeof *p->context);
  p->context->eip = (uint)forkret;

  return p;
}

//PAGEBREAK: 32
// Set up first user process.
void
userinit(void)
{
  struct proc *p;
  extern char _binary_initcode_start[], _binary_initcode_size[];


  p = allocproc();
  
  initproc = p;
  if((p->pgdir = setupkvm()) == 0)
    panic("userinit: out of memory?");
  inituvm(p->pgdir, _binary_initcode_start, (int)_binary_initcode_size);
  p->sz = PGSIZE;
  memset(p->tf, 0, sizeof(*p->tf));
  p->tf->cs = (SEG_UCODE << 3) | DPL_USER;
  p->tf->ds = (SEG_UDATA << 3) | DPL_USER;
  p->tf->es = p->tf->ds;
  p->tf->ss = p->tf->ds;
  p->tf->eflags = FL_IF;
  p->tf->esp = PGSIZE;
  p->tf->eip = 0;  // beginning of initcode.S

  safestrcpy(p->name, "initcode", sizeof(p->name));
  p->cwd = namei("/");

  // this assignment to p->state lets other cores
  // run this process. the acquire forces the above
  // writes to be visible, and the lock is also needed
  // because the assignment might not be atomic.
  acquire(&ptable.lock);

  p->state = RUNNABLE;

  release(&ptable.lock);
}

// Grow current process's memory by n bytes.
// Return 0 on success, -1 on failure.
int
growproc(int n)
{
  uint sz;
  struct proc *curproc = myproc();

  sz = curproc->sz;
  if(n > 0){
    if((sz = allocuvm(curproc->pgdir, sz, sz + n)) == 0)
      return -1;
  } else if(n < 0){
    if((sz = deallocuvm(curproc->pgdir, sz, sz + n)) == 0)
      return -1;
  }
  curproc->sz = sz;
  switchuvm(curproc);
  return 0;
}

// Create a new process copying p as the parent.
// Sets up stack to return as if from system call.
// Caller must set state of returned proc to RUNNABLE.
int
fork(void)
{
  int i, pid;
  struct proc *np;
  struct proc *curproc = myproc();

  // Allocate process.
  if((np = allocproc()) == 0){
    return -1;
  }

  // Copy process state from proc.
  if((np->pgdir = copyuvm(curproc->pgdir, curproc->sz)) == 0){
    kfree(np->kstack);
    np->kstack = 0;
    np->state = UNUSED;
    return -1;
  }
  np->sz = curproc->sz;
  np->parent = curproc;
  *np->tf = *curproc->tf;

  // Clear %eax so that fork returns 0 in the child.
  np->tf->eax = 0;

  for(i = 0; i < NOFILE; i++)
    if(curproc->ofile[i])
      np->ofile[i] = filedup(curproc->ofile[i]);
  np->cwd = idup(curproc->cwd);

  safestrcpy(np->name, curproc->name, sizeof(curproc->name));

  pid = np->pid;

  acquire(&ptable.lock);

  np->state = RUNNABLE;

  // MINE. Set the nice value for the child process
  np->nice = curproc->nice;
  np->vruntime = curproc->vruntime;
  np->runtime = 0;
  np->weight = weight[np->nice];
  int total_weight = 0;
  for(struct proc *p = ptable.proc; p<&ptable.proc[NPROC]; p++) {
    if (p->state == RUNNABLE){
      total_weight += weight[p->nice];}}
  np->timeslice = 10000*(weight[np->nice] / (total_weight == 0 ? 1 : total_weight));


  release(&ptable.lock);

  return pid;
}

// Exit the current process.  Does not return.
// An exited process remains in the zombie state
// until its parent calls wait() to find out it exited.
void
exit(void)
{
  struct proc *curproc = myproc();
  struct proc *p;
  int fd;

  if(curproc == initproc)
    panic("init exiting");

  // Close all open files.
  for(fd = 0; fd < NOFILE; fd++){
    if(curproc->ofile[fd]){
      fileclose(curproc->ofile[fd]);
      curproc->ofile[fd] = 0;
    }
  }

  begin_op();
  iput(curproc->cwd);
  end_op();
  curproc->cwd = 0;

  acquire(&ptable.lock);

  // Parent might be sleeping in wait().
  wakeup1(curproc->parent);

  // Pass abandoned children to init.
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->parent == curproc){
      p->parent = initproc;
      if(p->state == ZOMBIE)
        wakeup1(initproc);
    }
  }

  // Jump into the scheduler, never to return.
  curproc->state = ZOMBIE;
  sched();
  panic("zombie exit");
}

// Wait for a child process to exit and return its pid.
// Return -1 if this process has no children.
int
wait(void)
{
  struct proc *p;
  int havekids, pid;
  struct proc *curproc = myproc();
  
  acquire(&ptable.lock);
  for(;;){
    // Scan through table looking for exited children.
    havekids = 0;
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
      if(p->parent != curproc)
        continue;
      havekids = 1;
      if(p->state == ZOMBIE){
        // Found one.
        pid = p->pid;
        kfree(p->kstack);
        p->kstack = 0;
        freevm(p->pgdir);
        p->pid = 0;
        p->parent = 0;
        p->name[0] = 0;
        p->killed = 0;
        p->state = UNUSED;
        release(&ptable.lock);
        return pid;
      }
    }

    // No point waiting if we don't have any children.
    if(!havekids || curproc->killed){
      release(&ptable.lock);
      return -1;
    }

    // Wait for children to exit.  (See wakeup1 call in proc_exit.)
    sleep(curproc, &ptable.lock);  //DOC: wait-sleep
  }
}

//PAGEBREAK: 42
// Per-CPU process scheduler.
// Each CPU calls scheduler() after setting itself up.
// Scheduler never returns.  It loops, doing:
//  - choose a process to run
//  - swtch to start running that process
//  - eventually that process transfers control
//      via swtch back to the scheduler.


void
scheduler(void)
{
  struct proc *p;
  struct proc *minp; 
  struct cpu *c = mycpu();
  c->proc = 0;
  
  for(;;){
    // Enable interrupts on this processor.
    sti();
    minp = 0;

    // Loop over process table looking for process to run.
    acquire(&ptable.lock);
    
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
      if(p->state != RUNNABLE)
        continue;

        //// Choose the process with the least vruntime.
      if(minp == 0 || compare_biguint(p->vruntime, minp->vruntime) < 0) {
        minp = p;
      }
    } 

    if(minp != 0) { 
      ///minp->runtime += 1000;

      int total_weight = 0;
      for(p = ptable.proc; p<&ptable.proc[NPROC]; p++) {
        if (p->state == RUNNABLE){
          total_weight += weight[p->nice];}}
      minp->timeslice = 10000*(weight[p->nice] / (total_weight == 0 ? 1 : total_weight));
      
      // Switch to chosen process.  It is the process's job
      // to release ptable.lock and then reacquire it
      // before jumping back to us.
      c->proc = minp;
      c->proc->cpu_start_time = ticks;
      switchuvm(minp);
      minp->state = RUNNING;

      swtch(&(c->scheduler), minp->context);
      switchkvm();

      // Process is done running for now.
      // It should have changed its p->state before coming back.
      c->proc = 0;
    }
    release(&ptable.lock);

  }
}


// Enter scheduler.  Must hold only ptable.lock
// and have changed proc->state. Saves and restores
// intena because intena is a property of this
// kernel thread, not this CPU. It should
// be proc->intena and proc->ncli, but that would
// break in the few places where a lock is held but
// there's no process.

void
sched(void)
{
  int intena;
  struct proc *p = myproc();

  if(!holding(&ptable.lock))
    panic("sched ptable.lock");
  if(mycpu()->ncli != 1)
    panic("sched locks");
  if(p->state == RUNNING)
    panic("sched running");
  if(readeflags()&FL_IF)
    panic("sched interruptible");
  intena = mycpu()->intena;
  swtch(&p->context, mycpu()->scheduler);
  mycpu()->intena = intena;
}

// Give up the CPU for one scheduling round.
void
yield(void)
{
  acquire(&ptable.lock);  //DOC: yieldlock
  myproc()->state = RUNNABLE;
  sched();
  release(&ptable.lock);
}

// A fork child's very first scheduling by scheduler()
// will swtch here.  "Return" to user space.
void
forkret(void)
{
  static int first = 1;
  // Still holding ptable.lock from scheduler.
  release(&ptable.lock);

  if (first) {
    // Some initialization functions must be run in the context
    // of a regular process (e.g., they call sleep), and thus cannot
    // be run from main().
    first = 0;
    iinit(ROOTDEV);
    initlog(ROOTDEV);
  }

  // Return to "caller", actually trapret (see allocproc).
}

// Atomically release lock and sleep on chan.
// Reacquires lock when awakened.
void
sleep(void *chan, struct spinlock *lk)
{
  struct proc *p = myproc();
  
  if(p == 0)
    panic("sleep");

  if(lk == 0)
    panic("sleep without lk");

  // Must acquire ptable.lock in order to
  // change p->state and then call sched.
  // Once we hold ptable.lock, we can be
  // guaranteed that we won't miss any wakeup
  // (wakeup runs with ptable.lock locked),
  // so it's okay to release lk.
  if(lk != &ptable.lock){  //DOC: sleeplock0
    acquire(&ptable.lock);  //DOC: sleeplock1
    release(lk);
  }
  // Go to sleep.
  p->chan = chan;
  p->state = SLEEPING;

  sched();

  // Tidy up.
  p->chan = 0;

  // Reacquire original lock.
  if(lk != &ptable.lock){  //DOC: sleeplock2
    release(&ptable.lock);
    acquire(lk);
  }
}


//PAGEBREAK!
// Wake up all processes sleeping on chan.
// The ptable lock must be held.
static void
wakeup1(void *chan)
{
  struct proc *p;
  BigUInt min_vrt = minvruntime();
  BigUInt sub_value;

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    if(p->state == SLEEPING && p->chan == chan){
      p->state = RUNNABLE;
      if(min_vrt.high == 0 && min_vrt.low == 0) {
        p->vruntime.high = 0;
        p->vruntime.low = 0;
      } else {
        sub_value.high = 0;
        sub_value.low = 1000*(1024 / weight[p->nice]);
        p->vruntime = subtract_value(min_vrt, sub_value); 
      }
    }
}

// Wake up all processes sleeping on chan.
void
wakeup(void *chan)
{
  acquire(&ptable.lock);
  wakeup1(chan);
  release(&ptable.lock);
}

// Kill the process with the given pid.
// Process won't exit until it returns
// to user space (see trap in trap.c).
int
kill(int pid)
{
  struct proc *p;

  acquire(&ptable.lock);
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->pid == pid){
      p->killed = 1;
      // Wake process from sleep if necessary.
      if(p->state == SLEEPING)
        p->state = RUNNABLE;
      release(&ptable.lock);
      return 0;
    }
  }
  release(&ptable.lock);
  return -1;
}

//PAGEBREAK: 36
// Print a process listing to console.  For debugging.
// Runs when user types ^P on console.
// No lock to avoid wedging a stuck machine further.
void
procdump(void)
{
  static char *states[] = {
  [UNUSED]    "unused",
  [EMBRYO]    "embryo",
  [SLEEPING]  "sleep ",
  [RUNNABLE]  "runble",
  [RUNNING]   "run   ",
  [ZOMBIE]    "zombie"
  };
  int i;
  struct proc *p;
  char *state;
  uint pc[10];

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->state == UNUSED)
      continue;
    if(p->state >= 0 && p->state < NELEM(states) && states[p->state])
      state = states[p->state];
    else
      state = "???";
    cprintf("%d %s %s", p->pid, state, p->name);
    if(p->state == SLEEPING){
      getcallerpcs((uint*)p->context->ebp+2, pc);
      for(i=0; i<10 && pc[i] != 0; i++)
        cprintf(" %p", pc[i]);
    }
    cprintf("\n");
  }
}


//getpname
int
getpname(int pid)
{
	struct proc *p;

	acquire(&ptable.lock);
	for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
		if(p->pid == pid){
			cprintf("%s\n", p->name);
			release(&ptable.lock);
			return 0;
		}
	}
	release(&ptable.lock);
	return -1;
}


//getnice
int
getnice(int pid)
{
	struct proc *p;
	int nice;

	acquire(&ptable.lock);

	for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
		if(p->pid == pid){
			nice = p->nice;
			release(&ptable.lock);
			return nice;
		}
	}

	release(&ptable.lock);
	return -1;
}


//setnice
int
setnice(int pid, int value)
{
	struct proc *p;

	acquire(&ptable.lock);
	
	for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
		if(p->pid == pid){
			p->nice = value;
			release(&ptable.lock);
			return 0;
		}
	}
	release(&ptable.lock);
	return -1;
}


//ps

char* procstate_strings[] = {
	"UNUSED",
	"EMBRYO",
	"SLEEPING",
	"RUNNABLE",
	"RUNNING",
	"ZOMBIE"
};


void print_uint(uint value) {
    char buf[11];
    int pos = sizeof(buf) - 1;
    buf[pos] = '\0';  
    do {
        --pos;
        buf[pos] = '0' + (value % 10);
        value /= 10;
    } while (value > 0);
    cprintf("%s", &buf[pos]);
}


void print_big_uint(BigUInt *num){
  if (num->high != 0) {
        print_uint(num->high);
        uint temp = num->low;
        int count = 0;
        while (temp) {
            count++;
            temp /= 10;
        }
        for (int i = 0; i < 10 - count; i++) {
            cprintf("0");
        }
        print_uint(num->low);
        cprintf("HIGH DIGIT EXISTING");
    } else if (num->high == 0){
        print_uint(num->low);
    }
}


void
ps(int pid)
{
	struct proc *p;
	int found = 0;
	acquire(&ptable.lock);
	
	if(pid == 0){
	cprintf("name\t\tpid\t\tstate\t\tpriority\t\truntime/weight\t\truntime\t\tvruntime\t\t\ttick ");
  print_uint(ticks);
  cprintf("\n");

	for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
		if(p->state == UNUSED)
			continue;

    if(strlen(procstate_strings[p->state]) >= 8) {
      cprintf("%s\t\t%d\t\t%s\t%d\t\t\t", p->name, p->pid, procstate_strings[p->state], p->nice);
      print_uint(p->runtime / weight[p->nice]); 
      cprintf("\t\t\t");
      print_uint(p->runtime);
      cprintf("\t\t");
      print_big_uint(&(p->vruntime));
      cprintf("\n"); 
      //cprintf("ORIGINAL: %s\t\t%d\t\t%s\t%d\t\t\t%d\t\t\t%d\t\t%d\n", p->name, p->pid, procstate_strings[p->state], p->nice);
    } else {
      cprintf("%s\t\t%d\t\t%s\t\t%d\t\t\t", p->name, p->pid, procstate_strings[p->state], p->nice);
      print_uint(p->runtime / weight[p->nice]); 
      cprintf("\t\t\t");
      print_uint(p->runtime);
      cprintf("\t\t");
      print_big_uint(&(p->vruntime));
      cprintf("\n");
      //cprintf("ORIGINAL: %s\t\t%d\t\t%s\t\t%d\t\t\t%d\t\t\t%d\t\t%d\n", p->name, p->pid, procstate_strings[p->state], p->nice);
    }
  }
	} else{
		for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
			if(p->state == UNUSED)
				continue;

			if(p->pid == pid) {
				found = 1;
				cprintf("name\t\tpid\t\tstate\t\tpriority\t\truntime/weight\t\truntime\t\tvruntime\t\t\ttick %d\n", ticks);
				cprintf("%s		%d		%s		%d	%d	%d	%d\n", p->name, p->pid, procstate_strings[p->state], p->nice, p->runtime / (weight[p->nice] == 0?1:weight[p->nice]), (p->runtime), (p->vruntime));
				break;
			}
		}
	}
		
	release(&ptable.lock);
	if(pid != 0 && !found) {
	}
}







