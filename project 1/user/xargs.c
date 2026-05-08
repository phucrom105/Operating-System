#include "kernel/types.h"
#include "kernel/param.h"
#include "user/user.h"

// xv6 binaries live at /, exec() does not search PATH.
// "echo" -> "/echo", "/echo" -> "/echo"
char* resolve_path(char *name, char *buf, int bufsz) {
    if (name[0] == '/') {
        memmove(buf, name, strlen(name) + 1);
        return buf;
    }
    buf[0] = '/';
    memmove(buf + 1, name, strlen(name) + 1);
    return buf;
}

void run_command(char *argv[], int argc, char *line) {
    char *exec_argv[MAXARG];
    int exec_argc = 0;
    char pathbuf[64];

    // Copy original args (skip "xargs" at argv[0])
    for (int i = 1; i < argc; i++) {
        exec_argv[exec_argc++] = argv[i];
    }

    // Each line from stdin becomes ONE argument appended to the command.
    // Do NOT tokenize by whitespace — this matches standard xargs behavior.
    if (line[0] != 0) {
        if (exec_argc >= MAXARG - 1) {
            fprintf(2, "xargs: too many arguments\n");
            exit(1);
        }
        exec_argv[exec_argc++] = line;
    }

    exec_argv[exec_argc] = 0;

    if (exec_argc == 0) return;

    if (fork() == 0) {
        char *cmd = resolve_path(exec_argv[0], pathbuf, sizeof(pathbuf));
        exec_argv[0] = cmd;
        exec(cmd, exec_argv);
        fprintf(2, "xargs: exec %s failed\n", cmd);
        exit(1);
    }
    wait(0);
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(2, "Usage: xargs <command> [args...]\n");
        exit(1);
    }

    char buf[512];
    // linebuf is a SEPARATE copy passed to run_command.
    // This prevents the main read-loop from overwriting buf
    // while the child process is still referencing the string.
    char linebuf[512];
    int idx = 0;
    char c;

    while (read(0, &c, 1) == 1) {
        if (c == '\n') {
            buf[idx] = 0;
            // Copy into separate buffer before calling run_command,
            // so the next read iteration cannot corrupt the string
            // that exec_argv[] points into.
            memmove(linebuf, buf, idx + 1);
            run_command(argv, argc, linebuf);
            idx = 0;
        } else {
            if (idx >= (int)(sizeof(buf) - 1)) {
                fprintf(2, "xargs: input line too long\n");
                exit(1);
            }
            buf[idx++] = c;
        }
    }

    // Handle last line without trailing '\n'
    if (idx > 0) {
        buf[idx] = 0;
        memmove(linebuf, buf, idx + 1);
        run_command(argv, argc, linebuf);
    }

    exit(0);
}