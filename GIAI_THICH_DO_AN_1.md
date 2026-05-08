# 📚 GIẢI THÍCH CHI TIẾT — xv6 Project 1

> Tài liệu này giải thích từng task, kiến thức cần nắm, và workflow chạy của code để chuẩn bị cho buổi vấn đáp.

---

## 🗂️ TỔNG QUAN DỰ ÁN

Đây là đồ án trên nền **xv6-riscv** — một hệ điều hành giáo dục dạy học OS từ MIT, chạy trên kiến trúc RISC-V. Dự án gồm **4 task**:

| Task | Loại | File |
|---|---|---|
| `xargs` | User program | `user/xargs.c` |
| `tree` | User program | `user/tree.c` |
| `trace` | System call mới | Kernel + user |
| `procinfo` | System call mới | Kernel + user |

---

## 🧠 NỀN TẢNG KIẾN THỨC BẮT BUỘC

Trước khi đi vào từng task, cần nắm rõ các khái niệm sau:

### 1. System Call là gì?
- **User space** (chương trình người dùng) không được phép truy cập trực tiếp phần cứng hay kernel data.
- Khi cần dịch vụ của kernel (đọc file, tạo process...), user program gọi **system call** — đây là "cổng ra vào" an toàn vào kernel.
- Cơ chế: user program đặt số syscall vào register `a7`, các tham số vào `a0`–`a5`, rồi thực thi lệnh `ecall` → CPU chuyển sang **supervisor mode** và nhảy vào kernel handler.

### 2. Process trong xv6
- Mỗi process có `struct proc` trong kernel lưu trữ: PID, trạng thái, page table, trapframe, tên, process cha (`parent`)...
- `myproc()` trả về con trỏ tới `struct proc` của process đang chạy.
- Các trạng thái: `UNUSED`, `USED`, `SLEEPING`, `RUNNABLE`, `RUNNING`, `ZOMBIE`.

### 3. fork() + exec()
- `fork()`: tạo process con — **copy toàn bộ** memory, file descriptors, và các field của process cha.
- `exec(path, argv)`: **thay thế** image của process hiện tại bằng chương trình mới từ file `path`.
- Pattern điển hình: `if (fork() == 0) { exec(...); }` — fork ra con, con thực thi lệnh mới, cha chờ.

### 4. File Descriptor & Pipe
- Stdin = fd 0, Stdout = fd 1, Stderr = fd 2.
- Pipe (`|` trong shell): stdout của lệnh trái được nối vào stdin của lệnh phải.

### 5. copyout() — Kernel trả dữ liệu về User Space
- Kernel không thể ghi thẳng vào pointer của user (khác address space).
- `copyout(pagetable, uaddr, src, len)`: copy `len` bytes từ địa chỉ kernel `src` sang địa chỉ user `uaddr`, thông qua page table của process.

---

## ✅ TASK 1 — `xargs`

### Mục đích
`xargs` đọc từng dòng từ **stdin**, rồi chạy một lệnh với mỗi dòng đó làm argument thêm vào.

**Ví dụ:**
```
echo hello too | xargs echo bye
→ bye hello too
```
Pipe chuyển `"hello too"` vào stdin của xargs. xargs chạy: `echo bye hello too`.

```
(echo 1; echo 2) | xargs echo
→ 1
→ 2
```
Mỗi dòng sinh ra một lệnh riêng: `echo 1`, `echo 2`.

---

### Kiến thức cần nắm
- `read(fd, buf, n)`: đọc tối đa `n` byte từ fd vào buf.
- `fork()` + `exec()`: tạo child process chạy lệnh.
- `wait(0)`: cha chờ con kết thúc trước khi tiếp tục.
- `memmove()`: copy vùng nhớ (an toàn khi overlap).
- Tại sao cần 2 buffer (`buf` và `linebuf`) — xem bên dưới.

---

### Workflow chi tiết

