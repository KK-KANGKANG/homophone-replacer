# Linux / Windows HTTP 服务部署设计

## 1. 背景

当前项目提供 `homophone-replacer-standalone` 命令行程序。每次启动都会重新加载 `lexicon.txt` 和约 57 MB 的 `replace.fst`，适合人工验证，不适合持续接收 ASR 请求。

本设计新增一个跨平台常驻 HTTP 服务，在进程启动时加载一次规则，并在 Linux 和 Windows 上提供一致的接口、配置和部署方式。现有命令行程序继续保留。

## 2. 目标

- 新增 `homophone-replacer-server`，通过局域网 HTTP 接口提供文本替换能力。
- 支持 Linux x64 和 Windows x64。
- 面向并发 10～50 的持续 ASR 请求。
- 不提供访问令牌，但部署文档必须要求使用防火墙限制可信局域网来源。
- 支持可选的临时 `mapping.txt`；修改后重启服务生效。
- 同时支持前台手动启动、Linux systemd 和 Windows Service。
- 提供可直接部署的 Linux 与 Windows 压缩包，不要求目标服务器安装 Python 或 Pynini。

## 3. 非目标

- 不提供公网部署能力、HTTPS、访问令牌或用户权限系统。
- 不提供批量文本接口。
- 不提供规则文件上传、在线编辑或热更新。
- 不在本次实现英文或中英混合词映射。
- 不替换或破坏现有命令行程序。

## 4. 技术方案选择

采用 C++ 原生 HTTP 服务，复用现有 `HomophoneReplacer`：

```text
局域网客户端
    → HTTP 服务
    → 有界请求队列
    → 替换工作线程
    → HomophoneReplacer
    → lexicon.txt + replace.fst + 可选 mapping.txt
```

不采用 Python 包装层，避免同时部署 Python、C++ 动态库及额外依赖；不采用独立网关与替换进程，避免引入当前规模不需要的进程通信和重连机制。

服务层使用以下固定版本的跨平台 C++ 依赖，并通过 CMake 管理：

- `cpp-httplib`：HTTP Server。
- `nlohmann/json`：配置和 HTTP JSON。
- `spdlog`：控制台、滚动文件和跨平台日志。

依赖版本必须在 CMake 中固定，发布包只包含运行所需文件，不要求目标服务器在线下载依赖。

项目最终生成两个程序：

```text
homophone-replacer-standalone  # 现有命令行工具
homophone-replacer-server      # 新增常驻 HTTP 服务
```

## 5. 服务生命周期

启动顺序：

1. 确定部署根目录并读取 `service.json`。
2. 应用命令行覆盖参数。
3. 检查 `lexicon.txt` 和 `replace.fst`。
4. 根据配置决定是否加载 `mapping.txt`。
5. 创建替换工作线程及各自的 `HomophoneReplacer` 实例。
6. 所有必需实例初始化成功后开放 HTTP 端口。

如果 lexicon、FST、配置格式或端口存在错误，服务必须打印明确原因并退出，不能在不可用状态下开放端口。

停止顺序：

1. 停止接受新请求。
2. 等待正在执行的请求完成。
3. 最长等待 10 秒。
4. 释放替换器和 FST 后退出。

Linux 信号、Windows Service 停止事件和前台 `Ctrl+C` 使用相同的退出流程。

## 6. 并发模型

不假定现有 FST 对象支持多线程同时调用。采用固定工作线程池，每个工作线程拥有独立的 `HomophoneReplacer` 实例。

- `worker_count` 默认值为 2。
- 合法范围为 1～4。
- 请求进入有界队列，默认容量为 100。
- 队列已满时立即返回 HTTP 429。

该设计以额外内存换取明确的线程安全。最终工作线程默认值需要通过 Linux 和 Windows 压测验证；内存较小的部署可以配置为 1。

## 7. HTTP API

### 7.1 文本替换

```http
POST /replace
Content-Type: application/json
```

请求：

```json
{
  "text": "左侧乱潮"
}
```

成功响应：

```json
{
  "code": 0,
  "message": "ok",
  "result": "左侧卵巢",
  "processing_ms": 0.2
}
```

约束：

- `text` 必填且必须为字符串。
- 请求和响应统一使用 UTF-8 JSON。
- 空文本返回 HTTP 400。
- 默认最多接收 10,000 个 UTF-8 字符；超过限制返回 HTTP 413。
- 第一版只接受单条文本，不返回分词和拼音调试数据。

### 7.2 健康检查

```http
GET /health
```

响应示例：

```json
{
  "status": "ok",
  "version": "1.0.0",
  "rules": {
    "lexicon_loaded": true,
    "fst_loaded": true,
    "mapping_enabled": false,
    "mapping_loaded": false
  }
}
```

健康检查不返回服务器绝对路径。

### 7.3 错误格式

```json
{
  "code": 40001,
  "message": "text is required"
}
```

状态码：

- 200：替换成功。
- 400：JSON、字段类型或文本不合法。
- 404：接口不存在。
- 413：文本超过限制。
- 429：请求队列已满。
- 500：单次替换发生内部错误。
- 503：服务未准备好或正在退出。

单次请求异常不得导致服务进程退出。

## 8. 配置

默认配置文件：

```text
config/service.json
```

示例：

```json
{
  "server": {
    "host": "0.0.0.0",
    "port": 18080,
    "worker_count": 2,
    "queue_capacity": 100,
    "max_text_characters": 10000
  },
  "rules": {
    "lexicon": "data/hr-files/lexicon.txt",
    "fst": "data/hr-files/replace.fst",
    "mapping_enabled": false,
    "mapping": "data/hr-files/mapping.txt"
  },
  "logging": {
    "level": "info",
    "directory": "logs",
    "max_file_mb": 50,
    "max_files": 10
  }
}
```

