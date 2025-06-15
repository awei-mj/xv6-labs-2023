#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"

struct spinlock tickslock;
uint ticks;

extern char trampoline[], uservec[], userret[];

// in kernelvec.S, calls kerneltrap().
void kernelvec();

extern int devintr();

void
trapinit(void)
{
  initlock(&tickslock, "time");
}

// set up to take exceptions and traps while in the kernel.
void
trapinithart(void)
{
  w_stvec((uint64)kernelvec);
}

//
// handle an interrupt, exception, or system call from user space.
// called from trampoline.S
//
void
usertrap(void)
{
  int which_dev = 0;

  if((r_sstatus() & SSTATUS_SPP) != 0)
    panic("usertrap: not from user mode");

  // send interrupts and exceptions to kerneltrap(),
  // since we're now in the kernel.
  w_stvec((uint64)kernelvec);

  struct proc *p = myproc();
  
  // save user program counter.
  p->trapframe->epc = r_sepc();
  
  if(r_scause() == 8){
    // system call

    if(killed(p))
      exit(-1);

    // sepc points to the ecall instruction,
    // but we want to return to the next instruction.
    p->trapframe->epc += 4;

    // an interrupt will change sepc, scause, and sstatus,
    // so enable only now that we're done with those registers.
    intr_on();

    syscall();
  } else if((which_dev = devintr()) != 0){
    // ok
    if(which_dev == 2) {
      // timer intr
      if(p->interval != 0) {
        p->ticks_since_handler += 1;
        if((p->ticks_since_handler >= p->interval) && !p->user_in_handler) {
          // invoke the handler of sigalarm
          // save the context and change epc/sp
          p->user_in_handler = 1;
          save_sigcontext(p->trapframe, &p->sigctx);
          p->trapframe->epc = p->handler;
        }
      }
    }
  } else {
    printf("usertrap(): unexpected scause %p pid=%d\n", r_scause(), p->pid);
    printf("            sepc=%p stval=%p\n", r_sepc(), r_stval());
    setkilled(p);
  }

  if(killed(p))
    exit(-1);

  // give up the CPU if this is a timer interrupt.
  if(which_dev == 2)
    yield();

  usertrapret();
}

//
// return to user space
//
void
usertrapret(void)
{
  struct proc *p = myproc();

  // we're about to switch the destination of traps from
  // kerneltrap() to usertrap(), so turn off interrupts until
  // we're back in user space, where usertrap() is correct.
  intr_off();

  // send syscalls, interrupts, and exceptions to uservec in trampoline.S
  uint64 trampoline_uservec = TRAMPOLINE + (uservec - trampoline);
  w_stvec(trampoline_uservec);

  // set up trapframe values that uservec will need when
  // the process next traps into the kernel.
  p->trapframe->kernel_satp = r_satp();         // kernel page table
  p->trapframe->kernel_sp = p->kstack + PGSIZE; // process's kernel stack
  p->trapframe->kernel_trap = (uint64)usertrap;
  p->trapframe->kernel_hartid = r_tp();         // hartid for cpuid()

  // set up the registers that trampoline.S's sret will use
  // to get to user space.
  
  // set S Previous Privilege mode to User.
  unsigned long x = r_sstatus();
  x &= ~SSTATUS_SPP; // clear SPP to 0 for user mode
  x |= SSTATUS_SPIE; // enable interrupts in user mode
  w_sstatus(x);

  // set S Exception Program Counter to the saved user pc.
  w_sepc(p->trapframe->epc);

  // tell trampoline.S the user page table to switch to.
  uint64 satp = MAKE_SATP(p->pagetable);

  // jump to userret in trampoline.S at the top of memory, which 
  // switches to the user page table, restores user registers,
  // and switches to user mode with sret.
  uint64 trampoline_userret = TRAMPOLINE + (userret - trampoline);
  ((void (*)(uint64))trampoline_userret)(satp);
}

// interrupts and exceptions from kernel code go here via kernelvec,
// on whatever the current kernel stack is.
void 
kerneltrap()
{
  int which_dev = 0;
  uint64 sepc = r_sepc();
  uint64 sstatus = r_sstatus();
  uint64 scause = r_scause();
  
  if((sstatus & SSTATUS_SPP) == 0)
    panic("kerneltrap: not from supervisor mode");
  if(intr_get() != 0)
    panic("kerneltrap: interrupts enabled");

  if((which_dev = devintr()) == 0){
    printf("scause %p\n", scause);
    printf("sepc=%p stval=%p\n", r_sepc(), r_stval());
    panic("kerneltrap");
  }

  // give up the CPU if this is a timer interrupt.
  if(which_dev == 2 && myproc() != 0 && myproc()->state == RUNNING)
    yield();

  // the yield() may have caused some traps to occur,
  // so restore trap registers for use by kernelvec.S's sepc instruction.
  w_sepc(sepc);
  w_sstatus(sstatus);
}

