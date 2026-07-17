# Linux / Windows HTTP Service Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 新增一个可在 Linux 和 Windows 常驻运行的 C++ HTTP 服务，复用现有同音词替换核心，支持可选 mapping 临时规则、前台验证、系统服务安装和跨平台发布包。

**Architecture:** 保留 `homophone-replacer-standalone`，新增 `homophone-replacer-server`。HTTP 请求进入有界工作队列，每个工作线程持有独立的 `HomophoneReplacer`；配置、API 处理、线程池、平台服务适配和发布脚本保持独立边界。

**Tech Stack:** C++17、CMake 3.13+、kaldifst/OpenFST、cpp-httplib 0.18.1、nlohmann/json 3.11.3、spdlog 1.15.3、CTest、Python 3 标准库、systemd、Windows SCM。

## Execution Status

- Completed locally: Tasks 1～9, macOS Release build, all CTest tests, Python regression tests, Docker Pynini tests, install-tree verification and 10,000-request load test.
- Verified load result: 10,000 requests, concurrency 50, 0 errors, average 5.965 ms, P95 9.669 ms, P99 12.342 ms.
- Linux amd64 verified in a clean Debian Docker container. Linux ARM64 was rebuilt against Ubuntu 22.04, deployed to `10.128.33.91:/home/teamhd/homophone-replacer`, and verified through the real `/health` and `/replace` endpoints on port 18080. Actual systemd registration has not been enabled during this manual validation.
- Platform verification still required: Windows x64 MSVC build, foreground launch and SCM lifecycle. These cannot be claimed from the current macOS environment.
- Git commits in individual tasks were intentionally skipped because repository `AGENTS.md` requires explicit user approval before Git operations.

## Global Constraints

- 保留现有 `homophone-replacer-standalone` 行为和命令行参数。
- 新服务只提供 `POST /replace` 和 `GET /health`。
- 服务面向可信局域网，不实现 HTTPS、访问令牌和权限系统。
- 默认监听 `0.0.0.0:18080`，默认 2 个工作线程，允许 1～4 个，队列默认容量 100。
- 单条文本默认最多 10,000 个 UTF-8 字符。
- 每个工作线程独占一个 `HomophoneReplacer`，不跨线程共享 FST 对象。
- `mapping_enabled=false` 时不加载 mapping；启用但文件不存在时记录警告并继续使用 FST。
- mapping 临时规则优先于 FST；修改规则文件后重启服务生效，不实现热加载。
- 相对路径以部署根目录，即服务进程工作目录为基准。
- 默认日志不记录完整 ASR 文本。
- Linux 和 Windows 发布包不依赖 Python、Pynini 或在线下载。
- 每个实现任务遵循 TDD；完成前运行全量构建、测试、HTTP 冒烟和并发验证。

## Planned File Structure

```text
cmake/http-service-deps.cmake          固定并加载 HTTP、JSON、日志依赖
config/service.json                    跨平台默认配置
src/server/service-config.h/.cc        配置类型、JSON 解析、命令行覆盖和路径解析
src/server/text-replacer.h/.cc         替换接口及 HomophoneReplacer 适配器
src/server/replacer-pool.h/.cc         有界队列和独占替换器工作线程
src/server/service-api.h/.cc           与网络无关的请求校验和响应生成
src/server/http-server.h/.cc           cpp-httplib 路由和 HTTP 生命周期
src/server/service-runner.h/.cc        前台运行、停止协调和共享启动入口
src/server/windows-service.cc          Windows SCM 适配，非 Windows 不编译
src/server/main.cc                     服务命令行入口
tests/service-config-test.cc           配置和 mapping 降级行为
tests/replacer-pool-test.cc            队列、并发和关闭行为
tests/service-api-test.cc              JSON、状态码和健康检查
tests/http-server-smoke-test.py        真实端口冒烟和并发回归
scripts/run-server.sh/.bat             解压后前台启动
scripts/install-service.sh             Linux systemd 安装
scripts/uninstall-service.sh           Linux systemd 卸载
scripts/install-service.bat            Windows Service 安装
scripts/uninstall-service.bat          Windows Service 卸载
scripts/start-service.bat              Windows Service 启动
scripts/stop-service.bat               Windows Service 停止
scripts/package-linux.sh               Linux 发布包
scripts/package-windows.bat            Windows 发布包
packaging/homophone-replacer.service   systemd 模板
```

