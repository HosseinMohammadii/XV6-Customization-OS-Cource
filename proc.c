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


struct {
  int front, rear, size;
  struct proc proc[NPROC];
} queue;

struct {
  int front, rear, size;
  struct proc proc[NPROC];
} midqueue;

// struct {
//   int front, rear, size;
//   struct proc proc[NPROC];
// } hqueue;

// struct {
//   int front, rear, size;
//   struct proc proc[NPROC];
// } mqueue;

// struct {
//   int front, rear, size;
//   struct proc proc[NPROC];
// } lqueue;

struct node {
   struct proc *data;
   struct node *next;
};

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
  p->rtime = 0;
  // p->etime = 0;
  p->ctime = ticks;
  p->priority = 3;
  


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

  // p->rtime = 0;


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
  #ifdef FRR
  enqueue(p);
  #endif

  #ifdef MLQ
  if(p->priority == 2){
    midenqueue(p);
  }
  #endif

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
  #ifdef FRR
  enqueue(np);
  #endif

  #ifdef MLQ
  if(p->priority == 2){
    midenqueue(np);
  }
  #endif
  

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
  curproc->etime = ticks;
  cprintf("exit time :  %d  \n"
   , ticks);
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




int
getPerformanceDatu(int *wtime, int *rtime)
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
        *rtime = curproc->rtime+1;
        *wtime = ticks -(curproc->ctime)-(curproc->rtime);
        kfree(p->kstack);
        p->kstack = 0;
        freevm(p->pgdir);
        p->pid = 0;
        p->parent = 0;
        p->name[0] = 0;
        p->killed = 0;
        p->state = UNUSED;
        p->rtime=0;
        p->ctime=0;
        p->etime=0;
        p->priority = 3;
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




int getPerformanceData(int *wtime, int *rtime) {
  struct proc *p;
  int havekids;
  struct proc *curproc = myproc();
  acquire(&ptable.lock);
  for(;;){
    // Scan through table looking for zombie children.
    havekids = 0;
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
      // if(p->parent != proc)
      if(p->parent != curproc)
        continue;
      havekids = 1;
      if(p->state == ZOMBIE){
        // Found one.
        *rtime = p->rtime;
        *wtime = p->etime-(p->ctime)-(p->rtime);
        // pid = p->pid;
        kfree(p->kstack);
        p->kstack = 0;
        freevm(p->pgdir);
        p->state = UNUSED;
        //p->pid = 0;
        p->parent = 0;
        p->name[0] = 0;
        p->killed = 0;
        p->ctime = 0;
        p->rtime = 0;
        p->etime = 0;
        // p->stime = 0;
        // p->priority = 0;
        release(&ptable.lock);
//////////////
	//printf("runtime = %d , waiting time = %d\n" , rutime , stime );
//////////////
        // return *rtime;
        // return pid;
        return 1;
      }
    }
     // No point waiting if we don't have any children.
    // if(!havekids || proc->killed){
      if(!havekids || curproc->killed){
      release(&ptable.lock);
      return -1;
    }

    // Wait for children to exit.  (See wakeup1 call in proc_exit.)
    sleep(curproc, &ptable.lock);  //DOC: wait-sleep
  }
}


