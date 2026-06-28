# SysY 编译器 Lambda 与函数指针扩展项目报告

## 1. 项目背景

本项目以现有 SysY 编译器为基础，扩展了接近 C++ 的 lambda 表达式和函数指针能力。原始编译器支持 SysY 语言到 Koopa IR 的前端翻译，以及基于 libkoopa 的 RISC-V 代码生成。本次扩展的重点在前端语法、AST 语义信息维护、Koopa IR 降低方式以及回归测试体系。

扩展后的输入仍然兼容标准 SysY 程序，同时允许在测试文件中使用如下 C++ 风格子集：

```c
int inc(int x) { return x + 1; }

int main() {
  int base = 3;
  auto add = [=](int x) -> int { return x + base; };
  auto rec = [=](auto &self, int n, int (*p)(int)) -> int {
    if (!n) return p(base);
    return self(self, n - 1, [=](int x) -> int { return p(x + n + base); });
  };
  return rec(rec, 2, inc);
}
```

该扩展主要服务于 `fp_tests` 中的函数指针和 lambda 测试，尤其是 `fp10.cpp` 所代表的递归 lambda、函数指针回调和嵌套闭包组合场景。标准 SysY 行为保持兼容，`sysy-testsuit-collection` 作为回归测试集持续验证。

## 2. 项目设计

### 2.1 输入与输出

编译器输入为 SysY 源文件，扩展测试中允许文件后缀为 `.sy` 或 `.cpp`，但实际语法仍是 SysY 加受限 C++ lambda 子集。编译器输出包括：

- `-koopa`：输出 Koopa IR 文本。
- `-riscv`：输出 RISC-V 汇编。
- `-perf`：沿用 RISC-V 后端输出路径。

本次功能主要在 `-koopa` 阶段完成语义降低。lambda、函数指针和闭包都被翻译成普通 Koopa 函数、整数函数编号和运行时环境表；后端不需要理解高级 lambda 语义。

### 2.2 编译流程与 Pass

整体流程如下：

```mermaid
flowchart LR
  A[源程序] --> B[Flex 词法分析]
  B --> C[Bison 语法分析]
  C --> D[AST]
  D --> E[预注册函数与 lambda]
  E --> F[捕获分析与闭包布局]
  F --> G[Koopa IR 生成]
  G --> H[libkoopa 解析]
  H --> I[RISC-V 代码生成]
```

各阶段职责如下：

- 词法分析：在原 SysY token 基础上识别 `auto`、`->` 等扩展 token。
- 语法分析：新增 `LambdaExp`、lambda 参数、函数指针声明和函数指针形参产生式。
- AST 预注册：在正式输出函数体前收集所有普通函数与 lambda，为每个可间接调用目标分配整数函数编号。
- 捕获分析：遍历 lambda 函数体，找出引用了外层作用域但不是形参和局部声明的标识符，并按 `[=]` 或 `[&]` 标记值捕获或引用捕获。
- 闭包布局：为 `auto f = ...` 生成函数编号槽和捕获槽；对捕获 lambda 逃逸为函数指针的情形，将函数编号和捕获值写入全局环境表。
- 函数指针分发：把 `p(args...)` 降低为基于函数编号的分发链。若编号表示闭包环境，则先从环境表取出真实函数编号和捕获值，再调用对应 lambda 函数。
- 后端代码生成：沿用原有 Koopa 到 RISC-V 的翻译，不额外引入 lambda 专用后端逻辑。

### 2.3 Lambda 降低方案

无捕获 lambda 可直接表示为函数编号：

```c
auto f = [](int x) -> int { return x + 1; };
int (*p)(int);
p = f;
return p(5);
```

降低后，`f` 的函数体成为普通 Koopa 函数，`p` 中保存该函数编号，调用 `p(5)` 时通过 `__fp_1_i32` 或内联分发链调用真实目标。

带捕获 lambda 会额外生成隐藏参数：

```c
int base = 10;
auto f = [=](int x) -> int { return x + base; };
```

会被降低为近似如下签名：

```c
__lambda(base, x)
```

其中 `base` 是隐藏捕获参数。`[=]` 在定义闭包时保存当前值；`[&]` 在直接调用时使用引用 ABI，保证写回外层变量；在逃逸为通用函数指针时，当前实现会把可表示的捕获值打包进环境表。

### 2.4 函数指针与闭包环境

由于 Koopa IR 不支持真正的函数指针类型，本项目使用整数函数编号模拟函数指针。每个可间接调用的普通函数或 lambda 都有一个唯一 `func_id`。普通函数指针变量保存这个编号。

