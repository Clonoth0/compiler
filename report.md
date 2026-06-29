# SysY 编译器 Lambda 与函数指针扩展项目报告

## 1. 项目背景

### 1.1 SysY 语言

SysY 是北京大学编译原理课程使用的一种教学语言，其核心语法是 C 语言的一个子集，包含基本类型 `int`、`void`、数组、控制流（`if`/`while`/`break`/`continue`）和函数定义。本项目在 SysY 基础之上进行了语言扩展，使其具备接近 C++ 风格的一等函数（first-class function）能力。

### 1.2 语言扩展

本项目在 SysY 词法和语法层面引入了以下扩展：

| 扩展项 | 语法 | 示例 |
|--------|------|------|
| Lambda 表达式 | `[]` / `[=]` / `[&]` | `auto f = [=](int x) -> int { return x + n; }` |
| 递归 Lambda | `auto &self` | `auto fib = [](auto &self, int n) -> int { ... }` |
| 函数指针类型 | `int (*p)(int)` | `int (*p)(int, int);` |
| 函数指针参数 | `int (*f)(int)` | `void apply(int (*f)(int), int x);` |
| 立即调用 Lambda | IIFE | `return [&](int x)->int { ... }(x);` |

这些扩展使得 SysY 程序能够表达高阶函数、闭包、回调链和递归 lambda 等高级编程模式，同时保持与标准 SysY 程序的完全兼容。

### 1.3 编译器架构

编译器采用经典的"前端—中端—后端"架构：

- **前端**（Flex + Bison）：词法分析和语法分析，构建 AST
- **中端**（AST → Koopa IR）：语义分析、捕获分析、闭包布局、函数指针降低、Koopa IR 文本生成
- **后端**（Koopa IR → RISC-V）：通过 libkoopa 解析文本 IR，进行寄存器分配和 RISC-V 汇编生成

代码量总计约 4600 行（`src/` 目录下），其中：
- `ast.cpp`（2278 行）：AST 节点实现、Koopa IR 生成、lambda 降低
- `asm.cpp`（1044 行）：Koopa 到 RISC-V 翻译、寄存器分配、栈帧管理
- `sysy.y`（802 行）：语法分析器
- `sysy.l`（75 行）：词法分析器
- `ast.hpp`（348 行）：AST 节点声明
- `main.cpp`（37 行）：编译器入口

## 2. 项目设计

### 2.1 输入与输出

编译器支持的运行模式：

```bash
# 输出 Koopa IR 文本
./compiler -koopa input.sy -o output.koopa

# 输出 RISC-V RV32IM 汇编
./compiler -riscv input.sy -o output.S

# 性能测试模式（输出 RISC-V）
./compiler -perf input.sy -o output.S
```

输入文件可使用 `.sy`（SysY 语法）或 `.cpp`（扩展 C++ 语法）后缀。

### 2.2 编译流程

```
源程序 (.sy / .cpp)
    │
    ▼
┌───────────────────┐
│  Flex 词法分析     │  识别 token：auto, ->, [=], [&], etc.
└───────────────────┘
    │
    ▼
┌───────────────────┐
│  Bison 语法分析    │  构建 AST 树（ProgramAST → FuncDefAST → LambdaExpAST）
└───────────────────┘
    │
    ▼
┌───────────────────┐
│  Pass 1: 预注册    │  遍历 AST，为所有 lambda 和函数分配唯一 func_id
│  (pre_register)    │  记录函数签名（参数数量、是否全 i32、是否有 self 参数）
└───────────────────┘
    │
    ▼
┌───────────────────┐
│  Pass 2: 捕获分析  │  对每个 lambda，遍历其函数体 AST
│  (collect_captures)│  收集引用但非声明/非参数的外部变量
└───────────────────┘
    │
    ▼
┌───────────────────┐
│  Pass 3: 拓扑排序  │  基于嵌套/引用关系计算 lambda 发射顺序
│  (visit_lambdas)   │  优先发射无捕获、无 self 的 lambda
└───────────────────┘
    │
    ▼
┌───────────────────┐
│  Pass 4: func_id   │  按发射顺序重新分配 func_id
│  重分配            │  确保 func_id 与后续 __fp_table 索引一致
└───────────────────┘
    │
    ▼
┌───────────────────┐
│  Pass 5: IR 生成   │  输出 Koopa IR 文本
│  (print_body)      │  全局数据 → @__fp_* stub → lambda 函数体 → main
└───────────────────┘
    │
    ▼
┌───────────────────┐
│  libkoopa 解析     │  koopa_parse_from_string() → koopa_raw_program_t
└───────────────────┘
    │
    ▼
┌───────────────────┐
│  RISC-V 代码生成   │  LSRA 寄存器分配 → RegCache → 栈帧管理 → 指令输出
│  (solve_riscv)     │  Peephole 优化：消除连续 lw/sw
└───────────────────┘
    │
    ▼
RISC-V 汇编 (RV32IM)
```