```
main()
│
├─ Đọc từng byte từ stdin (fd = 0) bằng read(0, &c, 1)
│
├─ Tích lũy byte vào buf[]
│
├─ Khi gặp '\n':
│   ├─ Null-terminate buf[idx] = 0
│   ├─ Copy sang linebuf (tách biệt!)
│   └─ Gọi run_command(argv, argc, linebuf)
│
└─ Sau khi hết stdin, xử lý dòng cuối không có '\n'


run_command(argv, argc, line)
│
├─ Tạo exec_argv[] = [cmd, original_args..., line_from_stdin, NULL]
│   Ví dụ: xargs echo bye  +  line="hello too"
│   → exec_argv = ["echo", "bye", "hello too", NULL]
│
├─ fork()
│   ├─ Child (fork()==0):
│   │   ├─ resolve_path: "echo" → "/echo" (xv6 không có PATH)
│   │   ├─ exec("/echo", exec_argv)
│   │   └─ Nếu exec fail → fprintf stderr + exit(1)
│   │
│   └─ Parent: wait(0) — chờ child hoàn thành
│
└─ Return (tiếp tục vòng lặp đọc stdin)
```

---

### Tại sao cần `linebuf` riêng? (Câu hỏi vấn đáp quan trọng!)

```c
char buf[512];
char linebuf[512];
```

- Sau `fork()`, **parent** tiếp tục chạy vòng lặp đọc stdin và ghi vào `buf`.
- `exec_argv[last]` trỏ vào `linebuf` — nếu dùng thẳng `buf`, parent có thể ghi đè lên string mà child đang dùng.
- Trong xv6, `fork()` copy memory → child có bản sao riêng — nhưng **trước khi fork**, pointer đang trỏ vào buffer của process. `memmove` sang `linebuf` đảm bảo dữ liệu không bị corrupt.

---

### resolve_path — Tại sao cần?

xv6 shell **không có biến PATH**. Tất cả binary nằm ở thư mục gốc `/`. `exec()` không tự tìm file → phải truyền đường dẫn tuyệt đối.

```c
"echo" → "/echo"
"/echo" → "/echo"  (đã có / rồi, giữ nguyên)
```

---

## ✅ TASK 2 — `tree`

### Mục đích
Hiển thị cấu trúc thư mục dạng cây có thụt lề, tương tự lệnh `tree` trên Linux.

```
a/
  b
  aa/
    b
    c
  ab/
    d
```

---

### Kiến thức cần nắm
- `open(path, 0)`: mở file/directory, trả về file descriptor.
- `fstat(fd, &st)`: lấy metadata của file (type, size, inode...).
- `struct stat`: có field `type` — `T_DIR` (thư mục), `T_FILE` (file thường).
- `struct dirent`: đại diện một entry trong directory, có `inum` (inode number) và `name`.
- `read(fd, &de, sizeof(de))`: đọc từng entry trong directory.
- **Đệ quy**: duyệt thư mục con bằng cách gọi lại chính mình.

---

### Workflow chi tiết

```
main()
│
├─ Nếu không có argument → path = "."
├─ open(path) → kiểm tra path có tồn tại không
├─ In "path/" (root của cây)
└─ Gọi tree(path, depth=1)


tree(path, depth)
│
├─ open(path) → lấy fd của directory
├─ fstat(fd, &st) → kiểm tra type == T_DIR (bỏ qua nếu là file)
│
├─ Vòng lặp: read(fd, &de, sizeof(de)) cho từng entry
│   ├─ Bỏ qua: de.inum == 0 (entry trống)
│   ├─ Bỏ qua: de.name == "." hoặc ".." (tránh loop vô hạn!)
│   │
│   ├─ In thụt lề: depth * 2 spaces
│   │
│   ├─ Xây dựng full path: path + "/" + de.name
│   │   Ví dụ: "a" + "/" + "aa" → "a/aa"
│   │
│   ├─ stat(full_path, &st2) → lấy type của entry
│   │
│   ├─ Nếu T_DIR:
│   │   ├─ In "name/"
│   │   └─ Gọi đệ quy tree(full_path, depth+1)
│   │
│   └─ Nếu file thường:
│       └─ In "name"
│
└─ close(fd)
```