对带捕获闭包，单个整数无法保存全部捕获值，因此引入全局环境表：

```c
global __fp_env_sp = alloc i32, zeroinit
global __fp_env = alloc [i32, FP_ENV_LIMIT * FP_ENV_WORDS], zeroinit
```

环境表每一行保存：

- 第 0 个 word：真实函数编号。
- 后续 word：最多 8 个捕获值。

环境编号使用一个较大的偏移量 `FP_ENV_BASE` 区分普通函数编号和闭包环境编号。函数指针调用时先判断编号是否大于等于该偏移量，若是则读取环境表恢复真实目标和捕获值。

## 3. 实现情况

### 3.1 代码结构

主要修改集中在以下文件：

- `src/sysy.y`：扩展语法，包括 `auto` lambda 声明、lambda 参数、函数指针声明和函数指针形参。
- `src/include/ast.hpp`：新增或扩展 `LambdaParamAST`、`LambdaExpAST`、`ClosureLayout` 等结构。
- `src/ast.cpp`：实现捕获分析、lambda 预注册、闭包布局、函数指针环境表、间接调用分发和相关 IR 生成。
- `fp_tests/`：维护函数指针和 lambda 专项测试集，新增 `66` 到 `83` 等复杂边界用例。

### 3.2 关键实现点

第一，lambda 预注册与定义顺序。Koopa 文本要求函数在调用前可见，因此 AST 生成阶段先遍历全程序收集 lambda，并在普通函数与 `main` 之间输出 lambda 函数体和函数指针 helper。这样 `main` 和大多数普通函数都可以调用 lambda 或通过函数指针 helper 分发。

第二，捕获分析。实现通过递归遍历 lambda block，收集其中出现的 `LVal`。再排除 lambda 形参、`auto &self` 参数、本地声明和已知普通函数，剩余标识符就是需要捕获的外层变量。`[=]` 将捕获值复制到闭包槽，`[&]` 将引用信息保存在闭包布局中，直接调用时使用引用 ABI。

第三，函数指针分发。普通函数和 lambda 都映射到整数编号。对于 `int (*p)(int)` 这样的变量，赋值 `p = add;` 保存普通函数编号，赋值 `p = lambda;` 保存 lambda 编号或环境编号。调用时生成条件分发链，匹配编号后调用真实函数。

第四，闭包逃逸。对于 `apply(lambda, x)` 或 `p = lambda` 等闭包逃逸场景，如果 lambda 有捕获，会将真实函数编号和捕获值写入全局环境表，实参或变量中保存环境编号。`fp10.cpp` 中递归 lambda 把新 lambda 作为回调继续传给 `self`，正是通过该机制实现。

第五，自递归 lambda。支持 `auto &self` 作为第一个参数，例如：

```c
int main() {
  auto fib = [](auto &self, int n) -> int {
    if (n <= 1) return n;
    return self(self, n - 1) + self(self, n - 2);
  };
  return fib(fib, 6);
}
```

实现中 `self` 不作为普通用户参数处理，而是在调用当前 lambda 时传递函数编号；后续参数按正常用户参数生成。

### 3.3 `fp10.cpp` 处理策略

`fp10.cpp` 是本项目中最能体现 lambda 扩展复杂性的样例。源程序如下：

```c
int main()
{
  int z = 3;
  auto f = [=](auto &self, int n, int (*p)(int)) -> int
  {
    if (!n)
      return p(z);
    else
      return self(self, n - 1, [=](int x) -> int { return p(x + n + z); });
  };
  int n = 5;
  return f(f, n, [](int x) -> int { return x; });
}
```

这个程序同时包含五个关键点：

- `f` 使用 `[=]` 捕获外层 `z`，所以 `z` 的值固定为定义 `f` 时的 3。
- `f` 使用 `auto &self` 实现递归，编译器需要把它识别为特殊递归参数，而不是普通泛型参数。
- `f` 的第三个参数 `p` 是函数指针，可以指向普通函数、无捕获 lambda，也可以指向带捕获闭包。
- 递归分支中又创建了一个新的 `[=]` lambda，它捕获当前层的 `p`、`n` 和 `z`。
- 新 lambda 作为第三个参数继续传给 `self`，因此它会逃逸到函数指针形参中，不能只传一个普通函数编号。

处理策略分为以下几步。

