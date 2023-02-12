#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "x86.h"
#include "proc.h"
#include "spinlock.h"

struct {
  struct spinlock lock;
  struct proc proc[NPROC];
} ptable;

static struct proc *initproc;

int nextpid = 1;
extern void forkret(void);
extern void trapret(void);

static void wakeup1(void *chan);

void
pinit(void)
{
  initlock(&ptable.lock, "ptable");
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
  
  // setting the init queue type to 1 and init quantumsize to 2 (20ms)
  p->queuetype = 1;
  p->quantumsize = 2;
  // insertion end

  p->pid = nextpid++;
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

  p->state = RUNNABLE;
}

// Grow current process's memory by n bytes.
// Return 0 on success, -1 on failure.
int
growproc(int n)
{
  uint sz;
  
  sz = proc->sz;
  if(n > 0){
    if((sz = allocuvm(proc->pgdir, sz, sz + n)) == 0)
      return -1;
  } else if(n < 0){
    if((sz = deallocuvm(proc->pgdir, sz, sz + n)) == 0)
      return -1;
  }
  proc->sz = sz;
  switchuvm(proc);
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

  // Allocate process.
  if((np = allocproc()) == 0)
    return -1;

  // Copy process state from p.
  if((np->pgdir = copyuvm(proc->pgdir, proc->sz)) == 0){
    kfree(np->kstack);
    np->kstack = 0;
    np->state = UNUSED;
    return -1;
  }
  np->sz = proc->sz;
  np->parent = proc;
  *np->tf = *proc->tf;

  // Clear %eax so that fork returns 0 in the child.
  np->tf->eax = 0;

  for(i = 0; i < NOFILE; i++)
    if(proc->ofile[i])
      np->ofile[i] = filedup(proc->ofile[i]);
  np->cwd = idup(proc->cwd);

  safestrcpy(np->name, proc->name, sizeof(proc->name));
 
  pid = np->pid;

  // lock to force the compiler to emit the np->state write last.
  acquire(&ptable.lock);
  np->state = RUNNABLE;
  release(&ptable.lock);
  
  return pid;
}

