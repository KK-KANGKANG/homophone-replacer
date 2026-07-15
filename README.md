# Homophone Replacer Standalone

这是一个从sherpa-onnx项目中独立出来的拼音词组替换工具，专门用于中文同音字的智能替换。

## 功能特点

- 独立的C++项目，无需完整的sherpa-onnx环境
- 支持Windows和Linux平台编译
- 基于 lexicon.txt (最大匹配分词) + kaldifst FST 规则（replace.fst）的同音词组替换
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
│   └── replace.fst             # 替换规则（FST）
├── third_party/                # 第三方依赖
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

# 准备词库文件
将data复制到生成的可执行文件homophone-replacer-standalone.exe目录下
```

**Linux:**
```bash
chmod +x build.sh
# 默认 Release
./build.sh

# # Debug
# CONFIG=Debug ./build.sh

# # 使用 Ninja
# GENERATOR="Ninja" ./build.sh

# # 指定构建目录
# BUILD_DIR=out ./build.sh

# 准备词库文件
将data复制到生成的可执行文件homophone-replacer-standalone目录下

# Debug
CONFIG=Debug ./build.sh

# 使用 Ninja
GENERATOR="Ninja" ./build.sh

# 指定构建目录
BUILD_DIR=out ./build.sh

# 运行（如遇共享库找不到，先设置环境变量）
export LD_LIBRARY_PATH="./build/lib:$LD_LIBRARY_PATH"
./build/bin/homophone-replacer-standalone --text "他想知道玄界芯片问题的答案" --debug
```

### 4. 使用

```bash
# 基本使用
./homophone-replacer-standalone --text "他想知道玄界芯片问题的答案"

# 启用调试信息
./homophone-replacer-standalone --text "他想知道答案" --debug

# 查看帮助
./homophone-replacer-standalone --help

# 运行时增删关键词（不会修改 replace.fst，只在本次进程内生效）
# 添加（可重复多次）： --add-rule 拼音=汉字
./homophone-replacer-standalone --text "他是排长" \
  --add-rule "pai2zhang3=排长" --debug

# 删除（可重复多次）： --del-rule 拼音
./homophone-replacer-standalone --text "器具损坏影响排长" \
  --rules-file ./data/hr-files/mapping.txt --del-rule qi4ju4 --debug

# 批量： --rules-file 文件（每行：拼音=汉字）
./homophone-replacer-standalone --text "器具损坏影响排长" \
  --rules-file ./data/hr-files/mapping.txt --debug
```

## 输入输出示例

```
输入: "他想知道玄界芯片问题的答案"
输出: "他想知道玄戒芯片问题的答案"
```

## 技术原理

1. **分词**: 基于 `lexicon.txt` 对输入文本进行最大前向匹配分词
2. **拼音转换**: 词语转为拼音序列（来自 `lexicon.txt`，单字回退）
3. **FST 规则重写**: 使用 `replace.fst` 在拼音序列上进行短语级匹配与替换
4. **结果重构**: 根据 FST 输出的词序列重组为文本

## 自定义扩展

### 生成长期超声规则

新规则的数据流如下：

```text
超声词汇.xlsx + data/hr-files/lexicon.txt
→ 候选 mapping.txt
→ 人工审核
→ replace.fst
```

可直接使用仓库内的 [Colab 生成脚本](https://colab.research.google.com/github/KK-KANGKANG/homophone-replacer/blob/main/make_replace/generate_replace_fst_colab.ipynb)。本地有 Pynini 环境时，也可以执行：

```bash
python3 -m make_replace.main prepare \
  --excel 超声词汇.xlsx \
  --lexicon data/hr-files/lexicon.txt \
  --mapping-output /tmp/ultrasound-mapping.generated.txt \
  --report-output /tmp/ultrasound-mapping-report.txt

python3 -m make_replace.main build \
  --mapping /tmp/ultrasound-mapping.reviewed.txt \
  --fst-output /tmp/replace.fst \
  --report-output /tmp/fst-build-report.txt
```

注意：

- 新候选 mapping 只来自 Excel 和 `lexicon.txt`，不会混入项目现有的历史 `mapping.txt`。
- 必须先审核 `prepare` 的结果，再执行 `build`。
- 专业多音字通常应修正到 `lexicon.txt`；若明确不调整通用词典，则在人工审核 mapping 时修正读音。
- 相同带调拼音对应多个目标时，保留 mapping 中先出现的目标，后续目标会被丢弃并写入构建报告。
- 去掉声调后对应多个目标时，保留各自的精确规则，但不生成无调规则。
- 生产环境只加载审核后生成的 `replace.fst`，不要再通过 `--rules-file` 重复加载整份 mapping。

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
