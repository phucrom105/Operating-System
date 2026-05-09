#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/param.h"
#include "kernel/memlayout.h"
#include "kernel/riscv.h"
#include "user/user.h"

// ugetpid: read pid directly from the USYSCALL shared page
// without trapping into the kernel.
int
ugetpid(void)
{
  struct usyscall *u = (struct usyscall *)USYSCALL;
  return u->pid;
}

void
pgaccess_test()
{
  // This test is a placeholder; pgaccess syscall not required here.
  printf("pgaccess_test: skipped (not implemented)\n");
}

void
ugetpid_test()
{
  int i;
  printf("ugetpid_test starting\n");
  for (i = 0; i < 64; i++) {
    int ret = fork();
    if (ret != 0) {
      int status;
      int child = wait(&status);
      if (status != 0) {
        printf("ugetpid_test: child %d failed\n", child);
        exit(1);
      }
    } else {
      // In child: compare ugetpid() with getpid() syscall
      if (ugetpid() != getpid()) {
        printf("ugetpid_test FAILED: ugetpid()=%d getpid()=%d\n",
               ugetpid(), getpid());
        exit(1);
      }
      exit(0);
    }
  }
  printf("ugetpid_test: OK\n");
}

int
main(int argc, char *argv[])
{
  ugetpid_test();
  exit(0);
}