# fp10.generated.riscv Bug Report

## 1. Overview

`fp10.generated.riscv` is RISC-V (RV32IM) assembly generated from `fp10.cpp` by the compiler in `src/asm.cpp`. The program crashes with a SIGSEGV when run under QEMU (exit code 139). This document identifies all bugs in the generated code, their root causes in the compiler source, and their propagation effects.

## 2. Source Program Semantics

`fp10.cpp` computes a fixed-point iteration using recursive lambdas:

```cpp
int main() {
    int z = 3;
    auto f = [=](auto &self, int n, int (*p)(int)) -> int {
        if (!n)
            return p(z);
        else
            return self(self, n - 1, [=](int x) -> int { return p(x + n + z); });
    };
    int n = 5;
    return f(f, n, [](int x) -> int { return x; }); // identity
}
```

With `n=5` and `z=3`, the expected result is `33` (the sum `3 + 5 + 4 + 3 + 2 + 1 + 3 + 3 + 3 + 3 + 3` = 33 when fully expanded). The compiler uses a fixed-point encoding scheme where each closure occupies 9 int32 slots in a global table `v1`, and a global counter `v0` tracks the next available slot.

## 3. Root Cause: Register Cache Spill Bug

### 3.1 The Release-Without-Spill Problem

The core bug is in `src/asm.cpp`, in the interaction between `RegCache::release()` and `RegCache::get()`.

**`RegCache::release()`** (line 430-440): Frees a register without ever writing the cached value to the stack, even if the value is marked as dirty:

```cpp
void release(koopa_raw_value_t v) {
    auto it = v2r.find(v);
    if (it == v2r.end()) return;
    string r = it->second;
    r2v.erase(r);
    dirty.erase(v);    // Just drops the dirty flag — no _sw to stack!
    v2r.erase(it);
    pool.push_back(r);
}
```

