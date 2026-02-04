# bsrvcore 测试架构与计划

> 本文档基于仓库当前实现（public headers + src/internal），用于指导和记录测试策略、范围与运行方式。

## 目标

- 覆盖核心公共 API 的正确性、边界条件、错误传播与生命周期行为。
- 覆盖路由/AOP/会话/上下文等关键模块的功能与线程安全特性。
- 提供稳定、可复现、可在 CI 与本地运行的测试套件。
- 为压力/并发/长跑测试提供可控参数与诊断信息。

## 模块与覆盖范围

### 1) Unit（单元测试）

| 模块 | 目标 | 主要断言/边界 | 备注 |
| --- | --- | --- | --- |
| `Context` | 线程安全 KV 容器 | Set/Get/Has 的正确性；并发写入与读取 | 只依赖 public header |
| `Attribute`/`CloneableAttribute` | 多态属性语义 | Clone 深拷贝；Type/Equals/Hash 默认语义 | 只依赖 public header |
| `ServerSetCookie` | Set-Cookie 生成 | 缺少 name/value 返回空；SameSite/HttpOnly/Secure/Max-Age/Path/Domain 组合 | 只依赖 public header |
| `HttpRouteTable`（内部） | 路由匹配与参数提取 | 参数路由/专属路由/默认路由/无效 path 处理 | 需要 internal 头（src/include/bsrvcore/internal） |
| `HttpRequestHandler`/`FunctionRouteHandler` | 异常吞吐与日志路径 | handler 抛异常时不崩溃 | 通过 mock logger（gmock）验证日志调用 |

### 2) Integration（集成测试）

| 场景 | 目标 | 主要断言 | 备注 |
| --- | --- | --- | --- |
| 最小 HTTP server | 端到端路由处理 | GET/POST 正常响应；body/headers 正确 | 使用 Boost.Beast 同进程客户端 |
| AOP 顺序 | Pre/Post 顺序 | 全局/方法/路由 aspect 顺序与 reverse post | 使用响应 body 标记验证顺序 |
| Session 与 Cookie | sessionId 生成与回写 | 无 cookie 时生成 sessionId，并 Set-Cookie | 用测试连接捕获响应头 |

### 3) Stress（压力/并发/长跑）

| 场景 | 目标 | 断言/阈值 | 参数控制 |
| --- | --- | --- | --- |
| 高并发 `Context` 写入/读取 | 验证锁与数据一致性 | 不死锁；最终统计一致 | `BSRVCORE_STRESS_THREADS/ITERATIONS/SEED` |
| `HttpServer::Post` 任务洪泛 | 并发执行可靠性 | 所有任务完成，超时失败 | `BSRVCORE_STRESS_THREADS/ITERATIONS/SEED` |
| 轻量端到端吞吐 | 基本吞吐回归 | N 次请求在合理时间内完成 | `BSRVCORE_STRESS_ITERATIONS/TIMEOUT_MS` |

> 压力测试默认 **OFF**，仅在 `BSRVCORE_ENABLE_STRESS_TESTS=ON` 时构建与运行。

## 单元/集成/压力边界

- **Unit**：不依赖网络/线程池或只用最小同步；不启动真实 server。
- **Integration**：启动 `HttpServer`，使用本地 loopback 进行真实 HTTP 往返。
- **Stress**：强调并发/吞吐/长跑；提供超时与可重复随机种子。

## 关键不变量与边界

- 路由参数匹配应正确提取参数值，并区分专属路由与参数路由。
- `HttpServer` 运行时不得允许配置修改（现有测试已覆盖）。
- Cookie 生成必须处理缺失字段，SameSite=None 强制 Secure。
- 会话 id 生成应稳定可回写为 Set-Cookie。
- 异常处理：`FunctionRouteHandler` 中异常应被吞掉并记录日志。

## 可复现性与诊断

- 所有压力测试使用固定 seed（默认常量），允许通过环境变量覆盖。
- 失败时打印 seed、线程数、迭代次数、最后一次操作索引。
- 所有并发测试含超时（避免死锁）并提供明确错误信息。

## 运行方式

### 本地

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo -DBSRVCORE_BUILD_TESTS=ON
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

### 仅运行单元/集成/压力

```bash
ctest --test-dir build -L unit
ctest --test-dir build -L integration
ctest --test-dir build -L stress
```

### 启用压力测试

```bash
cmake -S . -B build -DBSRVCORE_BUILD_TESTS=ON -DBSRVCORE_ENABLE_STRESS_TESTS=ON
cmake --build build --parallel
ctest --test-dir build -L stress --output-on-failure
```

### 压力测试参数（环境变量）

- `BSRVCORE_STRESS_THREADS`：线程数（默认 8）
- `BSRVCORE_STRESS_ITERATIONS`：迭代次数（默认 5000）
- `BSRVCORE_STRESS_SEED`：随机种子（默认 1337）
- `BSRVCORE_STRESS_TIMEOUT_MS`：超时（默认 5000）

## 运行时预算

- Unit：< 1s
- Integration：< 3s
- Stress：默认关闭；开启时 < 10s（可调）

## Sanitize / Coverage 建议

- ASan/UBSan/TSan：建议在 `RelWithDebInfo` 或 `Debug` 运行。
- Coverage：若后续启用，可在 CI 另行配置，不作为必需步骤。

## 测试可见性与依赖说明

- 大多数测试仅使用 public headers。
- 路由、会话内部结构测试需要 `src/include/bsrvcore/internal` 头，属于“仅测试可见”用法，已在 CMake 中明确说明。
- 未更改 public API；如后续需要 test hook，将使用编译时开关并记录于此文档。
