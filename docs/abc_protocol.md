# IDR Protocol — ID-matching Relay Protocol

## 概述

IDR 是一种三主机内网穿透协议。三端角色命名为 **A（匹配器）**、**B（访客）**、**C（提供者）**，通过 ID 实现自动配对和数据桥接。

## 角色

| 角色 | 名称 | 职责 |
|---|---|---|
| **A** | Matcher（匹配器） | 公网中继，负责 ID 匹配、会话路由、连接生命周期管理 |
| **B** | Visitor（访客） | 监听本地端口，桥接多个外部客户端到隧道 |
| **C** | Provider（提供者） | 保持到 A 的长连接，桥接隧道到目标内网服务 |

## 连接模型

B 和 C 各自持有一条到 A 的**持久连接**，所有控制消息和数据都在这条连接上传输。多个外部客户端通过 uuid 多路复用 B↔A↔C 的链路。

```
                   ┌───────┐
    ┌──────────────│   A   │──────────────┐
    │              └───────┘              │
    │  persistent                          │  persistent
    │  (all traffic)                      │  (all traffic)
    ▼                                     ▼
┌───────┐                             ┌───────┐
│   B   │                             │   C   │
└───────┘                             └───────┘
    │                                     │
    │ raw TCP × n                        │ raw TCP × n
    ▼                                     ▼
 Clients                           Remote Services
```

## 帧格式

所有消息统一使用 length-prefixed 帧：

```
┌─────────────────┬──────────┬──────────────────────┐
│  4 bytes        │  1 byte  │  body (variable)     │
│  Big-endian     │  method  │  method 参数         │
│  total length*  │          │                       │
└─────────────────┴──────────┴──────────────────────┘
```

- total length = 1 (method) + len(body)，即整个 payload 的长度

## Method 表

| 值 | 名称 | 方向 | Body 格式 |
|---|---|---|---|
| `0x01` | REGISTER | B/C → A | `<id> <role>`（UTF-8 文本，空格分隔） |
| `0x02` | READY | A → B/C | `MATCHED`（UTF-8 文本） |
| `0x03` | NEW_SESSION | B → A | `<uuid:36>` |
| `0x04` | FORWARD_SESSION | A → C | `<uuid:36>` |
| `0x05` | SESSION_OK | C → A → B | `<uuid>`（36 字节 ASCII UUID） |
| `0x06` | SESSION_ERR | C → A → B | `<uuid>`（36 字节 ASCII UUID） |
| `0x10` | DATA | 双向 | `<uuid:36>` + `<raw bytes>` |
| `0x11` | DATA_EOF | 双向 | `<uuid:36>`（该方向写半关闭） |
| `0x12` | SESSION_CLOSE | 双向 | `<uuid:36>`（立即全关，不等反向 DATA_EOF） |
| `0xFF` | EOF | 双向 | 空（连接关闭） |

## 会话生命周期

B 在发出 NEW_SESSION 后**不等待** SESSION_OK，立即开始发送 DATA 帧。C 侧在 remote 连接建立前到达的 DATA 帧暂存于 per-uuid arrival buffer，连接建立后 drain。

