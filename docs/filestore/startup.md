# 分布式文件存储系统 — 启动与验证流程

本文档说明如何从零启动文件存储系统并做端到端验证。

## 1. 前置条件

### 1.1 MySQL

MySQL 需已安装并运行（本机默认 `root/YOUR_PASSWORD@localhost:3306`）。文件存储系统使用**独立数据库 `filestore`**（对应 `config/mysql.cnf` 中的 `dbname`），元数据服务启动时会自动 `CREATE DATABASE IF NOT EXISTS` 建库并建表（`user`/`file_node`/`file_meta`/`file_chunk`/`cleanup_queue`），**无需手动建库**。

### 1.2 ZooKeeper

ZooKeeper 负责存储节点动态发现（临时节点）。启动（`$ZOOKEEPER_HOME` 为 ZooKeeper 安装目录）：

```bash
sudo $ZOOKEEPER_HOME/bin/zkServer.sh start
```

### 1.3 Redis

Redis 负责两件事：① 登录会话 token（`session:<token>`）；② 待清理队列 Stream。启动（默认 `127.0.0.1:6379`）：

```bash
redis-server
```

## 2. 构建

```bash
./autobuild.sh      # 或 ninja -C build
```

产物：

- `bin/meta_callee` — 元数据服务（虚拟文件树 + 鉴权 + 块分配，MySQL 持久化）
- `bin/storage_callee` — 存储服务（文件块落盘，数据文件以 file_id 命名）
- `bin/fs_caller` — 客户端 CLI（注册/登录/文件树/上传/下载/删除）

## 3. 配置文件

| 文件 | 用途 |
|------|------|
| `config/mysql.cnf` | MySQL 连接参数（`[database]` 段）与连接池参数（`[pool]` 段） |
| `config/filestore_meta.cnf` | 元数据服务监听地址、ZooKeeper 地址、存储节点种子 `storage_nodes` |
| `config/filestore_storage1.cnf` | 存储节点 1（8001）监听地址 + 数据目录 `data_dir` |
| `config/filestore_storage2.cnf` | 存储节点 2（8002）监听地址 + 数据目录 `data_dir` |
| `config/filestore_storage3.cnf` | 存储节点 3（8004）监听地址 + 数据目录 `data_dir` |

> 注意：连接池按相对路径读取 `config/mysql.cnf`，因此**必须在项目根目录启动**服务。

## 4. 启动服务

在项目根目录，依次启动 1 个元数据服务 + 3 个存储节点：

```bash
# 1. 元数据服务（端口 8003）
./bin/meta_callee -i config/filestore_meta.cnf

# 2. 存储节点（端口 8001/8002/8004）
./bin/storage_callee -i config/filestore_storage1.cnf
./bin/storage_callee -i config/filestore_storage2.cnf
./bin/storage_callee -i config/filestore_storage3.cnf
```

后台启动方式（日志重定向到 `/tmp`）：

```bash
nohup ./bin/meta_callee -i config/filestore_meta.cnf > /tmp/meta.log 2>&1 &
nohup ./bin/storage_callee -i config/filestore_storage1.cnf > /tmp/storage1.log 2>&1 &
nohup ./bin/storage_callee -i config/filestore_storage2.cnf > /tmp/storage2.log 2>&1 &
nohup ./bin/storage_callee -i config/filestore_storage3.cnf > /tmp/storage3.log 2>&1 &
```

## 5. 客户端使用（命令式 CLI）

客户端 `fs_caller` 是命令式子命令，登录后 token 存 `~/.fscli/token`、当前目录存 `~/.fscli/cwd`。统一格式：`./bin/fs_caller -i <configfile> <命令> [参数]`。

### 5.1 命令一览

| 命令 | 说明 |
|------|------|
| `register <user> <pwd>` | 注册账号 |
| `login <user> <pwd>` | 登录（签发 token） |
| `logout` | 退出登录 |
| `ls [path]` | 列出目录内容（缺省列当前目录） |
| `mkdir <path>` | 建目录（父目录须已存在） |
| `rmdir [-r] <path>` | 删目录（`-r` 递归删除子目录/文件，否则只删空目录） |
| `cd <path>` | 切换当前目录（本地记录） |
| `upload <本地文件> <虚拟路径>` | 上传文件到虚拟路径 |
| `download <虚拟路径> [-o 目标]` | 下载文件（默认 `~/Downloads/<文件名>`） |
| `delete <虚拟路径>` | 删除文件 |

### 5.2 示例

```bash
CFG="-i config/filestore_meta.cnf"

# 注册 / 登录 / 退出
./bin/fs_caller $CFG register alice 123456
./bin/fs_caller $CFG login alice 123456
./bin/fs_caller $CFG logout

# 目录操作
./bin/fs_caller $CFG mkdir /docs
./bin/fs_caller $CFG mkdir /docs/sub
./bin/fs_caller $CFG cd /docs
./bin/fs_caller $CFG ls /               # 列出根目录
./bin/fs_caller $CFG rmdir -r /docs     # 递归删除 /docs 及其所有子目录/文件

# 上传 / 下载 / 删除（按虚拟路径，文件以唯一 file_id 存储）
./bin/fs_caller $CFG upload ./report.txt /docs/report.txt
./bin/fs_caller $CFG download /docs/report.txt             # 默认存 ~/Downloads/report.txt
./bin/fs_caller $CFG download /docs/report.txt -o /tmp/x    # 指定目标位置
./bin/fs_caller $CFG delete /docs/report.txt
```

> 虚拟文件树硬限制：**深度 ≤64 层、总路径 ≤1024 字符、单级 ≤255 字节**。

## 6. 端到端验证清单

| 验证项 | 预期结果 |
|--------|---------|
| 注册登录 | `register`/`login` 成功；未登录执行 `mkdir` 返回 `not logged in` |
| 建目录 | `mkdir /docs` 成功，`mkdir` 重名返回 `already exists` |
| 上传 | `upload` 成功，`file_meta`/`file_node` 出现记录且 `status=1`，数据文件落盘为 `data_dir/<file_id>` |
| 同名共存 | `/docs/a.txt` 与 `/other/a.txt` 各自独立 file_id，可共存 |
| 同目录重名 | 再上传 `/docs/a.txt` 返回 `file already exists` |
| 下载一致性 | `download` 到 `~/Downloads`，`md5sum` 与原文件一致 |
| 删除 | `delete` 后 `file_node`/`file_meta` 无记录、`data_dir` 无残留 |
| 持久化 | 重启 `meta_callee` 后 `ls`/`download` 仍正常（索引在 MySQL） |
