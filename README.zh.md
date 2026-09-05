# duckdb-odata（中文版）

> [English](README.md) | 中文

以 DuckDB 扩展形式实现的 OData v4 服务器 —— 把任意已暴露的 DuckDB 表作为
OData EntitySet 对外提供，支持 `$select`、`$filter`、`$orderby`、`$top`、
`$skip` 与 `$count`，并根据实时 DuckDB catalog 自动生成 `$metadata`（EDMX）。

设计文档：[docs/design.md](docs/design.md)

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
- **默认安全**：白名单之外什么都不暴露；v0.1 仅服务 `GET`；
  `odata_serve()` 默认自动开启 bearer token 认证。
- 没有主键的表也可以暴露；执行
  `CALL odata_entity('msxlsx', key := 'col')` 即可启用 `(key)` 单实体查询。

## 功能范围

详见 [docs/architecture.md](docs/architecture.md)、
[docs/odata-support.md](docs/odata-support.md) 与
[docs/security.md](docs/security.md)。v0.1 为**只读 GET**，支持上文列出的
查询选项；写操作、`$expand`、分页 token 与 Quack gateway 执行器在后续版本
实现。

## 构建

本地开发流程见 [docs/architecture.md](docs/architecture.md#building)。

## 发布 Release

打 tag 即可触发全矩阵构建并把扩展作为 GitHub Release assets 发布：

```bash
git tag v0.1.0 && git push origin v0.1.0
```

CI 会把每个受支持的 DuckDB 版本 / ABI 的产物
`odata-<duckdb版本>-<platform>.duckdb_extension`（外加 `sha256sums.txt`）
附加到对应 release。

## 许可证

MIT —— 见 [LICENSE](LICENSE)。