第一步是预注册 lambda。编译器在输出 Koopa IR 前遍历整棵 AST，先找到外层递归 lambda 和递归分支里的内层回调 lambda，为它们分配函数编号。这样后续函数指针分发链可以用整数编号识别目标函数。对 `fp10.cpp` 而言，至少会生成三类可调用目标：外层递归 lambda、内层回调 lambda、初始的无捕获 identity lambda。

第二步是捕获分析。外层 lambda `f` 的函数体引用了外层变量 `z`，因此 `z` 被识别为值捕获；`self`、`n`、`p` 是 lambda 参数，不会被当作捕获。内层 lambda 的函数体引用 `p`、`n`、`z`，这些名字来自外层 lambda 的当前调用帧，因此被识别为内层闭包的值捕获。它的逻辑可以看成：

```c
inner(p, n, z, x) = p(x + n + z)
```

第三步是降低外层递归 lambda。外层 `f` 会变成一个普通 Koopa 函数，签名近似为：

```c
f(self_id, captured_z, n, p_id)
```

其中 `self_id` 是递归调用目标编号，`captured_z` 是 `[=]` 捕获的 `z`，`n` 是普通参数，`p_id` 是函数指针编号或闭包环境编号。直接调用 `f(f, n, identity)` 时，编译器会把 `f` 自己的函数编号作为 `self_id` 传入。

第四步是处理递归分支里的闭包逃逸。表达式：

```c
self(self, n - 1, [=](int x) -> int { return p(x + n + z); })
```

中的第三个实参是带捕获 lambda。它需要保存 `p`、`n`、`z` 三个捕获值。如果只传内层 lambda 的函数编号，下一层递归调用时就无法知道它应该继续调用哪一个上一层回调，也无法恢复当时的 `n` 和 `z`。因此编译器把它写入全局闭包环境表：

```text
env[row][0] = inner_lambda_func_id
env[row][1] = n
env[row][2] = z
env[row][3] = p_id
```

实际实现中环境表每行有固定 9 个 `i32` 槽位，第 0 个槽位存真实函数编号，后 8 个槽位存捕获值。写入完成后返回 `FP_ENV_BASE + row` 作为新的函数指针值。下一层递归接收到的 `p` 就不再是简单函数编号，而是一个闭包环境编号。

第五步是函数指针调用分发。无论在基本情况 `p(z)` 中，还是内层 lambda 的 `p(x + n + z)` 中，编译器都会先判断 `p_id >= FP_ENV_BASE`。如果不是闭包环境，就按普通函数编号分发；如果是闭包环境，就从环境表取出真实函数编号和捕获值，再调用对应 lambda。例如对内层回调，分发后实际调用近似为：

```c
inner(captured_n, captured_z, captured_p, x)
```

然后 `inner` 内部再用同样的函数指针分发机制调用 `captured_p`。这样多层回调链就可以逐层展开。

`fp10.cpp` 的运行结果也可以按这个模型解释。初始回调为：

```text
p0(x) = x
```

递归每下降一层，创建一个新回调：

```text
p1(x) = p0(x + 5 + 3) = x + 8
p2(x) = p1(x + 4 + 3) = x + 15
p3(x) = p2(x + 3 + 3) = x + 21
p4(x) = p3(x + 2 + 3) = x + 26
p5(x) = p4(x + 1 + 3) = x + 30
```

当 `n == 0` 时返回 `p5(z)`，其中外层 `z` 的捕获值仍为 3，所以最终结果是：

```text
p5(3) = 33
```

因此，`fp10.cpp` 的本质不是只支持递归 lambda，而是要同时支持递归 lambda、自身函数编号传递、函数指针参数、闭包逃逸、闭包环境恢复和多层回调链式调用。本项目通过“函数编号 + 闭包环境表 + 间接调用分发”的统一模型处理这些组合情况。

### 3.4 调试经验

实现过程中遇到的主要问题有三类。

一是闭包变量与函数指针变量的状态同步。函数指针变量可能先保存带捕获 lambda，再被普通函数覆盖。如果不清理旧捕获槽，后续调用会误判为闭包调用。本项目在赋值路径中同步复制或清空闭包布局信息，避免旧状态污染。

二是闭包作为函数实参时容易丢失环境。`apply2(plus, 1, 2)` 中 `plus` 如果只传递 lambda 函数编号，就会丢掉 `base` 捕获值。当前实现增加了实参求值阶段的闭包打包逻辑，对有捕获的闭包变量生成环境编号。