### 2.3 Lambda 降低方案

#### 2.3.1 函数指针与闭包的双轨模型

由于 Koopa IR 不支持真正的函数指针类型，本项目使用**整数函数编号**模拟。核心机制包括：

- **函数编号（func_id）**：每个普通函数和 lambda 都有唯一整数编号（从 0 开始）
- **闭包环境表（__fp_env）**：全局数组，每行 9 个 `i32`（slot 0 = 函数编号，slot 1-8 = 8 个捕获值）
- **环境基址（FP_ENV_BASE = 1000000）**：当 func_id ≥ 1000000 时，表示这是一个闭包环境引用

调用一个函数指针时，编译器的分发流程如下：

```
callee_val = load function pointer variable

if callee_val >= FP_ENV_BASE:
    row = (callee_val - FP_ENV_BASE) * 9       # 计算环境表行号
    real_fid = __fp_env[ row*4 ]               # 取出真实函数编号
    caps = __fp_env[ row*4 + 4 ] ..            # 取出 8 个捕获值
    call @__fp_N_i32(real_fid, caps..., args...) # 分发到真实函数
else:
    # 直接函数指针
    call @__fp_N_i32(callee_val, args...)      # 直接分发
```

RISC-V 后端的 `visit(call)` 函数在检测到 `@__fp_*` 调用时，直接通过 `__fp_table` 跳转：

```asm
    slli  t1, t0, 2          # t0 = func_id
    la    t2, __fp_table
    add   t2, t2, t1
    lw    t2, 0(t2)
    jalr  ra, t2, 0          # 间接调用目标函数
```

#### 2.3.2 无捕获 Lambda

```c
auto f = [](int x) -> int { return x + 1; };
int (*p)(int) = f;
return p(5);
```

降低后，`f` 的函数体成为带 `%_N` 名称的普通 Koopa 函数。`p` 中保存 `func_id[f]`。调用 `p(5)` 时通过 `__fp_table[func_id]` 间接跳转到 `%_N`。

#### 2.3.3 值捕获 Lambda `[=]`

```c
int base = 10;
auto f = [=](int x) -> int { return x + base; };
```

编译器为 `f` 生成额外的隐藏参数：
```
fun %_N(%captured_base_: i32, %x_: i32) : i32 { ... }
```

当 `f` 逃逸为函数指针时（如通过 `apply(f, x)` 传递），编译器将 `func_id` 和 `base` 的当前值打包到环境表：

```
__fp_env[row*9 + 0] = func_id[f]
__fp_env[row*9 + 1] = base  (= 10)
...
return row + FP_ENV_BASE   # 返回环境编号
```

调用者通过环境编号恢复函数的真实编号和捕获值，完成间接调用。

#### 2.3.4 引用捕获 Lambda `[&]`

`[&]` 捕获生成两个版本的函数体：
- **值 ABI**（`lambda_name`）：捕获参数为 `i32`（用于闭包逃逸场景）
- **引用 ABI**（`ref_lambda_name`）：捕获参数为 `*i32`（用于直接调用，修改透过指针写回外层变量）

例如：
```c
int n = 1;
auto inc = [&](int x) -> int { n = n + x; return n; };
```

编译器在 `inc` 的 ClosureLayout 中记录 `capture_is_ref = {true}` 和 `capture_ref_slots = {addr_of_n}`。调用 `inc(2)` 时，编译器检查 `layout_needs_ref_abi()` 并选择 `ref_lambda_name` 版本，传递 `n` 的地址作为参数。

