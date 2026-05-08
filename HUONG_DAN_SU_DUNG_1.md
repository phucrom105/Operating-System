# Hướng dẫn áp dụng mã nguồn — xv6 Project 1

## Yêu cầu môi trường

- Ubuntu/Linux
- `git`, `gcc`, `make`, `qemu-system-riscv64`
- Đã clone repo xv6-riscv và đang ở nhánh `syscall`

```bash
git fetch
git checkout syscall
make clean
```

---

## Phần 1 — User programs

### 1.1 user/xargs.c

Thay toàn bộ nội dung file `user/xargs.c` bằng đoạn sau:

```c
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
```

### 1.2 user/tree.c

Tạo file `user/tree.c` với nội dung sau:

```c
#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fs.h"

void tree(char *path, int depth) {
    int fd;
    struct stat st;
    struct dirent de;
    char buf[512];
    int len;

    if ((fd = open(path, 0)) < 0) {
        fprintf(2, "tree: cannot open %s\n", path);
        return;
    }
    if (fstat(fd, &st) < 0) {
        fprintf(2, "tree: cannot stat %s\n", path);
        close(fd);
        return;
    }
    if (st.type != T_DIR) {
        close(fd);
        return;
    }

    while (read(fd, &de, sizeof(de)) == sizeof(de)) {
        if (de.inum == 0) continue;
        if (strcmp(de.name, ".") == 0 || strcmp(de.name, "..") == 0) continue;

        // Print indentation (2 spaces per depth level)
        for (int i = 0; i < depth; i++) printf("  ");

        // Build full path: path + "/" + name
        len = strlen(path);
        memset(buf, 0, sizeof(buf));
        memmove(buf, path, len);
        if (len > 0 && buf[len - 1] != '/') {
            buf[len] = '/';
            len++;
        }
        memmove(buf + len, de.name, strlen(de.name));

        // Stat the entry
        struct stat st2;
        if (stat(buf, &st2) < 0) {
            fprintf(2, "tree: cannot stat %s\n", buf);
            // Print name anyway and continue
            printf("%s\n", de.name);
            continue;
        }

        if (st2.type == T_DIR) {
            printf("%s/\n", de.name);
            tree(buf, depth + 1);
        } else {
            printf("%s\n", de.name);
        }
    }
    close(fd);
}

int main(int argc, char *argv[]) {
    char *path = (argc < 2) ? "." : argv[1];

    // Check if path can be opened; exit on failure for root path
    int fd = open(path, 0);
    if (fd < 0) {
        fprintf(2, "tree: cannot open %s\n", path);
        exit(1);
    }
    close(fd);

    printf("%s/\n", path);
    tree(path, 1);
    exit(0);
}
```

### 1.3 user/trace.c

Tạo file `user/trace.c` với nội dung sau:

```c
#include "kernel/types.h"
#include "user/user.h"

int main(int argc, char *argv[]) {
    if (argc < 3) {
        fprintf(2, "Usage: trace mask command [args]\n");
        exit(1);
    }
    if (trace(atoi(argv[1])) < 0) {
        fprintf(2, "trace: trace failed\n");
        exit(1);
    }
    exec(argv[2], argv + 2);
    fprintf(2, "trace: exec %s failed\n", argv[2]);
    exit(0);
}
```

### 1.4 user/proctest.c

Tạo file `user/proctest.c` với nội dung sau:

```c
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
```

> **Lưu ý:** `proctest.c` in `State` dạng chuỗi (ví dụ: `running`) thay vì số nguyên — đáp ứng tiêu chí "output đúng format".

---

## Phần 2 — Kernel: System call trace

### 2.1 kernel/proc.h — Thêm field tracemask

Tìm `struct proc {` và thêm dòng sau vào **cuối struct**, trước dấu `}`:

```c
int tracemask;       // bitmask syscall cần trace
```

### 2.2 kernel/syscall.h — Đăng ký số syscall

Thêm vào cuối file (kiểm tra số cuối cùng hiện tại, dùng số tiếp theo):

```c
#define SYS_trace    22
#define SYS_procinfo 23
```

### 2.3 user/user.h — Thêm struct procinfo và prototypes

Thêm **trước** dòng `// system calls`:

```c
struct procinfo {
    int    pid;
    int    ppid;
    int    state;
    uint64 sz;
    char   name[16];
};
```

Thêm vào danh sách system call prototypes:

```c
int trace(int);
int procinfo(int, struct procinfo*);
```

### 2.4 user/usys.pl — Thêm entry stubs

Thêm vào cuối file:

```perl
entry("trace");
entry("procinfo");
```

### 2.5 kernel/sysproc.c — Thêm sys_trace() và sys_procinfo()

Thêm vào cuối file (sau các hàm có sẵn):

```c
uint64
sys_trace(void)
{
    int mask;
    argint(0, &mask);
    myproc()->tracemask = mask;
    return 0;
}
```

