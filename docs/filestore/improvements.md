# 分布式文件存储系统 — 问题清单与改进方案

> 针对 `feature/file-storage` 分支基础版文件存储系统的缺陷梳理与改进设计。
> 本文档只做方案设计，代码落地以此为准。

## 1. 概述

基础版文件存储系统（`filestore/`）采用「元数据服务（内存索引）+ 存储服务（分块落盘）」架构，元数据按 `chunk_index % 节点数` 将块分配到多个存储节点，客户端通过 ZooKeeper 发现元数据服务、直连存储节点上传/下载块。当前实现存在两类问题：

- **数据可靠性问题**：元数据存内存、上传/删除无一致性保证、失败不可感知等，会导致重启丢数据或数据混乱；
- **功能缺陷**：同名文件无法区分、路径穿越漏洞、并发数据竞争等。

本文档梳理出 16 个问题（P1~P16），其中 P1~P10 已**落地完成**，P11~P16 为待定增强项。

## 2. 问题总览

| 编号 | 问题 | 严重程度 | 方案 | 状态 |
|------|------|---------|------|------|
| P1 | 元数据纯内存存储，断电/重启即丢失 | 高 | MySQL 连接池 + 事务持久化 | ✅ 已完成 |
| P2 | 同名文件上传无条件覆盖，无法区分 | 高 | 拒绝重复上传 | ✅ 已完成 |
| P3 | 客户端 RPC 失败不可感知，且可能空指针崩溃 | 高 | 改用 `MprpcController` 并检查 `Failed()` | ✅ 已完成 |
| P4 | 元数据多线程并发读写无锁，数据竞争 | 高 | 连接池线程安全 + 事务保证原子性（移除内存 map） | ✅ 已完成 |
| P5 | 路径穿越安全漏洞，可越权读写删文件 | 高 | 存储端校验文件名 | ✅ 已完成 |
| P6 | 上传/删除无一致性回滚，产生孤儿块/不完整文件 | 高 | 事务状态机（PENDING→COMPLETE）+ 补偿清理 | ✅ 已完成 |
| P7 | 无块校验和，无法校验数据完整性 | 中 | 元数据记录每块 MD5 | ✅ 已完成 |
| P8 | 大文件全内存读写，易 OOM | 中 | 流式分块读写 | ✅ 已完成 |
| P9 | 存储节点故障无重试/降级 | 中 | 同节点重试（3 次） | ✅ 已完成 |
| P10 | `CHUNK_SIZE` 分散定义于两处，易漂移 | 低 | 统一定义到共享头文件 | ✅ 已完成 |
| P11 | 存储节点列表静态配置，扩容需重启 | 低 | 节点动态注册/心跳 | ❌ 未做 |
| P12 | 无鉴权，任意客户端可操作任意文件 | 中 | 增加用户身份校验 | ❌ 未做 |
| P13 | 删除需遍历 chunk 去重节点；file_chunk 每块重复存 ip/port | 中 | 新增 GetFileNodes 接口 + storage_node 表去重 | ❌ 未做 |
| P14 | 客户端无本地状态，重复上传需全量重传 | 中 | 本地 JSON 记录已上传文件，秒传/去重 | ❌ 未做 |
| P15 | 取模分配在节点增删时全量重映射 | 低 | 一致性哈希（虚拟节点环） | ❌ 未做 |
| P16 | 每块新建连接、固定偏移写产生碎片/空洞 | 中 | 单次上传内长连接复用 + 批量聚合 + 紧凑存储 | ❌ 未做 |

## 3. 核心方案设计（P1~P6）

### P1 元数据持久化：MySQL 连接池 + 事务

**现状**：`MetaService` 用 `std::unordered_map<std::string, FileMeta> m_meta` 在内存中维护「文件名 → 块位置」索引（`filestore/callee/meta_service.cc`），进程一旦退出或断电，全部索引丢失。

**方案**：索引改存 MySQL，通过连接池访问。

**连接池现状**：`filestore/callee/database/` 下已有接口声明 `Connection.hpp`（单连接：`connect/update/query`）与 `CommonConnectionPool.hpp`（连接池单例：`getInstance/getConnection/stop`），但**仅有类声明、尚无 `.cc` 实现**，需补齐：

