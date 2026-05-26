# msg801

- C++20, 依赖 Boost >= 1.83 和 spdlog (header-only)
- 提交日志必须用中文
- 只在用户明确说 `/commit` 时才 commit

## 构建与测试

```bash
make build          # cmake configure + build + install to dist/
bash scripts/test_tunnel.sh   # 全部集成测试（31 项）
```

Boost 路径通过 `.env` 中 `Boost_ROOT` 指定，`Makefile` 自动加载。`.env` 示例：

```
Boost_ROOT=/path/to/boost-1.83.0
```

## 项目结构

| 路径 | 说明 |
|---|---|
| `lib/src/*.cpp` / `lib/include/msg801/` | 共享库 `libmsg801.so`，所有业务逻辑 |
| `cli/src/main.cpp` | 可执行入口（链接 libmsg801.so + Boost.program_options） |
| `scripts/test_tunnel.sh` | 集成测试（单跳、双跳 CFB/Padding/组合） |
| `cmake/` | C++20 preset, warning-as-error, config template |

## 风格规范

- **类 / 类型别名**: PascalCase
- **枚举常量**: UPPER_SNAKE_CASE
- **常量（constexpr/const，文件或命名空间作用域）**: UPPER_SNAKE_CASE
- **函数 / 变量**: snake_case
- **非公开成员**: snake_case + 下划线后缀（`data_`、`count_`）
- **Getter**: `get_xxx()` 前缀（如 `get_buffer()`）
- **布尔 Getter**: `is_xxx()` 前缀（如 `is_ready()`）
- **Setter**: `set_xxx()` 前缀（如 `set_name()`）
- **头文件**: 声明仅含接口签名和短小内联函数（getter/setter/简单工厂），多行函数体放 `lib/src/*.cpp`
- **#include 顺序**: 标准 C → 标准 C++ → 第三方 → 项目，组间空行
- **Public 头文件引用**: `lib/include/msg801/` 下的头文件中，项目内 `#include` 用 `<>` 而非 `""`
- **代码格式化**: 编辑后运行 `clang-format -i <file>` 保持风格一致（配置见 `.clang-format`）
