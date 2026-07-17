# HTTP 服务部署

## 接口

健康检查：

```bash
curl http://127.0.0.1:18080/health
```

文本替换：

```bash
curl -X POST http://127.0.0.1:18080/replace \
  -H 'Content-Type: application/json' \
  -d '{"text":"左侧乱潮"}'
```

队列满时返回 HTTP `429`。服务仅面向可信局域网，不允许直接映射到公网。

## 配置

默认配置为 `config/service.json`。主要字段：

- `server.host`、`server.port`：监听地址和端口。
- `worker_count`：替换工作线程数，允许 1～4。
- `queue_capacity`：等待队列容量。
- `max_text_characters`：单条文本字符上限。
- `rules.lexicon`、`rules.fst`：必需规则文件。
- `mapping_enabled`：是否加载临时 mapping。
- `rules.mapping`：临时 mapping 路径。

`mapping_enabled=false` 时只使用 FST。启用但 mapping 文件不存在时，服务记录警告并继续使用 FST。修改规则文件后必须重启服务。

## 手动验证

Linux：

```bash
./scripts/run-server.sh
```

Windows：

```cmd
scripts\run-server.bat
```

两个脚本均可将配置路径作为第一个参数。按 `Ctrl+C` 停止。

## Linux systemd

在发布包根目录执行：

```bash
sudo ./scripts/install-service.sh
sudo systemctl start homophone-replacer
systemctl status homophone-replacer
```

卸载：

```bash
sudo ./scripts/uninstall-service.sh
```

传入 `--purge` 才会删除配置、数据和日志。

Linux 发布脚本会根据构建机器架构生成对应文件：

```text
dist/homophone-replacer-linux-x64.tar.gz
dist/homophone-replacer-linux-arm64.tar.gz
```

## Windows Service

以管理员身份执行：

```cmd
scripts\install-service.bat
scripts\start-service.bat
sc.exe query HomophoneReplacer
```

停止和卸载：

```cmd
scripts\stop-service.bat
scripts\uninstall-service.bat
```

## 常见问题

- 找不到 FST 或 lexicon：服务启动失败，检查 `rules` 路径和工作目录。
- 临时 mapping 不存在：服务继续启动，只使用 FST，并记录警告。
- 端口被占用：修改 `server.port` 或停止占用端口的程序。
- 请求 JSON 错误：返回 HTTP 400。
- 队列满：返回 HTTP 429，可降低调用并发或增加队列容量。
- Linux 动态库加载失败：通过 `scripts/run-server.sh` 启动，脚本会设置库路径。
- 日志默认位于部署根目录的 `logs/service.log`，默认不记录完整 ASR 文本。
