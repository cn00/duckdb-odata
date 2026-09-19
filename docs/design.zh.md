# duckdb-odata 技术设计文档

> 文档版本：v0.1 → v0.3 演进规划（含 v0.4 / v1.0 展望）
> 本文档由原始设计讨论稿整理而成，按版本演进顺序重新组织，语言经规范化处理。
> 本文件仅保留于本地，不纳入版本控制。

## 目录

1. [项目概述](#1-项目概述)
2. [总体架构与跨版本原则](#2-总体架构与跨版本原则)
3. [v0.1：只读 OData 核心](#3-v01只读-odata-核心)
4. [v0.2：安全、分页、流式与远程执行](#4-v02安全分页流式与远程执行)
5. [v0.3：导航属性与富类型](#5-v03导航属性与富类型)
6. [后续版本：v0.4 与 v1.0](#6-后续版本v04-与-v10)
7. [测试策略](#7-测试策略)
8. [开发顺序建议](#8-开发顺序建议)
9. [术语与版本归属说明](#9-术语与版本归属说明)

---

# 1. 项目概述

## 1.1 项目定位

项目暂定名为 **`duckdb-odata`**，其定位为：

> **"OData API for DuckDB analytical databases"（DuckDB 分析型数据库的 OData API）**

该定位区别于一般的 "DuckDB REST API"。REST 接口较为普遍；而 OData v4 协议天然适配
Power BI、Excel、Dynamics、SAP 生态、.NET、Java 及各类企业数据工具，将其与 DuckDB、
Quack、Parquet、DuckLake 组合，能够提供差异化的数据接入能力。

项目形态：

```
DuckDB
   │
   ├── SQL
   ├── Parquet
   ├── DuckLake
   ├── Quack
   │
   └── OData Extension          ← 本项目的交付物
           │
           ▼
      HTTP / OData v4
           │
           ▼
       BI / ERP / Power BI
       Excel / .NET / Java
       Browser / REST clients
```

## 1.2 最终用户视角

DuckDB 侧执行与用户侧请求的对应关系如下。

用户请求：

```
GET /odata/Customers
```

```
GET /odata/Customers?$filter=age%20gt%2030
```

```
GET /odata/Orders?$select=id,total&$orderby=total%20desc&$top=100
```

DuckDB 内部执行：

```
SELECT id, total
FROM Orders
ORDER BY total DESC
LIMIT 100;
```

## 1.3 技术前提与约束

- 目标是在 **同一个 DuckDB 进程中**提供独立的 OData HTTP endpoint。
- OData 语义层最终将请求编译为 DuckDB SQL / 表达式。
- **Quack 的定位是 server/client transport**，计划作为第二阶段集成对象，而非本项目
  第一阶段的改造目标。
- 依据：Quack 当前仍处于 beta 阶段，其协议、函数与实现细节可能发生变化；Quack
  官方定位是"通过 HTTP 实现两个 DuckDB 实例通信"，而非通用 REST/OData API。因此
  OData 项目不应直接依赖或修改 Quack 作为承载层。

---

# 2. 总体架构与跨版本原则

## 2.1 分层处理管线

项目遵循严格的分层管线，禁止将用户输入直接拼接为 SQL：

```
HTTP
 │
 ▼
OData HTTP Server           （解析请求、鉴权、路由）
 │
 ▼
OData Parser + Validator    （词法/语法解析 → AST → 语义校验）
 │
 ▼
Query Compiler              （AST → 类型化表达式 → SQL / DuckDB 表达式）
 │
 ▼
DuckDB SQL
 │
 ▼
DuckDB
```

具体编译链路：

```
HTTP Query
    │
    ▼
Lexer
    │
    ▼
Parser
    │
    ▼
OData AST
    │
    ▼
Semantic Validator
    │
    ▼
DuckDB Expression / SQL AST
    │
    ▼
DuckDB
```

## 2.2 OData 语义层与传输层解耦

- **OData semantic layer**（parser / AST / validator / compiler / metadata /
  serializer）与 **transport layer**（local / quack）相互独立。
- 执行路径存在两条：

```
OData Extension
       │
       ├── Local execution
       │       ↓
       │     DuckDB
       │
       └── Quack execution（第二阶段）
               ↓
          Quack Server
```

## 2.3 部署形态（Embedded / Gateway）

### Embedded（嵌入式）

```
DuckDB
 └── OData Extension
       └── local execution
```

### Gateway（网关模式，第二阶段）

```
OData
   │
   ▼
OData Gateway
   │
   ▼
Quack
   │
   ▼
DuckDB
```

两种部署形态共享 parser、AST、validator、compiler、metadata、serializer，
仅替换 **ExecutionBackend**。

## 2.4 最终目标形态

```
                         Clients
                            │
              ┌─────────────┼─────────────┐
              │             │             │
           OData          JDBC          DuckDB
              │             │             │
              ▼             ▼             ▼
       ┌─────────────────────────────────────┐
       │             DuckDB Server            │
       │                                     │
       │  ┌─────────────┐   ┌──────────────┐ │
       │  │ OData HTTP  │   │ Quack Server │ │
       │  │ Endpoint    │   │              │ │
       │  └──────┬──────┘   └──────┬───────┘ │
       │         │                 │         │
       │         └────────┬────────┘         │
       │                  ▼                  │
       │          DuckDB Connection          │
       │                  │                  │
       │                  ▼                  │
       │             DuckDB Engine           │
       └─────────────────────────────────────┘
```

端口规划示例：

```
:8815  → Quack
:8080  → OData
```

部署演进目标（经由 nginx / Envoy 统一提供 TLS、认证与路由）：

```
https://db.example.com/odata/*
https://db.example.com/quack/*
```

Quack 官方同样建议通过 reverse proxy / TLS 等方式部署公网服务。

## 2.5 代码组织与职责边界

仓库采用 core / transport 分离的组织方式，避免 Quack API 变化牵连整体改动：

```
src/
  odata/                 # OData 语义核心（与传输无关）
      parser/
      compiler/
      metadata/
      serializer/
      security/

  transport/             # 执行后端（可替换）
      local/
      quack/
```

反对将实现扁平化为单个耦合文件（例如 `quack_odata.cpp`）。

## 2.6 核心 C++ 接口

以下接口在项目早期予以固化；接口稳定后，后续能力均以"新增"而非"重构"方式演进。

```
class ODataParser {
public:
    ODataQuery Parse(const string &query);
};
```

```
class ODataCompiler {
public:
    CompiledQuery Compile(
        const ODataQuery &query,
        const EntityModel &model
    );
};
```

```
class QueryExecutor {
public:
    virtual QueryResult Execute(
        const CompiledQuery &query
    ) = 0;
};
```

```
class EntityModelProvider {
public:
    EntityModel GetModel(
        ClientContext &context
    );
};
```

```
class ResultSerializer {
public:
    void Serialize(
        QueryResult &result,
        HttpResponse &response
    );
};
```

端到端数据流：

```
HTTP
 │
 ▼
ODataParser
 │
 ▼
ODataQuery
 │
 ▼
ODataCompiler
 │
 ▼
CompiledQuery
 │
 ▼
QueryExecutor
 │
 ├── LocalDuckDBExecutor
 │
 └── QuackExecutor（第二阶段）
 │
 ▼
ResultSerializer
 │
 ▼
HTTP
```

## 2.7 通用工程约束（贯穿各版本）

### 2.7.1 安全处理顺序

```
HTTP
 │
 ▼
Authentication
 │
 ▼
Authorization
 │
 ▼
OData Parser
 │
 ▼
Semantic validation
 │
 ▼
SQL
```

禁止越过上述步骤将 HTTP 输入直接映射为 SQL。

### 2.7.2 防 SQL 注入原则

- **禁止**：`sql += filter;`（及任何对 `$filter`/`$orderby`/`$select`/`$key`/
  `$search` 的裸拼接）。
- **必须**：

```
OData
 ↓
AST
 ↓
typed expression
 ↓
DuckDB parameter / expression
```

### 2.7.3 资源限制参数

以下参数以 `SET` 形式提供（示例）：

```
SET odata_max_top = 10000;
SET odata_query_timeout = '30s';
SET odata_max_concurrent_queries = 32;
```

参数清单：

```
odata_max_top
odata_max_skip
odata_max_response_bytes
odata_query_timeout
odata_max_concurrent_queries
odata_max_filter_depth
odata_max_expand_depth
```

### 2.7.4 结果集大小控制与流式序列化取向

- 应避免"整结果集物化 → 整体 JSON → HTTP"的内存放大路径：

```
Query
 ↓
整个结果集 materialize
 ↓
JSON
 ↓
HTTP        ← 不推荐
```

- 应尽可能采用分块流式路径：

```
DuckDB DataChunk
 ↓
JSON encode
 ↓
HTTP stream
 ↓
next DataChunk
 ↓
...
```

- DuckDB 面向 OLAP 大结果集场景，流式输出将成为本项目的重要技术优势；Quack
  实现同样强调批量 / 流式数据传输。
- 结论：v0.1 允许缓冲式响应以控制复杂度；流式输出列入 v0.2（见第 4 章）。

### 2.7.5 并发模型

- v0.1 为只读 GET，支持多个并发连接：

```
GET
 ↓
read-only
 ↓
multiple concurrent connections
```

- 写冲突遵循 DuckDB 自身并发模型；Quack 是多进程访问 DuckDB 的官方
  client-server 路径。
- v0.1 不解决写场景的并发问题（POST/PATCH/DELETE、事务、batch、ETag、
  乐观并发均不在首版范围）。

### 2.7.6 序列化原则

- 目标路径：`DuckDB DataChunk → OData Serializer → JSON`。
- 反对路径：`DuckDB → vector<map<string,string>> → JSON`（内存浪费明显）。

---

# 3. v0.1：只读 OData 核心

## 3.1 版本范围总览

v0.1 的目标是证明核心命题：**OData 客户端能否无感访问 DuckDB**。

交付范围：

```
v0.1
 ├── HTTP
 ├── $metadata
 ├── GET entity
 ├── $select
 ├── $filter（基础算子）
 ├── $orderby
 ├── $top
 ├── $skip
 └── $count
```

配套里程碑：M0（Skeleton）、M1（HTTP Hello World）、M2（Metadata）、
M3（Basic Entity API）、M4（Query）、M5（Security），详见 3.11。

## 3.2 扩展入口与生命周期 API

### 3.2.1 Extension 入口

```
class ODataExtension : public Extension {
public:
    std::string Name() override {
        return "odata";
    }

    void Load(ExtensionLoader &loader) override {
        // register SQL functions
    }

    std::string Version() const override {
        return "0.1.0";
    }
};
```

### 3.2.2 加载

```
INSTALL odata;
LOAD odata;
```

### 3.2.3 服务生命周期

启动：

```
CALL odata_serve(
    'http://0.0.0.0:8080'
);
```

停止：

```
CALL odata_stop();
```

状态查询：

```
SELECT * FROM odata_status();
```

## 3.3 HTTP 服务层

### 3.3.1 HTTP 后端选型

社区已存在将 DuckDB 变为 HTTP OLAP API server 的 HTTP Server extension
（支持认证等能力）。选型存在两个方案：

| 方案 | 说明 | 优势 | 劣势 |
| --- | --- | --- | --- |
| A：复用 HTTP server extension | duckdb 同时挂载 httpserver 与 odata | 可复用 HTTP 基础设施、日志、socket、TLS、认证 | 扩展依赖、API 稳定性、上游耦合 |
| B：OData 自带 HTTP 层 | duckdb 仅挂载 odata，内部含 HTTP server | 无外部扩展依赖 | 需自行维护 HTTP 基础能力 |

**决策**：v0.1 采用方案 B，但将 HTTP 抽象单独封装，便于后续替换实现：

```
class HttpServer {
public:
    virtual void Start(...);
    virtual void Stop();
};
```

### 3.3.2 路由与响应

v0.1 路由：

```
GET /odata
GET /odata/$metadata

GET /odata/{entity}
GET /odata/{entity}/{key}
```

集合响应示例（`GET /odata/customers`）：

```
{
  "@odata.context": "$metadata#customers",
  "value": [
    {
      "id": 1,
      "name": "Alice"
    },
    {
      "id": 2,
      "name": "Bob"
    }
  ]
}
```

## 3.4 EntitySet 与 Key 配置

### 3.4.1 映射规则

```
DuckDB table = OData EntitySet
```

建表：

```
CREATE TABLE customers (...);
CREATE TABLE orders (...);
CREATE TABLE products (...);
```

对应暴露路径：

```
/odata/customers
/odata/orders
/odata/products
```

### 3.4.2 暴露白名单

默认策略：**expose nothing**。否则生产库中大量内部表可能被全部暴露。

显式暴露单表：

```
CALL odata_expose('customers');
CALL odata_expose('orders');
```

按 schema 暴露：

```
CALL odata_expose_schema('public');
```

### 3.4.3 主键配置

Key 查询示例：

```
GET /odata/customers(123)
```

解析链：

```
customers
   ↓
primary key
   ↓
id
```

DuckDB schema metadata 未必总是提供关系约束信息，因此 v0.1 提供显式配置：

```
CALL odata_entity(
    'customers',
    key := 'id'
);
```

主键自动发现列入后续迭代。

`odata_entity` 的最终信息模型：

```
odata_entity
 ├── table
 ├── key
 ├── entity_name
 ├── navigation      （后续）
 └── permissions     （后续）
```

## 3.5 元数据生成（$metadata）

`$metadata` 是项目的核心能力之一：

```
GET /odata/$metadata
```

设计原则：**不维护 XML schema 文件**，而是从 DuckDB Catalog 实时生成：

```
DuckDB Catalog
       │
       ▼
CatalogEntry
       │
       ▼
TableSchema
       │
       ▼
EDM Model
       │
       ▼
EDMX XML
```

示例：DuckDB 建表

```
CREATE TABLE users (
    id BIGINT,
    name VARCHAR,
    age INTEGER,
    active BOOLEAN
);
```

自动生成：

```
<EntityType Name="users">

    <Key>
        <PropertyRef Name="id"/>
    </Key>

    <Property
        Name="id"
        Type="Edm.Int64"
        Nullable="false"/>

    <Property
        Name="name"
        Type="Edm.String"/>

    <Property
        Name="age"
        Type="Edm.Int32"/>

    <Property
        Name="active"
        Type="Edm.Boolean"/>

</EntityType>
```

## 3.6 DuckDB → OData 类型映射

类型映射应独立成层：

| DuckDB | OData |
| --- | --- |
| BOOLEAN | Edm.Boolean |
| TINYINT | Edm.SByte |
| SMALLINT | Edm.Int16 |
| INTEGER | Edm.Int32 |
| BIGINT | Edm.Int64 |
| HUGEINT | Edm.Decimal / String |
| FLOAT | Edm.Single |
| DOUBLE | Edm.Double |
| DECIMAL | Edm.Decimal |
| VARCHAR | Edm.String |
| DATE | Edm.Date |
| TIME | Edm.TimeOfDay |
| TIMESTAMP | Edm.DateTimeOffset |
| UUID | Edm.Guid |
| BLOB | Edm.Binary |

以下类型需要专门设计，**不在 MVP 阶段强制解决**：

```
LIST
STRUCT
MAP
UNION
JSON
GEOMETRY
INTERVAL
HUGEINT
```

## 3.7 查询选项

### 3.7.1 查询模型（ODataQuery）

Parser 输出：

```
struct ODataQuery {
    string entity_set;

    vector<string> select;

    unique_ptr<Expr> filter;

    vector<OrderBy> order_by;

    optional<int64_t> top;
    optional<int64_t> skip;

    bool count = false;
};
```

示例：

```
/odata/orders?
$select=id,total&
$filter=total gt 100&
$orderby=total desc&
$top=50
```

映射结果：

```
entity_set = orders

select:
  id
  total

filter:
  total > 100

order_by:
  total DESC

top:
  50
```

### 3.7.2 $filter：第一阶段文法

v0.1 支持范围（避免首版覆盖全部 OData 文法）：

```
eq
ne
gt
ge
lt
le

and
or
not

()
```

示例：

```
$filter=age gt 18
```

```
$filter=age gt 18 and active eq true
```

### 3.7.3 $select

输入：

```
$select=id,name,email
```

编译：

```
SELECT
    "id",
    "name",
    "email"
FROM "customers"
```

必须进行列存在性校验：

```
id       → valid
name     → valid
DROP...  → invalid
```

**绝不允许 `$select` 直接进入 SQL。**

### 3.7.4 $orderby

单列：

```
$orderby=name
$orderby=name desc
```

多列：

```
$orderby=country asc,name asc
```

编译：

```
ORDER BY
    "country" ASC,
    "name" ASC
```

### 3.7.5 $top / $skip

输入：

```
$top=100
$skip=200
```

编译：

```
LIMIT 100
OFFSET 200
```

必须设置服务端上限，防止超范围请求：

```
max_top = 10000        # 阻止 $top=999999999999
```

### 3.7.6 $count

请求：

```
GET /odata/customers?$count=true
```

返回：

```
{
  "@odata.count": 100234,
  "value": [...]
}
```

实现路线：

| 路线 | 说明 | 备注 |
| --- | --- | --- |
| 简单版 | 独立 `SELECT COUNT(*) FROM customers;` | v0.1 采用，代价是两次查询 |
| 复杂版 | COUNT 查询 + 数据查询合并 | 需处理快照一致性 |

### 3.7.7 $expand

**v0.1 明确不实现 `$expand`。**

原因：`$expand` 需要先定义 NavigationProperty、Association、Relationship、
Cardinality 等关系元数据，例如：

```
/orders?$expand=customer
```

其本质为 LEFT JOIN：

```
SELECT ...
FROM orders
LEFT JOIN customers
ON ...
```

纳入计划：

```
v0.1  ❌ $expand
v0.2  ❌
v0.3  ✅
```

### 3.7.8 写操作（CRUD）

**v0.1 为只读 GET（read-only OData）。**

只读约束可极大简化安全模型。写操作统一归属后续版本（以第 6 章正式路线为准，
早期草案中"V0.2 POST / V0.3 PATCH+DELETE"的表述由 v0.4 收口）。

## 3.8 SQL 编译与安全

### 3.8.1 编译输出示例

```
SELECT "id", "total"
FROM "main"."orders"
WHERE ("total" > 100)
ORDER BY "total" DESC
LIMIT 50
```

### 3.8.2 标识符转义

必须提供并统一使用：

```
string QuoteIdentifier(const string &name);
```

示例：

```
orders   →   "orders"
```

严禁直接拼接：

```
sql += user_input;   // 禁止
```

### 3.8.3 编译演进路径

阶段一（v0.1 可行）：

```
OData AST
     ↓
SQL string
     ↓
DuckDB
```

阶段二（后续迭代）：

```
OData AST
     ↓
DuckDB Expression / Logical Operator
     ↓
DuckDB Optimizer
```

阶段二的收益：能够利用 DuckDB optimizer 实现 filter pushdown（例如面向
Parquet 的谓词下推）。示例：

```
GET /odata/events?
$filter=timestamp gt 2026-01-01
```

## 3.9 AST 设计

`$filter` 示例：

```
$filter=age gt 18 and status eq 'active'
```

AST：

```
And
├── GreaterThan
│   ├── Property("age")
│   └── Literal(18)
│
└── Equal
    ├── Property("status")
    └── Literal("active")
```

C++ 描述：

```
enum class BinaryOp {
    EQ,
    NE,
    GT,
    GE,
    LT,
    LE,
    AND,
    OR
};

struct Expr {
    virtual ~Expr() = default;
};

struct PropertyExpr : Expr {
    string name;
};

struct LiteralExpr : Expr {
    Value value;
};

struct BinaryExpr : Expr {
    BinaryOp op;
    unique_ptr<Expr> left;
    unique_ptr<Expr> right;
};
```

基于该 AST，后续扩展函数能力（如 contains / startswith / endswith /
tolower / toupper / substring / length）时，无需改动 parser 主体结构。

## 3.10 序列化（v0.1）

响应示例：

```
{
  "@odata.context": "$metadata#customers",
  "value": [
    {
      "id": 1,
      "name": "Alice",
      "active": true
    }
  ]
}
```

需要处理的数据类型：

```
NULL
BOOLEAN
INTEGER
DOUBLE
DECIMAL
DATE
TIME
TIMESTAMP
UUID
VARCHAR
LIST
STRUCT
BLOB
```

MVP 范围：**scalar types only**。LIST / STRUCT 的 JSON 序列化列入后续版本。

## 3.11 v0.1 里程碑（M0–M5）

### Milestone 0 — Skeleton

验收目标：

```
INSTALL odata;
LOAD odata;
```

完成项：CMake、Extension、CI（Linux / macOS / Windows）。

### Milestone 1 — HTTP Hello World

实现：

```
odata_serve()
odata_stop()
```

以及：

```
GET /health
```

返回：

```
{
  "status": "ok"
}
```

### Milestone 2 — Metadata

实现：

```
GET /odata/$metadata
```

要求：从 DuckDB Catalog 自动生成 EDMX。

### Milestone 3 — Basic Entity API

实现：

```
GET /odata/customers
GET /odata/customers(1)
```

### Milestone 4 — Query

支持：

```
$select
$filter
$orderby
$top
$skip
$count
```

### Milestone 5 — Security（对外试用门槛）

加入：

```
Bearer token
HTTPS/reverse proxy assumptions
entity whitelist
read-only mode
query limits
```

完成 M5 后才适合对外试用。

---

# 4. v0.2：安全、分页、流式与远程执行

## 4.1 版本范围总览

```
v0.2
 ├── authentication（增强）
 ├── authorization
 ├── pagination
 ├── streaming
 └── QuackExecutor
```

## 4.2 认证方案演进

v0.1 采用最小实现：

```
Authorization: Bearer xxx
```

配置方式：

```
CALL odata_serve(
    'http://0.0.0.0:8080',
    token := 'secret'
);
```

v0.2 起可按序扩展：

```
API Key
Basic Auth
JWT
OAuth2/OIDC
```

## 4.3 授权模型

授权是数据 API 平台的重要能力，提供两类原语。

用户级授权（示例）：

```
CALL odata_grant(
    'alice',
    'customers',
    'SELECT'
);
```

列级白名单（示例）：

```
CALL odata_expose(
    'customers',
    columns := ['id', 'name']
);
```

授权决策链：

```
HTTP user
    ↓
OData entity
    ↓
authorization policy
    ↓
allowed columns / rows
    ↓
DuckDB
```

## 4.4 行级安全（Row-Level Security）

通过动态谓词实现行级过滤，例如：

```
tenant_id = current_user_tenant()
```

场景示例：

```
GET /odata/orders
Authorization: Bearer tenant-A
```

自动改写为：

```
SELECT *
FROM orders
WHERE tenant_id = 'tenant-A'
```

行级安全将项目从"HTTP wrapper"提升为真正的数据 API platform。

## 4.5 分页

- v0.1：仅 `$top` / `$skip`
- v0.2：增加 `@odata.nextLink`
- v0.3：`$skiptoken`（复杂游标分页）

## 4.6 流式输出

目标链路：

```
DuckDB DataChunk
 ↓
JSON encode
 ↓
HTTP stream
 ↓
next DataChunk
 ↓
...
```

避免以下路径（内存放大）：

```
Query
 ↓
整个结果集 materialize
 ↓
JSON
 ↓
HTTP
```

备注：Quack 的已知 issue 表明，现阶段部分响应可能在 server 侧整体物化后再发送，
这对 API gateway / reverse proxy 场景构成压力。OData server 应从设计上规避该问题。

## 4.7 QuackExecutor

Quack 的定位：

```
DuckDB Client
      │
      │ HTTP
      ▼
DuckDB Server
```

官方能力：

```
quack_serve(...)
quack_query(...)
```

设计原则：**OData 不应自行重新实现 Quack protocol**，而应通过编译产物
（SQL）选择执行后端：

```
OData
  │
  ▼
Query Compiler
  │
  ▼
SQL
  │
  ├── Local
  │     ↓
  │   DuckDB
  │
  └── Remote
        ↓
     Quack
```

### ExecutionBackend 抽象

```
class QueryExecutor {
public:
    virtual QueryResult Execute(
        const CompiledQuery &query
    ) = 0;
};
```

Local 实现：

```
class LocalDuckDBExecutor : public QueryExecutor {
};
```

Remote 实现：

```
class QuackExecutor : public QueryExecutor {
};
```

预留扩展方向：

```
ArrowExecutor
PostgresExecutor
MotherDuckExecutor
DuckLakeExecutor
```

## 4.8 远程 Gateway（odata_proxy）

注册方式：

```
CALL odata_proxy(
    'quack://db-server:8815',
    '/odata'
);
```

链路：

```
OData Client
     │
     ▼
OData Gateway
     │
     │ SQL
     ▼
Quack
     │
     ▼
DuckDB Server
```

示例：客户端请求

```
GET /odata/orders?$filter=amount gt 100
```

OData Gateway 编译：

```
SELECT *
FROM orders
WHERE amount > 100
```

随后调用：

```
quack_query(
    remote,
    sql
)
```

由此，OData 客户端完全无需理解 Quack。

## 4.9 $filter 函数扩展

v0.2 在基础算子之上增加字符串函数：

```
startswith
endswith
contains

tolower
toupper

length
substring
```

示例：

```
$filter=contains(name,'Alice')
```

可编译为：

```
contains("name", 'Alice')
```

或等价：

```
"name" LIKE '%Alice%'
```

## 4.10 $count 快照一致性优化（可选）

在 v0.2 中可评估"COUNT + 数据查询"合并执行的复杂度与快照一致性方案；
v0.1 允许两次查询作为过渡。

---

# 5. v0.3：导航属性与富类型

## 5.1 版本范围总览

```
v0.3
 ├── $expand
 ├── navigation property
 ├── functions
 └── richer types
```

## 5.2 $expand 与导航属性

示例：

```
/orders?$expand=customer
```

实施前提：在元数据层引入关系建模：

```
NavigationProperty
Association
Relationship
Cardinality
```

编译语义（LEFT JOIN）：

```
SELECT ...
FROM orders
LEFT JOIN customers
ON ...
```

## 5.3 富类型支持

- 在 v0.1 仅暴露标量类型的基础上，增加对 DuckDB 复杂类型的建模、投影与
  JSON 序列化：

```
LIST
STRUCT
MAP
UNION
JSON
GEOMETRY
INTERVAL
HUGEINT
```

## 5.4 函数能力扩展

- 在 v0.2 字符串函数基础上，按 OData 规范扩展函数集合与语义。

## 5.5 里程碑对应

### Milestone 6 — Quack（OData over DuckDB/Quack）

架构形态：

```
              OData
                │
         ┌──────┴──────┐
         │             │
      Local          Quack
         │             │
      DuckDB        DuckDB
```

说明：M6 在版本归属上可先行验证 QuackExecutor 与 Local 的并行执行；
严格按 v0.4/v1.0 的完整路线推进时，M6 亦可作为 v0.2–v0.3 阶段的验证前置。

### Milestone 7 — Navigation

实现 `$expand`：

```
/orders?$expand=customer
```

### Milestone 8 — Write

写操作及其配套能力最终收口于 v0.4：

```
POST
PUT
PATCH
DELETE

ETag
If-Match
transaction
$batch
```

---

# 6. 后续版本：v0.4 与 v1.0

## 6.1 v0.4：写操作与事务

```
v0.4
 ├── POST
 ├── PATCH
 ├── DELETE
 └── transactions
```

配套能力：ETag / If-Match（乐观并发）、$batch。

## 6.2 v1.0：合规与生产化

```
v1.0
 ├── OData compliance suite
 ├── Power BI
 ├── Excel
 ├── .NET client
 ├── security hardening
 └── production deployment
```

## 6.3 产品拆分（长期架构）

项目可演进为两个产品，代码共享度预计 70% 以上：

### `duckdb-odata`

嵌入 DuckDB：

```
DuckDB + OData
```

### `odata-quack-gateway`

独立网关：

```
OData
  ↓
Quack
  ↓
DuckDB
```

共享代码分层：

```
odata-core
 ├── parser
 ├── AST
 ├── compiler
 ├── metadata
 ├── serializer
 └── security

odata-duckdb
 └── LocalExecutor

odata-quack
 └── QuackExecutor
```

Quack 仓库（`duckdb/duckdb-quack`）应作为外部 transport 依赖 / 集成对象，
而非复制其实现。

## 6.4 推荐使用体验（长期目标）

用户仅需：

```
INSTALL odata;
LOAD odata;

CALL odata_serve(
    'http://0.0.0.0:8080',
    token := 'secret'
);
```

随后即可使用：

```
GET /odata/$metadata
GET /odata/customers
GET /odata/customers?$select=id,name
GET /odata/customers?$filter=age%20gt%2030
GET /odata/orders?$orderby=amount%20desc&$top=100
```

该能力足以让企业系统将 DuckDB 作为轻量级 OData 分析后端。

---

# 7. 测试策略

## 7.1 分层测试

### Parser tests

输入示例：

```
$filter=age gt 18
$filter=a eq 1 and b eq 2
$filter=contains(name,'foo')
```

输入 / 输出约定：输入 OData 表达式，输出期望 AST。

### Compiler tests

示例：

```
OData: $filter=age gt 18
期望：  WHERE "age" > 18
```

### Integration tests

启动 DuckDB + odata extension 后验证：

```
curl http://localhost:8080/odata/customers
```

校验返回 JSON。

### Compatibility tests

面向客户端生态：

```
Power BI
Excel
.NET OData Client
Java OData Client
Python
curl
```

## 7.2 测试形态

SQL 级测试（遵循 DuckDB extension 测试规范与 Quack 的 `test/sql` 实践）：

```
test/sql/server.test
test/sql/metadata.test
test/sql/expose.test
```

HTTP 集成测试：

```
test/http/basic.py
test/http/filter.py
test/http/metadata.py
```

---

# 8. 开发顺序建议

```
Day 1
│
├── fork extension-template
├── rename → odata
├── build
└── INSTALL/LOAD

Day 2
│
├── OData HTTP server
├── /health
└── odata_serve()

Day 3
│
├── DuckDB catalog reader
├── EDM model
└── $metadata

Day 4
│
├── /odata/{table}
└── JSON serializer

Day 5
│
├── $select
├── $top
└── $skip

Day 6-7
│
├── lexer
├── parser
└── $filter

Week 2
│
├── $orderby
├── $count
├── key lookup
└── security

Week 3
│
├── streaming
├── benchmark
└── QuackExecutor

Week 4
│
└── Power BI / .NET compatibility
```

---

# 9. 术语与版本归属说明

1. **版本路线以第 6 章（v0.1→v1.0）为正式口径。** 早期草案中出现的阶段性表述
   （例如字符串函数标注为 "V0.2"、写操作标注为 "V0.2/V0.3"）在本文档中按正式
   路线收口：
   - `$filter` 字符串函数扩展 → v0.2；
   - POST/PATCH/DELETE、事务、ETag、$batch → v0.4。
2. **Transport 解耦自始至终有效。** Local 与 Quack 共享 parser / compiler /
   metadata / serializer，仅替换 QueryExecutor 实现。
3. **只读原则贯穿 v0.1–v0.3**；写能力统一在 v0.4 引入，以持续简化安全模型。
4. **$metadata 实时生成**：不维护静态 EDMX 文件，从 DuckDB Catalog 动态生成。
5. **注入防护为硬约束**：所有查询选项（$select/$filter/$orderby/$key/$search）
   均须经 AST 与类型化编译后进入 SQL，标识符一律经 QuoteIdentifier 转义。
