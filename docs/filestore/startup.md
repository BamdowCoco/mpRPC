# 分布式文件存储系统 — 启动与验证流程

本文档说明如何从零启动文件存储系统并做端到端验证。

## 1. 前置条件

### 1.1 MySQL

MySQL 需已安装并运行（本机默认 `root/YOUR_PASSWORD@localhost:3306`）。文件存储系统使用**独立数据库 `filestore`**（对应 `config/mysql.cnf` 中的 `dbname`），元数据服务启动时会自动 `CREATE DATABASE IF NOT EXISTS` 建库并建表，**无需手动建库**。

> 如需改库名，同步修改 `config/mysql.cnf` 的 `dbname`。

### 1.2 ZooKeeper

启动 ZooKeeper 服务（`$ZOOKEEPER_HOME` 为 ZooKeeper 安装目录）：

```bash
sudo $ZOOKEEPER_HOME/bin/zkServer.sh start
```

## 2. 构建

```bash
./autobuild.sh
```

产物：

- `bin/meta_callee` — 元数据服务（文件索引，MySQL 持久化）
- `bin/storage_callee` — 存储服务（文件块落盘）
- `bin/fs_caller` — 客户端（上传/下载/删除）

## 3. 配置文件

| 文件 | 用途 |
|------|------|
| `config/mysql.cnf` | MySQL 连接参数（`[database]` 段）与连接池参数（`[pool]` 段） |
| `config/filestore_meta.cnf` | 元数据服务监听地址、ZooKeeper 地址、存储节点列表 `storage_nodes` |
| `config/filestore_storage1.cnf` | 存储节点 1 监听地址 + 数据目录 `data_dir` |
| `config/filestore_storage2.cnf` | 存储节点 2 监听地址 + 数据目录 `data_dir` |

> 注意：连接池按相对路径读取 `config/mysql.cnf`，因此**必须在项目根目录启动**服务。

## 4. 启动服务

在项目根目录，依次启动三个服务（每个一个终端，或都后台运行）：

```bash
# 1. 元数据服务（端口 8003）
./bin/meta_callee -i config/filestore_meta.cnf

# 2. 存储节点 1（端口 8001）
./bin/storage_callee -i config/filestore_storage1.cnf

# 3. 存储节点 2（端口 8002）
./bin/storage_callee -i config/filestore_storage2.cnf
```

后台启动方式（日志重定向到 `/tmp`）：

```bash
nohup ./bin/meta_callee -i config/filestore_meta.cnf > /tmp/meta.log 2>&1 &
nohup ./bin/storage_callee -i config/filestore_storage1.cnf > /tmp/storage1.log 2>&1 &
nohup ./bin/storage_callee -i config/filestore_storage2.cnf > /tmp/storage2.log 2>&1 &
```

## 5. 客户端使用

```bash
# 上传：自动分块（1024B/块）、按 chunk_index % 节点数 分布到存储节点、登记元数据
./bin/fs_caller -i config/filestore_meta.cnf upload <本地文件路径>

# 下载：查块位置 → 逐块直连存储节点 → 拼接写回 downloads/<文件名>
./bin/fs_caller -i config/filestore_meta.cnf download <文件名>

# 删除：删各存储节点数据块 → 删元数据索引
./bin/fs_caller -i config/filestore_meta.cnf delete <文件名>
```

## 6. 端到端验证清单

| 验证项 | 预期结果 |
|--------|---------|
| 上传后查库 | `SELECT * FROM filestore.file_meta` 出现记录且 `status=1`（COMPLETE） |
| 下载一致性 | `md5sum downloads/<文件名>` 与原文件一致 |
| 同名拒绝 | 再次上传同名文件，返回 `file already exists` |
| 持久化 | 重启 `meta_callee` 后 `download` 仍成功 |
| 删除 | MySQL 无记录、`data_dir` 无残留、`download` 返回 `file not found` |
| 失败检测 | 停掉 `meta_callee` 后客户端打印 `ErrorText`，不崩溃 |
