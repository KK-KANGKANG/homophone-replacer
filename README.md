# Homophone Replacer Standalone

这是一个从sherpa-onnx项目中独立出来的拼音词组替换工具，专门用于中文同音字的智能替换。

## 功能特点

- 独立的C++项目，无需完整的sherpa-onnx环境
- 支持Windows和Linux平台编译
- 基于 lexicon.txt (最大匹配分词) + kaldifst FST 规则（replace.fst）的同音词组替换
- FST 内部为每个拼音音节增加边界标记，避免无调规则从其他音节内部误命中
- 最小化依赖，快速编译和部署

## 项目结构

```
homophone-replacer-standalone/
├── src/                    # 源代码
│   ├── homophone-replacer.h/cc  # 核心同音字替换类
│   ├── main.cc                 # 主程序入口
│   └── utils/                  # 工具函数
├── data/hr-files/              # 数据文件
│   ├── lexicon.txt             # 拼音词典
│   ├── mapping.txt             # 长期规则源文件
│   ├── replace.fst             # 正式替换规则（FST）
│   └── fst-build-report.txt    # 正式 FST 构建报告
├── make_replace/               # mapping 与 FST 生成工具
├── scripts/
│   └── build_fst_docker.sh     # 本地 Docker FST 构建脚本
├── Dockerfile.pynini           # Pynini 构建环境
├── CMakeLists.txt              # CMake配置
├── build.bat                   # Windows构建脚本
├── build.sh                    # Linux构建脚本
└── README.md                   # 本文件
```

## 快速开始

### 1. 准备环境

**Windows:**
- Visual Studio 2019或更高版本
- CMake 3.13+

**Linux:**
- GCC 7+ 或 Clang 8+
- CMake 3.13+

### 2. 编译

**Windows:**
```cmd
# 在VS开发者命令提示符中
build.bat
```

构建完成后，从项目根目录运行程序，使程序能读取 `./data/hr-files/`。

**Linux:**
```bash
chmod +x build.sh
# 默认 Release
./build.sh

# Debug
# CONFIG=Debug ./build.sh

# 使用 Ninja
# GENERATOR="Ninja" ./build.sh

# 指定构建目录
# BUILD_DIR=out ./build.sh

# 运行（如遇共享库找不到，先设置环境变量）
export LD_LIBRARY_PATH="./build/lib:$LD_LIBRARY_PATH"
./build/bin/homophone-replacer-standalone --text "左侧乱潮" --debug
```

### 3. 使用

```bash
# 基本使用
./build/bin/homophone-replacer-standalone --text "左侧乱潮"

# 启用调试信息
./build/bin/homophone-replacer-standalone --text "左侧乱潮" --debug

# 查看帮助
./build/bin/homophone-replacer-standalone --help

# 运行时增删关键词（不会修改 replace.fst，只在本次进程内生效）
# 添加（可重复多次）： --add-rule 拼音=汉字
./build/bin/homophone-replacer-standalone --text "他是排长" \
  --add-rule "pai2zhang3=排长" --debug

# 删除（可重复多次）： --del-rule 拼音
./build/bin/homophone-replacer-standalone --text "器具损坏影响排长" \
  --rules-file ./data/hr-files/mapping.txt --del-rule qi4ju4 --debug

# 批量： --rules-file 文件（每行：拼音=汉字）
./build/bin/homophone-replacer-standalone --text "器具损坏影响排长" \
  --rules-file ./data/hr-files/mapping.txt --debug
```

## 输入输出示例

```
输入: "左侧乱潮"
输出: "左侧卵巢"
```

## 技术原理

1. **分词**: 基于 `lexicon.txt` 对输入文本进行最大前向匹配分词
2. **拼音转换**: 词语转为拼音序列（来自 `lexicon.txt`，单字回退）
3. **FST 规则重写**: 使用 `replace.fst` 在拼音序列上进行短语级匹配与替换
4. **结果重构**: 根据 FST 输出的词序列重组为文本

## HTTP 服务

项目提供常驻服务程序 `homophone-replacer-server`，启动时加载一次 FST，适合持续接收 ASR 请求。

```bash
./scripts/run-server.sh
curl http://127.0.0.1:18080/health
curl -X POST http://127.0.0.1:18080/replace \
  -H 'Content-Type: application/json' \
  -d '{"text":"左侧乱潮"}'
```

Windows 可执行 `scripts\run-server.bat`。完整配置、systemd、Windows Service 和排错说明见 [HTTP 服务部署](docs/service-deployment.md)。

