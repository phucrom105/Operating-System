#ifndef PROCINFO_H
#define PROCINFO_H

struct procinfo {
    int   pid;      // Process ID
    int   ppid;     // Parent process ID
    int   state;    // Process state
    uint64 sz;      // Memory size (bytes)
    char  name[16]; // Process name
};

#endif