// Exit the current process.  Does not return.
// An exited process remains in the zombie state
// until its parent calls wait() to find out it exited.
void
exit(void)
{
  struct proc *p;
  int fd;

  if(proc == initproc)
    panic("init exiting");

  // Close all open files.
  for(fd = 0; fd < NOFILE; fd++){
    if(proc->ofile[fd]){
      fileclose(proc->ofile[fd]);
      proc->ofile[fd] = 0;
    }
  }

  begin_op();
  iput(proc->cwd);
  end_op();
  proc->cwd = 0;

  acquire(&ptable.lock);

  // Parent might be sleeping in wait().
  wakeup1(proc->parent);

  // Pass abandoned children to init.
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->parent == proc){
      p->parent = initproc;
      if(p->state == ZOMBIE)
        wakeup1(initproc);
    }
  }

  // Jump into the scheduler, never to return.
  proc->state = ZOMBIE;
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

  acquire(&ptable.lock);
  for(;;){
    // Scan through table looking for zombie children.
    havekids = 0;
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
      if(p->parent != proc)
        continue;
      havekids = 1;
      if(p->state == ZOMBIE){
        // Found one.
        pid = p->pid;
        kfree(p->kstack);
        p->kstack = 0;
        freevm(p->pgdir);
        p->state = UNUSED;
        p->pid = 0;
        p->parent = 0;
        p->name[0] = 0;
        p->killed = 0;
        release(&ptable.lock);
        return pid;
      }
    }

    // No point waiting if we don't have any children.
    if(!havekids || proc->killed){
      release(&ptable.lock);
      return -1;
    }

    // Wait for children to exit.  (See wakeup1 call in proc_exit.)
    sleep(proc, &ptable.lock);  //DOC: wait-sleep
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

  // initialize queue counts, used to ensure the scheduler 
  // will always choose the front process in the
  // highest priority queue, because it will not process a
  // proc in q2 if q1count > 0, will not process a proc in q3 if 
  // q1count > 0 OR q2count > 0
  int q1count = 0;
  int q2count = 0;
  int q3count = 0;

  // outer for loop: infinite
  for(;;){
    // Enable interrupts on this processor.
    sti();
    acquire(&ptable.lock);

    // loop through process table
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
      if(p->state != RUNNABLE) {
        if(p->pid > 0 && p->inqueue == 1) {
            // if the process was in a queue but is no longer runnable,
            // decrement the corresponding qcount
            switch(p->queuetype) {
              case 1 :
                q1count--;
                break;
              case 2 :
                q2count--;
                break;
              case 3 :
                q3count--; 
            }
            // and reset its queue parameters
            p->inqueue = 0;
            p->queuetype = 1;
            p->quantumsize = 2;
          }
          // since process isn't runnable, 
          // exit the loop and go to next process in ptable
        continue;
        }

      // if the process is not already in another queue:
      // place it into a queue based on queuetype
      if (p->pid > 0 && p->inqueue != 1) {
        switch(p->queuetype) {
          case 1 :
            q1count++;
            break;
          case 2 :
            q2count++;
            break;
          case 3 :
            q3count++; 
        }
        p->inqueue = 1;
      }

      proc = p;

      // q1: top priority tasks go first
      if(proc->queuetype == 1 && q1count > 0) {
        while(proc->quantumsize != 0) {
          switchuvm(p);
          p->state = RUNNING;
          // switch from kernel to process until timer interrupt
          swtch(&cpu->scheduler, proc->context);
          switchkvm();
          // jumps back here after timer interrupt (10ms)
          // output for testing
          if (strncmp(proc->name, "spin", 4) == 0 || strncmp(proc->name, "sh", 2) == 0) {
            cprintf("Process %d, '%s' has consumed 10 ms in Q%d\n", 
              proc->pid, proc->name, proc->queuetype);
          };
          proc->quantumsize--; // decrement quantumsize on each interrupt
          // if the process finishes while in the queue,
          // remove it, reset it's queue parameters, and exit the loop
          if(p->state == ZOMBIE) { 
            q1count--;
            p->inqueue = 0;
            p->queuetype = 1;
            p->quantumsize = 2;
            // if (strncmp(proc->name, "spin", 4) == 0 || strncmp(proc->name, "sh", 2) == 0) {
            //   cprintf("Process %d, '%s' reached ZOMBIE state, removed from queue\n", proc->pid, proc->name);
            // }
            break;
          }
        }
      }
      // 2nd priority, will only run if q1 is empty
      else if(p->queuetype == 2 && q1count == 0 && q2count > 0) {
        while(p->quantumsize != 0) {
          switchuvm(p);
          p->state = RUNNING;       
          swtch(&cpu->scheduler, proc->context);
          switchkvm();
          if (strncmp(proc->name, "spin", 4) == 0 || strncmp(proc->name, "sh", 2) == 0) {
            cprintf("Process %d, '%s' has consumed 10 ms in Q%d\n", 
              proc->pid, proc->name, proc->queuetype);
          };
          proc->quantumsize--;
          if(p->state == ZOMBIE) {
            q2count--;
            p->inqueue = 0;
            p->queuetype = 1;
            p->quantumsize = 2;
            // if (strncmp(proc->name, "spin", 4) == 0 || strncmp(proc->name, "sh", 2) == 0) {
            //   cprintf("Process %d, '%s' reached ZOMBIE state, removed from queue\n", proc->pid, proc->name);
            // }
            break;
          }
        }           
      }
      // 3rd priority goes last, will only run if q1 and q2 are empty
      else if(p->queuetype == 3 && q1count == 0 && q2count == 0 && q3count > 0) {
        while(p->quantumsize != 0) {
          switchuvm(p);
          p->state = RUNNING;        
          swtch(&cpu->scheduler, proc->context);
          switchkvm();
          if (strncmp(proc->name, "spin", 4) == 0 || strncmp(proc->name, "sh", 2) == 0) {
            cprintf("Process %d, '%s' has consumed 10 ms in Q%d\n", 
              proc->pid, proc->name, proc->queuetype);
          };
          proc->quantumsize--;
          if(p->state == ZOMBIE) {
            q3count--;
            p->inqueue = 0;
            p->queuetype = 1;
            p->quantumsize = 2;
            // if (strncmp(proc->name, "spin", 4) == 0 || strncmp(proc->name, "sh", 2) == 0) {
            //   cprintf("Process %d, '%s' reached ZOMBIE state, removed from queue\n", proc->pid, proc->name);
            // }
            break;
          }
          // if the quantumsize == 8, the process has gone through it's
          // first of two rounds in q3 -> do round robin, 
          // exit q3 loop and move to the next process in scheduler
          if(proc->quantumsize == 8) break;
        }
      }
      // handler for if process quantumsize == 0,
      // the process didn't finish in this round
      if(p->quantumsize == 0) {
        if(p->queuetype == 1) {
          // demote to q2
          q1count--;
          q2count++;
          p->queuetype = 2;
          p->quantumsize = 4;
        }
        else if(p->queuetype == 2){
          // demote to q3
          q2count--;
          q3count++;
          p->queuetype = 3;
          p->quantumsize = 16;
        }
        else if(p->queuetype == 3){
          // boost to q1
          q3count--;
          q1count++;
          p->queuetype = 1;
          p->quantumsize = 2;
        }
      }
    // Process is done running for now.
    // It should have changed its p->state before coming back.
      proc = 0;
    }
    release(&ptable.lock);
  }
}
// spin 10000000 &; spin 10000000 &; spin 10000000 &;

// Enter scheduler.  Must hold only ptable.lock
// and have changed proc->state.
void
sched(void)
{
  int intena;

  if(!holding(&ptable.lock))
    panic("sched ptable.lock");
  if(cpu->ncli != 1)
    panic("sched locks");
  if(proc->state == RUNNING)
    panic("sched running");
  if(readeflags()&FL_IF)
    panic("sched interruptible");
  intena = cpu->intena;
  swtch(&proc->context, cpu->scheduler);
  cpu->intena = intena;
}

// Give up the CPU for one scheduling round.
void
yield(void)
{
  acquire(&ptable.lock);  //DOC: yieldlock
  proc->state = RUNNABLE;
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
  if(proc == 0)
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
  proc->chan = chan;
  proc->state = SLEEPING;
  sched();

  // Tidy up.
  proc->chan = 0;

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

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    if(p->state == SLEEPING && p->chan == chan)
      p->state = RUNNABLE;
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