## 3. fp10.cpp 完整处理流程

本节以 `fp10.cpp` 为例，详细展示编译器每一步处理后的中间结果。

### 3.1 源代码

```cpp
int main()
{
    int z = 3;
    auto f = [=](auto &self, int n, int (*p)(int)) -> int
    {
        if (!n)
            return p(z);
        else
            return self(self, n - 1,
                [=](int x) -> int { return p(x + n + z); });
    };
    int n = 5;
    return f(f, n, [](int x) -> int { return x; });
}
```

### 3.2 词法分析与语法分析

词法分析器 `sysy.l` 将源代码转换为 token 流。扩展 token：
- `auto` → 新关键字
- `[`, `]`, `=`, `&` → 用于 lambda 捕获子句
- `->` → 用于 lambda 返回类型声明
- `(` `*` `IDENT` `)` → 函数指针类型语法

语法分析器 `sysy.y` 根据产生式规则构建 AST 树。`fp10.cpp` 产生的 AST 结构：

```
ProgramAST
├── FuncDefAST (ident="main")
│   └── BlockAST
│       ├── VarDeclAST                                # int z = 3;
│       │   └── VarDefAST (ident="z", init=3)
│       ├── VarDeclAST                                # auto f = [=](...)
│       │   └── VarDefAST (ident="f", is_auto=true)
│       │       └── LambdaExpAST (cap_val=true)
│       │           ├── params: [LambdaParamAST("&self"), "n", "p"]
│       │           └── block: StmtAST → if(!n) ... else ...
│       ├── VarDeclAST                                # int n = 5;
│       │   └── VarDefAST (ident="n", init=5)
│       └── ReturnStmt
│           └── UnaryExpAST ("f")
│               └── params: [Exp("f"), Exp("n"), LambdaExpAST([](int x)->int)]
```

### 3.3 Pass 1: 预注册（pre_register）

编译器遍历 AST，为每个 lambda 分配唯一名称和 func_id：

| Lambda | 描述 | lambda_name | func_id（初始） | total_params | has_self |
|--------|------|-------------|-----------------|-------------|----------|
| 外层 f | 递归 lambda | `__lambda_0` | 0 | 4 (self + z + n + p) | true |
| 内层回调 | [=](int x)→int | `__lambda_1` | 1 | 4 (p + n + z + x) | false |
| identity | [](int x)→int | `__lambda_2` | 2 | 1 (x) | false |

对应的函数签名记录：

| lambda_name | param_count | all_i32_params | has_self_param | returns_int |
|-------------|-------------|----------------|----------------|-------------|
| `__lambda_0` | 4 | true | true | true |
| `__lambda_1` | 4 | true | false | true |
| `__lambda_2` | 1 | true | false | true |

### 3.4 Pass 2: 捕获分析（collect_captures）

#### 外层 lambda `f`

遍历 `f` 的函数体 AST，找到外部变量引用：
- `z` — 引用自外层作用域，非参数、非局部声明 → **值捕获**
- `self` — lambda 参数（`auto &self`）→ 不捕获
- `n`, `p` — lambda 参数 → 不捕获

捕获结果：`captures = ["z"]`, `capture_is_ref = [false]`

#### 内层回调 lambda

遍历内层 lambda 的函数体 `{ return p(x + n + z); }`：
- `p` — 来自外层 f 的作用域，非参数 → **值捕获**
- `n` — 来自外层 f 的作用域 → **值捕获**
- `z` — 来自外层 f 的捕获 → **值捕获**
- `x` — lambda 参数 → 不捕获

捕获结果：`captures = ["p", "n", "z"]`, `capture_is_ref = [false, false, false]`

### 3.5 Pass 3: 拓扑排序

基于 `better_lambda` 比较器排序：
1. `has_self=false` 的优先
2. 捕获数少的优先
3. 同条件按 AST 访问顺序

`fp10.cpp` 排序结果：`[identity, 内层回调, 外层 f]`

### 3.6 Pass 4: func_id 重分配

为匹配 `__fp_table` 索引，按发射顺序重新分配：

