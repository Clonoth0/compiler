# SysY 编译器 — Lambda 与函数指针扩展

在标准 SysY 语言基础上，扩展了 Lambda 表达式、函数指针、闭包捕获等特性，使其具备一等函数（first-class function）能力。

编译器基于 [sysy-make-template](https://github.com/pku-minic/sysy-make-template)，前端使用 Flex + Bison 构建 AST，中端输出 Koopa IR 文本，后端通过 libkoopa 解析 IR 并生成 RISC-V RV32IM 汇编。

## 语言扩展

| 扩展项          | 语法                   | 示例                                              |
| --------------- | ---------------------- | ------------------------------------------------- |
| Lambda 表达式   | `[]` / `[=]` / `[&]`   | `auto f = [=](int x) -> int { return x + n; }`    |
| 递归 Lambda     | `auto &self`           | `auto fib = [](auto &self, int n) -> int { ... }` |
| 函数指针类型    | `int (*p)(int)`        | `int (*p)(int, int);`                             |
| 函数指针参数    | `int (*f)(int)`        | `void apply(int (*f)(int), int x);`               |
| 立即调用 Lambda | IIFE                   | `return [&](int x)->int { ... }(x);`              |

## 构建

在 [compiler-dev](https://github.com/pku-minic/compiler-dev) Docker 环境内：

```sh
make
```

或指定输出目录：

```sh
make BUILD_DIR=build LIB_DIR=/opt/lib/native INC_DIR=/opt/include
```

生成的可执行文件为 `build/compiler`。

## 使用

```sh
# 输出 Koopa IR
./compiler -koopa input.sy -o output.koopa

# 输出 RISC-V RV32IM 汇编
./compiler -riscv input.sy -o output.S

# 性能测试模式（输出 RISC-V）
./compiler -perf input.sy -o output.S
```

## 运行测试

```sh
# Lambda/函数指针专项测试（fp_tests 目录）
docker run --rm -v $(pwd):/root/compiler maxxing/compiler-dev \
  autotest -riscv -t /root/compiler/fp_tests /root/compiler

# 标准 SysY 测试
docker run --rm -v $(pwd):/root/compiler maxxing/compiler-dev \
  autotest -riscv /root/compiler
```

## 系统架构

```
源程序 (.sy)
    │
    ▼
┌─────────────────┐
│ Flex 词法分析    │
└─────────────────┘
    │
    ▼
┌─────────────────┐
│ Bison 语法分析   │  → AST
└─────────────────┘
    │
    ▼
┌─────────────────┐
│ 多 Pass 中间处理  │  预注册 → 捕获分析 → 拓扑排序 → func_id 重分配
└─────────────────┘
    │
    ▼
┌─────────────────┐
│ Koopa IR 生成    │  全局数据 → __fp_* stub → lambda 函数体
└─────────────────┘
    │
    ▼
┌─────────────────┐
│ RISC-V 代码生成  │  libkoopa 解析 → LSRA 寄存器分配 → 指令输出
└─────────────────┘
    │
    ▼
RISC-V 汇编 (RV32IM)
```

Lambda 降低通过函数编号（func_id）和闭包环境表（`__fp_env`）将闭包改写成普通 Koopa 函数。函数指针调用在 Koopa IR 层通过 `@__fp_N_*` 占位函数完成，实际分派在 RISC-V 后端由 `__fp_table` 跳转表实现。

## 测试结果

| 测试集                    | 结果                       |
| ------------------------- | -------------------------- |
| `SysY educg` 在线测试     | **lv1-lv9 + final 均通过** |
| `fp_tests`（lambda 专项） | **均通过**                 |

## 功能支持

| 功能                                | 状态       |
| ----------------------------------- | ---------- |
| 标准 SysY 程序                      | ✅ 完全兼容 |
| 无捕获 lambda                       | ✅          |
| `[=]` 值捕获 lambda                 | ✅          |
| `[&]` 引用捕获 lambda               | ✅          |
| `auto &self` 递归 lambda            | ✅          |
| 函数指针变量、形参、赋值            | ✅          |
| lambda 作为函数指针实参             | ✅          |
| 闭包逃逸（lambda 通过 FP 形参传递） | ✅          |
| IIFE（立即调用 lambda）             | ✅          |
| 递归回调链                          | ✅          |
| 多函数指针递归                      | ✅          |
| 混合值/引用捕获                     | ✅          |
| void lambda + 引用写回              | ✅          |
| 多级嵌套捕获                        | ✅          |
| 动态计算捕获上限                    | ✅          |
| 名称遮蔽下的捕获                    | ✅          |
| 分支中创建闭包并逃逸                | ✅          |

## 限制

- 不支持显式捕获列表（如 `[x, &y]`）
- 不支持 `mutable` lambda
- `auto` 仅用于 `auto &self` 语法糖
- 不支持捕获数组/结构体