int getPerformanceDato(int *wtime, int *rtime) {
  // struct proc *p;
  int pid;
  struct proc *curproc = myproc();
  acquire(&ptable.lock);
  pid = curproc->pid;
  *rtime = curproc->rtime+1;
  *wtime = ticks -(curproc->ctime)-(curproc->rtime);
  cprintf("etime: %d  ctime: %d  rtime: %d  \n", curproc->etime,
  curproc->ctime , curproc->rtime);
  release(&ptable.lock);
  return pid;
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
  struct cpu *c = mycpu();
  c->proc = 0;
  
  for(;;){
    // Enable interrupts on this processor.
    sti();

    // Loop over process table looking for process to run.
    acquire(&ptable.lock);

    #ifdef RR
      for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
      if(p->state != RUNNABLE)
        continue;
      c->proc = p;
      switchuvm(p);
      p->tickCounter = 0;
      p->state = RUNNING;
      swtch(&(c->scheduler), p->context);
      switchkvm();
      c->proc = 0;
    }  
    
    #endif
    // #else


    #ifdef FRR 
    while(queue.size>0){
      p = dequeue();
      if(p->state == RUNNABLE){
        c->proc = p;
        switchuvm(p);
        p->tickCounter = 0;
        p->state = RUNNING;
        swtch(&(c->scheduler), p->context);
        switchkvm();
        c->proc = 0;
        break;
      }
    }
    
    #endif
    // #else
    
     
    #ifdef GRT
      struct proc *minP = NULL;
      for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
        if(p->state != RUNNABLE)
          continue;
        if(p->state == RUNNABLE){
            if (minP!=NULL){
              if((p->rtime/(ticks - p->ctime)) < (minP->rtime/(ticks - minP->ctime)))
                minP = p;
            }
            else
              minP = p;
          }
      }
      if(minP != NULL){
      p = minP;
      c->proc = p;
      switchuvm(p);
      p->tickCounter = 0;
      p->state = RUNNING;
      swtch(&(c->scheduler), p->context);
      switchkvm();
      c->proc = 0;
      }
    

    #endif
    // #else
    

    #ifdef MLQ
    struct proc *minP = NULL;
    bool haselement = false;
      for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
        if(p->state != RUNNABLE)
          continue;
        if(p->state == RUNNABLE && p->priority==3){
            if (minP!=NULL){
              if((p->rtime/(ticks - p->ctime)) < (minP->rtime/(ticks - minP->ctime)))
                minP = p;
            }
            else{
              minP = p;
              haselement=true;
            }
        }
      }
      if(minP != NULL){
      p = minP;
      c->proc = p;
      switchuvm(p);
      p->state = RUNNING;
      swtch(&(c->scheduler), p->context);
      switchkvm();
      c->proc = 0;
      }

      bool haselement2 = false;
      if(!haselement){
        while(midqueue.size>0){
          p = middequeue();
          if(p->state == RUNNABLE){
            haselement2=true;
            c->proc = p;
            switchuvm(p);
            p->tickCounter = 0;
            p->state = RUNNING;
            swtch(&(c->scheduler), p->context);
            switchkvm();
            c->proc = 0;
            break;
          }
        }
      }

      if(!haselement && !haselement2){
        for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
          if(p->state != RUNNABLE)
            continue;
          c->proc = p;
          switchuvm(p);
          p->tickCounter = 0;
          p->state = RUNNING;
          swtch(&(c->scheduler), p->context);
          switchkvm();
          c->proc = 0;
        }
      }

    #endif
    // #endif
    // #endif
    // #endif
    
    


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
  if(myproc()->tickCounter < QUANTA){

  }
  else{
    acquire(&ptable.lock);  //DOC: yieldlock
    #ifdef FRR
    enqueue(myproc());
    #endif
    #ifdef MLQ
      if(p->priority == 2){
        midenqueue(myproc());
      }
    #endif
    myproc()->state = RUNNABLE;
    sched();
    release(&ptable.lock);
  }
}