相对路径统一以部署根目录为基准。命令行允许覆盖少量关键配置：

```bash
homophone-replacer-server \
  --config config/service.json \
  --host 0.0.0.0 \
  --port 18080 \
  --mapping data/hr-files/mapping.txt
```

优先级：

```text
命令行参数 > service.json > 程序默认值
```

命令行传入 `--mapping FILE` 时，自动视为本次启动启用 mapping，无需同时修改 `mapping_enabled`。

## 9. mapping.txt 行为

`mapping.txt` 是临时补充规则，优先级高于正式 FST：

```text
mapping.txt 临时规则 > replace.fst 长期规则
```

具体行为：

- `mapping_enabled=false`：不读取 mapping，只使用 FST。
- `mapping_enabled=true` 且文件存在：启动时加载 mapping。
- `mapping_enabled=true` 但文件不存在：记录警告，继续启动并只使用 FST。
- mapping 内容发生变化后必须重启服务。
- 不提供文件监控和热加载。

## 10. 日志

- 支持 `debug`、`info`、`warning` 和 `error` 等级。
- 默认写入部署根目录下的 `logs/`。
- 单文件默认上限 50 MB，默认保留 10 个文件。
- 记录启动、停止、配置校验、规则加载、请求状态、文本长度和处理耗时。
- 默认不记录完整 ASR 文本，避免泄露业务内容。

## 11. Linux 部署

发布包：

```text
homophone-replacer-linux-x64/
├── bin/homophone-replacer-server
├── bin/homophone-replacer-standalone
├── config/service.json
├── data/hr-files/lexicon.txt
├── data/hr-files/replace.fst
├── data/hr-files/mapping.txt
├── scripts/run-server.sh
├── scripts/install-service.sh
├── scripts/uninstall-service.sh
└── README.md
```

### 11.1 手动验证

```bash
./scripts/run-server.sh
./scripts/run-server.sh config/service-test.json
```

脚本自动切换到部署根目录、设置动态库路径、检查程序与配置，并以前台模式启动。按 `Ctrl+C` 停止。

### 11.2 systemd

正式安装目录为 `/opt/homophone-replacer/`。安装脚本创建 `homophone-replacer.service`，设置固定工作目录、开机启动和异常自动重启。

```bash
systemctl start homophone-replacer
systemctl stop homophone-replacer
systemctl restart homophone-replacer
systemctl status homophone-replacer
```

## 12. Windows 部署

发布包：

```text
homophone-replacer-windows-x64/
├── bin/homophone-replacer-server.exe
├── bin/homophone-replacer-standalone.exe
├── config/service.json
├── data/hr-files/lexicon.txt
├── data/hr-files/replace.fst
├── data/hr-files/mapping.txt
├── scripts/run-server.bat
├── scripts/install-service.bat
├── scripts/uninstall-service.bat
├── scripts/start-service.bat
├── scripts/stop-service.bat
└── README.md
```

### 12.1 手动验证

```cmd
scripts\run-server.bat
scripts\run-server.bat config\service-test.json
```

脚本自动切换到部署根目录、检查程序与配置，并以前台模式启动。按 `Ctrl+C` 停止。

### 12.2 Windows Service

同一个服务程序支持前台模式和 Windows Service 模式：

```cmd
bin\homophone-replacer-server.exe --config config\service.json
bin\homophone-replacer-server.exe --service --config config\service.json
```

安装脚本使用 Windows 自带的 `sc.exe` 注册服务，不依赖 NSSM。

## 13. 局域网安全边界

服务不实现访问令牌，因此：

- 只允许部署在可信局域网。
- Linux 使用防火墙限制允许访问的来源网段。
- Windows 使用 Windows 防火墙限制允许访问的来源网段。
- 不允许将端口直接映射到公网。

## 14. 测试

### 14.1 功能测试

- `左侧乱潮 → 左侧卵巢`。
- 正确词汇保持不变。
- 无调冲突词不猜测。
- `mapping_enabled=false` 时只使用 FST。
- `mapping_enabled=true` 时 mapping 优先于 FST。
- mapping 文件不存在时记录警告并继续使用 FST。
- 空文本、错误 JSON、错误字段类型和超长文本返回正确错误码。
- `/health` 正确报告规则加载状态。
- 前台模式、systemd 和 Windows Service 的替换结果一致。

### 14.2 并发测试

- 并发连接数 50。
- 连续请求至少 10,000 次。
- 记录平均响应时间、P95 和 P99。
- 服务不得崩溃、泄漏请求或返回错误结果。
- 队列满时返回 HTTP 429，不能无限增长。

### 14.3 部署验证

Linux：

```bash
./scripts/run-server.sh
curl http://127.0.0.1:18080/health
curl -X POST http://127.0.0.1:18080/replace \
  -H 'Content-Type: application/json' \
  -d '{"text":"左侧乱潮"}'
```

Windows：

```cmd
scripts\run-server.bat
curl http://127.0.0.1:18080/health
curl -X POST http://127.0.0.1:18080/replace ^
  -H "Content-Type: application/json" ^
  -d "{\"text\":\"左侧乱潮\"}"
```

## 15. 验收标准

- Linux x64 与 Windows x64 发布包均能独立运行。
- 目标服务器不需要安装 Python 或 Pynini。
- 服务启动时加载一次规则，后续请求复用替换器。
- 并发 50、连续 10,000 次请求通过验证。
- 可选 mapping 行为和优先级符合本设计。
- 手动启动脚本和系统服务模式均可用。
- README 包含配置、部署、服务管理、接口调用和常见故障排查说明。