---

### Task 1: Add Pinned Service Dependencies and Configuration Model

**Files:**
- Create: `cmake/http-service-deps.cmake`
- Create: `config/service.json`
- Create: `src/server/service-config.h`
- Create: `src/server/service-config.cc`
- Create: `tests/service-config-test.cc`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Produces: `ServiceConfig LoadServiceConfig(const std::filesystem::path &, const ServiceOverrides &)`
- Produces: `ServiceOverrides ParseServiceOverrides(int argc, char **argv)`
- Produces: `std::filesystem::path ResolveFromRoot(const std::filesystem::path &root, const std::filesystem::path &value)`
- Consumes: `nlohmann::json`, `spdlog`

- [ ] **Step 1: Add failing configuration tests**

Create `tests/service-config-test.cc` with a small `CHECK` macro matching the existing C++ tests. Cover defaults, JSON values, command-line precedence and mapping behavior:

```cpp
int main() {
  TempDirectory temp;
  Write(temp.path() / "service.json", R"({
    "server":{"host":"127.0.0.1","port":19090,"worker_count":3},
    "rules":{"lexicon":"data/lexicon.txt","fst":"data/replace.fst",
             "mapping_enabled":false,"mapping":"data/mapping.txt"}
  })");

  ServiceOverrides overrides;
  overrides.port = 20000;
  auto config = LoadServiceConfig(temp.path() / "service.json", overrides);
  CHECK(config.server.host == "127.0.0.1");
  CHECK(config.server.port == 20000);
  CHECK(config.server.worker_count == 3);
  CHECK(!config.rules.mapping_enabled);

  overrides.mapping = temp.path() / "override.txt";
  config = LoadServiceConfig(temp.path() / "service.json", overrides);
  CHECK(config.rules.mapping_enabled);
  CHECK(config.rules.mapping.filename() == "override.txt");
  return 0;
}
```

Also assert that worker counts 0 and 5, port 0, queue capacity 0 and max text length 0 throw `std::invalid_argument`.

- [ ] **Step 2: Register the test and verify failure**

Add `service-config-test` to CMake without implementation sources, then run:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --target service-config-test -j4
```

Expected: compilation fails because `service-config.h` and its functions do not exist.

- [ ] **Step 3: Pin third-party dependencies**

Create `cmake/http-service-deps.cmake` using `FetchContent_Declare` with exact tags:

```cmake
include(FetchContent)

set(HTTPLIB_USE_OPENSSL_IF_AVAILABLE OFF CACHE BOOL "" FORCE)
FetchContent_Declare(cpp_httplib
  GIT_REPOSITORY https://github.com/yhirose/cpp-httplib.git
  GIT_TAG v0.18.1
  GIT_SHALLOW TRUE)
FetchContent_Declare(nlohmann_json
  GIT_REPOSITORY https://github.com/nlohmann/json.git
  GIT_TAG v3.11.3
  GIT_SHALLOW TRUE)