// void
// yield(void)
// {
//   acquire(&ptable.lock);  //DOC: yieldlock
//   myproc()->state = RUNNABLE;
//   #ifdef FRR
//   enqueue(myproc());
//   #endif
//   sched();
//   release(&ptable.lock);
// }


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

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->state == SLEEPING && p->chan == chan){
      p->state = RUNNABLE;
      #ifdef FRR
      enqueue(myproc());
      #endif
      #ifdef MLQ
      if(p->priority == 2){
        midenqueue(myproc());
      }
      #endif
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
      if(p->state == SLEEPING){
        p->state = RUNNABLE;
          #ifdef FRR
          enqueue(myproc());
          #endif
          #ifdef MLQ
          if(p->priority == 2){
            midenqueue(p);
          }
          #endif
      }
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

void
enqueue(struct proc proc){
  if(queue.size<NPROC)
    {
        if(queue.size<0)
        {
            queue.proc[0] = proc;
            queue.front = queue.rear = 0;
            queue.size = 1;
        }
        else if(queue.rear == NPROC-1)
        {
            queue.proc[0] = proc;
            queue.rear = 0;
            queue.size++;
        }
        else
        {
          queue.rear++;
          queue.size++;
          queue.proc[queue.rear] = proc;
        }
    }
    else
    {
        cprintf("Queue is full\n");
    }
}

void
midenqueue(struct proc proc){
  if(midqueue.size<NPROC)
    {
        if(midqueue.size<0)
        {
            midqueue.proc[0] = proc;
            midqueue.front = midqueue.rear = 0;
            midqueue.size = 1;
        }
        else if(midqueue.rear == NPROC-1)
        {
            midqueue.proc[0] = proc;
            midqueue.rear = 0;
            midqueue.size++;
        }
        else
        {
          midqueue.rear++;
          midqueue.size++;
          midqueue.proc[midqueue.rear] = proc;
        }
    }
    else
    {
        cprintf("Queue is full\n");
    }
}

struct proc*
dequeue(){
  struct proc *p = myproc();
  if(queue.size<=0)
    {
        cprintf("Queue is empty\n");
    }
  else
    {
        if(queue.front == NPROC-1)
        {
            queue.front = 0;
            queue.size--;
            p = &queue.proc[NPROC-1];
        }
        else{
            queue.front++;
            queue.size--;
            p = &queue.proc[queue.front-1];
        }
    }
    
    return p;
}

struct proc*
middequeue(){
  struct proc *p = myproc();
  if(midqueue.size<=0)
    {
        cprintf("Queue is empty\n");
    }
  else
    {
        if(midqueue.front == NPROC-1)
        {
            midqueue.front = 0;
            midqueue.size--;
            p = &midqueue.proc[NPROC-1];
        }
        else{
            midqueue.front++;
            midqueue.size--;
            p = &midqueue.proc[midqueue.front-1];
        }
    }
    
    return p;
}

int
nice(void){
  
  acquire(&ptable.lock);
  if(myproc()->priority > 1){
    myproc()->priority--;
    // if()
    return 1;
  }
  else{
    return 0;
  }
  release(&ptable.lock);
}

// void insert_at_end(struct proc *pp) {
//    struct node *t, *temp;
   
//    t = (struct node*)malloc(sizeof(struct node));
//    count++;
   
//    if (start == NULL) {
//       start = t;
//       start->data = pp;
//       start->next = NULL;
//       return;
//    }
//    temp = start;
   
//    while (temp->next != NULL)
//       temp = temp->next;  
   
//    temp->next = t;
//    t->data    = x;
//    t->next    = NULL;
// }

// void delete_from_begin() {
//    struct node *t;
//    int n;
   
//    if (start == NULL) {
//       printf("Linked list is already empty.\n");
//       return;
//    }
   
//    n = start->data;
//    t = start->next;
//    free(start);
//    start = t;
//    count--;
   
//    printf("%d deleted from beginning successfully.\n", n);
// }
 
// void delete_from_end() {
//    struct node *t, *u;
//    int n;
     
//    if (start == NULL) {
//       printf("Linked list is already empty.\n");
//       return;
//    }
   
//    count--;
   
//    if (start->next == NULL) {
//       n = start->data;
//       free(start);
//       start = NULL;
//       printf("%d deleted from end successfully.\n", n);
//       return;
//    }
   
//    t = start;
   
//    while (t->next != NULL) {
//       u = t;
//       t = t->next;
//    }
   
//    n = t->data;
//    u->next = NULL;
//    free(t);
   
//    printf("%d deleted from end successfully.\n", n);
// }


// struct proc*
// delete(int pid){
//   struct node p*;
//   struct proc pp*;
//   struct node t*;
//   while (t->next != NULL ) {
//     if(t->next->data->pid !=pid){
//       p = t;
//       t = t->next;
//       break
//     }
//       p = t;
//       t = t->next;
//    }
// }