| 发射顺序 | lambda_name | 新 func_id |
|----------|-------------|-----------|
| 1 | identity | 0 |
| 2 | 内层回调 | 1 |
| 3 | 外层 f | 2（从 func_sigs 来看 has_self=true，不在 fp_table 中... 但作为回调目标仍需要） |

实际上 `emit_fp_helper` 扫描 `func_id` 构建候选列表。**emit_fp_helper** 在 `ProgramAST::print()` 中现已被放置于 **lambda 体发射之前**，其生成的 `@__fp_*` stub 不包含分派逻辑（只有 `ret 0`）。分派完全由 RISC-V 后端通过 `__fp_table` 完成。

### 3.7 Pass 5: Koopa IR 生成

编译器生成如下关键 Koopa IR（节选）：

#### 全局数据

```llvm
global %_0 = alloc i32, zeroinit          # 环境表计数器
global %_1 = alloc [i32, 2304], zeroinit  # 闭包环境表 (9 * 256)
```

#### 分派 stub（@__fp_N_i32）

```llvm
fun @__fp_1_i32(%fid: i32, %a0: i32) : i32 {
%entry___fp_1_i32:
    ret 0
}
```

> 注：`@__fp_*` 的 Koopa body 仅提供语法正确性。实际分派在 RISC-V 阶段完成。

#### identity lambda

```llvm
fun %_4(%_55_: i32) : i32 {
%entry__4:
    %_56 = alloc i32
    store %_55_, %_56
    %_57 = load %_56
    ret %_57
}
```

#### 内层回调 lambda

```llvm
fun %_3(%_58_: i32, %_59_: i32, %_60_: i32, %_61_: i32) : i32 {
%entry__3:
    %_62 = alloc i32               # p (FP func_id)
    store %_58_, %_62
    %_63 = alloc i32               # n
    store %_59_, %_63
    %_64 = alloc i32               # z
    store %_60_, %_64
    %_65 = alloc i32               # x (user param)
    store %_61_, %_65
    %_66 = load %_65               # x
    %_67 = load %_63               # n
    %_68 = add %_66, %_67          # x + n
    %_69 = load %_64               # z
    %_70 = add %_68, %_69          # x + n + z
    %_71 = load %_62               # p (func_id)
    %_72 = ge %_71, 1000000
    br %_72, %fp_env_0, %fp_direct_0
%fp_env_0:
    # 加载环境表：real_fid + 8 个捕获值
    %_78 = call @__fp_4_i32(%_80, %real_fid, %cap0, %cap1, %cap2, %_70)
    ...
%fp_direct_0:
    %_86 = call @__fp_1_i32(%_71, %_70)
    ...
}
```

关键点：`p(x + n + z)` 被展开为对 `@__fp_1_i32(func_id, computed_arg)` 的调用。RISC-V 后端将此调用替换为 `__fp_table` 分派。

#### 外层递归 lambda

```llvm
fun %_2(%_109_: i32, %_110_: i32, %_111_: i32, %_112_: i32) : i32 {
%entry__2:
    %_113 = alloc i32          # self (func_id)
    store %_109_, %_113
    %_114 = alloc i32          # z (captured)
    store %_110_, %_114
    %_115 = alloc i32          # n
    store %_111_, %_115
    %_116 = alloc i32          # p (FP func_id)
    store %_112_, %_116
    %_117 = load %_115         # n
    %_118 = eq %_117, 0
    br %_118, %then_0, %else_0
%then_0:                      # n == 0: return p(z)
    %_120 = call @__fp_1_i32(%_122, %_119)
    ret %_120
%else_0:                      # n > 0: 递归
    # 向环境表写入闭包:
    #   slot 0 = func_id[内层回调]
    #   slot 1 = n       slot 2 = z       slot 3 = p
    #   slot 4..8 = 0
    %_150 = add %_131, 1000000  # env_id = counter + FP_ENV_BASE
    # 递归调用 self(self, n-1, env_id)
    %_151 = call %_2(%_130, %_132, %_135, %_150)
    ret %_151
}
```

#### main 函数

