# 架构概览

## 模块划分

- `cli/`：命令行入口与参数解析（`boost::program_options`）
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
- 可选流处理：`CfbProcessor`

## CFB 处理器模型

`CfbProcessor` 维护方向隔离状态：

- `enc_iv_`：local->remote 加密状态
- `dec_iv_`：remote->local 解密状态

`--cfb-reverse` 会交换角色，以支持双跳场景中的出口节点。

## 错误处理模型

- 远端连接失败：记录错误并继续 accept 循环
- 关闭套接字使用共享 `closed` 标志，避免重复关闭竞态
- 单方向循环出错：结束该方向并触发会话收敛关闭