- `Connection.cc`：实现 `connect()`（`mysql_real_connect`）、`update()`（`mysql_query` 执行 insert/update/delete）、`query()`（`mysql_store_result`）、空闲时间刷新、析构释放；
- `CommonConnectionPool.cc`：实现单例、`loadConfigFile()` 读配置、生产者线程 `produceConnectionTask()`（按 `_initSize/_maxSize` 创建连接）、回收线程 `scanConnectionTask()`（回收超 `_maxIdleTime` 的空闲连接）、`getConnection()` 带 `_connectionTimeout` 超时。

**建表 DDL**（InnoDB，支持事务）：

```sql
CREATE TABLE IF NOT EXISTS file_meta (
    filename    VARCHAR(255) PRIMARY KEY,
    filesize    BIGINT       NOT NULL,
    chunk_count INT          NOT NULL,
    status      TINYINT      NOT NULL DEFAULT 0,  -- 0=PENDING 1=COMPLETE
    created_at  TIMESTAMP    NOT NULL DEFAULT CURRENT_TIMESTAMP
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS file_chunk (
    filename    VARCHAR(255) NOT NULL,
    chunk_index INT          NOT NULL,
    ip          VARCHAR(64)  NOT NULL,
    port        INT          NOT NULL,
    PRIMARY KEY (filename, chunk_index)
) ENGINE=InnoDB;
```

**配置**：`config/filestore_meta.cnf` 新增 `[mysql]` 段，供连接池 `loadConfigFile()` 读取：

```ini
[mysql]
# MySQL 连接参数
mysql_ip=127.0.0.1
mysql_port=3306
mysql_user=root
mysql_password=YOUR_PASSWORD
mysql_dbname=filestore
# 连接池参数
init_size=2
max_size=8
max_idle_time=60
connection_timeout=5000
```

**MetaService 改造**：

- `main()` 启动时调用 `ConnectionPool::getInstance()` 初始化，并执行 `CREATE TABLE IF NOT EXISTS` 建表；
- `UploadFile/QueryFile/DeleteFile` 改走 MySQL（SQL 见 P2、P6），**移除内存 `m_meta`**。

**依赖**：环境已装 MySQL 开发库（`mysql_config --cflags --libs` 可用，`-lmysqlclient`）。`filestore/CMakeLists.txt` 需为 `meta_callee` 增加 database 源文件与 `-lmysqlclient` 链接（详见第 5 节）。

### P2 同名文件：拒绝重复上传

**现状**：`UploadFile` 无条件执行 `m_meta[request->filename()] = meta`，后上传的同名文件覆盖先前的索引；存储端又只按偏移覆盖、不 truncate，旧文件尾部块残留，导致索引与数据错乱。

**方案**：上传登记阶段在**同一事务内**先查重，命中即拒绝：

```sql
-- UploadFile 事务内
SELECT filename FROM file_meta WHERE filename = ?;   -- 命中则 ROLLBACK，返回「文件已存在」
INSERT INTO file_meta(filename, filesize, chunk_count, status)
VALUES (?, ?, ?, 0);                                  -- status=PENDING
-- 循环插入块位置
INSERT INTO file_chunk(filename, chunk_index, ip, port) VALUES (?, ?, ?, ?);
COMMIT;
```

- 已有同名文件（含 PENDING 与 COMPLETE 状态）时，返回 `errcode` 非 0 且 `errmsg="file already exists"`，客户端提示后退出；
- 覆盖重传 / 版本化（`filename#v1`）作为后续可选语义，本次不实现。

### P3 客户端 RPC 失败检测

**现状**：`fs_caller.cc` 中所有 stub 调用都传 `nullptr` 作为 controller：

```cpp
metaStub.UploadFile(nullptr, &request, &response, nullptr);
```

而 `MprpcChannel::CallMethod` 在 socket 创建、连接、发送、接收、解析失败的每一处都调用 `controller->SetFailed(reason)`（`src/mprpc_channel.cc`）——传入 `nullptr` 会**解引用空指针崩溃**；即便不崩，失败时 `response` 仍是默认构造（`errcode=0`），会被误判为成功。

**方案**：所有调用改用 `MprpcController`（继承自 `google::protobuf::RpcController`，提供 `Failed()/ErrorText()`，见 `include/mprpc_controller.h`），调用后先检查失败：

```cpp
MprpcController controller;
metaStub.UploadFile(&controller, &request, &response, nullptr);
if (controller.Failed()) {
    std::cerr << "upload register failed: " << controller.ErrorText() << std::endl;
    return;
}
```

覆盖 `doUpload/doDownload/doDelete` 中对元数据与存储节点的**每一处** stub 调用。

