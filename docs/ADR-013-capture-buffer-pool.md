# ADR-013：开机常驻的 HDMI 采集缓冲池

状态：已实现（2026-09-28），待实机验收。

## 问题

T6 连续运行 4 天后，客户端出现“看到画面一闪就断开”。worker 在 `VIDIOC_REQBUFS(MMAP)` 时需要为每个采集缓冲申请一整块物理连续内存（1080p BGR 约 6 MB，4K BGR 约 24 MB）。CMA 区长期运行后被无法迁移的页面碎片化，申请返回 EBUSY/ENOMEM，worker 立即退出，服务端日志只有 “IPC peer closed”。

实测：`drop_caches` 后 CmaFree 约 233 MB，但最大可得连续块只有约 1.9 MB。该内核没有 `/proc/sys/vm/compact_memory` 与 `compaction_proactiveness`，运行时无法主动整理。

## 决定

1. 新增 root 服务 `rkmoon-capture-pool.service`（`tools/capture_pool.py`）：开机早期从 `/dev/dma_heap/reserved` 分配 4 块按 4K BGR 计算的缓冲（每块 25,165,824 字节），终身持有；失败时 drop_caches 后重试，最多 5 次。
2. 通过 `/run/rkmoon-capture-pool/pool.sock`（属主为串流账户、0600，并校验 SO_PEERCRED uid）以 SCM_RIGHTS 把 dma-buf fd 借给 worker，同时发送 `{"version": 1, "count": N, "size": S}`。
3. worker 新增 `--capture-pool`：借到的缓冲足够当前模式时用 `V4L2_MEMORY_DMABUF` 入队；否则记录 `capture_pool_unavailable` / `capture_pool_unused` 并回退原 MMAP 路径。
4. worker 异常退出前通过 IPC 发送 `Kind::error`，服务端日志显示真实原因（例如 “worker: REQBUFS ...”）。
5. 安装脚本在 pool 已运行时不重启它，避免释放已占有的连续内存。

## 约束

不改变 CMA 大小、内核参数、引导配置或任何 sysctl；服务本身只做分配和 fd 传递。串流服务仍以非 root 账户运行。池占用 96 MB CMA，剩余 CMA 供 MPP/RGA 使用。

## 验收

- 离线：Python 测试、原生构建、离线测试。
- 实机：开机后 `journalctl -u rkmoon-capture-pool` 显示 ready；worker 日志出现 `capture_pool`；客户端可以连续多次连接出画面；故障时 sunshine.log 显示 worker 原因。