```
B                          A                          C
│ persistent conn          │ persistent conn          │ persistent conn
│                          │                          │
├── REGISTER → id,B ──────►│                          │
│                          │◄── REGISTER → id,C ──────┤
│                          ├── 匹配成功 ─┤            │
│◄────── READY → MATCHED ──┤                          │
│                          ├────── READY → MATCHED ──►│
│                          │                          │
│ ▸ client1 connect        │                          │
│ ▸ 生成 uuid1             │                          │
├── NEW_SESSION → u1 ─────►│                          │
│                          ├── FORWARD_SESSION → u1 ─►│
│                          │                          │ ▸ 创建 arrival buffer[u1]
│ ▸ data from client1      │                          │
├── DATA → u1 + raw ──────►├── DATA → u1 + raw ─────►│
│                          │                          │ ▸ 入 buffer[u1]
│ ▸ more data from client1 │                          │
├── DATA → u1 + raw ──────►├── DATA → u1 + raw ─────►│
│                          │                          │ ▸ 入 buffer[u1]
│                          │                          │ ▸ connect remote1...
│                          │                          │ ▸ 成功 →
│                          │◄─── SESSION_OK → u1 ─────┤ ▸ drain buffer[u1]
│◄─── SESSION_OK → u1 ─────┤                          │ ▸ data 发往 remote
│                          │                          │
│ ▸ client2 connect        │                          │
│ ▸ 生成 uuid2             │                          │
├── NEW_SESSION → u2 ─────►│                          │
│                          ├── FORWARD_SESSION → u2 ─►│
│ ▸ data from client2      │                          │ ▸ 创建 buffer[u2]
├── DATA → u2 + raw ──────►├── DATA → u2 + raw ─────►│
│                          │                          │ ▸ 入 buffer[u2]
│                          │                          │ ▸ connect remote2...
│                          │                          │ ▸ 失败 →
│                          │◄─── SESSION_ERR → u2 ────┤ ▸ discard buffer[u2]
│◄─── SESSION_ERR → u2 ────┤                          │
│ ▸ close client2          │                          │
│                          │                          │
│                          │◄── DATA → u1 + raw ─────┤ ◄─ raw from remote1
│◄── DATA → u1 + raw ──────┤                          │
│ ▸ raw write to client1   │                          │
│                          │                          │
│ ▸ client shutdown_wr     │                          │
│   (half-close)           │                          │
├── DATA_EOF → u1 ────────►├── DATA_EOF → u1 ───────►│
│                          │                          │ ▸ remote shutdown_send
│                          │                          │
│  (still reading)         │                          │ ▸ remote 还有响应
│                          │◄── DATA → u1 + res ─────┤
│◄── DATA → u1 + res ──────┤                          │
│ ▸ write to client        │                          │
│                          │                          │
│                          │                          │ ▸ remote shutdown_wr
│                          │                          │   (half-close)
│                          │◄── DATA_EOF → u1 ────────┤
│◄── DATA_EOF → u1 ────────┤                          │
│ ▸ client shutdown_send   │                          │
│                          │                          │
│ ▸ 两端 DATA_EOF 到齐     │                          │
│ ▸ cleanup uuid1          │                          │ ▸ cleanup uuid1
```

## C 侧 arrival buffer

C 在收到 FORWARD_SESSION 时创建 per-uuid arrival buffer，用于暂存在 remote 连接建立前到达的 DATA 帧。

```
FORWARD_SESSION(uuid)
  → 创建 buffer[uuid] = []
  → 开始异步 connect remote

DATA(uuid, payload) 到达:
  if remote 尚未连接 →
    buffer[uuid].push(payload)
  else →
    直接写入 remote socket

remote 连接完成:
  SESSION_OK → A → B
  drain buffer[uuid]: 依次写入 remote socket
  此后该 uuid 的 DATA 直接写入 remote

remote 连接失败:
  SESSION_ERR → A → B
  discard buffer[uuid]
  B 收到 SESSION_ERR 后关闭对应 client
```

## 数据路由

- A 维护 `(B_conn, C_conn)` 配对映射
- 收到来自 B 的帧 → 查找配对的 C → 原样转发（method + body 不变）
- 收到来自 C 的帧 → 查找配对的 B → 原样转发
- B 和 C 本端根据 uuid 将 DATA/DATA_EOF 关联到具体的 client/remote socket

## 多路复用

- **B 多客户端**：一条长连接上通过 uuid 区分不同外部客户端，帧交错传输
- **A 多组配对**：持有多个 `(B_conn, C_conn)`，使用不同 id，互不干扰

## Half-shutdown（半关闭）

DATA_EOF 是**单向**语义，只关闭该方向的数据写入。另一方仍可继续向回发送数据。

### DATA_EOF（半关闭）

本端从 client/remote 读到 EOF（正常半关）时发送，表示该方向不再写数据，但继续收。

