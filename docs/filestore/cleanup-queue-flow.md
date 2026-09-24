# 待清理队列（孤儿块 GC）流程说明

> 本文是文字版简要流程，说明「删块失败产生的孤儿块」是如何被可靠清理的。代码见 [filestore/callee/meta_service.cc](../../filestore/callee/meta_service.cc) 与 [filestore/callee/redis/redis.cpp](../../filestore/callee/redis/redis.cpp)。

## 0. 为什么会有孤儿块

删除一个文件时（客户端 [fs_caller.cc](../../filestore/caller/fs_caller.cc) `doDelete`）：

1. 先删元数据（`file_meta`/`file_chunk`）——文件从索引消失；
2. 再逐个直连存储节点删数据文件（`data_dir/<file_id>`）。

如果第 2 步某个存储节点删失败（节点宕机/网络抖动），该节点的数据文件就变成了**孤儿块**：索引已删、但磁盘上的数据文件还在。如果不处理，会一直占磁盘。待清理队列就是用来**可靠地重试删除这些孤儿块**的。

## 1. 唯一标识 file_id（关键前提）

每个文件在 `file_meta` 表有一个自增主键 `id`（即 `file_id`），**数据文件以 `file_id` 命名**（`data_dir/<file_id>`），而不是用文件名。这样同名文件重传会拿到新的 `file_id`，旧删除任务指向旧 `file_id`，**不会误删新文件的数据**。

- `file_meta`：`id`（唯一标识）+ `filename`（逻辑名，唯一索引用于查重）。
- `file_chunk`：`file_id` 关联到文件。
- 待清理任务记录的是 `file_id`，不是文件名。

## 2. 数据模型（cleanup_queue 表）

```
cleanup_queue
  id             自增任务 ID
  node_ip/port   孤儿块所在的存储节点
  file_id        待删除的数据文件唯一标识
  status         0=待清理  1=已完成  3=需人工（终态）
  retry_count    已重试次数
  next_retry_at  下次重试时间（指数退避用）
```

MySQL 是**最终可靠存储**（持久化）；Redis Stream 只是「快速通知」队列。

## 3. 完整流程

### ① 入队（AddCleanupTask）

客户端删数据失败时，调元数据服务 `AddCleanupTask(node_ip, node_port, file_id)`：

1. 写 MySQL `cleanup_queue`（幂等 `INSERT ... ON DUPLICATE KEY UPDATE`，重置为待清理）；
2. 把**任务 ID** 推入 Redis Stream（`XADD cleanup_queue * id <任务ID>`）。

### ② 消费（消费者组 XREADGROUP）

后台消费线程从 Redis Stream 拉任务 ID：

1. 先 `XREADGROUP ... 0` **重领本消费者未确认（PEL）的消息**——崩溃恢复用；
2. 再 `XREADGROUP ... >` 读新消息；
3. 对每个任务 ID，去 MySQL 查完整信息（节点 + file_id），**检查 `next_retry_at` 是否到期**（未到期跳过）；
4. 直连存储节点 `DeleteFile(file_id)` 删数据。

### ③ 确认（XACK）

处理完一条消息后调 `XACK`，把它从 PEL 移除（不管成功失败都 XACK，实际重试由 MySQL 退避控制）。若处理前崩溃，消息留在 PEL，重启后靠 `XREADGROUP ... 0` 重领。

### ④ 退避重试（next_retry_at 真正生效）

删除失败时：

- `retry_count + 1`，`next_retry_at = 当前时间 + 60 * 2^retry_count 秒`（指数退避：60s、120s、240s…）。

一个独立的**退避线程**每 ~60s 从 MySQL 捞 `status=0 AND next_retry_at <= NOW()` 的到期任务，直接调删除（`next_retry_at` 真正控制重试时机，也兼作 Redis 丢失时的 MySQL 兜底）。

### ⑤ 终态（status=3）

重试次数达到上限（`retry_count >= 10`，约相当于故障一天以上）后，置 `status=3`（需人工），**不再自动重试**，只能人工介入处理后改回 0 或删除。

### ⑥ 兜底分片扫描

针对「根本没进队列」的孤儿（如上传中途崩溃残留），元数据服务还有一个分片扫描线程：把每个节点的 `data_dir` 按 `file_id` 哈希分成 K=1440 片，每 60s 扫一片，24 小时覆盖全量一次。扫到「节点上有、但既不在 `file_meta` 也不在 `cleanup_queue`」的文件才删（**跳过队列在管的文件**，避免动 `status=3` 终态任务）。

## 4. 一张图（文字版）

```
删数据失败
   │
   ▼
AddCleanupTask ──► MySQL cleanup_queue（持久化，status=0）
   │                    │
   └── XADD ──► Redis Stream（任务 ID）
                    │
                    ▼
            消费线程 XREADGROUP ──► 查 MySQL 完整信息
                    │                    │
                    │           检查 next_retry_at 到期？
                    │              ├─ 否：跳过
                    │              └─ 是：DeleteFile(file_id)
                    │                         │
                    │                 成功 ──► status=1
                    │                 失败 ──► retry_count+1 + 退避
                    │                         │
                    │                  retry_count>=10 ──► status=3（终态，人工）
                    ▼
                XACK（移除 PEL）

兜底：
  退避线程 每 60s 扫 MySQL 到期任务（Redis 丢失也能恢复）
  分片扫描线程 每 60s 扫 1/1440 分片，删「不在 file_meta 且不在 cleanup_queue」的孤儿
```

## 5. 消费者组 vs 简单 XREAD/XDEL 的区别

| | 简单 XREAD/XDEL | 消费者组 XREADGROUP/XACK |
|---|---|---|
| 读取 | XREAD | XREADGROUP |
| 确认 | XDEL | XACK |
| 崩溃恢复 | 重读（至少一次） | 消息留在 PEL，`XREADGROUP ... 0` 重领 |
| 多消费者 | 手动协调，易重复 | 组内自动分配，不重复 |

本项目单 meta 进程、单消费线程，消费者组主要带来「PEL 精确跟踪在途消息 + 崩溃后重领」，比简单 XDEL 更规范可靠。
