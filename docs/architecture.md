# 架构概览

## 模块划分

- `cli/`：命令行入口与参数解析（`CLI11`）
- `lib/`：网络运行时实现（UDP 发送/监听、TCP 隧道）
- `scripts/`：集成测试脚本
- `thirdparty/spdlog/`：内置头文件日志依赖

## 隧道运行时

核心实现位于 `lib/src/tunnel.cpp`。

每个 TCP 新连接的处理流程：

1. 创建会话状态（`SessionState`）
2. 建立远端连接
3. 启动两个异步循环
   - local -> remote
   - remote -> local
4. 可选启动周期 `stat` 输出循环

## 处理器链

隧道数据路径支持可插拔处理器：

- 接口：`msg801::tunnel::Processor`
- 组合器：`msg801::tunnel::ProcessorChain`
- 默认透传：`IdentityProcessor`
- 可选流处理：`CfbProcessor`、`PaddingProcessor`

Processor 的装配由 CLI `--processor` 参数驱动，按声明顺序加入 `ProcessorChain`。

### Pipeline 方向规则

`ProcessorChain` 的 apply 方向：

- `on_local_data` / `flush_local`：按添加顺序（正向）依次调用各处理器
- `on_remote_data` / `flush_remote`：按添加顺序的**逆序**依次调用各处理器

```
例：--processor "cfb" --processor "padding"
   on_local_data:  cfb → padding
   on_remote_data: padding → cfb
```

数据从本地发往远端时经正向 pipeline 编码；从远端回本地时经逆序 pipeline 解码，
保证编码与解码操作完全对称。

## CFB 处理器模型

`CfbProcessor` 维护方向隔离状态：

- `enc_iv_`：local->remote 加密状态
- `dec_iv_`：remote->local 解密状态
- `enc_offset_` / `dec_offset_`：各方向已处理字节计数器

IV 索引使用全局偏移（`(offset + i) % iv.size()`）而非单次调用的局部位置，
确保数据分多次送入 `on_local_data` / `on_remote_data` 时 IV 位置不重置。

`--reverse=1` 会交换角色，并自动将当前节点 processor 顺序反转，以支持双跳场景中的出口节点。

## Padding 处理器模型

`PaddingProcessor` 使用分帧随机填充：

- 帧头：`payload_len` + `pad_len`（8 字节）
- 帧体：`payload` + 随机 `padding`

参数：

- `chunk`：每帧最大业务负载
- `max`：每帧最大随机填充字节数
- `seed`：随机种子（可选；不配置时使用时间源自动生成）
- 反向角色由全局 `--reverse=1` 控制（同时自动反转 processor 顺序）

说明：解码按帧头 `payload_len` / `pad_len` 进行，不依赖双方 seed 一致。

## 错误处理模型

- 远端连接失败：记录错误并继续 accept 循环
- 关闭套接字使用共享 `closed` 标志，避免重复关闭竞态
- 单方向循环出错：结束该方向并触发会话收敛关闭