---

### Tại sao phải bỏ qua "." và ".."? (Câu hỏi vấn đáp quan trọng!)

Mỗi directory trong hệ thống file đều có 2 entry đặc biệt:
- `.` → trỏ về chính thư mục đó
- `..` → trỏ về thư mục cha

Nếu không bỏ qua, hàm đệ quy sẽ loop vô hạn: `a/ → a/. → a/./. → ...`

---

### Tại sao cần cả `fstat` và `stat`?

- `fstat(fd, &st)`: dùng cho thư mục **gốc** đang mở — kiểm tra xem path ban đầu có phải directory không.
- `stat(buf, &st2)`: dùng cho từng **entry con** — cần biết entry đó là file hay directory để quyết định có đệ quy không và có thêm `/` vào tên không.

---

## ✅ TASK 3 — `trace` (System Call)

### Mục đích
Theo dõi các system call mà một process gọi. Chỉ in log những syscall có bit tương ứng được bật trong **bitmask**.

```
trace 32 grep hello README
→ 3: syscall read -> 1023
→ 3: syscall read -> 966
...
```

`32 = 1 << 5 = bit thứ 5` → theo dõi syscall số 5 là `read`.

---

### Kiến thức cần nắm
- **Bitmask**: dùng số nguyên để đại diện tập hợp. Bit thứ `n` bật = theo dõi syscall số `n`.
  - Kiểm tra: `mask & (1 << num)` — nếu khác 0 thì bật.
  - `2147483647 = 0x7FFFFFFF` → tất cả 31 bit thấp đều bật → theo dõi tất cả syscall.
- **Trap/Ecall flow**: khi user gọi syscall → CPU trap vào kernel → `syscall()` trong `kernel/syscall.c` được gọi.
- **Inheritance qua fork()**: child kế thừa `tracemask` từ parent.

---

### Workflow toàn bộ (từ user đến kernel và ngược lại)

```
[USER SPACE]
trace.c: main()
│
├─ Gọi trace(mask) — user syscall wrapper
│   (được sinh từ usys.pl → usys.S)
│   ├─ Đặt mask vào a0
│   ├─ Đặt SYS_trace (22) vào a7
│   └─ ecall ──────────────────────────────────────────────┐
│                                                           │
[KERNEL SPACE] ◄──────────────────────────────────────────┘
│                                                           
kernel/syscall.c: syscall()
│   ├─ Đọc num = p->trapframe->a7  (= 22)
│   ├─ Gọi syscalls[22]()  = sys_trace()
│   │
│   kernel/sysproc.c: sys_trace()
│   │   ├─ argint(0, &mask) → lấy tham số từ a0
│   │   └─ myproc()->tracemask = mask  ← LƯU VÀO STRUCT PROC
│   │
│   └─ Return 0 về trapframe->a0
│
[USER SPACE] tiếp tục
│
trace.c: exec(argv[2], argv+2)
    → Chạy lệnh cần trace (vd: grep hello README)
    → Mọi syscall của grep đều đi qua syscall() trong kernel
    → syscall() kiểm tra: p->tracemask & (1 << num)
    → Nếu bit bật → printf("%d: syscall %s -> %d\n", ...)
```

---

### Các file phải sửa và lý do

| File | Thay đổi | Lý do |
|---|---|---|
| `kernel/proc.h` | Thêm `int tracemask` vào `struct proc` | Mỗi process cần lưu mask riêng |
| `kernel/syscall.h` | Thêm `#define SYS_trace 22` | Đăng ký số hiệu syscall |
| `kernel/sysproc.c` | Thêm `sys_trace()` | Hàm xử lý syscall trong kernel |
| `kernel/syscall.c` | Đăng ký extern, thêm vào mảng, thêm logic trace | Dispatcher cần biết hàm nào ứng với số nào |
| `kernel/proc.c` | Khởi tạo `tracemask=0` trong `allocproc()`, copy trong `fork()` | Init đúng + kế thừa |
| `user/user.h` | Thêm prototype `int trace(int)` | User code cần biết signature |
| `user/usys.pl` | Thêm `entry("trace")` | Sinh ra assembly stub `ecall` |
| `user/trace.c` | Chương trình user dùng trace | Frontend cho người dùng |

