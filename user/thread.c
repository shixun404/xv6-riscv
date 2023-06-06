#include "user/thread.h"
int thread_create(void *(start_routine)(void*), void *arg){
    uint64* user_stack = malloc(PGSIZE+1) + PGSIZE;
    int ret = clone((void*) user_stack);
    if(ret < 0) return -1;
    if(ret == 0) return 0;
    start_routine(arg);
    exit(0);
}


void
lock_init(struct lock_t* lock)
{
  lock->locked = 0;
}

// Acquire the lock.
// Loops (spins) until the lock is acquired.
void
lock_acquire(struct lock_t *lock)
{
  while(__sync_lock_test_and_set(&lock->locked, 1) != 0)
    ;

  // Tell the C compiler and the processor to not move loads or stores
  // past this point, to ensure that the critical section's memory
  // references happen strictly after the lock is acquired.
  // On RISC-V, this emits a fence instruction.
  __sync_synchronize();
}

// Release the lock.
void
lock_release(struct lock_t *lock)
{
  __sync_synchronize();

  // Release the lock, equivalent to lk->locked = 0.
  // This code doesn't use a C assignment, since the C standard
  // implies that an assignment might be implemented with
  // multiple store instructions.
  // On RISC-V, sync_lock_release turns into an atomic swap:
  //   s1 = &lk->locked
  //   amoswap.w zero, zero, (s1)
  __sync_lock_release(&lock->locked);
}