Linux 发布脚本会根据构建环境自动生成 x64 或 ARM64 安装包。

构建相关脚本用途：

```text
build.sh / build.bat             构建命令行程序和 HTTP 服务
scripts/build_fst_docker.sh      构建长期 replace.fst
scripts/run-server.sh / .bat     前台运行 HTTP 服务
```

## 自定义扩展

### 生成长期超声规则

新规则的数据流如下：

```text
超声词汇.xlsx + data/hr-files/lexicon.txt
→ .fst-build/mapping.generated.txt
→ 人工审核
→ data/hr-files/mapping.txt
→ replace.fst
```

`data/hr-files/mapping.txt` 是长期 FST 的规则源文件，格式为每行一条 `带声调拼音=目标文字`：

```text
luan3chao2=卵巢
chang2jing4=长径
```

需要从新的 Excel 生成候选 mapping 时，在项目根目录执行：

```bash
mkdir -p .fst-build

python3 -m make_replace.main prepare \
  --excel 超声词汇.xlsx \
  --lexicon data/hr-files/lexicon.txt \
  --mapping-output .fst-build/mapping.generated.txt \
  --report-output .fst-build/mapping-report.txt
```

审核 `.fst-build/mapping.generated.txt` 和报告，修正专业读音后，将审核结果保存为 `.fst-build/mapping.reviewed.txt`。确认无误后覆盖正式规则源：

```bash
cp .fst-build/mapping.reviewed.txt data/hr-files/mapping.txt
```

项目使用本地 Docker 中的 Linux x86_64 Pynini 环境构建 FST。Docker 至少分配 16GB 内存，推荐 24GB；构建必须串行执行：

```bash
./scripts/build_fst_docker.sh
```

FST 候选文件生成在：

```text
.fst-build/replace-tone-aware.fst
.fst-build/tone-aware-report.txt
```

确认 mapping 和构建结果后，可重新构建并安装到正式数据目录：

```bash
./scripts/build_fst_docker.sh --install
```

仅生成带声调精确匹配基准文件：

```bash
./scripts/build_fst_docker.sh --exact-only
```

注意：

- 新候选 mapping 只来自 Excel 和 `lexicon.txt`，不会混入项目现有的历史 `mapping.txt`。
- `.fst-build/` 位于项目目录内，保存生成过程中的候选文件，并已加入 `.gitignore`。
- 必须先审核候选 mapping，再覆盖 `data/hr-files/mapping.txt` 和构建 FST。
- 专业多音字通常应修正到 `lexicon.txt`；若明确不调整通用词典，则在人工审核 mapping 时修正读音。
- 相同带调拼音对应多个目标时，保留 mapping 中先出现的目标，后续目标会被丢弃并写入构建报告。
- 去掉声调后对应多个目标时，保留各自的精确规则，但不生成无调规则。
- 正常运行只读取 `lexicon.txt` 和 `replace.fst`，不会直接读取 `mapping.txt`。
- 生产环境只加载审核后生成的 `replace.fst`，不要再通过 `--rules-file` 重复加载整份 mapping。
- 当前正式 FST 的构建信息见 `data/hr-files/fst-build-report.txt`。

### FST 性能

当前目标 FST 包含 1,957 条精确规则、1,943 条无调规则和 7 组冲突保护。实测首次加载约 174ms；3,400 字长文本处理 5 次平均约 94.7ms。普通 20～50 字 ASR 单句通常为毫秒级，单个词汇通常低于 1ms。

`HomophoneReplacer` 应在程序启动时创建并长期复用。不要为每次识别请求重新初始化，否则会重复支付 FST 加载时间。

### 运行时增删关键词（拼音→汉字）

- `--add-rule K=V`：添加规则，示例 `pai2zhang3=排长`。
- `--del-rule K`：删除（屏蔽）某个拼音键，示例 `qi4ju4`。
- `--rules-file FILE`：批量加载，每行 `K=V`。

说明：
- 这些规则只在当前进程内生效，不会写回或修改 `replace.fst`。
- 运行时规则优先级高于 `replace.fst`，可用于热修或兜底；长期规则建议编入新的 `replace.fst` 以获得更好性能与一致性。
- 运行时先匹配带声调拼音；精确失败后，只有无调拼音唯一对应一个目标时才会忽略声调匹配。
- 若同一无调拼音对应多个目标，程序不会猜测，只保留带声调精确匹配。
- 遇到无调冲突长词时，也不会让更短的无调规则从长词内部抢先替换。

注意：当前仅支持单个 `replace.fst` 文件
