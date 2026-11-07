# duckdb-odata（中文版）

> [English](README.md) | 中文

以 DuckDB 扩展形式实现的 OData v4 服务器 —— 把任意已暴露的 DuckDB 表作为
OData EntitySet 对外提供，支持 `$select`、`$filter`、`$orderby`、`$top`、
`$skip` 与 `$count`，并根据实时 DuckDB catalog 自动生成 `$metadata`（EDMX）。

架构说明：[docs/architecture.md](docs/architecture.md)

```
DuckDB ── odata extension ──► HTTP / OData v4 ──► BI / ERP / Power BI / Excel / REST 客户端
```

## 快速开始（v0.1）

```sql
INSTALL odata;
LOAD odata;

CREATE TABLE customers (id BIGINT, name VARCHAR, active BOOLEAN);
CALL odata_expose('customers');
CALL odata_entity('customers', key := 'id');
CALL odata_serve('http://0.0.0.0:8080', token := 'secret');
```

不带参数调用 `odata_serve()` 会绑定 `localhost` 的空闲端口并自动生成一个
bearer token；它返回一行结果（`listen_uri` / `listen_url` / `auth_token`），
方便直接读回实际地址与 token：

```sql
CALL odata_serve();
-- ┌─────────────────┬──────────────────────────┬──────────────────────────────┐
-- │   listen_uri    │        listen_url        │          auth_token          │
-- ├─────────────────┼──────────────────────────┼──────────────────────────────┤
-- │ odata:localhost │ http://localhost:54321   │ 120C318F6F3E221DFEE5D6A7860… │
-- └─────────────────┴──────────────────────────┴──────────────────────────────┘
```

除非显式关闭认证（`token := ''`），否则所有请求都必须携带该 token：

```bash
curl -H "Authorization: Bearer secret" http://localhost:8080/odata/$metadata
curl -H "Authorization: Bearer secret" http://localhost:8080/odata/customers
curl -H "Authorization: Bearer secret" 'http://localhost:8080/odata/customers?$filter=active%20eq%20true&$top=10'
curl -H "Authorization: Bearer secret" http://localhost:8080/odata/customers(1)
```

## 暴露任意数据源（与其它 DuckDB 扩展组合）

`odata_expose` / `odata_expose_schema` 白名单的是 DuckDB catalog 中的**任意
表或视图**——包括由其它 DuckDB 扩展打开的、基于文件或外部数据库的视图。
只要 DuckDB 能 `SELECT` 的数据，都能用几行 SQL 暴露成只读的 OData v4 API：

```sql
-- Excel 工作簿（需要：INSTALL excel; LOAD excel）
CREATE VIEW msxlsx AS SELECT * FROM read_xlsx('/data/Active.xlsx');
CALL odata_expose('msxlsx');

-- ATTACH 的 SQL Server / Postgres / SQLite 数据库
ATTACH 'Server=localhost,1433;Database=weconnect;User Id=sa;Password=xxx' AS ms1 (TYPE mssql);
CALL odata_expose_schema('ms1.dbo');        -- 暴露 ms1.dbo 下所有表

ATTACH 'host=db.example.com port=5432 dbname=app user=svc password=yyy' AS pg (TYPE postgres);
CALL odata_expose_schema('pg.public');

ATTACH 'local.db' AS sq (TYPE sqlite);
CALL odata_expose_schema('sq.main');

-- S3 上的 Parquet / CSV（需要：INSTALL httpfs; LOAD httpfs）
CREATE VIEW s3_events AS SELECT * FROM read_parquet('s3://bucket/events/*.parquet');
CALL odata_expose('s3_events');

CALL odata_serve();   -- 从返回行读取 listen_url / auth_token
```

说明：

- **实时数据，零拷贝**：每个 HTTP 请求都会对视图或 ATTACH 的表执行
  `SELECT`，数据更新立即反映。
- **支持跨 catalog 暴露**（`'ms1.dbo'`、`'db.schema.table'`）：适用于
  ATTACH 的外部数据库；EntitySet 名在不同数据源之间不会冲突
  （`expose_schema('ms1.dbo')` → 实体 `ms1_dbo_<表名>`）。
- **默认安全**：白名单之外什么都不暴露；默认仅服务 `GET`；
  `odata_serve()` 默认自动开启 bearer token 认证。
- **列级策略**：`odata_expose(..., columns := [...])` 会限制元数据和全部查询表达式只能访问列出的字段。
- 没有主键的表也可以暴露；执行
  `CALL odata_entity('msxlsx', key := 'col')` 即可启用 `(key)` 单实体查询。

### PostgreSQL

在新的 DuckDB 会话中执行；加载未签名的 OData 二进制时，请使用
`duckdb -unsigned` 启动。替换连接信息和扩展路径，并确认当前
DuckDB 版本及平台支持 `postgres` 连接器。

```sql
INSTALL postgres;
LOAD postgres;
ATTACH 'host=localhost port=5432 dbname=app user=odata_reader password=REPLACE_ME'
    AS pg (TYPE postgres, READ_ONLY);

LOAD '/path/to/odata.duckdb_extension';
CALL odata_expose_schema('pg.public');
FROM odata_serve();
```

例如，当 `pg` 不是当前 catalog 时，`pg.public.customers` 对应实体
`pg_public_customers`。实际名称以返回的 `entity` / `table` 映射为准。

### Microsoft SQL Server：同时暴露两个数据库

