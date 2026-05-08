#include "kernel/types.h"
#include "user/user.h"

static char *states[] = {
    "unused", "used", "sleeping", "runnable", "running", "zombie"
};

int main(int argc, char *argv[]) {
    struct procinfo info;
    int pid;

    // Allow optional pid argument, default to current process
    if (argc >= 2) {
        pid = atoi(argv[1]);
    } else {
        pid = getpid();
    }

    if (procinfo(pid, &info) == 0) {
        char *state_str = (info.state >= 0 && info.state <= 5)
                          ? states[info.state] : "unknown";
        printf("Process : %s\n", info.name);
        printf("PID     : %d\n", info.pid);
        printf("PPID    : %d\n", info.ppid);
        printf("State   : %s\n", state_str);
        printf("Memory  : %d bytes\n", (int)info.sz);
    } else {
        printf("procinfo failed for pid %d\n", pid);
    }
    exit(0);
}