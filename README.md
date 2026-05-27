# msg801

`msg801` 是一个基于 C++20 与 Boost.Asio 的轻量网络工具集，当前包含：

- UDP 发送客户端（`udp send`）
- UDP 监听服务（`udp serve`）
- TCP 隧道（`tunnel`），支持可配置 Processor Pipeline

本文档聚焦“用户快速上手”。
更详细的设计、测试、日志说明请查看 `docs/`。

## 功能特性

- **UDP 单次发送**：快速向目标地址发送 UDP 消息
- **UDP 监听接收**：在指定端口接收并输出 UDP 数据
- **TCP 隧道转发**：本地监听并转发到远端
- **可扩展处理器链**：通过重复 `--processor` 按顺序组装处理链
- **内置处理器**：`identity`、`cfb`、`padding`（分帧随机填充）
- **结构化隧道日志**：JSON 行日志（`conn_new`、`conn_close`、`stat`）

## 环境要求

- C++20 编译器
- CMake >= 3.16
- Boost >= 1.83（`program_options`、`asio` 头文件模式）
- OpenSSL >= 1.1
- pthreads

项目内置了 `spdlog` 头文件依赖：`thirdparty/spdlog`。

## 构建

```bash
make build
```

可执行文件输出：

- `dist/bin/msg801`

默认构建为“单文件发布”模式：`msg801` 会静态链接项目内 `msg801` 库，不再额外安装 `libmsg801.so`。
如需恢复动态库构建，可在配置时显式开启：

```bash
cmake -B build -DMSG801_BUILD_SHARED=ON
cmake --build build -j
cmake --install build --prefix dist
```

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
./dist/bin/msg801 udp serve 9000
```

发送消息：

```bash
./dist/bin/msg801 udp send 127.0.0.1 9000 "hello"
```

### 3）TCP 单跳隧道（明文）

将本地 `7777` 转发到远端 `127.0.0.1:8080`：

```bash
./dist/bin/msg801 tunnel --listen 0.0.0.0:7777 --remote 127.0.0.1:8080
```

### 4）TCP 双跳 Pipeline 隧道（CFB + Padding）

使用两个隧道进程：

- 入口节点（本地方向加密）：

```bash
./dist/bin/msg801 tunnel \
  --listen 0.0.0.0:7000 \
  --remote 10.0.0.2:7001 \
  --processor "padding:chunk=1024,max=64,seed=42" \
  --processor "cfb:key=your-key"
```

- 出口节点（本地方向解密）：

```bash
./dist/bin/msg801 tunnel \
  --listen 0.0.0.0:7001 \
  --remote 127.0.0.1:8080 \
  --processor "padding:chunk=1024,max=64,seed=42" \
  --processor "cfb:key=your-key" \
  --reverse=1
```

### 5）Supervisor 配置示例

将入口/出口节点托管到 supervisord：

```ini
[program:msg801-tunnel-a]
command=msg801 tunnel
    --listen 0.0.0.0:7000
    --remote 10.0.0.2:7001
    --processor "padding:chunk=1024,max=64"
    --processor "cfb:key=your-key"
directory=%(ENV_HOME)s/project/msg801
autorestart=true
stderr_logfile=/var/log/msg801/a-err.log
stdout_logfile=/var/log/msg801/a-out.log

[program:msg801-tunnel-b]
command=msg801 tunnel
    --listen 0.0.0.0:7001
    --remote 127.0.0.1:8080
    --processor "padding:chunk=1024,max=64"
    --processor "cfb:key=your-key"
    --reverse=1
directory=%(ENV_HOME)s/project/msg801
autorestart=true
stderr_logfile=/var/log/msg801/b-err.log
stdout_logfile=/var/log/msg801/b-out.log
```

## 命令一览

- `msg801 udp send <ip> <port> <message>`
- `msg801 udp serve <port>`
- `msg801 tunnel --listen <ip:port> --remote <ip:port> [--processor <spec>]... [--reverse=0|1]`

常用 `--processor` 示例：

- `identity`
- `cfb:key=secret`
- `cfb_nonce:iv=seed-iv,hmac_key=shared-secret`
- `padding:chunk=1024,max=64,seed=42`

`--reverse=1` 会对当前节点的全部处理器启用反向角色，并自动将处理器顺序反转。

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
- 双跳 Padding 模式
- 双跳 CFB + Padding 组合模式
- 客户端流水线发送（不等逐包响应）

详见 `docs/testing.md`。

## 文档索引

- `docs/usage.md`：命令行与部署拓扑
- `docs/testing.md`：测试方案与场景
- `docs/logging.md`：日志事件与字段
- `docs/architecture.md`：内部架构概览
