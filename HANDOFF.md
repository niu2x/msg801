# Handoff — msg801

## 最近变更

### 1. CLI 命令重组
- `send` / `serve` → `udp send` / `udp serve`（嵌套在 `udp` 父命令下）
- 文档已同步更新

### 2. CfbNonceProcessor 重构（关键变更）
移除 handshake 协议，改为确定性 IV 推导：
- `enc_iv_ = dec_iv_ = SHA-512(base_iv)` 在构造时计算
- 不再发送 544 字节 handshake（tag + nonce）
- 不再需要 HMAC 认证（`hmac_key` 参数保留但忽略）
- `encrypt`/`decrypt` 实现保持不变（CFB 反馈模式正确：`cipher = plaintext XOR ks → mix(cipher)`）

**原因**：原 handshake 在 wire 上发原始字节，与 `padding` 组合时 padding 排在前面会导致 handshake 不被 padding 帧包裹，远端 padding decode 吃到 handshake 乱码报错。

### 3. Processor 顺序校验
`build_processor_chain()` 中新增校验：
- `cfb` / `cfb_nonce` 必须出现在 `padding` **之前**
- 违反时日志输出 `"processor X must be placed before padding"` 并返回 `nullopt`

## 已知问题

1. **Tunnel 在校验失败时不停止 listen**
   `handle_session` 中 `build_processor_chain()` 返回 `nullopt` 时只 `co_return` 当前协程，listen 循环继续。下一个 session 又会触发同样的错误。期望行为：log 后直接退出进程或停止 listen。

2. **AGENTS.md 测试数量未更新**
   当前测试从原来的 31 项增加到 49 项（含新增的 padding+cfb_nonce 组合测试和顺序校验测试），AGENTS.md 中仍写着 31 项。

3. **processor 顺序文档**
   `docs/usage.md` 中 pipeline 顺序部分应补充说明：加密类 processor（cfb/cfb_nonce）必须放在 padding 之前。

## 构建与测试

```bash
make build
bash scripts/test_tunnel.sh
```

当前测试：49 passed, 0 failed。