```c
uint64
sys_procinfo(void)
{
    int pid;
    uint64 uinfo;
    struct procinfo info;
    struct proc *p;
    extern struct proc proc[];

    argint(0, &pid);
    argaddr(1, &uinfo);

    int found = 0;
    for (p = proc; p < &proc[NPROC]; p++) {
        acquire(&p->lock);
        if (p->pid == pid) {
            info.pid   = p->pid;
            info.ppid  = (p->parent) ? p->parent->pid : 0;
            info.state = p->state;
            info.sz    = p->sz;
            strncpy(info.name, p->name, 16);
            release(&p->lock);
            found = 1;
            break;
        }
        release(&p->lock);
    }

    if (!found) return -1;

    if (copyout(myproc()->pagetable, uinfo,
                (char*)&info, sizeof(info)) < 0)
        return -1;

    return 0;
}
```

Thêm include ở đầu file `kernel/sysproc.c` nếu chưa có:

```c
#include "procinfo.h"
```

### 2.6 kernel/procinfo.h — Tạo header file mới

Tạo file `kernel/procinfo.h`:

```c
#ifndef PROCINFO_H
#define PROCINFO_H

struct procinfo {
    int    pid;
    int    ppid;
    int    state;
    uint64 sz;
    char   name[16];
};

#endif
```

### 2.7 kernel/proc.c — Copy tracemask trong fork() và khởi tạo trong allocproc()

Trong hàm `allocproc()`, sau dòng khởi tạo các field khác, thêm:

```c
p->tracemask = 0;
```

Trong hàm `fork()`, tìm đoạn copy properties từ parent sang child (gần dòng `np->sz = p->sz`), thêm:

```c
np->tracemask = p->tracemask;
```

### 2.8 kernel/syscall.c — Đăng ký syscall + thêm tên + sửa hàm syscall()

**Bước a** — Thêm extern declarations cùng khu vực với các extern khác:

```c
extern uint64 sys_trace(void);
extern uint64 sys_procinfo(void);
```

**Bước b** — Thêm vào mảng `syscalls[]`:

```c
[SYS_trace]    sys_trace,
[SYS_procinfo] sys_procinfo,
```

**Bước c** — Thêm mảng tên syscall (đặt trước hàm `syscall()`):

```c
static char *syscall_names[] = {
  "",          // 0 — không dùng
  "fork",      // 1
  "exit",      // 2
  "wait",      // 3
  "pipe",      // 4
  "read",      // 5
  "kill",      // 6
  "exec",      // 7
  "fstat",     // 8
  "chdir",     // 9
  "dup",       // 10
  "getpid",    // 11
  "sbrk",      // 12
  "sleep",     // 13
  "uptime",    // 14
  "open",      // 15
  "write",     // 16
  "mknod",     // 17
  "unlink",    // 18
  "link",      // 19
  "mkdir",     // 20
  "close",     // 21
  "trace",     // 22
  "procinfo",  // 23
};
```

**Bước d** — Sửa hàm `syscall()` để in trace output:

```c
void
syscall(void)
{
  int num;
  struct proc *p = myproc();

  num = p->trapframe->a7;
  if(num > 0 && num < NELEM(syscalls) && syscalls[num]) {
    p->trapframe->a0 = syscalls[num]();

    if (p->tracemask & (1 << num)) {
      printf("%d: syscall %s -> %d\n",
             p->pid,
             syscall_names[num],
             (int)p->trapframe->a0);
    }
  } else {
    printf("%d %s: unknown sys call %d\n",
            p->pid, p->name, num);
    p->trapframe->a0 = -1;
  }
}
```

---

## Phần 3 — Makefile

Tìm dòng `UPROGS=` trong `Makefile` và thêm 4 dòng sau:

```makefile
$U/_xargs\
$U/_tree\
$U/_trace\
$U/_proctest\
```

---

## Phần 4 — Build

```bash
# Dọn sạch build cũ
make clean

# Build toàn bộ
make qemu
```

Nếu build thành công, terminal hiển thị:

```
xv6 kernel is booting
...
init: starting sh
$
```

Nếu có lỗi biên dịch, kiểm tra lại theo bảng sau:

| Lỗi | Nguyên nhân thường gặp |
|-----|------------------------|
| `trace undeclared` | Thiếu prototype trong `user/user.h` hoặc entry trong `usys.pl` |
| `procinfo undeclared` | Thiếu `struct procinfo` trong `user/user.h` |
| `sys_trace not found` | Thiếu extern hoặc thiếu entry trong mảng `syscalls[]` |
| `tracemask not a member` | Chưa thêm field vào `struct proc` trong `proc.h` |
| `NPROC undeclared` | Thiếu `#include "param.h"` trong `sysproc.c` |

---

## Phần 5 — Test theo tiêu chí đánh giá

### Setup môi trường test (chạy 1 lần sau khi vào xv6 shell)

```sh
mkdir a
echo > a/b
mkdir a/aa
echo > a/aa/b
echo > a/aa/c
mkdir a/ab
echo > a/ab/d
echo hello world > testfile
```

---

### Tiêu chí 1 — xargs: truyền đối số và fork()+exec() đúng (1.0đ)

```sh
echo hello too | xargs echo bye
```
**Kỳ vọng:**
```
bye hello too
```

