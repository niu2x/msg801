# 隧道日志说明

隧道运行时通过 spdlog 输出 JSON 行日志。

## 事件：`conn_new`

字段：

- `t`：毫秒级 Unix 时间戳
- `ev`：固定为 `"conn_new"`
- `id`：会话 ID
- `src`：客户端端点
- `dst`：远端端点

示例：

```json
{"t":1779629345285,"ev":"conn_new","id":2,"src":"127.0.0.1:35236","dst":"127.0.0.1:19998"}
```

## 事件：`conn_close`

字段：

- `t`：毫秒级 Unix 时间戳
- `ev`：固定为 `"conn_close"`
- `id`：会话 ID
- `rx`：写向本地侧的字节数
- `tx`：写向远端侧的字节数
- `dur`：会话持续时间（秒）

示例：

```json
{"t":1779629345393,"ev":"conn_close","id":2,"rx":13656,"tx":13656,"dur":0.000601}
```

## 事件：`stat`

按会话周期输出速率统计（当前间隔 5 秒）：

- `t`：毫秒级 Unix 时间戳
- `ev`：固定为 `"stat"`
- `id`：会话 ID
- `rx_rate`：写向本地侧的字节速率（bytes/sec）
- `tx_rate`：写向远端侧的字节速率（bytes/sec）

示例：

```json
{"t":1779624292977,"ev":"stat","id":0,"rx_rate":3,"tx_rate":3}
```