**`load()`** (line 287-303): When a value needs to be reloaded from the stack, it calls `addr.query(value)` to get a stack offset. But if that offset was never written to (because `release()` didn't spill), it reads garbage:

```cpp
static void load(const string &rd, const koopa_raw_value_t &value) {
    // ...
    _lw(rd, "sp", addr.query(value));  // Reads uninitialized stack memory
}
```

**`visit(binary)`** (line 815-872): Prematurely calls `rc.release(i.lhs)` at line 820, even when the lhs value is still needed by later instructions:

```cpp
void visit(const koopa_raw_binary_t &i, const koopa_raw_value_t &value) {
    rc.ensure_free(2);
    string lhs = rc.get(i.lhs);
    string rhs = rc.get(i.rhs);
    rc.release(i.lhs);   // <-- BUG: releases without spilling
    // ... binary operation using lhs register ...
    rc.set(value, lhs);  // result now occupies the same register
}
```

### 3.2 Why LSRA Doesn't Prevent This

The Linear Scan Register Allocator (LSRA, lines 256-285) correctly identifies which values have overlapping live intervals. However, `visit(binary)` at line 820 `rc.release(i.lhs)` ignores the live interval information — it eagerly releases the register regardless of whether the value is still live.

### 3.3 The Missing Stack Backup for Load Results

In `visit(load)` (line 729-753), when a value is loaded (especially from a global), `rc.set()` marks it in a register as dirty, but the code never calls `addr.query(value)` to reserve a stack slot, and never issues a `_sw` to back up the loaded value:

```cpp
void visit(const koopa_raw_load_t &i, const koopa_raw_value_t &value) {
    auto p = global_table.find(i.src);
    string result_reg = rc.alloc_pool_reg();
    if (p != global_table.end()) {
        string addr_reg = rc.alloc_pool_reg();
        _la(addr_reg, p->second);
        _lw(result_reg, addr_reg, 0);
        rc.free_pool_reg(addr_reg);
    }
    rc.set(value, result_reg);  // Only in register, NOT on stack
    // Missing: _sw(result_reg, "sp", addr.query(value));
}
```

## 4. Specific Bug Manifestations in fp10.generated.riscv

### 4.1 Bug 1: `main` — Counter Read from Uninitialized Stack (`line 2039`)

**Generated code (lines 2035-2041):**

```asm
  la t5, v0
  lw t4, 0(t5)          # t4 = v0 (counter, correctly loaded)
  li t5, 1000000
  add t4, t4, t5         # t4 = counter + 1000000 (fid, correct)
  lw t5, 16(sp)          # *** BUG: 16(sp) never stored — reads garbage ***
  li t6, 9
  mul t5, t5, t6         # t5 = garbage * 9 (should be counter * 9)
```

**Corresponding Koopa IR:**

```
%_316 = load %_0          # Load counter
%_317 = add %_316, 1000000  # Compute fid
%_318 = mul %_316, 9      # Compute base index — should reuse %_316
```

**Why it happens:**

1. `%_316 = load %_0` → global load puts counter in register `t4`, marked dirty
2. `%_317 = add %_316, 1000000` → `rc.get(%_316)` finds `t4`, computes `t4 = t4 + 1000000`, then `rc.release(%_316)` discards `%_316` from cache **without spilling**
3. `%_318 = mul %_316, 9` → `rc.get(%_316)` fails (released), calls `load(t5, %_316)` → `addr.query(%_316)` returns offset 16 → `_lw(t5, sp, 16)` — **reads garbage**

**Consequence:** All subsequent uses of the counter in `main` (lines 2049, 2062, 2078, 2093, 2108, 2123, 2138, 2153, 2168) load from the same garbage-valued `16(sp)`, corrupting all v1 writes and reads.

### 4.2 Bug 2: `main` — Global Counter Corruption (`line 2172`)

```asm
  lw t4, 16(sp)          # t4 = garbage (from §4.1)
  li t3, 1
  add t4, t4, t3          # t4 = garbage + 1
  la t3, v0
  sw t4, 0(t3)            # *** v0 = garbage + 1 — CORRUPTS GLOBAL COUNTER ***
```

After this, the global `v0` holds an arbitrary value. All subsequent closures in recursive calls use this corrupted counter as their base index into `v1`.

### 4.3 Bug 3: `_2:else_0` — Counter Read from Uninitialized Stack (`line 285`)

**Generated code (lines 281-291):**

```asm
  la s8, v0
  lw t6, 0(s8)           # t6 = counter (correct)
  li s8, 1000000
  add t6, t6, s8          # t6 = fid (correct)
  lw s11, 32(sp)          # *** BUG: 32(sp) never stored — reads garbage ***
  li s8, 9
  mul s11, s11, s8        # s11 = garbage * 9 (should be counter * 9)
  la s8, v1
  add s11, s11, s11
  add s11, s11, s11
  add s8, s8, s11
  li s9, 1
  sw s9, 0(s8)            # v1[garbage_offset] = 1 — WRITES TO WRONG LOCATION
```

**Same Koopa IR pattern as §4.1:**

```
%_114 = load %_0          # Load counter
%_115 = add %_114, 1000000  # Compute fid
%_116 = mul %_114, 9      # %_114 already released — loads garbage
```

**Consequence:** All 9 slots of the closure are written at a garbage offset in `v1`, corrupting:
- Possibly v1 slots of other closures
- Possibly writing beyond v1 bounds (SIGSEGV)

### 4.4 Bug 4: `_2:else_0` — Global Counter Corruption (`line 413`)

```asm
  lw s9, 32(sp)          # s9 = garbage (from §4.3)
  li s10, 1
  add s9, s9, s10         # s9 = garbage + 1
  la s10, v0
  sw s9, 0(s10)           # *** v0 = garbage + 1 — SECOND CORRUPTION ***
```

After this, `v0` is doubly corrupted, making all subsequent recursive calls compute wrong `v1` indices.

### 4.5 Bug 5: `_2:then_0 → fp_env_1` — Function Pointer Read from Uninitialized Stack (`line 438`)

**In `then_0` (lines 261-270):**

```asm
then_0:
  lw t1, 4(sp)           # t1 = z (%_69)
  lw t2, 12(sp)          # t2 = p (%_70, the function pointer fid)
  li t3, 1000000
  slt t2, t2, t3         # Overwrites t2! p's value is lost
  seqz t2, t2
  sw t2, 24(sp)
  sw t1, 28(sp)
  bnez t2, long_branch5  # If fid >= 1000000, go to fp_env_1
  j fp_direct_1
```

**In `fp_env_1` (line 437-438):**

```asm
fp_env_1:
  lw t5, 164(sp)         # *** BUG: 164(sp) never stored — reads garbage ***
  li t1, 1000000
  sub t5, t5, t1          # t5 = garbage - 1000000 (wrong base index)
```

**Why it happens:**

1. `%_70 = load %_66` loads `p` from stack arg into `t2`, marked dirty in cache
2. `%_72 = ge %_70, 1000000` in `visit(binary)`:
   - `rc.get(%_70)` returns `t2`
   - After `slt` + `seqz`, `rc.release(%_70)` discards from cache
3. Later, `fp_env_1`'s `%_73 = sub %_70, 1000000`:
   - `rc.get(%_70)` fails → `load(t5, %_70)` → `addr.query(%_70)` returns 164 → `_lw(t5, sp, 164)` — **reads garbage**
4. The garbage `164(sp)` is used as the base closure index, causing wrong/out-of-bounds `v1` accesses in the dispatch logic.

## 5. Propagation and Crash Chain

The bugs cascade in the following order:

```
1. main creates first closure
   ├── [Bug 1] Writes closure at garbage v1 offset
   ├── [Bug 2] Corrupts v0 with garbage
   └── Calls _2(self=0, z=3, n=5, p=fid=1000000)
       │
       ▼
2. _2(n=5) → else_0 → creates second closure
   ├── [Bug 3] Reads garbage as counter (from corrupted v0 or uninit stack)
   ├── [Bug 3] Writes closure at garbage v1 offset
   ├── [Bug 4] Rewrites v0 with more garbage
   └── Calls _2(self=0, z=3, n=4, p=garbage_fid)
       │
       ▼
3. Recursion continues with corrupt fid pointers
   ├── nth call may try to read v1 at out-of-bounds index
   └── SIGSEGV (QEMU exit code 139)
```

## 6. Fix Recommendations

### 6.1 Primary Fix: Spill on Release

Modify `RegCache::release()` to spill dirty values before discarding:

```cpp
void release(koopa_raw_value_t v) {
    auto it = v2r.find(v);
    if (it == v2r.end()) return;
    string r = it->second;
    if (dirty.count(v)) {
        _sw(r, "sp", addr.query(v));  // Spill to stack first
    }
    dirty.erase(v);
    r2v.erase(r);
    v2r.erase(it);
    pool.push_back(r);
}
```

### 6.2 Secondary Fix: Stack Backup on Load

In `visit(load)`, after `rc.set()`, also store the loaded value to the stack:

```cpp
// After rc.set(value, result_reg) at line 750:
_sw(result_reg, "sp", addr.query(value));
```

### 6.3 Tertiary Fix: Live Range Aware Release

In `visit(binary)`, only release `i.lhs` if it has no further uses (check via LSRA `lsra_last_use` map):

```cpp
// Replace rc.release(i.lhs) with conditional release:
auto it = lsra_last_use.find(i.lhs);
int cur_idx = /* current instruction index */;
if (it == lsra_last_use.end() || it->second <= cur_idx)
    rc.release(i.lhs);
// Otherwise, keep it in the cache (it will be spilled later if evicted)
```

## 7. Verification

To verify the fix, rebuild the compiler and regenerate:

```bash
make BUILD_DIR=build
./build/compiler -riscv fp10.cpp /dev/null fp10.generated.riscv
# Then assemble, link, and run:
docker exec -w /root/compiler compiler bash -c '
clang -x assembler fp10.generated.riscv -c -o fp10.o \
  -target riscv32-unknown-linux-elf -march=rv32im -mabi=ilp32 &&
ld.lld fp10.o -L$CDE_LIBRARY_PATH/riscv32 -lsysy -o fp10 &&
qemu-riscv32-static fp10 && echo "PASS: exit code $?"
'
```

Expected output: `PASS: exit code 33`

### 6.4 Fourth Fix (Critical): Release Index Before In-Place Modification in getelemptr

In `visit(get_elem_ptr)` (line 814-815), the index register is multiplied by 4 **in place**:
```cpp
string idx_reg = rc.get(i.index);
_add(idx_reg, idx_reg, idx_reg);  // idx_reg *= 2 (in-place)
_add(idx_reg, idx_reg, idx_reg);  // idx_reg *= 4 (in-place)
_add(addr_reg, addr_reg, idx_reg);
```

This destroys the original value in the register. The RegCache still thinks the register holds `i.index`, but it now holds `i.index * 4`. Later, when `rc.release(i.index)` (or reload) occurs, it spills/loads `i.index * 4` instead of the correct `i.index` value.

**Fix:** Release the index from the cache BEFORE modifying the register:
```cpp
string idx_reg = rc.get(i.index);
rc.release(i.index);                  // Spill correct value to stack first
_add(idx_reg, idx_reg, idx_reg);      // Safe to modify freed register
_add(idx_reg, idx_reg, idx_reg);
_add(addr_reg, addr_reg, idx_reg);
```

## 7. All Three Fixes Applied

| Fix | Location in asm.cpp | Description |
|-----|---------------------|-------------|
| 1 | `RegCache::release()` (line 430) | Spill dirty values before discarding |
| 2 | `visit(load)` (line 755) | Back up loaded values to stack immediately |
| 3 | `visit(get_elem_ptr)` (line 814) | Release index BEFORE modifying register in-place |

## 8. Verification Results After Fix

```
n=0: 3  (expected: id(z) = 3) ✓
n=1: 7  (expected: id(3+1+3) = 7) ✓
n=2: 12 (expected: (3+2+3)+1+3 = 12) ✓
n=5: 33 (expected: full chain = 33) ✓

All fp tests: fp2=7, fp3=8, fp4=25, fp5=16, fp6=139, fp7=12, fp8=7, fp9=13, fp10=33 ✓
```

## 9. Summary of Findings

The root cause has three facets, all in `src/asm.cpp`:

1. **Premature register release without spilling** (`rc.release()` at line 430): Values in registers were discarded without being saved to the stack. When later reloaded via `addr.query()`, they read garbage from uninitialized stack memory.

2. **No stack backup for loaded values** (`visit(load)` at line 750-753): Values loaded from globals or pointers were kept only in registers with no stack backup. When the register was evicted across basic block boundaries (via `rc.invalidate()`), the value was lost.

3. **Register corruption by in-place index computation** (`visit(get_elem_ptr)` at line 814-815): The `_add` operations on the index register modified the register value from `index` to `index * 4`. The RegCache wasn't notified, so subsequent uses of the index obtained `index * 4` instead of `index`, causing closure slot accesses to use wildly wrong memory addresses.

| Line(s) in fp10.generated.riscv | Bug | Source in asm.cpp |
|----------------------------------|-----|-------------------|
| 2039: `lw t5, 16(sp)` | Uninitialized stack read (should be counter) | visit(binary):820 |
| 2049, 2062, 2078, 2093, 2108, 2123, 2138, 2153, 2168: `lw t4, 16(sp)` | Propagation of bug 1 | visit(binary):820 |
| 2172: `sw t4, 0(t3)` (t3=&v0) | Corrupts global counter v0 | visit(binary):820 |
| 285: `lw s11, 32(sp)` | Uninitialized stack read (should be counter) | visit(binary):820 |
| 294, 307, 320, 333, 349, 364, 379, 394, 409: `lw s9/s8/s10, 32(sp)` | Propagation of bug 3 | visit(binary):820 |
| 413: `sw s9, 0(s10)` (s10=&v0) | Corrupts global counter v0 | visit(binary):820 |
| 438: `lw t5, 164(sp)` | Uninitialized stack read (should be function pointer fid) | visit(binary):820 |