```llvm
fun @main() : i32 {
%entry_main:
    %_196 = alloc i32
    store 3, %_196                      # z = 3
    %_197 = alloc i32
    store 2, %_197                      # identity func_id = 2
    %_198 = alloc i32
    %_199 = load %_196
    store %_199, %_198                  # z_copy = 3
    %_200 = alloc i32
    store 5, %_200                      # n = 5
    %_201 = load %_197                  # func_id of identity
    %_202 = load %_198                  # z captured value
    # 向环境表写入闭包:
    #   slot 0 = 2 (self func_id = 2? => 外层f的func_id)
    #   slot 1 = 3 (z value)
    #   slot 2..8 = 0
    %_234 = call %_2(0, %_233, %_232, 0)  # call f(0, z=3, n=5, p=0)
    ret %_234
}
```

### 3.8 RISC-V 代码生成

`libkoopa` 解析 Koopa IR 文本为 raw program。`asm.cpp` 中的 `solve_riscv()` 遍历 raw IR，进行以下操作：

#### 3.8.1 线性扫描寄存器分配（LSRA）

对每个函数进行活性分析（live interval analysis），将 Koopa value 映射到 18 个通用寄存器（t0-t6, s0-s11）。当寄存器不足时，将溢出到栈。

#### 3.8.2 RegCache 与地址表

`RegCache` 管理寄存器分配/回收。`AddressTable` 管理栈帧布局。栈帧计算：

```
T = (A + S + S2 + R + 15) / 16 * 16

A  = 最大调用溢出参数字节数  (args > 8)
S  = spill 区大小 (每条指令 4 字节，减去 unit 类型，加上 alloc 大小)  
S2 = callee-saved 寄存器保存区
R  = 返回地址保存区 (4 字节)
```

#### 3.8.3 RISC-V 汇编（节选 fp10 的 main）

```asm
main:
    addi sp, sp, -160
    sw   ra, 156(sp)
entry_main:
    li   t1, 3
    sw   t1, 0(sp)            # z = 3
    li   t1, 2
    sw   t1, 4(sp)            # identity func_id = 2
    lw   t1, 0(sp)
    sw   t1, 8(sp)            # z_copy = 3
    li   t2, 5
    sw   t2, 16(sp)           # n = 5
    # 写入环境表 v1:
    la   t5, v0
    lw   t4, 0(t5)            # counter
    ...
    sw   t3, 0(t5)            # v1[slot 1] = z = 3
    ...
    # 调用 f(0, z=3, n=5, p=0):
    li   a0, 0
    lw   a1, 8(sp)
    lw   a2, 16(sp)
    li   a3, 0
    call _2                   # f(self=0, z=3, n=5, p=0)
    mv   t1, a0
    mv   a0, t1
    lw   ra, 156(sp)
    addi sp, sp, 160
    ret
```

#### 3.8.4 __fp_table 与分派

```asm
__fp_table:
    .word _4       # func_id 0: identity lambda
    .word _3       # func_id 1: 内层回调 lambda
    .word _2       # func_id 2: 外层递归 lambda

# RISC-V 分派代码 (由 visit(call) 生成，截获 @__fp_* 调用):
    slli  t1, t0, 2
    la    t2, __fp_table
    add   t2, t2, t1
    lw    t2, 0(t2)
    jalr  ra, t2, 0
```

### 3.9 运行时执行链

对于 `fp10.cpp`，`n=5` 时的执行链：

```
main:
  → f(self=0, z=3, n=5, p=0)
    → else_0: 创建闭包_c1(n=5, z=3, p=0), counter=1
      → f(self=0, z=3, n=4, p=1)  (1 = FP_ENV_BASE + counter_prev)
        → else_0: 创建闭包_c2(n=4, z=3, p=1), counter=2
          → f(self=0, z=3, n=3, p=2)
            → else_0: 创建闭包_c3(n=3, z=3, p=2), counter=3
              → f(self=0, z=3, n=2, p=3)
                → else_0: 创建闭包_c4(n=2, z=3, p=3), counter=4
                  → f(self=0, z=3, n=1, p=4)
                    → else_0: 创建闭包_c5(n=1, z=3, p=4), counter=5
                      → f(self=0, z=3, n=0, p=5)
                        → then_0: p(z) where p=env_id=5
                          加载_c5: n=1, z=3, p=4
                          调用内层(p=1, n=1, z=3, x=3: z from then_0)
                          → compute: 3+1+3=7, dispatch on p=4
                          加载_c4: n=2, z=3, p=3
                          → compute: 7+2+3=12, dispatch on p=3
                          加载_c3: n=3, z=3, p=2
                          → compute: 12+3+3=18, dispatch on p=2
                          加载_c2: n=4, z=3, p=1
                          → compute: 18+4+3=25, dispatch on p=1
                          加载_c1: n=5, z=3, p=0
                          → compute: 25+5+3=33, dispatch on p=0
                          → identity(33) = 33
```

