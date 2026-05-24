# 使用指南

## 可执行文件

构建后使用：

```bash
./dist/bin/msg801
```

## 全局参数

```bash
msg801 --help
msg801 --about
```

## UDP 命令

### `send`（发送）

```bash
msg801 send <ip> <port> <message>
```

示例：

```bash
msg801 send 127.0.0.1 9000 "hello"
```

### `serve`（监听）

```bash
msg801 serve <port>
```

示例：

```bash
msg801 serve 9000
```

## TCP 隧道命令

```bash
msg801 tunnel --listen <ip:port> --remote <ip:port> [--cfb-key <key>] [--cfb-reverse]
```

参数说明：

- `--listen`：本地监听地址
- `--remote`：远端转发地址
- `--cfb-key`：启用 CFB 处理器密钥
- `--cfb-reverse`：反转加解密角色（出口节点使用）

## 部署拓扑

### 单跳明文隧道

```bash
msg801 tunnel --listen 0.0.0.0:7000 --remote 127.0.0.1:8080
```

### 双跳 CFB 隧道

入口节点（A）：

```bash
msg801 tunnel \
  --listen 0.0.0.0:7000 \
  --remote 10.0.0.2:7001 \
  --cfb-key "your-key"
```

出口节点（B）：

```bash
msg801 tunnel \
  --listen 0.0.0.0:7001 \
  --remote 127.0.0.1:8080 \
  --cfb-key "your-key" \
  --cfb-reverse
```