### P4 元数据并发加锁

**现状**：`RpcProvider` 底层使用 muduo `TcpServer` 且 `setThreadNum(4)`，4 个 I/O 线程会并发调用 `MetaService` 的 `UploadFile/QueryFile/DeleteFile`，而 `m_meta` 无锁并发 `insert/erase/find` 属数据竞争（rehash、迭代器失效、未定义行为）。

**方案**：引入 MySQL 后，`m_meta` 内存 map 被移除，并发访问改由**数据库侧**承担：

- `ConnectionPool::getConnection()` 内部用 `std::mutex` + `std::condition_variable` 保证连接队列线程安全，每个请求线程拿到独立连接；
- 「查重 + 插入」「状态翻转」等复合操作封装在**单个 SQL 事务**内，由 InnoDB 行锁/事务隔离保证原子性，无需应用层再加锁；
- 若未来引入内存只读缓存加速查询，则需为该缓存补 `std::mutex`（或 `std::shared_mutex`），并注意缓存失效。

### P5 路径穿越防护

**现状**：`StorageService` 用 `m_dataDir + "/" + request->filename()` 直接拼路径（`filestore/callee/storage_service.cc`），`filename` 未做任何校验。恶意/错误客户端可传 `../../etc/passwd` 或绝对路径，越权读写删任意文件。

**方案**：在 `PutChunk/GetChunk/DeleteFile` 入口统一校验文件名，不合法直接返回错误：

- 拒绝空字符串；
- 拒绝含 `/` 或 `\`（不允许多级路径）；
- 拒绝 `..`、`.`（独立段）；
- 拒绝绝对路径（首字符为 `/`）。

建议封装为 `static bool isValidFilename(const std::string& name)`，三个方法复用。同时客户端 `basename()` 已剥离目录前缀，正常路径仅剩纯文件名，天然通过校验。

### P6 上传/删除一致性：事务状态机 + 回滚

**现状**：

- **上传**：先 `UploadFile` 登记索引，再逐块 `PutChunk`；任一块失败即 `return`，但索引已登记 → 下载到「索引存在、数据不完整」的文件；
- **删除**：客户端先直连存储节点删块，再删索引；删块失败时索引已删（或 `doDelete` 直接忽略 `dresp` 返回值）→ 残留孤儿块。

**方案**：引入「上传状态机」把「索引登记」与「块上传」解耦为两阶段，MySQL 事务保证**元数据侧**的一致性（跨系统的块清理仍需补偿逻辑）。

**上传（两阶段）**：

1. `UploadFile`：事务内查重 + 插入 `file_meta(status=PENDING)` + 插入 `file_chunk`（见 P2），返回块分配方案；
2. 客户端逐块 `PutChunk` 直连存储节点；
3. 全部成功 → 新 RPC `CommitUpload(filename)`：事务内 `UPDATE file_meta SET status=COMPLETE WHERE filename=? AND status=PENDING`；
4. 任一块失败 → 新 RPC `CancelUpload(filename)`：事务内删除 `file_meta`/`file_chunk`（仅限 `status=PENDING`），并触发已传块的清理。

**效果**：`QueryFile` 只返回 `status=COMPLETE` 的记录（`WHERE filename=? AND status=COMPLETE`），PENDING 记录对外不可见。即使进程在块上传中途崩溃，重启后数据库里只有 PENDING 记录，可通过启动清理或 `CancelUpload` 移除，**不会出现「索引指向不完整数据」**。

**删除**：

1. 客户端 `QueryFile` 取块位置 → 直连各存储节点删块；
2. 元数据 `DeleteFile` 在事务内删除 `file_meta` + `file_chunk`；
3. 删块失败的节点仅产生**孤儿块**（索引已删，不影响正确性，只占磁盘），由后台 GC 补偿清理（定期扫描存储节点数据目录，与 `file_meta` 对比，删除无索引对应的文件）。

**proto 变更**：`filestore/proto/file_storage.proto` 的 `MetaServiceRpc` 新增两个 RPC：

```proto
message CommitUploadRequest  { string filename = 1; }
message CommitUploadResponse { ResultCode result = 1; }
message CancelUploadRequest  { string filename = 1; }
message CancelUploadResponse { ResultCode result = 1; }

