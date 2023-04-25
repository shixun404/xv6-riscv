#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
// #include "proc.c"
#include "syscall.h"
#include "defs.h"

// Fetch the uint64 at addr from the current process.
int
fetchaddr(uint64 addr, uint64 *ip)
{
  struct proc *p = myproc();
  if(addr >= p->sz || addr+sizeof(uint64) > p->sz) // both tests needed, in case of overflow
    return -1;
  if(copyin(p->pagetable, (char *)ip, addr, sizeof(*ip)) != 0)
    return -1;
  return 0;
}

// Fetch the nul-terminated string at addr from the current process.
// Returns length of string, not including nul, or -1 for error.
int
fetchstr(uint64 addr, char *buf, int max)
{
  struct proc *p = myproc();
  if(copyinstr(p->pagetable, buf, addr, max) < 0)
    return -1;
  return strlen(buf);
}

static uint64
argraw(int n)
{
  struct proc *p = myproc();
  switch (n) {
  case 0:
    return p->trapframe->a0;
  case 1:
    return p->trapframe->a1;
  case 2:
    return p->trapframe->a2;
  case 3:
    return p->trapframe->a3;
  case 4:
    return p->trapframe->a4;
  case 5:
    return p->trapframe->a5;
  }
  panic("argraw");
  return -1;
}

// Fetch the nth 32-bit system call argument.
void
argint(int n, int *ip)
{
  *ip = argraw(n);
}

// Retrieve an argument as a pointer.
// Doesn't check for legality, since
// copyin/copyout will do that.
void
argaddr(int n, uint64 *ip)
{
  *ip = argraw(n);
}

// Fetch the nth word-sized system call argument as a null-terminated string.
// Copies into buf, at most max.
// Returns string length if OK (including nul), -1 if error.
int
argstr(int n, char *buf, int max)
{
  uint64 addr;
  argaddr(n, &addr);
  return fetchstr(addr, buf, max);
}

// Prototypes for the functions that handle system calls.
extern struct proc proc[NPROC];
// extern char *states[6];
char *states[] = {
  [UNUSED]    "unused",
  [USED]      "used",
  [SLEEPING]  "sleep ",
  [RUNNABLE]  "runble",
  [RUNNING]   "run   ",
  [ZOMBIE]    "zombie"
  };
extern struct {
  struct spinlock lock;
  struct run *freelist;
} kmem;

struct run {
  struct run *next;
};
extern uint64 sys_fork(void);
extern uint64 sys_exit(void);
extern uint64 sys_wait(void);
extern uint64 sys_pipe(void);
extern uint64 sys_read(void);
extern uint64 sys_kill(void);
extern uint64 sys_exec(void);
extern uint64 sys_fstat(void);
extern uint64 sys_chdir(void);
extern uint64 sys_dup(void);
extern uint64 sys_getpid(void);
extern uint64 sys_sbrk(void);
extern uint64 sys_sleep(void);
extern uint64 sys_uptime(void);
extern uint64 sys_open(void);
extern uint64 sys_write(void);
extern uint64 sys_mknod(void);
extern uint64 sys_unlink(void);
extern uint64 sys_link(void);
extern uint64 sys_mkdir(void);
extern uint64 sys_close(void);
extern struct spinlock wait_lock;
uint64 sys_sysinfo(void);
uint64 sys_procinfo(void);

// An array mapping syscall numbers from syscall.h
// to the function that handles the system call.
static uint64 (*syscalls[])(void) = {
[SYS_fork]    sys_fork,
[SYS_exit]    sys_exit,
[SYS_wait]    sys_wait,
[SYS_pipe]    sys_pipe,
[SYS_read]    sys_read,
[SYS_kill]    sys_kill,
[SYS_exec]    sys_exec,
[SYS_fstat]   sys_fstat,
[SYS_chdir]   sys_chdir,
[SYS_dup]     sys_dup,
[SYS_getpid]  sys_getpid,
[SYS_sbrk]    sys_sbrk,
[SYS_sleep]   sys_sleep,
[SYS_uptime]  sys_uptime,
[SYS_open]    sys_open,
[SYS_write]   sys_write,
[SYS_mknod]   sys_mknod,
[SYS_unlink]  sys_unlink,
[SYS_link]    sys_link,
[SYS_mkdir]   sys_mkdir,
[SYS_close]   sys_close,
[SYS_sysinfo] sys_sysinfo,
[SYS_procinfo] sys_procinfo
};

int num_syscall = 0;

void
syscall(void)
{
  int num;
  struct proc *p = myproc();
  num = p->trapframe->a7;
  if(num > 0 && num < NELEM(syscalls) && syscalls[num]) {
    // Use num to lookup the system call function for num, call it,
    // and store its return value in p->trapframe->a0
    p->trapframe->a0 = syscalls[num]();
    num_syscall += 1;
    myproc()->num_syscall += 1;
  } else {
    printf("%d %s: unknown sys call %d\n",
            p->pid, p->name, num);
    // printf("\n*** %d ****\n", num_syscall);
    p->trapframe->a0 = -1;
  }
}


uint64
sys_sysinfo(void)
{
  int arg = 0;
  int ret_val = 0;
  argint(0, &arg);
  // printf("\n*** %d ****\n", arg);
  if(arg == 0){
    struct proc *p;
    int num_proc = 0;
    for(p = proc; p < &proc[NPROC]; p++){
      if(p->state == UNUSED)
        continue;
      if(p->state >= 0 && p->state < NELEM(states) && states[p->state])
        num_proc += 1;
    }
    ret_val = num_proc;
  } 
  else if(arg == 1){
    ret_val = num_syscall;
  }
  else if(arg == 2){
    struct run *r = kmem.freelist;
    int num_free_page = 0;
    acquire(&kmem.lock);
    while(r){
      r = r->next;
      num_free_page += 1;
    }
    release(&kmem.lock);
    ret_val = num_free_page;
  }
  else{
    ret_val = -1;
  }
  return ret_val;
}

uint64
sys_procinfo(void)
{
  uint64 dest;
  argaddr(0, &dest);
  struct {
    int ppid;
    int syscall_count;
    int page_usage;
  }pinfo;
  struct proc *p = myproc();
  acquire(&wait_lock);
  pinfo.ppid = p->parent->pid;
  release(&wait_lock);
  pinfo.syscall_count = p->num_syscall;
  pinfo.page_usage = (int)((p->sz+4095) / 4096);
  if(copyout(p->pagetable, dest, (char*)&pinfo, sizeof(pinfo))<0)
  return -1;
  return 0;
}