void
clockintr()
{
  acquire(&tickslock);
  ticks++;
  wakeup(&ticks);
  release(&tickslock);
}

// check if it's an external interrupt or software interrupt,
// and handle it.
// returns 2 if timer interrupt,
// 1 if other device,
// 0 if not recognized.
int
devintr()
{
  uint64 scause = r_scause();

  if((scause & 0x8000000000000000L) &&
     (scause & 0xff) == 9){
    // this is a supervisor external interrupt, via PLIC.

    // irq indicates which device interrupted.
    int irq = plic_claim();

    if(irq == UART0_IRQ){
      uartintr();
    } else if(irq == VIRTIO0_IRQ){
      virtio_disk_intr();
    } else if(irq){
      printf("unexpected interrupt irq=%d\n", irq);
    }

    // the PLIC allows each device to raise at most one
    // interrupt at a time; tell the PLIC the device is
    // now allowed to interrupt again.
    if(irq)
      plic_complete(irq);

    return 1;
  } else if(scause == 0x8000000000000001L){
    // software interrupt from a machine-mode timer interrupt,
    // forwarded by timervec in kernelvec.S.

    if(cpuid() == 0){
      clockintr();
    }
    
    // acknowledge the software interrupt by clearing
    // the SSIP bit in sip.
    w_sip(r_sip() & ~2);

    return 2;
  } else {
    return 0;
  }
}

void sigreturn()
{
  struct proc *p = myproc();
  // restore the saved context TODO:
  restore_sigcontext(p->trapframe, &p->sigctx);
  
  // reset the user_in_handler flag
  p->user_in_handler = 0;
  p->ticks_since_handler = 0;
}

void save_sigcontext(struct trapframe *tf, struct sigcontext *sc)
{
  sc->epc = tf->epc;
  sc->ra = tf->ra;
  sc->sp = tf->sp;
  sc->gp = tf->gp;
  sc->tp = tf->tp;
  sc->t0 = tf->t0;
  sc->t1 = tf->t1;
  sc->t2 = tf->t2;
  sc->s0 = tf->s0;
  sc->s1 = tf->s1;
  sc->a0 = tf->a0;
  sc->a1 = tf->a1;
  sc->a2 = tf->a2;
  sc->a3 = tf->a3;
  sc->a4 = tf->a4;
  sc->a5 = tf->a5;
  sc->a6 = tf->a6;
  sc->a7 = tf->a7;
  sc->s2 = tf->s2;
  sc->s3 = tf->s3;
  sc->s4 = tf->s4;
  sc->s5 = tf->s5;
  sc->s6 = tf->s6;
  sc->s7 = tf->s7;
  sc->s8 = tf->s8;
  sc->s9 = tf->s9;
  sc->s10 = tf->s10;
  sc->s11 = tf->s11;
  sc->t3 = tf->t3;
  sc->t4 = tf->t4;
  sc->t5 = tf->t5;
  sc->t6 = tf->t6;
}
void restore_sigcontext(struct trapframe *tf, struct sigcontext *sc)
{
  tf->epc = sc->epc;
  tf->ra = sc->ra;
  tf->sp = sc->sp;
  tf->gp = sc->gp;
  tf->tp = sc->tp;
  tf->t0 = sc->t0;
  tf->t1 = sc->t1;
  tf->t2 = sc->t2;
  tf->s0 = sc->s0;
  tf->s1 = sc->s1;
  tf->a0 = sc->a0;
  tf->a1 = sc->a1;
  tf->a2 = sc->a2;
  tf->a3 = sc->a3;
  tf->a4 = sc->a4;
  tf->a5 = sc->a5;
  tf->a6 = sc->a6;
  tf->a7 = sc->a7;
  tf->s2 = sc->s2;
  tf->s3 = sc->s3;
  tf->s4 = sc->s4;
  tf->s5 = sc->s5;
  tf->s6 = sc->s6;
  tf->s7 = sc->s7;
  tf->s8 = sc->s8;
  tf->s9 = sc->s9;
  tf->s10 = sc->s10;
  tf->s11 = sc->s11;
  tf->t3 = sc->t3;
  tf->t4 = sc->t4;
  tf->t5 = sc->t5;
  tf->t6 = sc->t6;
}