service MetaServiceRpc {
    rpc UploadFile(UploadFileRequest) returns(UploadFileResponse);
    rpc CommitUpload(CommitUploadRequest) returns(CommitUploadResponse);
    rpc CancelUpload(CancelUploadRequest) returns(CancelUploadResponse);
    rpc QueryFile(QueryFileRequest) returns(QueryFileResponse);
    rpc DeleteFile(DeleteFileRequest) returns(DeleteFileResponse);
}
```

> **事务边界说明**：MySQL 事务只保证元数据侧（`file_meta` + `file_chunk` 两表）的一致性；「块已写入存储节点」属跨系统操作，无法被数据库事务回滚，故仍需 `CancelUpload`/GC 等**补偿逻辑**清理已落盘的块。

## 4. 其他已知问题与建议方案（P7~P16）

- **P7 块校验和**：在 `ChunkLocation` 或 `FileMeta` 中记录每块 MD5/SHA256，下载后比对，检测块损坏/被篡改；存储端 `PutChunk` 计算并返回校验和。
- **P8 大文件流式**：`fs_caller` 当前用 `std::string` 一次性读入/拼接整个文件。改为按 `CHUNK_SIZE` 流式读写（上传边读边发、下载边收边写），避免大文件 OOM。
- **P9 节点故障重试/降级**：`PutChunk/GetChunk` 失败时重试同节点或换副本节点；引入块副本（如每块 2 副本）提升可用性。
- **P10 CHUNK_SIZE 统一定义**：`CHUNK_SIZE=1024` 目前分别定义在 `fs_caller.cc` 与 `storage_service.cc`，应下沉到共享头文件或 proto，防止两端漂移。
- **P11 节点动态扩缩容**：存储节点列表从静态配置改为节点主动注册 + 心跳，元数据动态维护节点集合，扩容无需重启。
- **P12 鉴权**：上传/下载/删除前校验客户端身份（token/账号体系），防止未授权访问与删库。

### 第二轮优化方案（P13~P16）

> 在 P1~P10 落地后，针对**性能与可扩展性**提出的第二轮优化，目前仅做方案设计，代码尚未落地。

#### P13 删除流程优化 + 存储节点表去重

**现状**：删除时客户端先 `QueryFile` 拿到**逐块**位置（N 块 = N 条 ip/port），再用 `std::set` 去重出涉及节点，逐个节点 `DeleteFile`（`filestore/caller/fs_caller.cc` 的 `doDelete`）。而一个存储节点上、一个文件只对应一个数据文件（`data_dir/<filename>`），客户端去重是多余开销；同时 `file_chunk` 表每个块都重复存一份 `ip VARCHAR(64) + port INT`，N 块就冗余 N 份。

**方案 A —— 新增节点查询接口**：元数据服务新增 `GetFileNodes(filename)`，在数据库侧直接去重返回「该文件落在哪些节点」，删除流程简化为 `GetFileNodes → 逐节点 DeleteFile → 元数据 DeleteFile`，客户端不再遍历 chunks 去重。

```sql
-- GetFileNodes 查询（方案 B 落地前）
SELECT DISTINCT ip, port FROM file_chunk WHERE filename = ?;
```

**方案 B —— 新增存储节点表消除冗余**：把节点地址从 `file_chunk` 抽到独立表，`file_chunk` 只存 `node_id` 外键。

```sql
CREATE TABLE IF NOT EXISTS storage_node (
    node_id INT PRIMARY KEY AUTO_INCREMENT,
    ip      VARCHAR(64) NOT NULL,
    port    INT         NOT NULL,
    UNIQUE KEY uk_ip_port (ip, port)
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS file_chunk (
    filename    VARCHAR(255) NOT NULL,
    chunk_index INT          NOT NULL,
    node_id     INT          NOT NULL,
    checksum    VARCHAR(32)  NOT NULL DEFAULT '',
    PRIMARY KEY (filename, chunk_index)
) ENGINE=InnoDB;
```

- 收益：每块用 4 字节 `node_id` 替代约 70 字节的 `ip + port`，冗余大幅下降；节点地址变更只改 `storage_node` 一行。
- 迁移：启动时把现有 `file_chunk` 的 `(ip, port)` 去重插入 `storage_node`，再回填 `node_id`（幂等、一次性）。
- 受影响 RPC/SQL：`UploadFile`（查重 + 插入）、`QueryFile`（JOIN 出 ip/port）、`CommitUpload`（更新 checksum）、`GetFileNodes`（新增）、`DeleteFile`（删除）。

**proto 变更**：

```proto
message StorageNode { string ip = 1; int32 port = 2; }

message GetFileNodesRequest  { string filename = 1; }
message GetFileNodesResponse {
    ResultCode result = 1;
    repeated StorageNode nodes = 2;   // 去重后的存储节点列表
}

service MetaServiceRpc {
    // ... 原有 rpc 不变
    rpc GetFileNodes(GetFileNodesRequest) returns(GetFileNodesResponse);
}
```

#### P14 客户端持久化存储（秒传/去重）

**现状**：客户端 `fs_caller` 无任何本地状态，同名文件每次上传都全量重读、重传（即使远端已有）。

**方案**：客户端维护本地 JSON 记录（用 `thirdparty/json.hpp`，nlohmann json 单头文件，仓库已有），记录 `filename → {filesize, chunk_count}`（可扩展记录每块 checksum）。

```json
{
  "a.txt": { "filesize": 1048576, "chunk_count": 1024 },
  "b.txt": { "filesize": 512, "chunk_count": 1 }
}
```

- 上传前：本地命中同名且 `filesize/chunk_count` 一致 → 调 `QueryFile` 确认远端已 COMPLETE → 跳过上传（秒传）；
- 上传成功后写入、删除成功后移除该记录；
- **边界**：本地 JSON 只是客户端缓存，权威元数据仍在 MySQL；两者不一致时以 `QueryFile` 为准。断点续传（记录已传块号、失败后跳过）列为后续增强。

#### P15 块分配策略：一致性哈希

**现状缺陷**：`m_nodes[chunk_index % m_nodes.size()]` 取模分配（`filestore/callee/meta_service.cc` 的 `UploadFile`）。「奇偶」只是 N=2 的特例；一旦节点数变化（增删节点），几乎**所有块都会重映射**到不同节点，需要全量迁移。

**方案**：改为一致性哈希环 + 虚拟节点。

- 为每个物理节点生成 K 个虚拟节点（如 K=150），`hash("ip:port#k")` 落到 `[0, 2^32)` 的环上；
- 块分配：`hash(filename + chunk_index)` 落环，顺时针找到下一个虚拟节点，映射到其物理节点；
- 节点增删时，只有环上相邻区间的块（约 1/N）需要重映射，迁移量最小化。

**实现要点**：

- 环结构：`std::map<uint32_t, nodeId> ring` + `upper_bound` 二分查找顺时针后继；
- 哈希函数：建议复用 `md5Hex()` 取前 4 字节（`filestore/common/common.h` 已有），避免 `std::hash` 跨平台/进程不一致导致环不稳定；
- 元数据服务启动时按 `storage_nodes` 配置构建环，`UploadFile` 分配块时改用一致性哈希。

**两点关联**：① 一致性哈希会让一个文件在**单个节点上的块号变得稀疏**，必须配合 P16 的紧凑存储，否则固定偏移写会产生大量空洞；② 一致性哈希顺带解决了 P11「扩缩容需重启」的分配侧问题（节点的动态注册/心跳仍需另做）。

#### P16 上传流程优化（长连接复用 + 批量聚合 + 紧凑存储）

**子问题 A —— 连接开销大**：`putChunkWithRetry/getChunkWithRetry` 每块都新建一个 `MprpcChannel(ip, port)`（一条 TCP 连接），传完即析构关闭，N 块 = N 次 connect/close，握手与 RTT 开销大。

**方案 A**：客户端在单次上传内按「目标节点」对块分组，同一节点的多块复用一条 `MprpcChannel`（一条连接）顺序发送；重试粒度仍为单块。进一步可新增 `PutChunksBatch` RPC，一次请求携带同节点的多块，减少往返次数。

**子问题 B —— 固定偏移写产生碎片/空洞**：存储端 `offset = chunk_index * CHUNK_SIZE` 固定偏移写（`filestore/callee/storage_service.cc` 的 `PutChunk/GetChunk`）。当块号稀疏（P15 一致性哈希后，同一节点上某文件只落部分块）或最后一块不满时，数据文件里会出现空洞与内部碎片。

**方案 B**：存储端改为**顺序紧凑追加**写，元数据记录每块的 `(offset, size)`：

- `file_chunk` 增加 `offset BIGINT`、`size INT` 两列；
- `PutChunk` 追加写数据文件并返回本块 `(offset, size)`；
- `CommitUpload` 把每块 `(offset, size)` 登记到 `file_chunk`；
- `GetChunk` 改为按记录的 `(offset, size)` 读取，而非 `chunk_index * CHUNK_SIZE`。

> P16B 是 P15 一致性哈希的**必要配套**：不做紧凑存储，哈希分配带来的稀疏块号会把每个数据文件打出大量空洞，空间浪费远超收益。

## 5. 代码改动点清单

| 文件 | 改动 |
|------|------|
| `filestore/callee/database/Connection.cc` | 新增：实现 `Connection` 各方法（连接/更新/查询/析构） |
| `filestore/callee/database/CommonConnectionPool.cc` | 新增：实现连接池单例、配置加载、生产/回收线程、`getConnection` |
| `filestore/callee/meta_service.cc` | 改造：初始化连接池 + 建表；`UploadFile/QueryFile/DeleteFile` 改走 MySQL；新增 `CommitUpload/CancelUpload`；移除内存 `m_meta` |
| `filestore/proto/file_storage.proto` | 新增 `CommitUpload`/`CancelUpload` 消息与 RPC，重新 `protoc` 生成 `.pb.h/.pb.cc` |
| `filestore/caller/fs_caller.cc` | 所有 stub 调用改用 `MprpcController` + 检查 `Failed()`；上传走「UploadFile → 逐块 PutChunk → CommitUpload / CancelUpload」两阶段流程 |
| `filestore/callee/storage_service.cc` | `PutChunk/GetChunk/DeleteFile` 入口增加文件名合法性校验 |
| `config/filestore_meta.cnf` | 新增 `[mysql]` 段（连接参数 + 连接池参数） |
| `filestore/CMakeLists.txt` | `meta_callee` 增加 database 源文件与 `-lmysqlclient` 链接 |

**P13~P16 待实现改动点（仅方案，尚未落地）：**

| 文件 | 改动（P13~P16） |
|------|------|
| `filestore/proto/file_storage.proto` | 新增 `StorageNode`、`GetFileNodes` 消息与 RPC；`PutChunkResponse` 增加 `offset/size`；可选 `PutChunksBatch` |
| `filestore/callee/meta_service.cc` | 新增 `storage_node` 表与迁移；`file_chunk` 改 `node_id`/`offset`/`size`；新增 `GetFileNodes`；`UploadFile` 改用一致性哈希分配；`QueryFile` JOIN 出 ip/port/offset/size |
| `filestore/callee/storage_service.cc` | `PutChunk` 改为紧凑追加写并返回 `(offset, size)`；`GetChunk` 按 `(offset, size)` 读 |
| `filestore/caller/fs_caller.cc` | 单次上传内按节点分组复用连接（+ 可选批量聚合）；本地 JSON 记录（`thirdparty/json.hpp`）实现秒传；`doDelete` 改用 `GetFileNodes` |

## 6. 验证方式

1. **构建**：`./autobuild.sh` 编译通过，`meta_callee` 正确链接 `libmysqlclient`。
2. **持久化恢复**：上传文件后重启 `meta_callee`，`QueryFile` 仍能查到索引（数据在 MySQL 中）。
3. **同名拒绝**：对已存在文件再次上传，返回「文件已存在」，不再覆盖。
4. **事务一致性**：模拟「块上传中途失败」（如停掉某个存储节点），`CancelUpload` 后数据库无 PENDING 残留，`QueryFile` 查不到该文件，且已传块被清理。
5. **路径穿越**：以 `../../etc/passwd` 为文件名调用，存储端拒绝写入。
6. **失败感知**：停掉元数据服务后再调用客户端，客户端打印 `controller.ErrorText()` 而非崩溃或误判成功。
7. **并发**：多客户端并发上传不同文件，无崩溃、索引完整。

**P13~P16 待验证项（方案落地后）：**

8. **删除去重**：`GetFileNodes` 返回去重后的节点列表，删除不再遍历 chunks；`storage_node` 表落地后 `file_chunk` 无重复 ip/port。
9. **秒传**：本地 JSON 命中同名文件且远端已 COMPLETE 时，客户端跳过上传，无第二次全量重传。
10. **一致性哈希**：增删一个存储节点后，仅约 1/N 的块重映射（而非取模的全量重映射）。
11. **紧凑存储**：稀疏块号（哈希分配）下数据文件无空洞，`offset/size` 与 `GetChunk` 读取一致。
12. **连接复用**：单次上传发往同一节点的多块共用一条 TCP 连接，连接建立次数由 N 降为节点数。
