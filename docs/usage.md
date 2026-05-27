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

### `udp send`（发送）

```bash
msg801 udp send <ip> <port> <message>
```

示例：

```bash
msg801 udp send 127.0.0.1 9000 "hello"
```

### `udp serve`（监听）

```bash
msg801 udp serve <port>
```

示例：

```bash
msg801 udp serve 9000
```

## TCP 隧道命令

```bash
msg801 tunnel --listen <ip:port> --remote <ip:port> [--processor <spec>]...
```

参数说明：

- `--listen`：本地监听地址
- `--remote`：远端转发地址
- `--processor`：处理器定义（可重复，按出现顺序构建 pipeline）
- `--reverse`：是否将当前节点设为反向角色（可选，`0|1`，默认 `0`）

`--processor` 格式：

```text
name[:k=v,k=v,...]
```

当前内置处理器：

- `identity`
- `cfb:key=<str>`
- `cfb_nonce:iv=<str>,hmac_key=<str>`
- `padding:chunk=<N>,max=<M>[,seed=<u64>]`

### 各处理器参数说明

#### `identity`

- 无参数
- 行为：原样透传，不做任何变换

#### `cfb`

格式：`cfb:key=<str>`

- `key`：CFB 处理器密钥（必填）
  - 两端必须一致，否则无法正确还原
- 角色方向由全局 `--reverse` 控制：
  - `0`：入口常规角色（local->remote 加密，remote->local 解密）
  - `1`：出口反向角色（local->remote 解密，remote->local 加密）

#### `padding`

格式：`padding:chunk=<N>,max=<M>[,seed=<u64>]`

- `chunk`：每帧最大业务数据字节数（必填）
  - 业务流会按该值分段后再加随机填充
- `max`：每帧随机填充上限（必填）
  - 每帧实际填充范围为 `0..max`
- `seed`：随机种子（可选）
  - 不配置时，程序会自动使用“时间 + 随机设备”生成种子（推荐）
  - 仅在你需要可复现实验时才建议手动固定 `seed`
- 角色方向由全局 `--reverse` 控制：
  - `0`：入口常规角色（local 编帧加填充，remote 解帧去填充）
  - `1`：出口反向角色（local 解帧去填充，remote 编帧加填充）

#### `cfb_nonce`

格式：`cfb_nonce:iv=<str>,hmac_key=<str>`

- `iv`：基础 IV 字节串（必填）
  - 仅用于与每连接随机 nonce 拼接后做 SHA-512，派生实际 CFB IV
- `hmac_key`：握手认证密钥（必填）
  - 用于握手认证：`tag = hmac_sha256(nonce, hmac_key)`
- 角色方向由全局 `--reverse` 控制：
  - `0`：入口常规角色（local->remote 加密，remote->local 解密）
  - `1`：出口反向角色（local->remote 解密，remote->local 加密）

握手行为（每连接一次）：

- 首次 `on_local_data` 前会先发送固定长度握手包：`tag(32B) + nonce(512B)`，其中 `tag = hmac_sha256(nonce, hmac_key)`
- 然后用 `sha512(nonce || iv)` 作为该方向的真实 IV，继续按 CFB 逻辑处理业务流
- 首次 `on_remote_data` 会先消费并校验对端握手包，HMAC 不合法则直接断开连接

### pipeline 顺序规则

- 同一个节点内，`--processor` 按出现顺序构成 pipeline
- 数据从本地发往远端时，pipeline 按添加顺序执行（`on_local_data`）
- 数据从远端回本地时，pipeline 按添加顺序的**逆序**执行（`on_remote_data`）
- 双节点组合时，出口节点只需设置 `--reverse=1`，框架会自动把处理器顺序反转

```
例：入口 A（--processor "cfb" --processor "padding"）
   on_local_data:  cfb.encrypt → padding.encode    （发送到 B）
   on_remote_data: padding.decode → cfb.decrypt    （从 B 接收后解码）
```

### 关于 `padding seed` 是否需要两端一致

- 当前解码依赖帧头中的 `payload_len` / `pad_len`，不依赖随机数序列
- 因此 `seed` 不要求两端一致也能正确解码
- `seed` 主要用于控制“填充分布”的可复现性

## 部署拓扑

### 单跳明文隧道

```bash
msg801 tunnel --listen 0.0.0.0:7000 --remote 127.0.0.1:8080
```

### 双跳 CFB + Padding 隧道

入口节点（A）：

```bash
msg801 tunnel \
  --listen 0.0.0.0:7000 \
  --remote 10.0.0.2:7001 \
  --processor "padding:chunk=1024,max=64,seed=42" \
  --processor "cfb:key=your-key"
```

出口节点（B）：

```bash
msg801 tunnel \
  --listen 0.0.0.0:7001 \
  --remote 127.0.0.1:8080 \
  --processor "padding:chunk=1024,max=64,seed=42" \
  --processor "cfb:key=your-key" \
  --reverse=1

注意：双节点组合时，出口节点无需手动调整处理器顺序，`--reverse=1` 会自动完成顺序反转与角色反转。
```