---

### Tiêu chí 2 — xargs: xử lý nhiều dòng riêng biệt (0.5đ)

```sh
(echo 1 ; echo 2) | xargs echo
```
**Kỳ vọng:**
```
1
2
```

```sh
(echo foo ; echo bar ; echo baz) | xargs echo prefix
```
**Kỳ vọng:**
```
prefix foo
prefix bar
prefix baz
```

---

### Tiêu chí 3 — xargs: kết hợp với lệnh khác (0.5đ)

```sh
echo testfile | xargs grep hello
```
**Kỳ vọng:**
```
hello world
```

---

### Tiêu chí 1 — tree: indentation + trailing / (1.0đ)

```sh
tree a
```
**Kỳ vọng:**
```
a/
  b
  aa/
    b
    c
  ab/
    d
```

Test nesting sâu:
```sh
mkdir a/aa/aaa
echo > a/aa/aaa/file
tree a
```
**Kỳ vọng:**
```
a/
  b
  aa/
    b
    c
    aaa/
      file
  ab/
    d
```

---

### Tiêu chí 2 — tree: đệ quy đúng, bỏ qua . và .. (0.5đ)

```sh
tree /
```
**Kỳ vọng:** Hiển thị cấu trúc `/` không bị loop vô hạn, kết thúc bình thường.

---

### Tiêu chí 3 — tree: default "." và error handling (0.5đ)

```sh
tree
```
**Kỳ vọng:** Dòng đầu là `./`, liệt kê thư mục hiện tại, không crash.

```sh
tree /notexist
```
**Kỳ vọng:**
```
tree: cannot open /notexist
```

---

### Tiêu chí 1 — trace: format output + bitwise AND (1.0đ)

```sh
trace 32 grep hello README
```
**Kỳ vọng** (PID có thể khác):
```
3: syscall read -> 1023
3: syscall read -> 966
3: syscall read -> 70
3: syscall read -> 0
```

```sh
trace 2147483647 grep hello README
```
**Kỳ vọng:** Xuất hiện các dòng `syscall trace`, `syscall exec`, `syscall open`, `syscall read`, `syscall close`.

Kiểm tra bitwise AND đúng (không false positive):
```sh
trace 2 grep hello README
```
**Kỳ vọng:** Không có dòng trace nào (grep không gọi fork).

---

### Tiêu chí 2 — trace: mask kế thừa sang child qua fork() (0.5đ)

```sh
trace 2 usertests forkforkfork
```
**Kỳ vọng:** Nhiều PID khác nhau đều xuất hiện trong output trace, chứng tỏ child processes kế thừa mask.

---

### Tiêu chí 3 — trace: không ảnh hưởng process khác (0.5đ)

```sh
grep hello README
```
**Kỳ vọng:** Không có dòng `syscall` nào — chạy grep bình thường sau khi đã chạy trace không làm lây mask.

---

### Tiêu chí 1 — procinfo: tìm đúng process và điền đủ fields (1.0đ)

```sh
proctest 1
```
**Kỳ vọng:**
```
Process : init
PID     : 1
PPID    : 0
State   : sleeping
Memory  : <số bytes>
```

```sh
proctest 2
```
**Kỳ vọng:**
```
Process : sh
PID     : 2
PPID    : 1
State   : sleeping
Memory  : <số bytes>
```

---

### Tiêu chí 2 — procinfo: copyout() + trả về -1 khi thất bại (0.5đ)

```sh
proctest 9999
```
**Kỳ vọng:**
```
procinfo failed for pid 9999
```

---

### Tiêu chí 3 — procinfo: compile được + output đúng format (0.5đ)

```sh
proctest
```
**Kỳ vọng:** In đủ 5 dòng `Process`, `PID`, `PPID`, `State`, `Memory`. State hiển thị dạng chuỗi (ví dụ: `running`), không phải số.

---

## Checklist trước khi nộp

```
[ ] make qemu không có lỗi biên dịch
[ ] echo hello too | xargs echo bye  →  "bye hello too"
[ ] (echo 1 ; echo 2) | xargs echo  →  2 dòng riêng "1" và "2"
[ ] tree a  →  đúng indentation 2 spaces, thư mục có "/"
[ ] tree  →  chạy được, dòng đầu "./"
[ ] tree /notexist  →  in lỗi, không crash
[ ] trace 32 grep hello README  →  format "PID: syscall read -> N"
[ ] trace 2147483647 grep hello README  →  có cả "trace" và "exec" trong output
[ ] grep hello README  →  không có dòng trace nào
[ ] trace 2 usertests forkforkfork  →  nhiều PID khác nhau được trace
[ ] proctest 1  →  PPID=0, name=init
[ ] proctest 2  →  PPID=1, name=sh
[ ] proctest 9999  →  "procinfo failed for pid 9999"
[ ] proctest  →  in đủ 5 trường, State là chuỗi
[ ] git diff > StudentID1_StudentID2_StudentID3.patch
[ ] make clean && zip -r StudentID1_StudentID2_StudentID3.zip xv6-riscv/
```