---

### Tại sao tracemask phải kế thừa qua fork()? (Câu hỏi vấn đáp)

```c
// kernel/proc.c - hàm fork()
np->tracemask = p->tracemask;
```

Khi bạn chạy:
```
trace 2 usertests forkforkfork
```
- `trace` set mask cho process cha.
- `usertests forkforkfork` gọi fork() nhiều lần.
- Nếu không copy, các child process sẽ có `tracemask = 0` → không trace được syscall của chúng.
- Kết quả mong muốn: nhiều PID khác nhau đều xuất hiện trong log → chứng tỏ kế thừa đúng.

---

### Flow đăng ký syscall mới (phải thuộc lòng!)

```
1. kernel/syscall.h  : #define SYS_xxx  N       ← số hiệu
2. kernel/sysproc.c  : uint64 sys_xxx(void){...} ← implementation
3. kernel/syscall.c  : 
    - extern uint64 sys_xxx(void);               ← khai báo
    - [SYS_xxx] sys_xxx,                         ← đăng ký vào mảng
4. user/user.h       : int xxx(args);            ← prototype cho user
5. user/usys.pl      : entry("xxx");             ← sinh ecall stub
```

---

## ✅ TASK 4 — `procinfo` (System Call)

### Mục đích
Cho phép user program lấy thông tin về một process bất kỳ qua PID: tên, PID, PPID, trạng thái, kích thước memory.

```
proctest 1
→ Process : init
→ PID     : 1
→ PPID    : 0
→ State   : sleeping
→ Memory  : 12345 bytes
```

---

### Kiến thức cần nắm
- `struct proc` trong kernel: lưu toàn bộ thông tin process.
- `proc[]`: mảng toàn cục trong kernel, chứa tất cả process slots (tối đa `NPROC`).
- **Spinlock** (`acquire`/`release`): kernel data (như `proc[]`) được bảo vệ bằng lock — phải acquire trước khi đọc, release sau.
- `copyout()`: cách duy nhất để kernel trả dữ liệu (struct) về cho user space.
- `argint()`: lấy tham số số nguyên từ trapframe.
- `argaddr()`: lấy tham số địa chỉ (pointer) từ trapframe.

---

### Workflow chi tiết

```
[USER SPACE]
proctest.c: main()
│
├─ Khai báo: struct procinfo info;  ← buffer ở user space
├─ Gọi procinfo(pid, &info)
│   (usys.pl → ecall với SYS_procinfo = 23)
│   ├─ a0 = pid
│   ├─ a1 = địa chỉ &info (user pointer)
│   └─ ecall ──────────────────────────────────────────────┐
│                                                           │
[KERNEL SPACE] ◄──────────────────────────────────────────┘
│
kernel/sysproc.c: sys_procinfo()
│
├─ argint(0, &pid)      → lấy pid từ a0
├─ argaddr(1, &uinfo)   → lấy địa chỉ user buffer từ a1
│
├─ struct procinfo info; ← buffer TẠM trong kernel
│
├─ Duyệt mảng proc[] từ proc[0] đến proc[NPROC-1]:
│   ├─ acquire(&p->lock)          ← LOCK trước khi đọc
│   ├─ So sánh p->pid == pid
│   ├─ Nếu tìm thấy:
│   │   ├─ Copy các field: pid, ppid, state, sz, name
│   │   ├─ release(&p->lock)      ← UNLOCK
│   │   └─ found = 1; break
│   └─ release(&p->lock)          ← UNLOCK dù không tìm thấy
│
├─ Nếu !found → return -1
│
├─ copyout(myproc()->pagetable, uinfo, &info, sizeof(info))
│   └─ Copy struct từ kernel buffer → user buffer
│      Dùng page table của calling process để dịch địa chỉ
│
└─ return 0 (thành công)
│
[USER SPACE] tiếp tục
│
proctest.c:
├─ Nếu procinfo() == 0 → in các field
└─ Nếu == -1 → "procinfo failed for pid XXXX"
```

