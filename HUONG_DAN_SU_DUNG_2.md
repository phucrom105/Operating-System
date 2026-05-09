# HƯỚNG DẪN THỰC HIỆN PROJECT 2 — PAGE TABLE & FILE SYSTEM (xv6)

> **Môn:** Operating System  
> **Mục tiêu:** Đạt tối đa 8.0/8.0 điểm (Task 1: 2đ | Task 2: 3đ | Task 3: 3đ)

---

## MỤC LỤC

1. [Cấu trúc thư mục tham khảo](#1-cấu-trúc-thư-mục-tham-khảo)
2. [Chuẩn bị môi trường](#2-chuẩn-bị-môi-trường)
3. [Task 1 — Speed up system calls (2.0đ)](#3-task-1--speed-up-system-calls-20đ)
4. [Task 2 — Print a page table (3.0đ)](#4-task-2--print-a-page-table-30đ)
5. [Task 3 — Large file (3.0đ)](#5-task-3--large-file-30đ)
6. [Build & Chạy](#6-build--chạy)
7. [Test & Kiểm tra điểm](#7-test--kiểm-tra-điểm)
8. [Checklist nộp bài](#8-checklist-nộp-bài)

---

## 1. Cấu trúc thư mục tham khảo

Nếu bạn tổ chức code riêng ngoài xv6, cấu trúc gợi ý:

```
OSPj2/
├── src/
│   ├── task1_proc_changes.c          ← Đoạn code thêm vào kernel/proc.c
│   ├── task2_vmprint.c               ← Hàm vmprint() cho kernel/vm.c
│   ├── task2_defs_exec_changes.c     ← Prototype + lời gọi cho defs.h và exec.c
│   ├── task3_fs_h_changes.c          ← Hằng số thay đổi trong kernel/fs.h
│   ├── task3_bmap.c                  ← Hàm bmap() hoàn chỉnh cho kernel/fs.c
│   └── task3_itrunc.c                ← Hàm itrunc() hoàn chỉnh cho kernel/fs.c
├── apply_all_tasks.py                ← Script tự động áp dụng TẤT CẢ thay đổi
└── patches/
    └── task1_usyscall.patch          ← Git patch mẫu
```

---

## 2. Chuẩn bị môi trường

Task 1 & 2 dùng nhánh `pgtbl`. Task 3 dùng nhánh `fs` — làm **riêng biệt**.

```bash
# Task 1 + 2
git fetch
git checkout pgtbl
make clean

# Task 3 (làm xong Task 1 & 2 mới chuyển)
git fetch
git checkout fs
make clean
```

### Cách nhanh: Script tự động

Nếu có file `apply_all_tasks.py`:

```bash
cd ~/xv6-labs-2024
python3 apply_all_tasks.py
make clean
make qemu
```

Script tự động xử lý toàn bộ: thêm USYSCALL, vmprint(), doubly-indirect, tạo file test, sửa Makefile.

---

## 3. Task 1 — Speed up system calls (2.0đ)

### Mô tả
Map một trang bộ nhớ read-only tại địa chỉ ảo `USYSCALL` để user space đọc `pid` **mà không cần trap vào kernel**.

---

### Bước 1: `kernel/memlayout.h` — Khai báo USYSCALL

```bash
nano kernel/memlayout.h
# Tìm dòng "#define TRAPFRAME (TRAMPOLINE - PGSIZE)"
# Thêm NGAY SAU dòng đó:
```

```c
// USYSCALL: shared read-only page for fast syscalls (Task 1)
#define USYSCALL (TRAPFRAME - PGSIZE)

struct usyscall {
  int pid;  // Process ID
};
```

> **Tiêu chí:** Định nghĩa đúng địa chỉ `USYSCALL` và struct `usyscall` chứa `pid`.

---

### Bước 2: `kernel/proc.h` — Thêm field vào `struct proc`

```bash
nano kernel/proc.h
# Tìm dòng "struct trapframe *trapframe;"
# Thêm NGAY SAU dòng đó:
```

```c
  struct usyscall *usyscall;   // shared page for fast syscalls (Task 1)
```

> ⚠️ **Lỗi phổ biến:** Thiếu field này → lỗi biên dịch, trừ 0.3đ.

---

### Bước 3: `kernel/proc.c` — 4 vị trí cần sửa

```bash
nano kernel/proc.c
```

#### 3a. Trong `allocproc()` — Cấp phát trang usyscall

Tìm đoạn xử lý `trapframe`, thêm **ngay sau**:

```c
  // Allocate a usyscall page (Task 1: speed up system calls).
  if((p->usyscall = (struct usyscall *)kalloc()) == 0){
    freeproc(p);
    release(&p->lock);
    return 0;
  }
  // Ghi pid vào shared page để user space đọc trực tiếp.
  p->usyscall->pid = p->pid;
```

> ⚠️ **Lỗi phổ biến:** Không ghi `pid` vào `usyscall->pid` → trừ 0.5đ.

---

#### 3b. Trong `proc_pagetable()` — Map trang vào page table

Tìm `return pagetable;`, thêm **ngay trước**:

```c
  // Map the USYSCALL page below TRAPFRAME (Task 1).
  // Read-only cho user: PTE_R | PTE_U — KHÔNG có PTE_W.
  if(mappages(pagetable, USYSCALL, PGSIZE,
              (uint64)(p->usyscall), PTE_R | PTE_U) < 0){
    uvmunmap(pagetable, TRAMPOLINE, 1, 0);
    uvmunmap(pagetable, TRAPFRAME, 1, 0);
    uvmfree(pagetable, 0);
    return 0;
  }
```

> ⚠️ **Lưu ý quan trọng:** Trong error path này, **KHÔNG** gọi `uvmunmap(pagetable, USYSCALL, 1, 0)` vì USYSCALL chưa được map thành công tại thời điểm này.

> ⚠️ **Lỗi phổ biến:** Thêm `PTE_W` cho phép user ghi → trừ 0.5đ.

---

#### 3c. Trong `freeproc()` — Giải phóng trang

Tìm dòng `p->trapframe = 0;`, thêm **ngay sau**:

```c
  // Free the usyscall page (Task 1).
  if(p->usyscall)
    kfree((void*)p->usyscall);
  p->usyscall = 0;
```

> ⚠️ **Lỗi phổ biến:** Không giải phóng → memory leak, trừ 0.3đ.

---

#### 3d. Trong `proc_freepagetable()` — Unmap khỏi page table

Tìm dòng `uvmunmap(pagetable, TRAPFRAME, 1, 0);`, thêm **ngay sau**:

```c
  // Unmap the USYSCALL page (Task 1).
  uvmunmap(pagetable, USYSCALL, 1, 0);
```

> ⚠️ **Lỗi phổ biến:** Không unmap → kernel panic khi process kết thúc, trừ 0.2đ.

---

### Bước 4: `user/pgtbltest.c` — Tạo file test

Tạo mới file `user/pgtbltest.c`:

```c
#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/memlayout.h"
#include "user/user.h"

int
ugetpid(void)
{
  // Đọc trực tiếp từ shared page — KHÔNG dùng syscall.
  struct usyscall *u = (struct usyscall *)USYSCALL;
  return u->pid;
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
```

> ⚠️ **Lỗi phổ biến:** `ugetpid()` dùng syscall thay vì đọc shared memory → trừ 0.2đ.

---

### Bước 5: `Makefile` — Đăng ký chương trình

Tìm biến `UPROGS`, thêm vào:

```makefile
	$U/_pgtbltest\
```

---

## 4. Task 2 — Print a page table (3.0đ)

### Mô tả
Viết hàm `vmprint()` in cấu trúc page table 3 cấp theo format chuẩn; tự động gọi cho process đầu tiên (`pid == 1`) khi exec.

> Task 1 & 2 cùng nhánh `pgtbl` — **không cần chuyển nhánh** nếu vừa làm xong Task 1.

---

### Bước 1: `kernel/vm.c` — Thêm 2 hàm vào cuối file

```bash
nano kernel/vm.c
# Cuộn xuống cuối file, thêm 2 hàm sau:
```

```c
// Helper: đệ quy in một cấp của page table.
// level: 2 = L2 (root), 1 = L1, 0 = L0 (leaf)
static void
vmprintlevel(pagetable_t pagetable, int level)
{
  for(int i = 0; i < 512; i++){
    pte_t pte = pagetable[i];
    if(pte & PTE_V){
      // Số cặp ".." tương ứng với độ sâu trong cây
      int depth = 3 - level;
      for(int d = 0; d < depth; d++){
        printf("..");
        if(d < depth - 1)
          printf(" ");
      }
      uint64 pa = PTE2PA(pte);
      // Cast (void*) cho %p để tránh compiler warning
      printf("%d: pte %p pa %p\n", i, (void*)pte, (void*)pa);
      // Chỉ đệ quy nếu là non-leaf (không có bit R/W/X).
      if(level > 0 && (pte & (PTE_R|PTE_W|PTE_X)) == 0){
        vmprintlevel((pagetable_t)pa, level - 1);
      }
    }
  }
}

// In page table bắt đầu từ root pagetable.
void
vmprint(pagetable_t pagetable)
{
  printf("page table %p\n", (void*)pagetable);
  vmprintlevel(pagetable, 2);
}
```

> ⚠️ **Lỗi phổ biến:**
> - Không kiểm tra `PTE_V` → in entry không hợp lệ, trừ 0.5đ
> - Đệ quy vào leaf page (có bit R/W/X) → crash, trừ 0.5đ
> - Sai số cặp `..` theo level → trừ 0.5đ
> - Không cast `(void*)` cho `%p` → compiler warning

---

### Bước 2: `kernel/defs.h` — Thêm prototype

```bash
nano kernel/defs.h
# Tìm section "// vm.c", thêm vào:
```

```c
void            vmprint(pagetable_t);   // Task 2: print page table
```

> ⚠️ **Lỗi phổ biến:** Thiếu prototype → lỗi biên dịch, trừ 0.3đ.

---

### Bước 3: `kernel/exec.c` — Gọi vmprint cho pid == 1

```bash
nano kernel/exec.c
# Tìm "return argc;" ở cuối hàm exec()
# Thêm NGAY TRƯỚC dòng return:
```

```c
  // Task 2: In page table cho process đầu tiên (pid == 1) khi exec.
  if(p->pid == 1)
    vmprint(p->pagetable);
```

> ⚠️ **Lỗi phổ biến:** Gọi `vmprint()` cho mọi process → trừ 0.2đ. Đặt sau `return argc` → không bao giờ được gọi.

---

### Output mẫu khi boot xv6

Output xuất hiện **tự động trước dòng `init: starting sh`** — không cần chạy lệnh thêm:

```
page table 0x0000000087f6b000
..0: pte 0x0000000021fd9c01 pa 0x0000000087f67000
.. ..0: pte 0x0000000021fd9801 pa 0x0000000087f66000
.. .. ..0: pte 0x0000000021fda01b pa 0x0000000087f68000
.. .. ..1: pte 0x0000000021fd9417 pa 0x0000000087f65000
.. .. ..2: pte 0x0000000021fd9007 pa 0x0000000087f64000
.. .. ..3: pte 0x0000000021fd8c17 pa 0x0000000087f63000
..255: pte 0x0000000021fda801 pa 0x0000000087f6a000
.. ..511: pte 0x0000000021fda401 pa 0x0000000087f69000
.. .. ..509: pte 0x0000000021fdcc13 pa 0x0000000087f73000
.. .. ..510: pte 0x0000000021fdd007 pa 0x0000000087f74000
.. .. ..511: pte 0x0000000021ffb417 pa 0x0000000087fed000
```

**Giải thích format:**

| Prefix | Level | Ý nghĩa |
|--------|-------|---------|
| `..X:` | Level 2 (root) | 1 cặp `..` |
| `.. ..X:` | Level 1 | 2 cặp `..` cách nhau bằng space |
| `.. .. ..X:` | Level 0 (leaf) | 3 cặp `..` |

> Giá trị địa chỉ sẽ khác theo từng lần chạy, nhưng **số entry và cấu trúc cây phải giống mẫu**.

---

## 5. Task 3 — Large file (3.0đ)

### Chuyển sang nhánh `fs`

```bash
git fetch
git checkout fs
make clean   # BẮT BUỘC để xóa fs.img cũ
```

> ⚠️ Luôn `make clean` khi chuyển nhánh và khi thay đổi `NDIRECT` — nếu không xóa `fs.img` cũ, kernel sẽ panic vì mismatch.

### Mô tả
Bổ sung **doubly-indirect block** vào inode để hỗ trợ file tới **65803 blocks** (thay vì 268 blocks mặc định):
- 11 direct blocks + 1 singly-indirect (256) + 1 doubly-indirect (256×256 = 65536) = **65803**

---

### Bước 1: `kernel/fs.h` — Sửa hằng số và struct dinode

```bash
nano kernel/fs.h
```

**Tìm và thay thế phần hằng số:**

```c
// CŨ:
#define NDIRECT 12
#define NINDIRECT (BSIZE / sizeof(uint))
#define MAXFILE (NDIRECT + NINDIRECT)

// MỚI:
#define NDIRECT 11                            // Giảm từ 12 xuống 11
#define NINDIRECT (BSIZE / sizeof(uint))      // = 256
#define NDBLINDIRECT (NINDIRECT * NINDIRECT)  // = 65536
#define MAXFILE (NDIRECT + NINDIRECT + NDBLINDIRECT)  // = 65803
```

**Tìm trong `struct dinode`, thay thế:**

```c
// CŨ:
uint addrs[NDIRECT+1];

// MỚI:
uint addrs[NDIRECT+2];   // 11 direct + 1 indirect + 1 doubly-indirect
```

> ⚠️ **Lỗi phổ biến:** Chỉ sửa `fs.h` mà không sửa `file.h` → struct mismatch, trừ 0.5đ.

---

### Bước 2: `kernel/file.h` — Sửa struct inode

```bash
nano kernel/file.h
```

**Tìm trong `struct inode`, thay thế:**

```c
// CŨ:
uint addrs[NDIRECT+1];

// MỚI:
uint addrs[NDIRECT+2];   // Phải khớp với struct dinode trong fs.h
```

---

### Bước 3: `kernel/fs.c` — Thay thế hàm `bmap()`

```bash
nano kernel/fs.c
# Tìm hàm bmap(), thay thế TOÀN BỘ bằng:
```

```c
static uint
bmap(struct inode *ip, uint bn)
{
  uint addr, *a;
  struct buf *bp;

  // Direct blocks (0 → NDIRECT-1)
  if(bn < NDIRECT){
    if((addr = ip->addrs[bn]) == 0)
      ip->addrs[bn] = addr = balloc(ip->dev);
    return addr;
  }
  bn -= NDIRECT;

  // Singly-indirect block
  if(bn < NINDIRECT){
    if((addr = ip->addrs[NDIRECT]) == 0)
      ip->addrs[NDIRECT] = addr = balloc(ip->dev);
    bp = bread(ip->dev, addr);
    a = (uint*)bp->data;
    if((addr = a[bn]) == 0){
      a[bn] = addr = balloc(ip->dev);
      log_write(bp);
    }
    brelse(bp);
    return addr;
  }
  bn -= NINDIRECT;

  // Doubly-indirect block (Task 3)
  if(bn < NDBLINDIRECT){
    // Cấp phát doubly-indirect block nếu chưa có
    if((addr = ip->addrs[NDIRECT+1]) == 0)
      ip->addrs[NDIRECT+1] = addr = balloc(ip->dev);

    // Đọc doubly-indirect block → lấy địa chỉ singly-indirect block
    bp = bread(ip->dev, addr);
    a = (uint*)bp->data;
    uint idx1 = bn / NINDIRECT;
    if((addr = a[idx1]) == 0){
      a[idx1] = addr = balloc(ip->dev);
      log_write(bp);
    }
    brelse(bp);

    // Đọc singly-indirect block → lấy địa chỉ data block
    bp = bread(ip->dev, addr);
    a = (uint*)bp->data;
    uint idx2 = bn % NINDIRECT;
    if((addr = a[idx2]) == 0){
      a[idx2] = addr = balloc(ip->dev);
      log_write(bp);
    }
    brelse(bp);
    return addr;
  }

  panic("bmap: out of range");
}
```

> ⚠️ **Lỗi phổ biến:**
> - Sai index: quên trừ `NINDIRECT` trước khi tính `bn / NINDIRECT` → trừ 0.5đ
> - Thiếu `brelse()` sau mỗi `bread()` → buffer cache đầy, kernel panic → trừ 0.3đ
> - Thiếu `log_write()` khi allocate block mới → trừ 0.2đ

---

### Bước 4: `kernel/fs.c` — Thay thế hàm `itrunc()`

```bash
# Vẫn trong kernel/fs.c, tìm hàm itrunc(), thay thế TOÀN BỘ bằng:
```

```c
void
itrunc(struct inode *ip)
{
  int i, j;
  struct buf *bp, *bp2;
  uint *a, *a2;

  // Giải phóng direct blocks
  for(i = 0; i < NDIRECT; i++){
    if(ip->addrs[i]){
      bfree(ip->dev, ip->addrs[i]);
      ip->addrs[i] = 0;
    }
  }

  // Giải phóng singly-indirect block
  if(ip->addrs[NDIRECT]){
    bp = bread(ip->dev, ip->addrs[NDIRECT]);
    a = (uint*)bp->data;
    for(j = 0; j < NINDIRECT; j++){
      if(a[j])
        bfree(ip->dev, a[j]);
    }
    brelse(bp);
    bfree(ip->dev, ip->addrs[NDIRECT]);
    ip->addrs[NDIRECT] = 0;
  }

  // Giải phóng doubly-indirect block (Task 3)
  // Thứ tự BẮT BUỘC: data blocks → singly-indirect blocks → doubly-indirect block
  if(ip->addrs[NDIRECT+1]){
    bp = bread(ip->dev, ip->addrs[NDIRECT+1]);
    a = (uint*)bp->data;
    for(i = 0; i < NINDIRECT; i++){
      if(a[i]){
        bp2 = bread(ip->dev, a[i]);
        a2 = (uint*)bp2->data;
        for(j = 0; j < NINDIRECT; j++){
          if(a2[j])
            bfree(ip->dev, a2[j]);
        }
        brelse(bp2);
        bfree(ip->dev, a[i]);
      }
    }
    brelse(bp);
    bfree(ip->dev, ip->addrs[NDIRECT+1]);
    ip->addrs[NDIRECT+1] = 0;
  }

  ip->size = 0;
  iupdate(ip);
}
```

> ⚠️ **Lỗi phổ biến:**
> - Giải phóng sai thứ tự (free doubly-indirect block trước data blocks) → mất dữ liệu, trừ 0.3đ
> - Thiếu `brelse()` cho `bp2` trong vòng lặp → trừ 0.2đ

---

## 6. Build & Chạy

### Task 1 & 2 (nhánh pgtbl)

```bash
make clean
make qemu
```

### Task 3 (nhánh fs)

```bash
make clean   # BẮT BUỘC — xóa fs.img cũ do NDIRECT thay đổi
make qemu
```

> Nếu file system vào trạng thái lỗi (crash giữa chừng), xóa thủ công rồi build lại:
> ```bash
> rm fs.img
> make qemu
> ```

### Thoát xv6

```
Ctrl + A, sau đó nhấn X
```

---

## 7. Test & Kiểm tra điểm

### Test Task 1 — ugetpid

```
$ pgtbltest
```

**Output mong đợi:**
```
ugetpid_test starting
ugetpid_test: OK
```

Nếu thấy `FAILED` → kiểm tra `ugetpid()` đang đọc từ `USYSCALL` chưa, và `pid` có được ghi trong `allocproc()` không.

---

### Test Task 2 — vmprint

Không cần chạy lệnh. Khi boot, output tự xuất hiện trước `init: starting sh`.

Kiểm tra:
- Dòng đầu: `page table 0x...`
- Số cặp `..` đúng theo level (1 / 2 / 3)
- Đúng số entry theo mẫu (0 và 255 ở L2; 0 và 511 ở L1; 0–3, 509–511 ở L0)

---

### Test Task 3 — bigfile

```
$ bigfile
```

**Output mong đợi** (mất khoảng 1.5–3 phút):
```
..................................................................................
wrote 65803 blocks
bigfile: done; ok
```

Nếu thấy `wrote 268 blocks` → chưa apply đúng thay đổi NDIRECT hoặc chưa `make clean`.

---

### Test Task 3 — usertests

```
$ usertests
```

**Output mong đợi (cuối):**
```
ALL TESTS PASSED
```

---

### Test tổng hợp — không memory leak

```
$ usertests -q
```

**Output mong đợi:**
```
ALL TESTS PASSED
```

---

## 8. Checklist nộp bài

### Checklist kỹ thuật

#### Task 1 (2.0đ)
- [ ] `USYSCALL` và `struct usyscall` trong `memlayout.h`
- [ ] `struct usyscall *usyscall` trong `struct proc` (`proc.h`)
- [ ] `kalloc()` + ghi `pid` trong `allocproc()` (`proc.c`)
- [ ] `mappages()` với `PTE_R | PTE_U` (không có `PTE_W`) trong `proc_pagetable()`
- [ ] `kfree()` trong `freeproc()`
- [ ] `uvmunmap()` trong `proc_freepagetable()`
- [ ] `ugetpid()` đọc từ `USYSCALL`, không dùng syscall (`pgtbltest.c`)
- [ ] `$U/_pgtbltest` được thêm vào `Makefile`
- [ ] Chạy `pgtbltest` → `ugetpid_test: OK`

#### Task 2 (3.0đ)
- [ ] Kiểm tra `PTE_V` trước khi in entry
- [ ] Dùng `PTE2PA()` để lấy physical address
- [ ] Chỉ đệ quy khi không có bit `R/W/X` (non-leaf)
- [ ] Số cặp `..` đúng theo level (1 / 2 / 3)
- [ ] Dòng đầu output: `page table 0x...`
- [ ] Dùng `%p` với cast `(void*)` cho pte và pa
- [ ] Prototype `vmprint(pagetable_t)` trong `defs.h`
- [ ] Gọi `vmprint()` chỉ khi `p->pid == 1`, đặt trước `return argc` (`exec.c`)
- [ ] Output khớp cấu trúc mẫu khi boot

#### Task 3 (3.0đ)
- [ ] `NDIRECT = 11`, `NDBLINDIRECT`, `MAXFILE = 65803` trong `fs.h`
- [ ] `addrs[NDIRECT+2]` trong cả `fs.h` (struct dinode) và `file.h` (struct inode)
- [ ] `bmap()` xử lý doubly-indirect với index đúng (`idx1 = bn/NINDIRECT`, `idx2 = bn%NINDIRECT`)
- [ ] `log_write()` khi allocate block mới trong `bmap()`
- [ ] `brelse()` đầy đủ sau mỗi `bread()` trong `bmap()`
- [ ] `itrunc()` giải phóng đúng thứ tự 3 tầng (data → singly → doubly)
- [ ] `brelse()` đầy đủ trong `itrunc()` (cả `bp` và `bp2`)
- [ ] `make clean` trước khi build để xóa `fs.img` cũ
- [ ] `bigfile` → `wrote 65803 blocks` và `done; ok`
- [ ] `usertests` → `ALL TESTS PASSED`

---

### Checklist file nộp

```
page_table/
├── <ID1>_<ID2>_<ID3>_Report.pdf
├── <ID1>_<ID2>_<ID3>.patch
└── xv6_page_table.zip             ← đã make clean trước khi zip

file_system/
├── <ID1>_<ID2>_<ID3>_Report.pdf
├── <ID1>_<ID2>_<ID3>.patch
└── xv6_file_system.zip
```

**Tạo patch và zip cho Task 1 & 2:**

```bash
git checkout pgtbl
git add kernel/memlayout.h kernel/proc.h kernel/proc.c \
        kernel/vm.c kernel/defs.h kernel/exec.c \
        user/pgtbltest.c Makefile
git diff HEAD > <ID1>_<ID2>_<ID3>.patch
make clean
cd ..
zip -r xv6_page_table.zip xv6-labs-2024/
```

**Tạo patch và zip cho Task 3:**

```bash
git checkout fs
git add kernel/fs.h kernel/file.h kernel/fs.c Makefile
git diff HEAD > <ID1>_<ID2>_<ID3>.patch
make clean
cd ..
zip -r xv6_file_system.zip xv6-labs-2024/
```

**Tên file nộp cuối cùng** (`ID1 < ID2 < ID3`):
```
<ID1>_<ID2>_<ID3>.zip
```

---

## Tóm tắt điểm số

| Task | Điểm | File thay đổi chính |
|------|------|---------------------|
| Task 1 — Speed up syscalls | 2.0đ | `memlayout.h`, `proc.h`, `proc.c`, `pgtbltest.c`, `Makefile` |
| Task 2 — Print page table | 3.0đ | `vm.c`, `defs.h`, `exec.c` |
| Task 3 — Large file | 3.0đ | `fs.h`, `file.h`, `fs.c` |
| **Tổng** | **8.0đ** | |
