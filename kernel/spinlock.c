// Mutual exclusion spin locks.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "proc.h"
#include "defs.h"

void
initlock(struct spinlock *lk, char *name)
{
  lk->name = name;
  lk->locked = 0;
  lk->cpu = 0;
}

// Acquire the lock.
// Loops (spins) until the lock is acquired.

// Xv6的acquire使用了可移植的C库调用__sync_lock_test_and_set，它本
// 质上为amoswap指令;返回值是lk->locked的旧(被交换出来的)内容。

// 由于锁被广泛使用，多核处理器通常提供了一些原子版的指令。在RISC-V上，这条指令是amoswap r,a。
// amoswap读取内存地址a处的值，将寄存器r的内容写入该地址，并将其读取的值放入r中，也就是说，它将寄
// 存器的内容和内存地址进行了交换。它原子地执行这个序列，使用特殊的硬件来防止任何其他CPU使用读和写
// 之间的内存地址。

void
acquire(struct spinlock *lk)
{
  push_off(); // disable interrupts to avoid deadlock.
  if(holding(lk))
    panic("acquire");

  // On RISC-V, sync_lock_test_and_set turns into an atomic swap:
  //   a5 = 1
  //   s1 = &lk->locked
  //   amoswap.w.aq a5, a5, (s1)
  while(__sync_lock_test_and_set(&lk->locked, 1) != 0)
    ;

  // Tell the C compiler and the processor to not move loads or stores
  // past this point, to ensure that the critical section's memory
  // references happen strictly after the lock is acquired.
  // On RISC-V, this emits a fence instruction.

  // 许多编译器和CPU为了获得更高的性能，会不按顺序执行代码。如果一条指令需要很多周期才能完成，CPU
  // 可能会提前发出该指令，以便与其他指令重叠，避免CPU停顿。

  // 为了告诉硬件和编译器不要执行这样的re-ordering，xv6在acquire(kernel/spinlock.c:22)和
  // release(kernel/spinlock.c:47)中都使用了__sync_synchronize()。__sync_synchronize/
  // ()是一个内存屏障(memory barrier):它告诉编译器和CPU不要在越过屏障重新排列任何的内存读写操
  // 作。acquire和release中的屏障几乎在所有重要的情况下都会强制锁定顺序，因为xv6在访问共享数据
  // 的周围使用锁。

  __sync_synchronize();

  // Record info about lock acquisition for holding() and debugging.
  lk->cpu = mycpu();
}

// Release the lock.
void
release(struct spinlock *lk)
{
  if(!holding(lk))
    panic("release");

  lk->cpu = 0;

  // Tell the C compiler and the CPU to not move loads or stores
  // past this point, to ensure that all the stores in the critical
  // section are visible to other CPUs before the lock is released,
  // and that loads in the critical section occur strictly before
  // the lock is released.
  // On RISC-V, this emits a fence instruction.
  __sync_synchronize();

  // Release the lock, equivalent to lk->locked = 0.
  // This code doesn't use a C assignment, since the C standard
  // implies that an assignment might be implemented with
  // multiple store instructions.
  // On RISC-V, sync_lock_release turns into an atomic swap:
  //   s1 = &lk->locked
  //   amoswap.w zero, zero, (s1)
  __sync_lock_release(&lk->locked);

  pop_off();
}

// Check whether this cpu is holding the lock.
// Interrupts must be off.
int
holding(struct spinlock *lk)
{
  int r;
  r = (lk->locked && lk->cpu == mycpu());
  return r;
}

// push_off/pop_off are like intr_off()/intr_on() except that they are matched:
// it takes two pop_off()s to undo two push_off()s.  Also, if interrupts
// are initially off, then push_off, pop_off leaves them off.

// 自旋锁和中断的相互作用带来了一个潜在的危险。假设sys_sleep持有tickslock，而它的CPU接收到一个
// 时钟中断。clockintr会尝试获取tickslock，看到它被持有，并等待它被释放。在这种情况下，
// tickslock永远不会被释放：只有sys_sleep可以释放它，但sys_sleep不会继续运行，直到clockintr
// 返回。所以CPU会死锁，任何需要其他锁的代码也会冻结。


// 所以acquire调用push_off和release调用pop_off
// 来跟踪当前CPU上锁的嵌套级别。当该计数达到零时，pop_off会恢复最外层临界区开始时的中断启
// 用状态。intr_off和intr_on函数分别执行RISC-V指令来禁用和启用中断。

void
push_off(void)
{
  int old = intr_get();

  intr_off();
  if(mycpu()->noff == 0)
    mycpu()->intena = old;
  mycpu()->noff += 1;
}

void
pop_off(void)
{
  struct cpu *c = mycpu();
  if(intr_get())
    panic("pop_off - interruptible");
  if(c->noff < 1)
    panic("pop_off");
  c->noff -= 1;
  if(c->noff == 0 && c->intena)
    intr_on();
}