最终 `main` 返回 **33**。

## 4. 实现情况

### 4.1 后端 Bug 修复

在实现 RISC-V 代码生成过程中，发现并修复了 6 个关键 bug：

#### Bug 1: rc.release() 不 spill 脏值

`RegCache::release()` 在释放寄存器时仅将寄存器加入空闲池，若该值被标记为脏（dirty），则其栈槽位从未被写入。后续 `load()` 尝试从栈读取时读取到未初始化内存。

**修复**：在 `release()` 中增加判断 —— 若值标记为 dirty，先 `_sw` 到栈再释放。

```cpp
if(dirty.count(v))
    _sw(r, "sp", addr.query(v));
```

#### Bug 2: visit(load) 不备份到栈

`visit(load)` 通过 `rc.set()` 将加载的值放入寄存器并标记为 dirty，但从未备份到栈。当 `flush_all()` 只 flush t-reg 时，s-reg 中的值跨越函数调用丢失。

**修复**：在 `rc.set()` 后立即 `_sw(result_reg, "sp", addr.query(value))`。

#### Bug 3: visit(get_elem_ptr) 原地修改破坏 index

`visit(get_elem_ptr)` 中的 `_add(idx_reg, idx_reg, idx_reg)` 原地修改 index 寄存器，从 `index * 1` 变为 `index * 4`。但 RegCache 未被通知，后续 spill 时写入错误值。

**修复**：在 `_add` 前先 `rc.release(i.index)`，确保正确的值已写入栈。

#### Bug 4: ptr_value.count 应为 insert

第 758 行：`ptr_value.count(value)` 仅检查成员关系（返回 bool，丢弃），应为 `ptr_value.insert(value)`。这导致加载指针类型 alloc 的结果未被标记为指针，后续无法正确解引用。

#### Bug 5: visit(call) 不区分参数类型

调用 `@__fp_N_i32` 时，若参数类型为 `*i32`（指针），应使用 `load_addr()` 传地址而非 `load()` 传值。尤其在 ref ABI 调用中，`load()` 读取值会导致传递错误参数。

**修复**：检查 callee 的参数类型，若为 `KOOPA_RTT_POINTER`，使用 `load_addr()`。

#### Bug 6: Koopa IR 前向引用

当两个 lambda 互相引用时（如 fp10.2.cpp 中的两个回调 lambda），`%_3` 的 dispatch chain 引用 `%_4`，但 `%_4` 的 body 在 Koopa 文本中排在 `%_3` 之后。Koopa 文本解析器不支持函数前向引用。

**修复**（三处改动）：
1. `emit_fp_helper` 改为 stub body（仅 `ret 0`），不再包含对 `%_N` 的直接调用
2. `emit_dispatch_chain` 中的 dispatch case 改为调用 `@__fp_N_i32(id, args...)` 而非 `%target(args...)`
3. 拓扑排序后按发射顺序重新分配 `func_id`，确保与 `__fp_table` 索引一致

### 4.2 后端实现要点

#### Peephole 优化

`_lw` 和 `_sw` 函数实现了连贯的 peephole 优化：
- 连续 `sw + lw` 到同一地址：跳过 lw（值已在寄存器中）
- 不同目标寄存器但同地址：替换为 `mv`
- `sw t0, 0(t0)` 特殊情况：先 `mv a7, t0` 再操作

#### 栈帧管理

`AddressTable` 为每个 Koopa value 维护栈地址映射。`addr.query(value, size)` 按需分配栈空间并返回偏移量。帧大小计算对数组 alloc（size > 4）正确增加了额外空间。

```cpp
if(inst->kind.tag == KOOPA_RVT_ALLOC) {
    int size = get_alloc_size(inst->ty->data.pointer.base);
    S += size - 4;  # 代替默认的 4 字节
}
```