三是引用捕获与递归 ABI。`[&]` lambda 的直接调用需要传递可写引用；自递归时如果把保存引用的本地槽地址再次传出，会形成 `**i32` 与 `*i32` 的类型错误。本项目修正为按当前 lambda 的引用 ABI 取出实际引用值。

## 4. 达成效果

### 4.1 测试结果

在 Docker 环境 `maxxing/compiler-dev` 中使用 `autotest` 验证，当前结果如下：

| 测试集 | 命令 | 结果 |
|---|---|---|
| 标准 SysY 回归 | `autotest -koopa -t /root/compiler/sysy-testsuit-collection /root/compiler` | 467/467 |
| lambda 与函数指针专项 | `autotest -koopa -t /root/compiler/fp_tests /root/compiler` | 81/81 |
| `fp10.cpp` 直接验证 | 编译 Koopa、链接运行 | 返回值 33 |

`fp_tests` 中新增了多类边界情况，包括：

- 嵌套 lambda 中引用捕获并写回外层变量。
- `[=]` 与 `[&]` 混合使用。
- 函数指针在普通函数和闭包之间反复赋值。
- lambda 参数中接收函数指针并多次间接调用。
- 递归 lambda 内嵌套回调传递，覆盖 `fp10.cpp` 的核心形态。
- `void` lambda 修改引用捕获变量。
- 立即调用 lambda 表达式同时覆盖值捕获和引用捕获。
- 分支中创建捕获闭包并逃逸到函数指针。
- 零参数闭包回调。
- 名字遮蔽下的捕获。
- 八个捕获值的上限边界。
- 闭包作为多参数回调函数的实参。
- 普通函数指针、无捕获 lambda、带捕获 lambda 的链式重赋值。

### 4.2 支持的功能

当前支持的功能包括：

- `auto f = [](...) -> int { ... };`
- `auto f = [](...) -> void { ... };`
- `[=]` 值捕获和 `[&]` 引用捕获。
- lambda 直接调用和立即调用表达式。
- `auto &self` 风格的递归 lambda。
- `int (*p)(...)` 函数指针变量、形参和赋值。
- 普通函数、无捕获 lambda、带捕获 lambda 混合作为函数指针目标。
- 闭包作为函数指针实参传递。
- 类似 `fp10.cpp` 的递归回调链。

例如以下用例已经支持：

```c
int apply2(int (*f)(int), int a, int b) {
  return f(a) + f(b);
}

int main() {
  int base = 6;
  auto plus = [=](int x) -> int { return x + base; };
  return apply2(plus, 1, 2); // 15
}
```

也支持自递归加引用捕获写回：

```c
int main() {
  int acc = 0;
  auto sum = [&](auto &self, int n) -> int {
    if (!n) return acc;
    acc = acc + n;
    return self(self, n - 1);
  };
  int r = sum(sum, 4);
  return r + acc; // 20
}
```

### 4.3 功能限制

为了让扩展范围与课程项目规模匹配，当前实现不是完整 C++ lambda。明确不支持或只有限支持的内容包括：

- 普通泛型 lambda 参数，如 `[](auto x) { ... }`。当前 `auto` 只用于 `auto &self` 递归参数。
- 显式捕获列表，如 `[x]`、`[&x]`、`[=, &x]`。
- `mutable` lambda。
- 捕获数组、结构体或非 `int` 类型对象。
- 返回 lambda 或保存嵌套闭包对象本身。
- C++ 重载决议、完整类型推导和模板语义。
- 通用函数指针环境表最多保存 8 个捕获值。
- `lambda` 函数体中再调用“后续才输出定义且带函数指针形参的普通函数”仍受 Koopa 文本前向定义限制；测试集中已避免把该限制作为目标功能。

这些限制不会影响标准 SysY 程序，也不会影响当前 `fp10.cpp` 和 `fp_tests` 中纳入设计范围的功能。

## 5. 总结

本项目在保持标准 SysY 回归全通过的基础上，为编译器加入了实用的 C++ 风格 lambda 子集。实现没有改动后端语义模型，而是在 AST 到 Koopa IR 阶段将 lambda、闭包和函数指针统一降低为普通函数、整数编号和环境表，从而以较小侵入性支持了递归 lambda、捕获、回调链和函数指针混合调用。

从测试效果看，标准 SysY 测试 `467/467` 通过，专项 `fp_tests` `81/81` 通过，并且 `fp10.cpp` 及其类似复杂情形已经纳入测试集。整体实现达到了课程项目中“在 SysY 编译器基础上扩展 lambda 与函数指针能力”的目标。
