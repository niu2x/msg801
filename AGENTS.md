# msg801

- C++20
- 依赖 Boost >= 1.89 (program_options, asio header-only) 和 spdlog (header-only)
- 提交日志必须用中文

## 命名

- **类 / 类型别名**: PascalCase（`MyClass`、`MyType`）
- **枚举常量**: UPPER_SNAKE_CASE（`VALUE_ONE`、`ANOTHER_VALUE`）
- **常量（`constexpr` / `const`，文件或命名空间作用域）**: UPPER_SNAKE_CASE（`SOME_CONSTANT`）
- **函数 / 变量**: snake_case（`do_something`、`some_variable`）
- **非公开成员**: snake_case + 下划线后缀（`data_`、`count_`）
- **Getter**: 必须用 `get_xxx()` 前缀，不能用裸名词或属性名。例：`get_buffer()`、`get_width()`。错误：`buffer()`、`width()`。
- **布尔 Getter**: 必须用 `is_xxx()` 前缀。例：`is_ready()`、`is_valid()`。错误：`ready()`、`valid()`。
- **Setter**: 必须用 `set_xxx()` 前缀。例：`set_name()`、`set_value()`。错误：`name()`、`value()`。