---

### Tại sao phải dùng copyout()? (Câu hỏi vấn đáp quan trọng!)

Kernel và user program chạy trong **address space khác nhau**:
- Kernel có page table riêng.
- Pointer `&info` trong user space là địa chỉ **ảo** của user — kernel không thể ghi thẳng vào đó.
- `copyout()` dùng page table của process đó để **dịch địa chỉ ảo → vật lý** rồi ghi dữ liệu.

Nếu không dùng `copyout()` mà ghi thẳng → segfault hoặc corrupt dữ liệu.

---

### Tại sao phải acquire/release lock?

`proc[]` là tài nguyên dùng chung:
- Scheduler có thể đang đọc/sửa `proc[i]` cùng lúc.
- Nếu không lock → **race condition**: đọc được dữ liệu không nhất quán (ví dụ: pid đúng nhưng state đã thay đổi giữa chừng).
- Pattern: lock → đọc → unlock → không giữ lock khi không cần → tránh deadlock.

---

### struct procinfo ở đâu?

Cần định nghĩa ở **hai nơi**:

```
kernel/procinfo.h  →  dùng trong kernel code (sysproc.c)
user/user.h        →  dùng trong user code (proctest.c, trace.c)
```

Hai bên **phải đồng nhất** (cùng field, cùng thứ tự, cùng type) — nếu khác nhau, `copyout` sẽ copy đúng bytes nhưng user sẽ đọc sai field!

---

## 🔄 BIG PICTURE — Luồng chạy System Call từ đầu đến cuối

```
User gọi trace(32)
    ↓
usys.S (sinh từ usys.pl):
    li a7, 22      ; SYS_trace
    li a0, 32      ; mask
    ecall          ; trap vào kernel
    ↓
kernel/trap.c: usertrap()
    → gọi syscall()
    ↓
kernel/syscall.c: syscall()
    num = p->trapframe->a7  (= 22)
    syscalls[22]()  →  sys_trace()
    ↓
kernel/sysproc.c: sys_trace()
    argint(0, &mask)  →  mask = 32
    myproc()->tracemask = 32
    return 0
    ↓
kernel/syscall.c: syscall() tiếp tục
    p->trapframe->a0 = 0   ; giá trị trả về
    ; kiểm tra tracemask & (1<<22) → không in (trace chính nó)
    ↓
Trở về user space
trace.c: exec(argv[2], argv+2)  →  chạy grep
    ↓
Mỗi syscall của grep đều lặp lại vòng trên,
nhưng syscall() sẽ kiểm tra:
    if (p->tracemask & (1 << num))
        printf("%d: syscall %s -> %d\n", ...)
```

---

## 📋 CHECKLIST CÂU HỎI VẤN ĐÁP

### Về xargs
- ❓ `xargs` đọc input từ đâu? → Stdin (fd = 0), từng byte một.
- ❓ Tại sao cần 2 buffer `buf` và `linebuf`? → Tránh parent ghi đè data mà child đang dùng sau fork.
- ❓ `resolve_path` làm gì? → Biến "echo" thành "/echo" vì xv6 không có PATH.
- ❓ Tại sao `wait(0)` trong parent? → Chờ child kết thúc trước khi đọc dòng tiếp theo, tránh zombie process.

