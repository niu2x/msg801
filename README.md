# msg801

`msg801` 是一个基于 C++20 与 Boost.Asio 的轻量网络工具集，当前包含：

- UDP 发送客户端（`send`）
- UDP 监听服务（`serve`）
- TCP 隧道（`tunnel`），可选 CFB 流加密传输

本文档聚焦“用户快速上手”。
更详细的设计、测试、日志说明请查看 `docs/`。

## 功能特性

- **UDP 单次发送**：快速向目标地址发送 UDP 消息
- **UDP 监听接收**：在指定端口接收并输出 UDP 数据
- **TCP 隧道转发**：本地监听并转发到远端
- **可选 CFB 传输混淆**：通过 `--cfb-key` 启用处理器
- **结构化隧道日志**：JSON 行日志（`conn_new`、`conn_close`、`stat`）

## 环境要求

- C++20 编译器
- CMake >= 3.16
- Boost >= 1.89（`program_options`、`asio` 头文件模式）
- pthreads

项目内置了 `spdlog` 头文件依赖：`thirdparty/spdlog`。

## 构建

```bash
make build
```

可执行文件输出：

- `dist/bin/msg801`

若 Boost 不在系统默认路径，可在 `.env` 中设置：

```bash
Boost_ROOT=/path/to/boost-1.89.0
```

然后重新执行 `make build`。

## 快速开始

### 1）查看帮助

```bash
./dist/bin/msg801 --help
./dist/bin/msg801 tunnel --help
```

### 2）UDP 发送 / 接收

启动监听：

```bash
./dist/bin/msg801 serve 9000
```

发送消息：

```bash
./dist/bin/msg801 send 127.0.0.1 9000 "hello"
```

### 3）TCP 单跳隧道（明文）

将本地 `7777` 转发到远端 `127.0.0.1:8080`：

```bash
./dist/bin/msg801 tunnel --listen 0.0.0.0:7777 --remote 127.0.0.1:8080
```

### 4）TCP 双跳 CFB 隧道

使用两个隧道进程：

- 入口节点（本地方向加密）：

```bash
./dist/bin/msg801 tunnel \
  --listen 0.0.0.0:7000 \
  --remote 10.0.0.2:7001 \
  --cfb-key "your-key"
```

- 出口节点（本地方向解密）：

```bash
./dist/bin/msg801 tunnel \
  --listen 0.0.0.0:7001 \
  --remote 127.0.0.1:8080 \
  --cfb-key "your-key" \
  --cfb-reverse
```

## 命令一览

- `msg801 send <ip> <port> <message>`
- `msg801 serve <port>`
- `msg801 tunnel --listen <ip:port> --remote <ip:port> [--cfb-key <key>] [--cfb-reverse]`

## 日志

隧道通过 spdlog 输出 JSON 行日志：

- `conn_new`：新连接建立
- `conn_close`：连接关闭（含字节计数、时长）
- `stat`：周期速率统计（rx/tx 字节每秒）

字段说明与样例请看 `docs/logging.md`。

## 测试

运行集成测试：

```bash
bash scripts/test_tunnel.sh
```

脚本会覆盖：

- 小/大消息完整性
- 并发客户端
- 快速连断稳定性
- 双跳 CFB 模式
- 客户端流水线发送（不等逐包响应）

详见 `docs/testing.md`。

## 文档索引

- `docs/usage.md`：命令行与部署拓扑
- `docs/testing.md`：测试方案与场景
- `docs/logging.md`：日志事件与字段
- `docs/architecture.md`：内部架构概览