- 收到 `DATA_EOF(uuid)` → 对 client/remote 做 `shutdown_send`
- 两端 `DATA_EOF` 到齐后 cleanup

### SESSION_CLOSE（全关闭）

**本端对 client/remote 写 error** 时发送，表示本端已无法继续转发，对端不必再等反向 DATA_EOF。

- 收到 `SESSION_CLOSE(uuid)` → 立即关 remote/client、cleanup uuid
- 本端：flush 完数据缓存后发 `SESSION_CLOSE`，等对端 `SESSION_CLOSE` 到齐或超时后 cleanup

### 会话清理时机

一个 uuid 的会话在**两端 DATA_EOF 都到齐**后才清理。只收到一个方向的 EOF 时保持半开状态，另一方继续传输。

```
client                    B                         A                         C                  remote
  │                       │                         │                         │                     │
  │  shutdown_wr          │                         │                         │                     │
  ├──── FIN ─────────────►│ read EOF                │                         │                     │
  │                       ├── DATA_EOF(uuid) ──────►├── DATA_EOF(uuid) ─────►│                     │
  │                       │                         │                         ├── shutdown_send ──►│
  │                       │                         │                         │                     │
  │  (still reading)      │                         │                         │  ▸ response         │
  │◄───── resp ───────────┤◄── DATA(uuid,data) ─────┤◄── DATA(uuid,data) ────┤◄────────────────────│
  │                       │                         │                         │                     │
  │                       │                         │                         │  ▸ remote half-close│
  │                       │                         │                         │◄──── FIN ───────────┤
  │                       │                         │                         │  read EOF            │
  │                       │◄─── DATA_EOF(uuid) ─────┤◄─── DATA_EOF(uuid) ────┤                     │
  ├── shutdown_send ─────►│                         │                         │                     │
  │                       │                         │                         │                     │
  │  ▸ 两端 DATA_EOF 到齐 → cleanup(uuid)            │                         │ cleanup(uuid)       │
```

## 断开处理

### 连接级

| 事件 | 行为 |
|---|---|
| TCP 链路 | 断开方 | 对方行为 |
|---|---|---|---|
| **B ↔ A**（持久连接） | B 断开 | A 向 C 发 EOF(`0xFF`) 并关 TCP；C 收到后关所有 remote，清理 |
| | A 断开 | B 关所有 client，退出或重连 |
| **C ↔ A**（持久连接） | C 断开 | A 向 B 发 EOF(`0xFF`) 并关 TCP；B 收到后关所有 client，退出或重连 |
| | A 断开 | C 关所有 remote，退出或重连 |
| **B ↔ client**（会话级） | client 断开（read EOF 或 write error） | B 发 `DATA_EOF(uuid)` → A → C → C 对 remote `shutdown_send` |
| **C ↔ remote**（会话级） | remote 断开（read EOF 或 write error） | C 发 `DATA_EOF(uuid)` → A → B → B 对 client `shutdown_send` |
| **B → A 写 error**（持久连接） | B 写 A 失败 | B↔A 已断，B 关所有 client，退出或重连 |
| **C → A 写 error**（持久连接） | C 写 A 失败 | C↔A 已断，C 关所有 remote，退出或重连 |

### 会话级（per-uuid）

| 事件 | 行为 |
|---|---|
| 本端读到 client/remote EOF（半关） | 发 `DATA_EOF(uuid)` + `shutdown_send(client/remote)` |
| 收到 `DATA_EOF(uuid)` | `shutdown_send(client/remote)` |
| 本端写 client/remote error（全关） | flush 缓存 → 发 `SESSION_CLOSE(uuid)` |
| 收到 `SESSION_CLOSE(uuid)` | 立即关 client/remote，`cleanup(uuid)` |
| 两端 `DATA_EOF` 到齐 | `cleanup(uuid)` |
| 收到 `SESSION_CLOSE` | 关对应 socket，`cleanup(uuid)` |