- ❓ **Trong lệnh pipe `echo hello | xargs echo bye`, bên nào chạy trước — trái hay phải?**

  → **Bên trái (`echo hello`) chạy trước**, bên phải (`xargs`) chạy sau. Nhưng cần hiểu rõ hơn:

  Shell tạo ra **pipe** trước, sau đó `fork()` ra **hai process con độc lập** — một chạy lệnh trái, một chạy lệnh phải — và chúng chạy **song song**. Pipe là buffer ở giữa: lệnh trái ghi vào pipe, lệnh phải đọc từ pipe.

  Tuy nhiên **trên thực tế** bên trái kết thúc nhanh (chỉ echo một dòng) nên khi bên phải (`xargs`) bắt đầu `read()`, data đã có sẵn trong pipe. Bên phải chỉ thực sự chạy `exec` lệnh con (`echo bye hello`) sau khi đọc được `\n` — lúc đó nó mới gọi `fork()` + `exec()` bên trong `run_command()`.

  **Tóm tắt thứ tự logic:**
  ```
  Shell fork() ra 2 process:
    Process A: chạy "echo hello"  → ghi "hello\n" vào pipe
    Process B: chạy "xargs echo bye"
        → read() từ pipe lấy "hello\n"
        → fork() + exec("/echo", ["echo", "bye", "hello"])
        → in ra "bye hello"
  ```

  > **Điểm mấu chốt:** Bản thân `xargs` KHÔNG tự exec lệnh — nó **fork ra process con** rồi process con đó mới exec. `xargs` (parent) chỉ chờ `wait(0)`.

---

### Về tree
- ❓ Tại sao bỏ qua `.` và `..`? → Tránh đệ quy vô hạn (loop).
- ❓ Tại sao dùng cả `fstat` và `stat`? → `fstat` cho fd đang mở; `stat` cho path của entry con.
- ❓ Thụt lề được tạo như thế nào? → Vòng lặp in `depth * 2` dấu cách.

- ❓ **Dòng code nào kiểm tra một entry là directory hay file?**

  → Có **hai lớp kiểm tra**, ở hai vị trí khác nhau trong code:

  **Lớp 1** — Kiểm tra path gốc có phải directory không (dùng `fstat`):
  ```c
  // Trong tree(), sau khi open(path)
  if (fstat(fd, &st) < 0) { ... }
  if (st.type != T_DIR) {   // ← NẾU KHÔNG PHẢI DIR → bỏ qua, return
      close(fd);
      return;
  }
  ```

  **Lớp 2** — Kiểm tra từng entry con là dir hay file (dùng `stat`):
  ```c
  // Trong vòng lặp read directory entries
  if (stat(buf, &st2) < 0) { ... }

  if (st2.type == T_DIR) {      // ← LÀ THƯ MỤC
      printf("%s/\n", de.name); //   in tên có dấu "/"
      tree(buf, depth + 1);     //   đệ quy vào trong
  } else {                      // ← LÀ FILE THƯỜNG
      printf("%s\n", de.name);  //   chỉ in tên
  }
  ```

  > `st2.type` là field trong `struct stat`, có giá trị `T_DIR` (1) hoặc `T_FILE` (2) hoặc `T_DEVICE` (3), được định nghĩa trong `kernel/stat.h`.

---

### Về trace
- ❓ Bitmask hoạt động thế nào? → `mask & (1 << syscall_number)` ≠ 0 thì trace.
- ❓ Tại sao phải thêm `tracemask` vào `struct proc`? → Mỗi process có mask riêng, phải lưu per-process.
- ❓ Tại sao `fork()` phải copy `tracemask`? → Child cần kế thừa behavior của parent.
- ❓ Ai in dòng log trace? → Hàm `syscall()` trong `kernel/syscall.c`, sau khi thực thi syscall thực sự.
- ❓ `trace 2 grep hello README` không in gì — tại sao? → `2 = 1<<1 = SYS_fork`, grep không gọi fork.