使用社区 `mssql` 连接器，将两个数据库分别 ATTACH 为独立 catalog。
在新的会话中执行（未签名 OData 二进制使用 `duckdb -unsigned` 启动）：

```sql
INSTALL mssql FROM community;
LOAD mssql;
ATTACH 'Server=$MSSQL_HOST,$MSSQL_PORT;Database=$MSSQL_DATABASE;User Id=$MSSQL_USER;Password=$MSSQL_PASSWORD;TrustServerCertificate=yes'
    AS mdm (TYPE mssql);
ATTACH 'Server=$MSSQL_HOST,$MSSQL_PORT;Database=$MSSQL_DATABASE2;User Id=$MSSQL_USER;Password=$MSSQL_PASSWORD;TrustServerCertificate=yes'
    AS isales (TYPE mssql);

LOAD '/path/to/odata.duckdb_extension';
CALL odata_expose_schema('mdm.dbo');
FROM odata_expose_schema('isales.dbo');
FROM odata_serve();
```

上面的 `$MSSQL_*` 都是占位符：DuckDB **不会**自动展开 SQL 字符串中的
环境变量。请在 SQL 客户端中安全替换连接信息和扩展路径后执行，
不要将真实凭据提交到仓库，并使用仅具备所需读取权限的数据库账号。
`TrustServerCertificate=yes` 会跳过服务端证书校验；生产环境应配置可信证书，
并移除该覆盖选项。安装前请确认连接器支持当前 DuckDB 版本及平台。

不切换当前 catalog/schema 时，`mdm.dbo.customers` 对应
`mdm_dbo_customers`，`isales.dbo.orders` 对应 `isales_dbo_orders`。
`CALL` 和 `FROM` 都能执行这些表函数并显示返回行；两个数据库共享同一个
OData 服务端点，无需复制数据。

### 访问 OData 服务

两种示例中的 `odata_serve()` 都会返回 `listen_uri`、`listen_url` 和
`auth_token`。将 `listen_url` 和 `auth_token` 复制到另一个终端：

```bash
export ODATA_URL='http://localhost:54321'  # 替换为 listen_url
export ODATA_TOKEN='REPLACE_WITH_AUTH_TOKEN'
curl -H "Authorization: Bearer $ODATA_TOKEN" "$ODATA_URL/odata/"
curl -H "Authorization: Bearer $ODATA_TOKEN" "$ODATA_URL/odata/\$metadata"
# SQL Server 示例；替换为 odata_expose_schema 返回的实际实体名：
curl -H "Authorization: Bearer $ODATA_TOKEN" "$ODATA_URL/odata/mdm_dbo_customers?\$top=10"
```

服务运行期间请保持 DuckDB 进程和 ATTACH 的数据库打开，不要使用执行完 SQL
就退出的一次性 CLI 调用。示例保留 localhost 监听、自动 bearer 认证及只读
OData 请求。暴露整个 schema 会发布其中发现的全部表和视图；
需要更小的开放范围时，请使用带列白名单的 `odata_expose`。
DETACH 数据源前先停止服务：

```sql
CALL odata_stop();
```

连接器参考：[PostgreSQL](https://duckdb.org/docs/stable/core_extensions/postgres/overview.html)
与 [mssql](https://duckdb.org/community_extensions/extensions/mssql)。

## 基本写操作

在 `odata_serve` 中设置 `read_only := false` 可开启 POST 新增（201 + Location）、
PATCH 部分更新及 DELETE 删除（204）。默认仍为只读。写操作沿用 Bearer 认证、
实体/列白名单，并在单请求事务内执行。用法、输入类型与限制见 [写入 API](docs/writes.md)。

## 功能范围

详见 [docs/architecture.md](docs/architecture.md)、
[docs/odata-support.md](docs/odata-support.md) 与
[docs/security.md](docs/security.md)。默认只读，支持上文列出的查询选项，
可显式开启基本写操作；`$expand`、不透明分页 token 与 Quack gateway 执行器仍待实现。

## 构建

先初始化官方 DuckDB 扩展模板所需的 submodule，再使用标准 Makefile：

```bash
git submodule update --init --recursive
make
make test
make test-http
```

详情见 [docs/architecture.md](docs/architecture.md#building)。

原生平台 CI/Release 保留官方默认的 `EXTENSION_STATIC_BUILD=1`：
将 DuckDB 静态链接进可加载扩展以保证可移植性，因此二进制可能达到数十 MB。
本地 Linux/macOS 开发可使用 `make EXTENSION_STATIC_BUILD=0` 生成较小的产物，
但需要宿主导出全部所需的 DuckDB C++ 符号；即使版本匹配，官方 CLI 也不一定
提供这些符号，使用 `RTLD_LOCAL` 的嵌入场景也可能加载失败。
当前锁定的 DuckDB 版本在 Windows 上强制静态链接。
选择本地动态构建前请参阅[链接说明](docs/architecture.md#building)。

## 发布 Release

推送版本 tag 即可触发官方 `duckdb-stable-build` 分发流水线，
并将其构建产物作为 GitHub Release assets 发布：

```bash
git tag v0.1.0 && git push origin v0.1.0
```

稳定版构建成功后，Release 任务会发布
`odata-<duckdb版本>-<platform>.duckdb_extension`（WebAssembly 使用
`.duckdb_extension.wasm` 后缀）及 `sha256sums.txt`。
DuckDB 版本和平台由官方分发流水线决定，不再通过独立矩阵重复构建历史版本。

## 许可证

MIT —— 见 [LICENSE](LICENSE)。