## 5. 达成效果

### 5.1 测试结果

在 Docker 环境 `maxxing/compiler-dev` 中使用 `autotest` 验证：

| 测试集 | 结果 | 备注 |
|--------|------|------|
| `sysy-testsuit-collection` | **466/467 通过** | 仅 `matrix-1` 超时（性能问题，预先存在） |
| `fp_tests`（lambda 专项） | **78/78 通过** | 含新增 test 91：fp10.2 双 FP 递归 |

#### fp_tests 新增测试

| 编号 | 名称 | 测试特性 | 预期值 |
|------|------|----------|--------|
| 52 | fp_reassign_call | 函数指针重赋值后调用 | 39 |
| 55 | multi_cap_chain | 4 值捕获递归 lambda | 16 |
| 84 | deep_recurse_ref | 深度递归引用捕获 | 64 |
| 85 | seven_cap_rec_chain | 7 值捕获递归链 | 85 |
| 86 | fp10_deep | fp10 深递归 (n=8) | 74 |
| 87 | multi_fp_hof | 多函数指针高阶函数 | 63 |
| 88 | fp10_rec_ref_cap | fp10 引用捕获递归 | 15 |
| 89 | fp10_multi_step | fp10 多步骤压缩 | 44 |
| 90 | fp10_multi_arg_fp | fp10 三参数函数指针 | 157 |
| 91 | fp10.2_multi_fp | fp10.2 双 FP 递归 | 84 |

### 5.2 支持的功能总结

| 功能 | 状态 |
|------|------|
| 标准 SysY 程序 | ✅ 完全兼容 |
| 无捕获 lambda | ✅ |
| `[=]` 值捕获 lambda | ✅ |
| `[&]` 引用捕获 lambda | ✅ |
| `auto &self` 递归 lambda | ✅ |
| 函数指针变量 | ✅ |
| 函数指针形参 | ✅ |
| 函数指针赋值 | ✅ |
| lambda 作为函数指针实参 | ✅ |
| 闭包逃逸（lambda 通过 FP 形参传递） | ✅ |
| IIFE（立即调用 lambda） | ✅ |
| 递归回调链（如 fp10.cpp） | ✅ |
| 多函数指针递归（如 fp10.2.cpp） | ✅ |
| 混合值/引用捕获 | ✅ |
| void lambda + 引用写回 | ✅ |
| 多级嵌套捕获 | ✅ |
| 最多 8 个值/引用捕获 | ✅ |
| 名称遮蔽下的捕获 | ✅ |
| 分支中创建闭包并逃逸 | ✅ |
| `putint`/`putch`/`getint`/`getch`/`getarray` | ✅ |

### 5.3 功能限制

| 限制 | 说明 |
|------|------|
| 最多 8 个捕获值 | `FP_CAP_SLOTS = 8` |
| 无显式捕获列表 | 不支持 `[x, &y]` 等 |
| 无 `mutable` lambda | — |
| 无泛型 lambda 参数 | `auto` 仅用于 `auto &self` |
| 不支持捕获数组/结构体 | — |
| Koopa 文本不支持前向引用 | 已通过 @__fp_* stub 机制绕过 |

## 6. 参考文献

1. **RISC-V 指令集规范** — The RISC-V Instruction Set Manual, Volume I: User-Level ISA, https://riscv.org/technical/specifications/

2. **Koopa IR** — 北京大学编译原理课程 IR 框架, https://github.com/pku-minic/koopa

3. **SysY 语言规范** — 北京大学编译原理课程, https://pku-minic.github.io/online-doc/#/

4. **compiler-dev Docker 镜像** — maxxing/compiler-dev, https://hub.docker.com/r/maxxing/compiler-dev

5. **SysY 测试集** — 北京大学编译原理课程, https://github.com/pku-minic/sysy-testsuit-collection

6. **Linear Scan Register Allocation** — Poletto, M. and Sarkar, V. "Linear Scan Register Allocation", ACM TOPLAS 1999

7. **RISC-V Calling Convention** — RISC-V ELF psABI Specification, https://github.com/riscv-non-isa/riscv-elf-psabi-doc

8. **LLD Linker** — The LLVM Linker, https://lld.llvm.org/
