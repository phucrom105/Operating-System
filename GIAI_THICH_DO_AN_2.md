# Giải Thích Chi Tiết Project 2 — Page Table & File System (xv6)

> Tài liệu này giải thích từng task một cách sâu, rõ ràng, bám sát luồng hoạt động của code để chuẩn bị cho buổi vấn đáp.

---

## MỤC LỤC

1. [Kiến thức nền tảng cần nắm](#1-kiến-thức-nền-tảng-cần-nắm)
2. [Task 1 — Speed up system calls](#2-task-1--speed-up-system-calls)
3. [Task 2 — Print a page table](#3-task-2--print-a-page-table)
4. [Task 3 — Large file](#4-task-3--large-file)
5. [Câu hỏi vấn đáp thường gặp](#5-câu-hỏi-vấn-đáp-thường-gặp)

---

## 1. Kiến Thức Nền Tảng Cần Nắm

### 1.1 Virtual Memory và Page Table trong RISC-V (Sv39)

xv6 dùng kiến trúc RISC-V với scheme phân trang **Sv39** — mỗi virtual address (VA) có 39 bit có nghĩa.

```
Virtual Address (64-bit, chỉ dùng 39 bit thấp):
 63        39 38      30 29      21 20      12 11         0
 [  unused  ] [  VPN[2] ] [  VPN[1] ] [  VPN[0] ] [ offset ]
     25 bit       9 bit       9 bit       9 bit      12 bit
```

**Ba cấp page table (3-level page walk):**

```
CR3 / satp
    |
    v
[ Page Table L2 ] (512 entries x 8 bytes = 4KB = 1 page)
    |
    +--> PTE[VPN[2]] --> [ Page Table L1 ]
                              |
                              +--> PTE[VPN[1]] --> [ Page Table L0 ]
                                                        |
                                                        +--> PTE[VPN[0]] --> Physical Page
                                                                                  |
                                                                                  + offset --> Physical Address
```

**Cấu trúc một PTE (Page Table Entry) — 64-bit:**

```
 63      54 53    28 27    19 18    10 9  8  7  6  5  4  3  2  1  0
 [reserved] [ PPN[2]] [ PPN[1]] [ PPN[0]] [RSW][D][A][G][U][X][W][R][V]
```

Các bit quan trọng:
- **V** (Valid): PTE có hợp lệ không — phải = 1 mới dùng được
- **R** (Read): trang có thể đọc
- **W** (Write): trang có thể ghi
- **X** (Execute): trang có thể thực thi
- **U** (User): userspace có thể truy cập — nếu = 0 thì chỉ kernel mới vào được
- **PPN** (Physical Page Number): số trang vật lý

**Leaf PTE vs Non-leaf PTE:**
- **Non-leaf**: R=W=X=0 → PTE trỏ đến page table cấp thấp hơn
- **Leaf**: có ít nhất một trong R/W/X = 1 → PTE trỏ đến physical page thật

### 1.2 Layout bộ nhớ user process trong xv6

```
MAXVA (0x4000000000)
┌─────────────────────┐
│     TRAMPOLINE      │  ← TRAMPOLINE = MAXVA - PGSIZE
│   (code nhảy vào   │    Mapped cả kernel và user page table
│    kernel trap)     │    Quyền: PTE_R | PTE_X (KHÔNG có PTE_U)
├─────────────────────┤
│     TRAPFRAME       │  ← TRAPFRAME = TRAMPOLINE - PGSIZE
│  (lưu registers    │    Quyền: PTE_R | PTE_W (KHÔNG có PTE_U)
│   khi trap)         │
├─────────────────────┤
│     USYSCALL        │  ← USYSCALL = TRAPFRAME - PGSIZE  [Task 1]
│  (shared read-only  │    Quyền: PTE_R | PTE_U (chỉ đọc, user vào được)
│   page với kernel)  │
├─────────────────────┤
│                     │
│    (heap grows up)  │
│                     │
│    user stack       │
│    user data        │
│    user text        │
└─────────────────────┘
0x0
```

### 1.3 Vòng đời của một process trong xv6

```
allocproc()          freeproc()
    │                    ▲
    │ kalloc() trapframe │ kfree() trapframe
    │ kalloc() usyscall  │ kfree() usyscall   [Task 1]
    │                    │
    ▼                    │
proc_pagetable()    proc_freepagetable()
    │                    ▲
    │ map TRAMPOLINE     │ uvmunmap TRAMPOLINE
    │ map TRAPFRAME      │ uvmunmap TRAPFRAME
    │ map USYSCALL [T1]  │ uvmunmap USYSCALL  [Task 1]
    ▼                    │
  Process chạy ─────────┘ (khi exit/freeproc)
```

---

## 2. Task 1 — Speed up system calls

### 2.1 Vấn đề cần giải quyết

**System call thông thường** (ví dụ `getpid()`) phải:
1. User code gọi `getpid()`
2. CPU trap vào kernel (ecall instruction)
3. Kernel lưu toàn bộ registers vào trapframe
4. Kernel đọc `p->pid` rồi trả về
5. Kernel restore registers, trở về userspace

→ **Tốn kém**: mỗi lần trap là context switch từ user mode sang kernel mode, mất hàng trăm cycles.

**Giải pháp**: Chia sẻ một trang nhớ (shared page) giữa kernel và userspace. Kernel ghi `pid` vào đó, user đọc trực tiếp mà **không cần trap vào kernel**.

### 2.2 Kiến trúc giải pháp

```
KERNEL SPACE                          USER SPACE
─────────────────────────────────────────────────────────
                    Physical Page
                   ┌────────────┐
struct proc {      │            │
  ...              │  struct    │
  *usyscall ──────►│  usyscall  │◄──── (void*)USYSCALL
  ...              │  { pid; }  │      (địa chỉ virtual
}                  │            │       trong user VA)
                   └────────────┘

Kernel ghi: p->usyscall->pid = p->pid;
User đọc:   struct usyscall *u = (struct usyscall *)USYSCALL;
            return u->pid;   // KHÔNG cần syscall!
```

**Cùng một physical page, nhưng được map vào hai page table khác nhau:**
- Kernel page table: truy cập qua pointer `p->usyscall` (địa chỉ vật lý)
- User page table: truy cập qua địa chỉ virtual `USYSCALL`, quyền chỉ đọc (PTE_R | PTE_U)

### 2.3 Luồng hoạt động đầy đủ (Workflow)

#### Bước 1: Khi tạo process — `allocproc()` trong `proc.c`

```c
static struct proc* allocproc(void) {
    // ... tìm proc slot trống ...

    p->pid = allocpid();           // gán PID

    // Cấp phát trapframe (đã có sẵn)
    p->trapframe = (struct trapframe *)kalloc();

    // [Task 1] Cấp phát physical page cho usyscall
    p->usyscall = (struct usyscall *)kalloc();
    // kalloc() trả về địa chỉ vật lý của trang 4KB vừa cấp phát

    // [Task 1] Ghi PID vào shared page NGAY LÚC NÀY
    p->usyscall->pid = p->pid;
    // Kernel truy cập qua physical address (pointer p->usyscall)

    // Tạo page table
    p->pagetable = proc_pagetable(p);
    // ...
}
```

**Tại sao phải ghi pid ngay trong allocproc?**
Vì sau khi `proc_pagetable()` map trang vào user VA, user có thể đọc ngay. Nếu chưa ghi pid thì user đọc được giá trị rác.

#### Bước 2: Map vào page table — `proc_pagetable()` trong `proc.c`

```c
pagetable_t proc_pagetable(struct proc *p) {
    pagetable_t pagetable = uvmcreate();   // tạo page table rỗng

    // Map TRAMPOLINE: kernel trap code
    mappages(pagetable, TRAMPOLINE, PGSIZE, (uint64)trampoline, PTE_R | PTE_X);

    // Map TRAPFRAME: nơi lưu registers khi trap
    mappages(pagetable, TRAPFRAME, PGSIZE, (uint64)(p->trapframe), PTE_R | PTE_W);

    // [Task 1] Map USYSCALL: shared read-only page
    mappages(pagetable, USYSCALL, PGSIZE,
             (uint64)(p->usyscall),   // physical address của trang đã kalloc()
             PTE_R | PTE_U);          // CHỈ đọc, user được phép vào
    // Lưu ý: KHÔNG có PTE_W → user KHÔNG ghi được
    // Lưu ý: CÓ PTE_U → user ĐƯỢC đọc (khác với TRAPFRAME và TRAMPOLINE)

    return pagetable;
}
```

**Tại sao chỉ PTE_R | PTE_U, không có PTE_W?**
- Nếu có PTE_W, user có thể sửa `pid` → **security hole** nghiêm trọng
- Kernel là bên duy nhất ghi → chỉ kernel cần W, nhưng kernel truy cập qua physical address (không qua user page table)

#### Bước 3: User đọc pid — `ugetpid()` trong `user/pgtbltest.c`

```c
int ugetpid(void) {
    // Cast địa chỉ USYSCALL thành pointer đến struct usyscall
    struct usyscall *u = (struct usyscall *)USYSCALL;
    // Đọc trực tiếp từ bộ nhớ — KHÔNG có ecall, KHÔNG trap vào kernel
    return u->pid;
}
```

**Hardware làm gì khi user đọc địa chỉ USYSCALL:**
1. CPU thấy địa chỉ virtual `USYSCALL`
2. MMU tra user page table: tìm PTE tại địa chỉ đó
3. PTE có V=1, R=1, U=1 → hợp lệ, user được đọc
4. MMU dịch sang physical address → đọc giá trị `pid`
5. Trả về cho user code — **không có trap, không vào kernel**

#### Bước 4: Khi process kết thúc — `freeproc()` và `proc_freepagetable()`

```c
static void freeproc(struct proc *p) {
    if(p->trapframe) kfree((void*)p->trapframe);
    p->trapframe = 0;

    // [Task 1] Giải phóng physical page
    if(p->usyscall) kfree((void*)p->usyscall);
    p->usyscall = 0;
    // Nếu không kfree → MEMORY LEAK: trang 4KB bị mất, không ai dùng được

    if(p->pagetable)
        proc_freepagetable(p->pagetable, p->sz);
    // ...
}

void proc_freepagetable(pagetable_t pagetable, uint64 sz) {
    uvmunmap(pagetable, TRAMPOLINE, 1, 0);   // unmap, không free physical
    uvmunmap(pagetable, TRAPFRAME, 1, 0);    // unmap, không free physical
    // [Task 1] Unmap USYSCALL khỏi page table
    uvmunmap(pagetable, USYSCALL, 1, 0);     // unmap, không free physical
    // (physical page đã được kfree() trong freeproc rồi)
    uvmfree(pagetable, sz);                  // free user pages + page table pages
}
```

**Tại sao `uvmunmap(..., 0)` (do_free = 0)?**
Vì physical page đã được `kfree()` trong `freeproc()` rồi. Nếu `do_free = 1` thì sẽ `kfree()` lần nữa → **double free** → kernel panic.

**Điều gì xảy ra nếu không gọi `uvmunmap`?**
Khi `uvmfree()` gọi `freewalk()`, nó duyệt page table và gặp PTE của USYSCALL vẫn valid → `panic("freewalk: leaf")` → kernel crash.

### 2.4 Tóm tắt các điểm quan trọng cho vấn đáp

| Câu hỏi | Trả lời |
|---------|---------|
| Tại sao map PTE_R\|PTE_U, không có PTE_W? | Chỉ kernel ghi (qua physical addr), user chỉ đọc. Cho W là lỗ hổng bảo mật |
| Tại sao phải ghi pid trong allocproc, không phải chỗ khác? | Phải ghi trước khi user có thể đọc. Sau fork cần update lại vì pid thay đổi |
| Physical page được map vào mấy page table? | 2: kernel (qua pointer) và user page table (qua mappages) |
| Hàm nào không cần trap vào kernel nhờ Task 1? | getpid() — thông qua ugetpid() đọc USYSCALL |
| Syscall nào khác có thể tối ưu tương tự? | getuid, gettid, clock_gettime (VDSO trong Linux làm tương tự) |

---

## 3. Task 2 — Print a page table

### 3.1 Mục tiêu

Viết hàm `vmprint()` in toàn bộ cấu trúc page table 3 cấp của một process ra console, dùng để debug.

### 3.2 Format output yêu cầu

```
page table 0x0000000087f6b000         ← địa chỉ của root page table (L2)
..0: pte 0x0000000021fd9c01 pa 0x0000000087f67000    ← L2, entry 0
.. ..0: pte 0x0000000021fd9801 pa 0x0000000087f66000  ← L1, entry 0
.. .. ..0: pte 0x0000000021fda01b pa 0x0000000087f68000  ← L0 (leaf)
.. .. ..1: pte 0x0000000021fd9417 pa 0x0000000087f65000
.. .. ..2: pte 0x0000000021fd9007 pa 0x0000000087f64000
.. .. ..3: pte 0x0000000021fd8c17 pa 0x0000000087f63000
..255: pte 0x0000000021fda801 pa 0x0000000087f6a000   ← L2, entry 255
.. ..511: pte 0x0000000021fda401 pa 0x0000000087f69000 ← L1, entry 511
.. .. ..509: pte ...
.. .. ..510: pte ...
.. .. ..511: pte ...
```

**Quy tắc indent:**
- Level 2 (root): `..` (1 cặp)
- Level 1: `.. ..` (2 cặp, ngăn cách bởi space)
- Level 0 (leaf): `.. .. ..` (3 cặp)

### 3.3 Thuật toán đệ quy

```
vmprint(pagetable):
    in "page table <addr>"
    gọi vmprintlevel(pagetable, level=2)

vmprintlevel(pagetable, level):
    for i = 0 to 511:
        pte = pagetable[i]
        if pte & PTE_V == 0: bỏ qua (entry không hợp lệ)
        
        in indent (3-level) cặp ".."
        in "i: pte <pte_value> pa <physical_addr>"
        
        if level > 0 AND (pte & (PTE_R|PTE_W|PTE_X)) == 0:
            // Non-leaf: đệ quy xuống cấp thấp hơn
            pa = PTE2PA(pte)
            vmprintlevel((pagetable_t)pa, level - 1)
        // Nếu là leaf (có R/W/X): KHÔNG đệ quy
```

**Tại sao kiểm tra `(pte & (PTE_R|PTE_W|PTE_X)) == 0` để xác định non-leaf?**

Trong RISC-V Sv39:
- Non-leaf PTE: R=W=X=0, trỏ đến page table cấp thấp hơn
- Leaf PTE: ít nhất một trong R/W/X = 1, trỏ đến physical page thật

Nếu không kiểm tra và đệ quy vào leaf PTE → interpret physical page thông thường như page table → đọc rác → crash hoặc in ra vô nghĩa.

### 3.4 Giải thích các macro sử dụng

```c
// PX(level, va): lấy 9-bit index tại level cho virtual address va
#define PX(level, va) (((va) >> (PGSHIFT + 9*(level))) & 0x1FF)
// PGSHIFT = 12 (offset bits), 9*(level) dịch đến đúng VPN

// PTE2PA(pte): lấy physical address từ PTE
#define PTE2PA(pte) (((pte) >> 10) << PGSHIFT)
// Bit 10-53 là PPN, dịch trái 12 bit để ra physical address

// PA2PTE(pa): chuyển physical address thành PTE (phần PPN)
#define PA2PTE(pa) ((((uint64)pa) >> PGSHIFT) << 10)

// PTE_FLAGS(pte): lấy 10 bit flag thấp
#define PTE_FLAGS(pte) ((pte) & 0x3FF)
```

### 3.5 Code thực tế và giải thích

```c
// Trong kernel/vm.c

static void
vmprintlevel(pagetable_t pagetable, int level)
{
    for(int i = 0; i < 512; i++){
        pte_t pte = pagetable[i];

        // Bỏ qua PTE không hợp lệ
        if(!(pte & PTE_V))
            continue;

        // In indent: level 2 → depth=1, level 1 → depth=2, level 0 → depth=3
        int depth = 3 - level;
        for(int d = 0; d < depth; d++){
            printf("..");
            if(d < depth - 1)
                printf(" ");  // space giữa các cặp ".."
        }

        uint64 pa = PTE2PA(pte);
        printf("%d: pte %p pa %p\n", i, (void*)pte, (void*)pa);

        // Chỉ đệ quy nếu là non-leaf (không có R/W/X)
        if(level > 0 && (pte & (PTE_R|PTE_W|PTE_X)) == 0){
            vmprintlevel((pagetable_t)pa, level - 1);
        }
    }
}

void
vmprint(pagetable_t pagetable)
{
    printf("page table %p\n", (void*)pagetable);
    vmprintlevel(pagetable, 2);  // bắt đầu từ level 2 (root)
}
```

### 3.6 Vị trí gọi vmprint trong exec.c

```c
// Trong kernel/exec.c, cuối hàm kexec():

  // Commit to the user image.
  oldpagetable = p->pagetable;
  p->pagetable = pagetable;        // page table mới đã sẵn sàng
  p->sz = sz;
  p->trapframe->epc = elf.entry;
  p->trapframe->sp = sp;
  proc_freepagetable(oldpagetable, oldsz);

  // [Task 2] In page table cho process đầu tiên
  if(p->pid == 1)
    vmprint(p->pagetable);         // in TRƯỚC khi return

  return argc;  // ← phải ở trước dòng này
```

**Tại sao chỉ in khi `p->pid == 1`?**
- pid=1 là process `init` — process đầu tiên chạy trong xv6
- In tất cả process sẽ tạo ra quá nhiều output, gây rối
- Bài lab yêu cầu rõ ràng: chỉ in init process

**Tại sao in TRƯỚC `return argc`?**
- Sau `return argc`, process bắt đầu chạy code user → page table có thể thay đổi
- Muốn in snapshot tại thời điểm exec() vừa xong → phải in trước khi trả quyền cho user

### 3.7 Giải thích ý nghĩa các entry trong output

```
page table 0x0000000087f6b000    ← physical addr của root L2 table

..0: pte 0x0000000021fd9c01 pa 0x0000000087f67000
  → L2 entry 0: VPN[2]=0, trỏ đến L1 table tại 0x87f67000
  → PTE = 0x21fd9c01 → bits thấp = 01 = V=1, non-leaf (R=W=X=0)

.. ..0: pte 0x0000000021fd9801 pa 0x0000000087f66000
  → L1 entry 0: VPN[1]=0, trỏ đến L0 table tại 0x87f66000

.. .. ..0: pte 0x0000000021fda01b pa 0x0000000087f68000
  → L0 entry 0: leaf PTE → physical page chứa user text
  → PTE bits = 0x1b = 0001_1011 → V=1, R=1, W=0, X=1, U=1
  → Trang code user (read + execute, không write)

.. .. ..1: pte 0x0000000021fd9417 pa 0x0000000087f65000
  → 0x17 = 0001_0111 → V=1, R=1, W=1, X=0, U=1
  → Trang data user (read + write, không execute)

..255: pte 0x0000000021fda801 pa 0x0000000087f6a000
  → L2 entry 255: VPN[2]=255 → vùng địa chỉ cao (gần MAXVA)
  → Chứa TRAMPOLINE, TRAPFRAME, USYSCALL

.. .. ..511: pte 0x0000000021fdd007 pa 0x0000000087f74000
  → 0x07 = 0000_0111 → V=1, R=1, W=1, X=0, U=1
  → Có thể là TRAPFRAME (read+write, không execute, user... thực ra không có U)
  → 0x07 = V=1, R=1, W=1, X=0, U=0 → kernel only → đây là TRAPFRAME

.. .. ..509: pte 0x0000000021fdcc13 pa 0x0000000087f73000
  → 0x13 = 0001_0011 → V=1, R=1, W=0, X=0, U=1
  → Read-only, user accessible → đây là USYSCALL page [Task 1]
```

---

## 4. Task 3 — Large file

### 4.1 Cấu trúc inode trong xv6 gốc (trước khi sửa)

```
struct dinode / struct inode {
    ...
    uint addrs[NDIRECT + 1 + 1];   // NDIRECT=12, mảng 14 phần tử
    //         [0..11]  [12]  [13] (13 là unused trong bản gốc)
};

addrs[0..11]  → 12 direct blocks (trỏ thẳng đến data block)
addrs[12]     → 1 singly-indirect block
                 └→ block chứa 256 địa chỉ (NINDIRECT = BSIZE/4 = 256)
                      └→ mỗi địa chỉ trỏ đến 1 data block

MAXFILE = 12 + 256 = 268 blocks
```

### 4.2 Cấu trúc sau khi sửa (doubly-indirect)

```
addrs[0..10]  → 11 direct blocks         (giảm từ 12 xuống 11)
addrs[11]     → 1 singly-indirect block
                 └→ 256 địa chỉ → 256 data blocks
addrs[12]     → 1 doubly-indirect block   (MỚI THÊM)
                 └→ 256 singly-indirect blocks
                      └→ mỗi cái chứa 256 địa chỉ
                           └→ 256 * 256 = 65536 data blocks

MAXFILE = 11 + 256 + 256*256 = 11 + 256 + 65536 = 65803 blocks
```

**Sơ đồ cấu trúc:**

```
inode.addrs[]:
[0]  ──────────────────────────────────────────► data block 0
[1]  ──────────────────────────────────────────► data block 1
...
[10] ──────────────────────────────────────────► data block 10
[11] ─────► [singly-indirect block]
                 [0] ──────────────────────────► data block 11
                 [1] ──────────────────────────► data block 12
                 ...
                 [255]─────────────────────────► data block 266
[12] ─────► [doubly-indirect block]
                 [0] ──► [singly-indirect #0]
                              [0] ────────────► data block 267
                              [1] ────────────► data block 268
                              ...
                              [255]───────────► data block 522
                 [1] ──► [singly-indirect #1]
                              [0] ────────────► data block 523
                              ...
                 ...
                 [255]──► [singly-indirect #255]
                              [255]───────────► data block 65802
```

### 4.3 Hàm bmap() — tra cứu block theo logical block number

`bmap(ip, bn)` nhận **logical block number** `bn` (số thứ tự block trong file, bắt đầu từ 0) và trả về **disk block number** (số block thật trên đĩa).

```c
static uint
bmap(struct inode *ip, uint bn)
{
    uint addr, *a;
    struct buf *bp;

    // === VÙNG 1: Direct blocks (bn = 0..10) ===
    if(bn < NDIRECT){                    // NDIRECT = 11
        if((addr = ip->addrs[bn]) == 0){
            addr = balloc(ip->dev);      // cấp phát block mới trên đĩa
            if(addr == 0) return 0;
            ip->addrs[bn] = addr;
            iupdate(ip);                 // ghi inode ra đĩa (qua log)
        }
        return addr;
    }
    bn -= NDIRECT;                       // bn bây giờ là offset trong vùng indirect

    // === VÙNG 2: Singly-indirect (bn = 0..255) ===
    if(bn < NINDIRECT){                  // NINDIRECT = 256
        if((addr = ip->addrs[NDIRECT]) == 0){
            addr = balloc(ip->dev);      // cấp phát singly-indirect block
            if(addr == 0) return 0;
            ip->addrs[NDIRECT] = addr;
            iupdate(ip);
        }
        bp = bread(ip->dev, addr);       // đọc singly-indirect block vào buffer
        a = (uint*)bp->data;
        if((addr = a[bn]) == 0){
            addr = balloc(ip->dev);      // cấp phát data block
            if(addr){
                a[bn] = addr;
                log_write(bp);           // ghi sửa đổi vào log (crash safety)
            }
        }
        brelse(bp);                      // PHẢI brelse() mỗi bread()
        return addr;
    }
    bn -= NINDIRECT;                     // bn bây giờ là offset trong vùng doubly-indirect

    // === VÙNG 3: Doubly-indirect (bn = 0..65535) ===
    if(bn < NDBLINDIRECT){               // NDBLINDIRECT = 256*256 = 65536
        int idx1 = bn / NINDIRECT;       // index vào doubly-indirect block (0..255)
        int idx2 = bn % NINDIRECT;       // index vào singly-indirect block (0..255)

        // Bước 1: lấy/cấp phát doubly-indirect block
        if((addr = ip->addrs[NDIRECT+1]) == 0){
            addr = balloc(ip->dev);
            if(addr == 0) return 0;
            ip->addrs[NDIRECT+1] = addr;
            iupdate(ip);
        }

        // Bước 2: đọc doubly-indirect block, lấy singly-indirect block
        bp = bread(ip->dev, addr);
        a = (uint*)bp->data;
        if((addr = a[idx1]) == 0){
            addr = balloc(ip->dev);
            if(addr){
                a[idx1] = addr;
                log_write(bp);           // ghi sửa đổi doubly-indirect block
            }
        }
        brelse(bp);                      // PHẢI brelse() trước khi bread() tiếp

        if(addr == 0) return 0;

        // Bước 3: đọc singly-indirect block, lấy data block
        bp = bread(ip->dev, addr);
        a = (uint*)bp->data;
        if((addr = a[idx2]) == 0){
            addr = balloc(ip->dev);
            if(addr){
                a[idx2] = addr;
                log_write(bp);           // ghi sửa đổi singly-indirect block
            }
        }
        brelse(bp);

        return addr;
    }

    panic("bmap: out of range");
}
```

**Tại sao phải gọi `log_write()` sau khi sửa block?**
xv6 dùng **write-ahead logging** để đảm bảo crash safety. Mọi thay đổi metadata phải đi qua log trước khi commit ra đĩa. Nếu không gọi `log_write()`, khi crash dữ liệu có thể mất hoặc inconsistent.

**Tại sao phải `brelse()` mỗi `bread()`?**
Buffer cache có giới hạn số lượng buffer. Mỗi `bread()` giữ một buffer lock. Nếu không `brelse()`, buffer cache đầy → các thao tác I/O khác bị block → deadlock hoặc kernel panic.

### 4.4 Hàm itrunc() — giải phóng tất cả blocks

`itrunc()` giải phóng **toàn bộ blocks** của file theo thứ tự từ lá lên gốc (data trước, sau đó indirect blocks):

```c
void
itrunc(struct inode *ip)
{
    int i, j;
    struct buf *bp, *bp2;
    uint *a, *a2;

    // === Giải phóng direct blocks ===
    for(i = 0; i < NDIRECT; i++){
        if(ip->addrs[i]){
            bfree(ip->dev, ip->addrs[i]);   // trả block về free list
            ip->addrs[i] = 0;
        }
    }

    // === Giải phóng singly-indirect ===
    if(ip->addrs[NDIRECT]){
        bp = bread(ip->dev, ip->addrs[NDIRECT]);
        a = (uint*)bp->data;
        for(j = 0; j < NINDIRECT; j++){
            if(a[j]) bfree(ip->dev, a[j]);  // giải phóng data blocks
        }
        brelse(bp);
        bfree(ip->dev, ip->addrs[NDIRECT]); // giải phóng singly-indirect block
        ip->addrs[NDIRECT] = 0;
    }

    // === Giải phóng doubly-indirect ===
    if(ip->addrs[NDIRECT+1]){
        bp = bread(ip->dev, ip->addrs[NDIRECT+1]);  // đọc doubly-indirect block
        a = (uint*)bp->data;
        for(i = 0; i < NINDIRECT; i++){
            if(a[i]){
                bp2 = bread(ip->dev, a[i]);          // đọc singly-indirect block thứ i
                a2 = (uint*)bp2->data;
                for(j = 0; j < NINDIRECT; j++){
                    if(a2[j]) bfree(ip->dev, a2[j]); // giải phóng data blocks
                }
                brelse(bp2);                          // brelse singly-indirect
                bfree(ip->dev, a[i]);                 // giải phóng singly-indirect block
            }
        }
        brelse(bp);                                   // brelse doubly-indirect
        bfree(ip->dev, ip->addrs[NDIRECT+1]);         // giải phóng doubly-indirect block
        ip->addrs[NDIRECT+1] = 0;
    }

    ip->size = 0;
    iupdate(ip);
}
```

**Thứ tự giải phóng QUAN TRỌNG — luôn phải từ lá lên gốc:**
```
SAI (gây mất tham chiếu):        ĐÚNG:
bfree(doubly_indirect_block)      bfree(data_block[0..255])
bfree(singly_indirect_block)      bfree(data_block[256..511])
bfree(data_block)                 ...
                                  bfree(singly_indirect_block[0])
                                  bfree(singly_indirect_block[1])
                                  ...
                                  bfree(doubly_indirect_block)
```

Nếu free block cha trước block con, ta mất địa chỉ của các block con → **disk leak** (các block con không bao giờ được trả về free list).

### 4.5 Thay đổi hằng số và cấu trúc

**`kernel/fs.h`:**
```c
#define NDIRECT 11                                    // giảm từ 12 xuống 11
#define NINDIRECT (BSIZE / sizeof(uint))              // = 256
#define NDBLINDIRECT (NINDIRECT * NINDIRECT)          // = 65536 (mới thêm)
#define MAXFILE (NDIRECT + NINDIRECT + NDBLINDIRECT)  // = 65803

struct dinode {
    ...
    uint addrs[NDIRECT+2];   // 11 + 2 = 13 phần tử: [direct x11, single, double]
};
```

**`kernel/file.h`:**
```c
struct inode {
    ...
    uint addrs[NDIRECT+2];   // phải khớp với dinode trong fs.h
};
```

**Tại sao phải sửa cả hai file?**
- `struct dinode` (fs.h): cấu trúc **on-disk** — cách inode được lưu trên đĩa
- `struct inode` (file.h): cấu trúc **in-memory** — inode được load vào RAM khi dùng
- Hai struct phải có cùng kích thước `addrs[]`, nếu khác nhau → đọc/ghi sai offset → corruption

---

## 5. Câu Hỏi Vấn Đáp Thường Gặp

### Task 1

**Q: Tại sao gọi là "speed up system calls"? Nhanh hơn bao nhiêu?**
A: Vì user đọc trực tiếp từ memory, không cần `ecall` instruction, không cần context switch user→kernel→user. Tiết kiệm được hàng trăm đến hàng nghìn CPU cycles. Linux dùng cơ chế tương tự gọi là VDSO (Virtual Dynamic Shared Object).

**Q: Nếu process fork(), pid của child có được update trong usyscall page không?**
A: Có. Trong `allocproc()`, mỗi process (kể cả child của fork) đều được cấp phát usyscall page mới và ghi `p->usyscall->pid = p->pid`. Child có pid khác parent nên có page riêng với pid đúng.

**Q: Physical page của USYSCALL có bị copy khi fork() không?**
A: Không. USYSCALL nằm ở địa chỉ cao (gần TRAPFRAME), không nằm trong vùng `[0, p->sz)` mà `uvmcopy()` sao chép. Child được cấp phát page USYSCALL mới trong `allocproc()`.

**Q: Điều gì xảy ra nếu quên `uvmunmap(USYSCALL)` trong `proc_freepagetable()`?**
A: Khi `uvmfree()` gọi `freewalk()` để giải phóng page table, `freewalk()` thấy PTE của USYSCALL vẫn valid (V=1) nhưng không phải non-leaf (có R bit) → gọi `panic("freewalk: leaf")` → kernel crash.

### Task 2

**Q: Hàm `freewalk()` trong vm.c có tương tự `vmprint()` không?**
A: Có cùng ý tưởng duyệt đệ quy page table, nhưng mục đích khác: `freewalk()` giải phóng bộ nhớ, `vmprint()` chỉ in ra.

**Q: Làm sao phân biệt non-leaf và leaf PTE?**
A: Theo RISC-V spec: nếu PTE có R=W=X=0 (cả ba đều 0) thì là non-leaf (trỏ đến page table cấp thấp hơn). Nếu có ít nhất một trong ba = 1 thì là leaf (trỏ đến physical page thật).

**Q: Tại sao `%p` mà không dùng `%x` hay `%lx` để in PTE và PA?**
A: `%p` in đủ 64-bit hex với prefix `0x`, đảm bảo đúng format yêu cầu của bài. `%x` có thể in thiếu bit trên 32-bit.

### Task 3

**Q: Tại sao NDIRECT giảm từ 12 xuống 11?**
A: Inode có mảng `addrs[]` cố định 13 phần tử (không thể thay đổi kích thước on-disk inode). Cần dùng 1 slot cho singly-indirect (addrs[11]) và 1 slot cho doubly-indirect (addrs[12]), nên direct chỉ còn 11.

**Q: Tại sao phải `brelse()` trước khi gọi `bread()` tiếp theo trong bmap()?**
A: Buffer cache có số lượng buffer giới hạn (`NBUF`). `bread()` giữ buffer lock (pin buffer). Nếu không `brelse()` buffer đầu tiên trước khi `bread()` buffer thứ hai, có thể gây cạn kiệt buffer cache, dẫn đến deadlock.

**Q: `log_write()` và `bwrite()` khác nhau thế nào?**
A: `bwrite()` ghi thẳng ra đĩa (không an toàn nếu crash giữa chừng). `log_write()` ghi vào log trước, đến khi `end_op()`/`commit()` mới ghi ra đĩa thật — đảm bảo atomicity (all-or-nothing).

**Q: MAXFILE = 65803 tính như thế nào?**
A: 11 (direct) + 256 (singly-indirect, 1 block × 256 entries) + 256×256 (doubly-indirect, 256 blocks × 256 entries) = 11 + 256 + 65536 = **65803**.

---

*Tài liệu được tạo cho Project 2 — Operating Systems, xv6 RISC-V.*
