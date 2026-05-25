# msg801

- C++20
- 依赖 Boost >= 1.89 (program_options, asio header-only) 和 spdlog (header-only)
- 提交日志必须用中文
- 不要自动 commit，只在用户明确说 `/commit` 时才 commit

## 命名

- **类 / 类型别名**: PascalCase（`MyClass`、`MyType`）
- **枚举常量**: UPPER_SNAKE_CASE（`VALUE_ONE`、`ANOTHER_VALUE`）
- **常量（`constexpr` / `const`，文件或命名空间作用域）**: UPPER_SNAKE_CASE（`SOME_CONSTANT`）
- **函数 / 变量**: snake_case（`do_something`、`some_variable`）
- **非公开成员**: snake_case + 下划线后缀（`data_`、`count_`）
- **Getter**: 必须用 `get_xxx()` 前缀，不能用裸名词或属性名。例：`get_buffer()`、`get_width()`。错误：`buffer()`、`width()`。
- **布尔 Getter**: 必须用 `is_xxx()` 前缀。例：`is_ready()`、`is_valid()`。错误：`ready()`、`valid()`。
- **Setter**: 必须用 `set_xxx()` 前缀。例：`set_name()`、`set_value()`。错误：`name()`、`value()`。
- **头文件**: 声明仅包含接口签名和短小的内联函数（如 getter/setter/简单工厂）。多行函数体必须放在 `lib/src/*.cpp` 中。
- **头文件顺序**: `#include` 应按以下分组依次排列，组间空行分隔：
    1. 标准 C 库（`<cstdint>`、`<cstdlib>` 等）
    2. 标准 C++ 库（`<vector>`、`<string>`、`<memory>` 等）
    3. 第三方库（`<boost/asio.hpp>`、`<spdlog/spdlog.h>` 等）
    4. 项目自己的头文件（`"msg801/tunnel.hpp"` 等）
- **Public 头文件引用**: `lib/include/msg801/` 下的头文件中，项目内部 `#include` 使用 `<>` 而非 `""`。例：`#include <msg801/export.hpp>`。