- ❓ **Giải thích cách nạp số để trace đúng syscall muốn theo dõi?**

  → Mỗi syscall có một **số hiệu cố định** (định nghĩa trong `kernel/syscall.h`). Để trace syscall số `N`, cần bật **bit thứ N** trong mask bằng phép dịch bit `1 << N`.

  **Bảng tham khảo các syscall thường gặp:**
  ```
  SYS_fork   = 1   →  mask = 1 << 1  = 2
  SYS_exit   = 2   →  mask = 1 << 2  = 4
  SYS_wait   = 3   →  mask = 1 << 3  = 8
  SYS_read   = 5   →  mask = 1 << 5  = 32
  SYS_exec   = 7   →  mask = 1 << 7  = 128
  SYS_write  = 16  →  mask = 1 << 16 = 65536
  SYS_trace  = 22  →  mask = 1 << 22 = 4194304
  ```

  **Ví dụ thực tế:**
  ```
  trace 32 grep hello README
       ↑
       32 = 1 << 5 = theo dõi SYS_read (số 5)
  ```

  Muốn trace **nhiều syscall cùng lúc** → dùng bitwise OR:
  ```
  trace 160 grep hello README
       ↑
       160 = 32 | 128 = (1<<5) | (1<<7) = trace read VÀ exec
  ```

  Muốn trace **tất cả syscall** → bật tất cả bit:
  ```
  trace 2147483647 grep hello README
       ↑
       2147483647 = 0x7FFFFFFF = 31 bit đều bật → trace mọi syscall
  ```

  **Cách kernel kiểm tra tại runtime** (trong `kernel/syscall.c`):
  ```c
  if (p->tracemask & (1 << num)) {
      // num = số syscall đang được gọi (lấy từ a7)
      // Nếu bit thứ num trong mask đang BẬT → in log
      printf("%d: syscall %s -> %d\n", p->pid, syscall_names[num], retval);
  }
  ```

---

### Về procinfo
- ❓ Tại sao phải dùng `copyout()`? → Kernel không thể ghi thẳng vào user address space.
- ❓ Tại sao phải lock khi duyệt `proc[]`? → Tránh race condition với scheduler.
- ❓ `argaddr()` khác `argint()` thế nào? → `argaddr` lấy pointer (uint64), `argint` lấy số nguyên.
- ❓ `struct procinfo` định nghĩa ở mấy chỗ? → 2 chỗ: `kernel/procinfo.h` và `user/user.h`.
- ❓ PPID của `init` (pid=1) là bao nhiêu? → 0, vì init không có parent (`p->parent == NULL`).

- ❓ **`Memory: X bytes` in ra là gì, lấy từ đâu trong kernel?**

  → Giá trị đó là **`p->sz`** — field `sz` trong `struct proc`, đại diện cho **kích thước virtual address space của process** tính bằng byte, cụ thể là địa chỉ byte cao nhất trong vùng nhớ của process đó.

  **Nằm ở đâu?** — Được lưu trong `struct proc` tại `kernel/proc.h`:
  ```c
  struct proc {
      ...
      uint64 sz;    // ← ĐÂY — Size of process memory (bytes)
      ...
  };
  ```

  **Ý nghĩa thực tế:**
  - Khi process được tạo ra (qua `exec`), kernel cấp phát vùng nhớ cho code + data + stack.
  - `sz` = địa chỉ cao nhất đã dùng = tổng kích thước vùng nhớ user-space của process.
  - Khi process gọi `sbrk(n)` để xin thêm heap → `sz` tăng thêm `n`.
  - `sz` **không phải** RAM vật lý thực sự dùng — là kích thước **virtual memory** được map.

  **Trong `sys_procinfo()`**, giá trị này được copy vào struct trả về:
  ```c
  info.sz = p->sz;   // lấy từ kernel struct proc
  ```

  Rồi `copyout()` chuyển về user space, và `proctest.c` in ra:
  ```c
  printf("Memory  : %d bytes\n", (int)info.sz);
  ```

---

## 🛠️ LƯU Ý KHI BUILD & DEBUG

```bash
make clean        # Xóa build cũ
make qemu         # Build + chạy xv6 trong QEMU

# Thoát QEMU:
Ctrl+A, rồi X
```

### Lỗi thường gặp
| Lỗi | Nguyên nhân | Sửa |
|---|---|---|
| `trace undeclared` | Thiếu prototype trong `user/user.h` | Thêm `int trace(int);` |
| `sys_trace not found` | Thiếu extern hoặc entry mảng | Kiểm tra `syscall.c` |
| `tracemask not a member` | Chưa thêm field vào `struct proc` | Sửa `proc.h` |
| `NPROC undeclared` | Thiếu include | Thêm `#include "param.h"` |
| Output sai format | State in số thay vì chuỗi | Dùng mảng `states[]` để map |