FetchContent_Declare(spdlog
  GIT_REPOSITORY https://github.com/gabime/spdlog.git
  GIT_TAG v1.15.3
  GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(cpp_httplib nlohmann_json spdlog)
```

The tags above are mandatory; do not replace them with floating branches such as `main` or `master`.

- [ ] **Step 4: Implement configuration types and parsing**

Define focused types in `service-config.h`:

```cpp
struct ServerSettings {
  std::string host = "0.0.0.0";
  uint16_t port = 18080;
  size_t worker_count = 2;
  size_t queue_capacity = 100;
  size_t max_text_characters = 10000;
};

struct RuleSettings {
  std::filesystem::path lexicon = "data/hr-files/lexicon.txt";
  std::filesystem::path fst = "data/hr-files/replace.fst";
  bool mapping_enabled = false;
  std::filesystem::path mapping = "data/hr-files/mapping.txt";
};

struct LoggingSettings {
  std::string level = "info";
  std::filesystem::path directory = "logs";
  size_t max_file_mb = 50;
  size_t max_files = 10;
};

struct ServiceConfig {
  std::filesystem::path root;
  ServerSettings server;
  RuleSettings rules;
  LoggingSettings logging;
};

struct ServiceOverrides {
  std::optional<std::filesystem::path> config;
  std::optional<std::string> host;
  std::optional<uint16_t> port;
  std::optional<std::filesystem::path> mapping;
  bool service_mode = false;
};
```

Resolve all relative rule and logging paths against `std::filesystem::current_path()`. `--mapping FILE` must set `mapping_enabled=true`. Validate numeric ranges and reject unknown CLI arguments with a usage error.

- [ ] **Step 5: Add the default configuration**

Create `config/service.json` exactly matching the approved design, with `mapping_enabled` set to `false`.

- [ ] **Step 6: Run tests**

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --target service-config-test -j4
ctest --test-dir build -R service-config --output-on-failure
```

Expected: `service-config` passes.

- [ ] **Step 7: Commit**

```bash
git add CMakeLists.txt cmake/http-service-deps.cmake config/service.json \
  src/server/service-config.h src/server/service-config.cc \
  tests/service-config-test.cc
git commit -m "feat(service): 增加服务配置解析"
```

---

### Task 2: Introduce the Replacer Adapter and Bounded Worker Pool

**Files:**
- Create: `src/server/text-replacer.h`
- Create: `src/server/text-replacer.cc`
- Create: `src/server/replacer-pool.h`
- Create: `src/server/replacer-pool.cc`
- Create: `tests/replacer-pool-test.cc`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: `ServiceConfig`, `HomophoneReplacerConfig`, `HomophoneReplacer`
- Produces: `class TextReplacer { virtual std::string Replace(const std::string &) = 0; }`
- Produces: `using TextReplacerFactory = std::function<std::unique_ptr<TextReplacer>()>`
- Produces: `std::optional<std::future<std::string>> ReplacerPool::Submit(std::string)`
- Produces: `void ReplacerPool::Stop(std::chrono::milliseconds timeout)`

- [ ] **Step 1: Write failing worker-pool tests**

Use a fake replacer that records its instance ID and optionally blocks:

```cpp
class FakeReplacer final : public TextReplacer {
 public:
  explicit FakeReplacer(int id) : id_(id) {}
  std::string Replace(const std::string &text) override {
    return std::to_string(id_) + ":" + text;
  }
 private:
  int id_;
};
```

Test that:

- a pool with two workers accepts and completes requests;
- factory is called exactly twice;
- queue capacity is enforced and the next `Submit` returns `std::nullopt`;
- `Stop` rejects new submissions and joins workers;
- exceptions from `Replace` propagate through the returned future without stopping other workers.

- [ ] **Step 2: Run the target and confirm failure**

```bash
cmake --build build --target replacer-pool-test -j4
```

Expected: compilation fails because `TextReplacer` and `ReplacerPool` are undefined.

- [ ] **Step 3: Implement the adapter**

`HomophoneTextReplacer` owns one `HomophoneReplacer`. Build its config as follows:

```cpp
HomophoneReplacerConfig core(
    config.rules.lexicon.string(), config.rules.fst.string(), false);
if (config.rules.mapping_enabled &&
    std::filesystem::exists(config.rules.mapping)) {
  core.rules_file = config.rules.mapping.string();
}
```

If mapping is enabled but missing, log one warning and leave `rules_file` empty. Lexicon or FST validation errors must throw during factory creation so the port never opens.

- [ ] **Step 4: Implement the bounded pool**

Use `std::deque<Job>`, `std::mutex`, `std::condition_variable` and one worker thread per replacer. Each `Job` contains the input and `std::promise<std::string>`. Enforce capacity while holding the mutex. Workers must finish queued jobs during graceful shutdown until the deadline; after the deadline, fail remaining promises with `std::runtime_error("service is stopping")`.

- [ ] **Step 5: Run focused and existing core tests**

```bash
cmake --build build --target replacer-pool-test runtime-rule-matcher-test -j4
ctest --test-dir build -R "replacer-pool|runtime-rule-matcher" --output-on-failure
```

Expected: both tests pass.

- [ ] **Step 6: Commit**

```bash
git add CMakeLists.txt src/server/text-replacer.h src/server/text-replacer.cc \
  src/server/replacer-pool.h src/server/replacer-pool.cc \
  tests/replacer-pool-test.cc
git commit -m "feat(service): 增加替换工作线程池"
```

---

### Task 3: Implement Network-Independent API Handling

**Files:**
- Create: `src/server/service-api.h`
- Create: `src/server/service-api.cc`
- Create: `tests/service-api-test.cc`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: `ReplacerPool`, `ServiceConfig`
- Produces: `struct ApiResponse { int status; std::string body; }`
- Produces: `ApiResponse ServiceApi::Replace(std::string_view json_body)`
- Produces: `ApiResponse ServiceApi::Health() const`
- Produces: `void ServiceApi::SetStopping()`

- [ ] **Step 1: Write failing API tests**

Create a pool using a fake replacer and verify exact response status and JSON fields for:

```cpp
auto ok = api.Replace(R"({"text":"左侧乱潮"})");
CHECK(ok.status == 200);
auto body = nlohmann::json::parse(ok.body);
CHECK(body["code"] == 0);
CHECK(body["result"] == "左侧卵巢");
CHECK(body["processing_ms"].is_number());
```

Also cover malformed JSON, missing `text`, non-string `text`, empty text, text over the UTF-8 character limit, a full queue, a worker exception and stopping state. Assert statuses 400, 413, 429, 500 and 503 respectively.

- [ ] **Step 2: Verify failure**

```bash
cmake --build build --target service-api-test -j4
```

Expected: compilation fails because `ServiceApi` does not exist.

- [ ] **Step 3: Implement UTF-8 character counting and common errors**

Add an internal function that validates UTF-8 continuation bytes and counts code points. Invalid UTF-8 returns HTTP 400. Generate all responses through one helper:

```cpp
ApiResponse Error(int status, int code, std::string message) {
  return {status, nlohmann::json{{"code", code},
                                 {"message", std::move(message)}}.dump()};
}
```

- [ ] **Step 4: Implement replace and health behavior**

Measure only queue wait plus replacement execution for `processing_ms`. Health JSON must include version, lexicon/FST loaded flags and mapping enabled/loaded flags, without absolute paths.

- [ ] **Step 5: Run tests**

```bash
cmake --build build --target service-api-test -j4
ctest --test-dir build -R service-api --output-on-failure
```

Expected: `service-api` passes.

- [ ] **Step 6: Commit**

```bash
git add CMakeLists.txt src/server/service-api.h src/server/service-api.cc \
  tests/service-api-test.cc
git commit -m "feat(service): 增加替换 HTTP 接口逻辑"
```

---

### Task 4: Add the HTTP Server and Foreground Executable

**Files:**
- Create: `src/server/http-server.h`
- Create: `src/server/http-server.cc`
- Create: `src/server/service-runner.h`
- Create: `src/server/service-runner.cc`
- Create: `src/server/main.cc`
- Create: `tests/http-server-smoke-test.py`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: `ServiceConfig`, `ReplacerPool`, `ServiceApi`, cpp-httplib, spdlog
- Produces: `int RunService(const ServiceConfig &, StopController &)`
- Produces: `class StopController { void RequestStop(); bool IsStopping() const; }`
- Produces: executable `homophone-replacer-server`

- [ ] **Step 1: Write a failing real-port smoke test**

Create `tests/http-server-smoke-test.py` using only Python standard library. It must:

1. reserve a free localhost port;
2. create a temporary config pointing at repository `data/hr-files`;
3. start `homophone-replacer-server` in the repository root;
4. poll `/health` for at most 10 seconds;
5. POST `{"text":"左侧乱潮"}` and assert `左侧卵巢`;
6. terminate the process and assert clean exit within 10 seconds.

Register the test only when `Python3::Interpreter` is found.

- [ ] **Step 2: Verify failure**

```bash
cmake --build build --target homophone-replacer-server -j4
ctest --test-dir build -R http-server-smoke --output-on-failure
```

Expected: target or executable is missing.

- [ ] **Step 3: Implement HTTP routing**

Configure cpp-httplib routes:

```cpp
server.Get("/health", ...);
server.Post("/replace", ...);
server.set_error_handler(...);  // JSON 404/500 responses
server.set_payload_max_length(max_payload_bytes);
```

Only accept `application/json` for `/replace`. Always set `Content-Type: application/json; charset=utf-8`.

- [ ] **Step 4: Implement logging**

Initialize spdlog console and rotating file sinks from `LoggingSettings`. Log startup configuration without absolute rule paths or request text. Per request log method, route, status, input character count and elapsed milliseconds.

- [ ] **Step 5: Implement foreground lifecycle**

On Linux, install handlers for `SIGINT` and `SIGTERM`; handlers only set an atomic flag and trigger the safe stop path outside signal context. On Windows foreground mode, handle `CTRL_C_EVENT` and `CTRL_CLOSE_EVENT` through `SetConsoleCtrlHandler`.

- [ ] **Step 6: Add the server executable**

`src/server/main.cc` must parse overrides, load the config, choose foreground or Windows Service mode, log fatal startup errors and return non-zero. Unknown arguments print usage:

```text
homophone-replacer-server [--config FILE] [--host HOST] [--port PORT]
                          [--mapping FILE] [--service]
```

- [ ] **Step 7: Run smoke and regression tests**

```bash
cmake --build build -j4
ctest --test-dir build --output-on-failure
python3 tests/http-server-smoke-test.py \
  --server build/bin/homophone-replacer-server
```

Expected: all tests pass and the server exits cleanly.

- [ ] **Step 8: Commit**

```bash
git add CMakeLists.txt src/server/http-server.h src/server/http-server.cc \
  src/server/service-runner.h src/server/service-runner.cc src/server/main.cc \
  tests/http-server-smoke-test.py
git commit -m "feat(service): 增加常驻 HTTP 服务"
```

---

### Task 5: Add Manual Linux and Windows Launch Scripts

**Files:**
- Create: `scripts/run-server.sh`
- Create: `scripts/run-server.bat`
- Modify: `tests/http-server-smoke-test.py`

**Interfaces:**
- Consumes: packaged `bin/homophone-replacer-server`, default `config/service.json`
- Produces: cross-platform foreground launch commands

- [ ] **Step 1: Extend the smoke test for the Linux script**

Add `--launcher` support so the test can start either the executable directly or `scripts/run-server.sh` with a temporary config. Verify the script works when invoked from `/tmp`, proving that it changes to the deployment root.

- [ ] **Step 2: Verify failure**

```bash
python3 tests/http-server-smoke-test.py \
  --launcher scripts/run-server.sh \
  --server build/bin/homophone-replacer-server
```

Expected: failure because the script does not exist.

- [ ] **Step 3: Implement `run-server.sh`**

The script must use `set -euo pipefail`, derive its parent deployment root, accept an optional config path, export the package library directory and use `exec`:

```bash
ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
CONFIG="${1:-config/service.json}"
cd "${ROOT_DIR}"
export LD_LIBRARY_PATH="${ROOT_DIR}/lib:${LD_LIBRARY_PATH:-}"
exec "${ROOT_DIR}/bin/homophone-replacer-server" --config "${CONFIG}"
```

Before `exec`, fail clearly if the executable or config is absent.

- [ ] **Step 4: Implement `run-server.bat`**

Use `%~dp0..` as the deployment root, accept `%~1` as the optional config, `pushd` to the root and invoke `bin\homophone-replacer-server.exe --config ...`. Preserve the executable exit code after `popd`.

- [ ] **Step 5: Run validation**

```bash
bash -n scripts/run-server.sh
python3 tests/http-server-smoke-test.py \
  --launcher scripts/run-server.sh \
  --server build/bin/homophone-replacer-server
```

On Windows, run:

```cmd
scripts\run-server.bat config\service.json
```

Expected: both platforms expose `/health` and `/replace`.

- [ ] **Step 6: Commit**

```bash
git add scripts/run-server.sh scripts/run-server.bat \
  tests/http-server-smoke-test.py
git commit -m "feat(service): 增加跨平台手动启动脚本"
```

---

### Task 6: Add Linux systemd Deployment

**Files:**
- Create: `packaging/homophone-replacer.service`
- Create: `scripts/install-service.sh`
- Create: `scripts/uninstall-service.sh`
- Create: `tests/linux-packaging-test.py`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Produces: install tree under `/opt/homophone-replacer`
- Produces: systemd unit `homophone-replacer.service`

- [ ] **Step 1: Write packaging structure tests**

Create `tests/linux-packaging-test.py` with `tempfile.TemporaryDirectory()`. Pass the directory returned by Python as the final argument of `cmake --install build --prefix`, then assert the presence of:

```text
bin/homophone-replacer-server
bin/homophone-replacer-standalone
config/service.json
data/hr-files/lexicon.txt
data/hr-files/replace.fst
data/hr-files/mapping.txt
scripts/run-server.sh
scripts/install-service.sh
scripts/uninstall-service.sh
packaging/homophone-replacer.service
```

- [ ] **Step 2: Verify failure**

```bash
python3 tests/linux-packaging-test.py --build-dir build
```

Expected: missing service and install files.

- [ ] **Step 3: Add CMake install rules**

Install executables, configuration, required data and scripts into the exact layout above. Do not install `make_replace`, Excel sources or Pynini.

- [ ] **Step 4: Implement systemd unit**

Use:

```ini
[Service]
Type=simple
WorkingDirectory=/opt/homophone-replacer
ExecStart=/opt/homophone-replacer/bin/homophone-replacer-server --config config/service.json
Restart=on-failure
RestartSec=3
TimeoutStopSec=15
```

Run as a dedicated `homophone-replacer` system user created by the install script. Set `NoNewPrivileges=true` and grant write access only to `/opt/homophone-replacer/logs`.

- [ ] **Step 5: Implement install and uninstall scripts**

`install-service.sh` must require root, copy a prepared package tree to `/opt/homophone-replacer`, create the system user and logs directory, install the unit, run `systemctl daemon-reload` and enable the service. It must not overwrite an existing `config/service.json` without first saving `service.json.bak`.

`uninstall-service.sh` stops and disables the service, removes the unit and reloads systemd. It preserves `/opt/homophone-replacer/config`, `data` and `logs` unless passed `--purge`.

- [ ] **Step 6: Run packaging tests and syntax checks**

```bash
bash -n scripts/install-service.sh scripts/uninstall-service.sh
python3 tests/linux-packaging-test.py --build-dir build
```

Expected: install tree matches the contract.

- [ ] **Step 7: Commit**

```bash
git add CMakeLists.txt packaging/homophone-replacer.service \
  scripts/install-service.sh scripts/uninstall-service.sh \
  tests/linux-packaging-test.py
git commit -m "feat(service): 增加 Linux systemd 部署"
```

---

### Task 7: Add Native Windows Service Support

**Files:**
- Create: `src/server/windows-service.h`
- Create: `src/server/windows-service.cc`
- Create: `scripts/install-service.bat`
- Create: `scripts/uninstall-service.bat`
- Create: `scripts/start-service.bat`
- Create: `scripts/stop-service.bat`
- Create: `tests/windows-script-test.py`
- Modify: `CMakeLists.txt`
- Modify: `src/server/main.cc`

**Interfaces:**
- Consumes: `RunService`, `ServiceConfig`, `StopController`
- Produces: `int RunWindowsService(const ServiceOverrides &)` under `_WIN32`

- [ ] **Step 1: Write static Windows script tests**

Create `tests/windows-script-test.py` that checks each script exists and contains the required quoted absolute paths, service name `HomophoneReplacer`, `sc.exe` commands and error-level propagation. This test runs on all platforms as a structural guard.

- [ ] **Step 2: Verify failure**

```bash
python3 tests/windows-script-test.py
```

Expected: missing scripts.

- [ ] **Step 3: Implement Windows SCM adapter**

Under `_WIN32`, implement `ServiceMain`, `RegisterServiceCtrlHandlerEx`, `SetServiceStatus` transitions and stop handling. Required transitions:

```text
START_PENDING → RUNNING → STOP_PENDING → STOPPED
```

`SERVICE_CONTROL_STOP` requests the shared `StopController`; it must not terminate the process directly. Fatal startup exceptions set `dwWin32ExitCode=ERROR_SERVICE_SPECIFIC_ERROR` and a non-zero service-specific code.

- [ ] **Step 4: Wire `--service` in main**

On Windows, `--service` calls `StartServiceCtrlDispatcher`. On non-Windows builds, reject `--service` with a clear message and exit code 2.

- [ ] **Step 5: Implement Windows management scripts**

`install-service.bat` must run as Administrator and register this quoted command:

```cmd
"C:\homophone-replacer\bin\homophone-replacer-server.exe" --service --config "C:\homophone-replacer\config\service.json"
```

Use service name `HomophoneReplacer`, display name `Homophone Replacer`, start mode `auto`, and configure recovery with `sc.exe failure`. The remaining scripts call `sc.exe start`, `stop` and `delete` and preserve non-zero exit codes.

- [ ] **Step 6: Build and validate on Windows**

From a Visual Studio Developer Command Prompt:

```cmd
build.bat
ctest --test-dir build -C Release --output-on-failure
python tests\windows-script-test.py
scripts\run-server.bat
```

Then install, start, query, stop and uninstall the service with `sc.exe query HomophoneReplacer` between steps.

- [ ] **Step 7: Commit**

```bash
git add CMakeLists.txt src/server/windows-service.h src/server/windows-service.cc \
  src/server/main.cc scripts/install-service.bat scripts/uninstall-service.bat \
  scripts/start-service.bat scripts/stop-service.bat \
  tests/windows-script-test.py
git commit -m "feat(service): 增加 Windows 服务部署"
```

---

### Task 8: Add Release Packaging and Concurrency Verification

**Files:**
- Create: `scripts/package-linux.sh`
- Create: `scripts/package-windows.bat`
- Create: `tests/http-server-load-test.py`
- Modify: `CMakeLists.txt`
- Modify: `.gitignore`

**Interfaces:**
- Produces: `dist/homophone-replacer-linux-x64.tar.gz`
- Produces: `dist/homophone-replacer-windows-x64.zip`

- [ ] **Step 1: Write the concurrency test**

Use `concurrent.futures.ThreadPoolExecutor(max_workers=50)` and Python standard-library HTTP calls. Send 10,000 requests distributed across known cases and assert every result. Print:

```text
requests=10000 errors=0 average_ms=... p95_ms=... p99_ms=...
```

The test fails on any non-200 response, wrong replacement, connection error or process exit.

- [ ] **Step 2: Run a reduced failing probe**

Before the final 10,000 request run, support `--requests 100 --concurrency 10` and execute:

```bash
python3 tests/http-server-load-test.py \
  --server build/bin/homophone-replacer-server \
  --requests 100 --concurrency 10
```

Expected before integration: failure if the server lacks stable concurrent handling.

- [ ] **Step 3: Implement Linux packaging**

`package-linux.sh` runs a Release build, installs to `dist/stage-linux`, removes build-only files, verifies the required layout, and creates the tarball. Use `ldd` to record runtime library dependencies in `dist/linux-dependencies.txt` and fail if a project library resolves outside the stage unexpectedly.

- [ ] **Step 4: Implement Windows packaging**

`package-windows.bat` builds Release, installs to `dist\stage-windows`, copies required MSVC runtime DLLs when necessary, validates the layout and creates the ZIP using PowerShell `Compress-Archive`.

- [ ] **Step 5: Run full Linux load and package verification**

```bash
python3 tests/http-server-load-test.py \
  --server build/bin/homophone-replacer-server \
  --requests 10000 --concurrency 50
./scripts/package-linux.sh
tar -tzf dist/homophone-replacer-linux-x64.tar.gz
```

Expected: zero request errors and a complete package tree.

- [ ] **Step 6: Run Windows package verification**

```cmd
scripts\package-windows.bat
powershell -Command "Expand-Archive -Force dist\homophone-replacer-windows-x64.zip dist\verify-windows"
dist\verify-windows\scripts\run-server.bat
```

Expected: `/health` and `/replace` work from the extracted package.

- [ ] **Step 7: Commit**

```bash
git add CMakeLists.txt .gitignore scripts/package-linux.sh \
  scripts/package-windows.bat tests/http-server-load-test.py
git commit -m "build(service): 增加跨平台发布包"
```

---

### Task 9: Document Operation, Configuration, and Troubleshooting

**Files:**
- Modify: `README.md`
- Create: `docs/service-deployment.md`
- Modify: `config/service.json`

**Interfaces:**
- Documents: API, mapping behavior, manual launch, system services, firewall and release packages

- [ ] **Step 1: Add a documentation checklist test**

Create a short Python test or extend `tests/windows-script-test.py` to assert the deployment document contains these literal topics:

```text
POST /replace
GET /health
mapping_enabled
run-server.sh
run-server.bat
systemctl
sc.exe
429
不允许直接映射到公网
```

- [ ] **Step 2: Verify failure**

```bash
python3 tests/windows-script-test.py
```

Expected: deployment documentation is missing.

- [ ] **Step 3: Update README**

Add a concise service overview, two curl examples, links to `docs/service-deployment.md`, and retain the existing CLI instructions. Clearly distinguish:

```text
build.sh / build.bat             构建程序
scripts/build_fst_docker.sh      构建长期 FST
scripts/run-server.*             前台运行 HTTP 服务
```

- [ ] **Step 4: Write the deployment guide**

Document exact Linux and Windows package extraction, foreground validation, config fields, optional mapping enablement, restart behavior, system service commands, log locations, firewall examples and common errors: missing FST, missing optional mapping, port occupied, invalid JSON, queue full and dynamic library loading failure.

- [ ] **Step 5: Run documentation and command checks**

```bash
git diff --check
bash -n scripts/*.sh
python3 tests/windows-script-test.py
rg -n "POST /replace|GET /health|mapping_enabled|run-server" \
  README.md docs/service-deployment.md
```

Expected: all required topics exist and shell scripts parse.

- [ ] **Step 6: Commit**

```bash
git add README.md docs/service-deployment.md config/service.json \
  tests/windows-script-test.py
git commit -m "docs(service): 增加跨平台部署说明"
```

---

### Task 10: Final Cross-Platform Verification and Delivery Review

**Files:**
- Modify only if verification finds defects in files owned by Tasks 1～9.

**Interfaces:**
- Verifies all design acceptance criteria.

- [ ] **Step 1: Run Linux build and all tests**

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j4
ctest --test-dir build --output-on-failure
python3 -m unittest discover -s make_replace/tests -v
```

Expected: all available tests pass; Pynini-only tests may skip locally but must pass in the existing Docker Pynini test command.

- [ ] **Step 2: Run Pynini regression tests**

```bash
docker run --rm --platform linux/amd64 \
  -v "$PWD:/work" -w /work homophone-pynini:2.1.7 \
  python3 -m unittest make_replace.tests.test_build_replace_fst -v
```

Expected: all four tests pass.

- [ ] **Step 3: Run HTTP function and load verification**

```bash
python3 tests/http-server-smoke-test.py \
  --launcher scripts/run-server.sh \
  --server build/bin/homophone-replacer-server
python3 tests/http-server-load-test.py \
  --server build/bin/homophone-replacer-server \
  --requests 10000 --concurrency 50
```

Expected: correct known outputs, clean shutdown and zero load-test errors.

- [ ] **Step 4: Verify mapping modes explicitly**

Run the server once with `mapping_enabled=false`, once with an existing temporary mapping that overrides a known FST output, and once with `mapping_enabled=true` pointing to a missing file. Assert respectively: FST result, mapping result, and FST result with one warning.

- [ ] **Step 5: Verify Linux package from a clean extraction**

```bash
./scripts/package-linux.sh
mkdir -p /tmp/homophone-replacer-release-check
tar -xzf dist/homophone-replacer-linux-x64.tar.gz \
  -C /tmp/homophone-replacer-release-check
```

Start the extracted `run-server.sh`, call both endpoints, stop it, then validate the systemd unit with `systemd-analyze verify` when available.

- [ ] **Step 6: Verify Windows build and package on Windows**

```cmd
build.bat
ctest --test-dir build -C Release --output-on-failure
scripts\package-windows.bat
```

From a clean extraction, validate foreground mode and the complete Windows Service install/start/query/stop/uninstall cycle.

- [ ] **Step 7: Review changed files against the design**

Check every section of `docs/superpowers/specs/2026-07-15-linux-windows-http-service-design.md` and record any unverified platform-specific requirement. Do not claim Windows support solely from a Linux build.

- [ ] **Step 8: Request code review**

Invoke `superpowers:requesting-code-review`, address findings through `superpowers:receiving-code-review`, then rerun affected verification.

- [ ] **Step 9: Prepare final commit if verification required fixes**

Stage only the concrete files changed while fixing verification findings, then commit them with `fix(service): 修正跨平台部署验证问题`. Do not create an empty commit. Do not push without explicit user approval.
