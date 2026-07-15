# Tone-Aware Ultrasound FST Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 从 `超声词汇.xlsx` 和 `lexicon.txt` 生成新的超声规则，并编译出一个同时支持带声调精确匹配和不限声调唯一目标匹配的 `replace.fst`。

**Architecture:** Python 准备阶段复刻 C++ 的 lexicon 加载、最大前向分词和单字回退行为，先生成可审核的新 `mapping.txt`。FST 编译阶段为每条规则生成高优先级精确路径，并只为纯拼音唯一目标生成低优先级任意声调路径，最终合并为一个 `replace.fst`；C++ 临时规则使用相同的精确优先和唯一目标宽松策略。

**Tech Stack:** C++17、Python 3 标准库、Pynini/OpenFST、kaldifst、CMake、Docker、`unittest`。

## Decision Amendment (2026-07-15)

- 用户确认：相同带调拼音对应多个目标时，不再停止构建。
- 按 mapping 文件顺序保留第一条规则，丢弃后续不同目标。
- 被丢弃的目标必须写入构建报告，不能静默忽略。
- 用户确认：`长径` 在最终 mapping 中使用 `chang2jing4`，但不修改 `lexicon.txt`；本次作为人工审核修正处理。
- 本修订覆盖下文所有“带调拼音冲突时停止并等待人工选择”的旧步骤；无调拼音对应多个目标时仍然禁用宽松规则。
- 无调冲突词需要在运行时和 FST 中阻断更短的无调规则，避免短词从冲突长词内部抢先替换。
- 最终放弃 Colab，使用本地 Docker Linux x86_64 构建。Docker 至少分配 16GB 内存，推荐 24GB。
- 删除 `make_replace/generate_replace_fst_colab.ipynb`，新增 `Dockerfile.pynini` 和 `scripts/build_fst_docker.sh`。
- 生产 FST 已在 24.9GB Docker 环境构建并通过本机 C++ 验证：59,252,050 字节，构建耗时 217.034 秒。

## Global Constraints

- 本阶段只处理纯中文词汇；中英混合词汇保持现状并写入排除报告。
- `lexicon.txt` 继续保存并输出带声调拼音，不改变其文件格式。
- 现有项目参与者提供的 `data/hr-files/mapping.txt` 不作为本次规则输入。
- 新 mapping 必须由 `超声词汇.xlsx` 和运行时同一份 `lexicon.txt` 生成并经人工审核。
- 专业多音字修正必须进入 `lexicon.txt`，不得只修改一次性生成结果。
- 纯拼音对应多个目标时，只生成带声调精确规则，不生成不限声调规则。
- 相同带调拼音对应多个目标时保留 mapping 第一条，丢弃项写入构建报告。
- 最终长期规则只部署一个 `replace.fst`，运行时只调用一次 FST。
- 正式运行不得再通过 `--rules-file` 重复加载用于生成 FST 的完整 mapping。
- FST 或 mapping 生成失败时不得覆盖上一个可用文件。
- 任何 Git 操作必须先获得用户明确批准；每个任务只准备提交建议，不自动提交。
- 修改根 `CMakeLists.txt` 属于根配置变更，执行对应任务前必须再次获得用户确认。

---

## File Structure

### New files

- `make_replace/lexicon_mapping.py`：读取 Excel、加载 lexicon、复刻分词和拼音生成、输出准备结果。
- `make_replace/rule_planner.py`：解析新 mapping、检测精确冲突和纯拼音冲突、生成精确/宽松规则计划。
- `make_replace/build_replace_fst.py`：使用 Pynini 将规则计划编译成单个 FST，并安全写入输出。
- `make_replace/tests/test_lexicon_mapping.py`：Excel、lexicon、分词和 mapping 生成测试。
- `make_replace/tests/test_rule_planner.py`：规则去重、冲突和宽松规则计划测试。
- `make_replace/tests/test_build_replace_fst.py`：Pynini 可用时的编译与改写集成测试。
- `Dockerfile.pynini`：固定 Linux x86_64 Pynini 构建环境。
- `scripts/build_fst_docker.sh`：本地测试、候选构建和可选安装入口。
- `src/runtime-rule-matcher.h`：临时规则匹配接口。
- `src/runtime-rule-matcher.cc`：临时规则精确优先、唯一目标宽松匹配实现。
- `tests/runtime-rule-matcher-test.cc`：不依赖第三方测试框架的 C++ 单元测试。

### Modified files

- `make_replace/main.py`：从硬编码示例改为 `prepare` / `build` 命令入口。
- `src/homophone-replacer.cc`：接入独立的临时规则匹配器。
- `CMakeLists.txt`：编译新 C++ 源文件并注册测试；执行前需要用户确认。
- `README.md`：更新 Excel → mapping → FST 和本地 Docker 生成方式。
- `data/hr-files/lexicon.txt`：只在人工确认专业多音字后增加或修正具体词条。
- `data/hr-files/mapping.txt`：只在人工审核生成结果后替换为新的超声规则。
- `data/hr-files/replace.fst`：只在全部测试和性能验证通过后替换。

---

### Task 1: Reproduce Runtime Lexicon Behavior in Python

**Files:**
- Create: `make_replace/lexicon_mapping.py`
- Create: `make_replace/tests/__init__.py`
- Create: `make_replace/tests/test_lexicon_mapping.py`

**Interfaces:**
- Produces: `Lexicon.from_file(path: Path) -> Lexicon`
- Produces: `Lexicon.segment(text: str) -> list[str]`
- Produces: `Lexicon.pronounce_word(word: str) -> str`
- Produces: `Lexicon.pronounce_text(text: str) -> PronunciationResult`
- Produces: `read_xlsx_terms(path: Path) -> list[str]`
- Produces: `prepare_mapping(terms: Iterable[str], lexicon: Lexicon) -> MappingPreparation`

- [ ] **Step 1: Write the failing lexicon and Excel tests**

```python
# make_replace/tests/test_lexicon_mapping.py
import tempfile
import unittest
from pathlib import Path
from zipfile import ZipFile

from make_replace.lexicon_mapping import Lexicon, prepare_mapping, read_xlsx_terms


def write_minimal_xlsx(path: Path, values: list[str]) -> None:
    shared = "".join(f"<si><t>{value}</t></si>" for value in values)
    rows = "".join(
        f'<row r="{index}"><c r="A{index}" t="s"><v>{index - 1}</v></c></row>'
        for index in range(1, len(values) + 1)
    )
    with ZipFile(path, "w") as archive:
        archive.writestr(
            "xl/sharedStrings.xml",
            '<?xml version="1.0" encoding="UTF-8"?>'
            '<sst xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main">'
            f"{shared}</sst>",
        )
        archive.writestr(
            "xl/workbook.xml",
            '<?xml version="1.0" encoding="UTF-8"?>'
            '<workbook xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main" '
            'xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships">'
            '<sheets><sheet name="Sheet1" sheetId="1" r:id="rId1"/></sheets></workbook>',
        )
        archive.writestr(
            "xl/_rels/workbook.xml.rels",
            '<?xml version="1.0" encoding="UTF-8"?>'
            '<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">'
            '<Relationship Id="rId1" Target="worksheets/sheet1.xml" Type="worksheet"/>'
            '</Relationships>',
        )
        archive.writestr(
            "xl/worksheets/sheet1.xml",
            '<?xml version="1.0" encoding="UTF-8"?>'
            '<worksheet xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main">'
            f'<sheetData>{rows}</sheetData>'
            '</worksheet>',
        )


class LexiconMappingTest(unittest.TestCase):
    def test_maximum_forward_segmentation_and_char_fallback(self):
        with tempfile.TemporaryDirectory() as directory:
            lexicon_path = Path(directory) / "lexicon.txt"
            lexicon_path.write_text(
                "卵 luan3\n巢 chao2\n肺 fei4\n静脉 jing4 mai4\n",
                encoding="utf-8",
            )
            lexicon = Lexicon.from_file(lexicon_path)
            self.assertEqual(lexicon.segment("卵巢肺静脉"), ["卵", "巢", "肺", "静脉"])
            self.assertEqual(lexicon.pronounce_text("卵巢").pinyin, "luan3chao2")

    def test_neutral_tone_defaults_to_one_like_cpp(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "lexicon.txt"
            path.write_text("子 zi\n", encoding="utf-8")
            self.assertEqual(Lexicon.from_file(path).pronounce_word("子"), "zi1")

    def test_prepare_mapping_deduplicates_and_excludes_mixed_terms(self):
        lexicon = Lexicon({"卵": "luan3", "巢": "chao2"})
        result = prepare_mapping(["卵巢", "卵巢", "NT筛查"], lexicon)
        self.assertEqual(result.rules, [("luan3chao2", "卵巢")])
        self.assertEqual(result.excluded_terms, ["NT筛查"])
        self.assertEqual(result.source_term_count, 3)
        self.assertEqual(result.unique_term_count, 2)

    def test_reads_all_nonempty_xlsx_cells_in_order(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "words.xlsx"
            write_minimal_xlsx(path, ["卵巢", "", "肺静脉"])
            self.assertEqual(read_xlsx_terms(path), ["卵巢", "肺静脉"])
```

- [ ] **Step 2: Run the test to verify it fails**

Run:

```bash
python3 -m unittest make_replace.tests.test_lexicon_mapping -v
```

Expected: `ModuleNotFoundError: No module named 'make_replace.lexicon_mapping'`.

- [ ] **Step 3: Implement the lexicon and Excel preparation module**

```python
# make_replace/lexicon_mapping.py
from dataclasses import dataclass
from pathlib import Path, PurePosixPath
from typing import Iterable
from xml.etree import ElementTree as ET
from zipfile import ZipFile
import re

MAIN_NS = "http://schemas.openxmlformats.org/spreadsheetml/2006/main"
REL_NS = "http://schemas.openxmlformats.org/officeDocument/2006/relationships"
PACKAGE_REL_NS = "http://schemas.openxmlformats.org/package/2006/relationships"
PURE_HAN = re.compile(r"^[\u3400-\u4dbf\u4e00-\u9fff\uf900-\ufaff]+$")


@dataclass(frozen=True)
class PronunciationResult:
    words: list[str]
    pinyin: str


@dataclass(frozen=True)
class MappingPreparation:
    source_term_count: int
    unique_term_count: int
    rules: list[tuple[str, str]]
    excluded_terms: list[str]
    missing_characters: dict[str, list[str]]
    invalid_pronunciations: list[str]


class Lexicon:
    def __init__(self, word_to_pronunciation: dict[str, str]):
        self._word_to_pronunciation = dict(word_to_pronunciation)
        self._all_words = set(word_to_pronunciation)

    @classmethod
    def from_file(cls, path: Path) -> "Lexicon":
        entries: dict[str, str] = {}
        for raw_line in path.read_text(encoding="utf-8").splitlines():
            fields = raw_line.split()
            if len(fields) < 2 or fields[0].lower() in entries:
                continue
            tokens = [token if token[-1] in "1234" else token + "1" for token in fields[1:]]
            entries[fields[0].lower()] = "".join(tokens)
        return cls(entries)

    def segment(self, text: str) -> list[str]:
        chars = list(text)
        words: list[str] = []
        index = 0
        while index < len(chars):
            matched = ""
            for length in range(min(10, len(chars) - index), 1, -1):
                candidate = "".join(chars[index:index + length])
                if candidate in self._all_words:
                    matched = candidate
                    break
            if matched:
                words.append(matched)
                index += len(matched)
            else:
                words.append(chars[index])
                index += 1
        return words

    def pronounce_word(self, word: str) -> str:
        if word in self._word_to_pronunciation:
            return self._word_to_pronunciation[word]
        if len(word) == 1:
            return word
        return "".join(self._word_to_pronunciation.get(char, char) for char in word)

    def contains(self, word: str) -> bool:
        return word in self._word_to_pronunciation

    def pronounce_text(self, text: str) -> PronunciationResult:
        words = self.segment(text)
        return PronunciationResult(words, "".join(self.pronounce_word(word) for word in words))


def prepare_mapping(terms: Iterable[str], lexicon: Lexicon) -> MappingPreparation:
    source_terms = [term.strip() for term in terms if term.strip()]
    unique_terms = list(dict.fromkeys(source_terms))
    rules: list[tuple[str, str]] = []
    excluded: list[str] = []
    missing: dict[str, list[str]] = {}
    invalid: list[str] = []
    for term in unique_terms:
        if not PURE_HAN.fullmatch(term):
            excluded.append(term)
            continue
        result = lexicon.pronounce_text(term)
        unknown = [char for char in term if not lexicon.contains(char)]
        if unknown:
            missing[term] = unknown
            continue
        if sum(char in "1234" for char in result.pinyin) != len(term):
            invalid.append(term)
            continue
        rules.append((result.pinyin, term))
    return MappingPreparation(
        len(source_terms), len(unique_terms), rules, excluded, missing, invalid
    )
```

Add the complete Excel reader below to the same file:

```python
def _xml_text(element: ET.Element) -> str:
    return "".join(node.text or "" for node in element.iter(f"{{{MAIN_NS}}}t"))


def read_xlsx_terms(path: Path) -> list[str]:
    with ZipFile(path) as archive:
        shared: list[str] = []
        if "xl/sharedStrings.xml" in archive.namelist():
            root = ET.fromstring(archive.read("xl/sharedStrings.xml"))
            shared = [_xml_text(item) for item in root.findall(f"{{{MAIN_NS}}}si")]

        workbook = ET.fromstring(archive.read("xl/workbook.xml"))
        relationships = ET.fromstring(archive.read("xl/_rels/workbook.xml.rels"))
        targets = {item.attrib["Id"]: item.attrib["Target"] for item in relationships}
        values: list[str] = []
        sheets = workbook.find(f"{{{MAIN_NS}}}sheets")
        if sheets is None:
            return values

        for sheet in sheets:
            relation_id = sheet.attrib[f"{{{REL_NS}}}id"]
            target = targets[relation_id]
            sheet_path = target.lstrip("/") if target.startswith("/") else str(PurePosixPath("xl") / target)
            sheet_root = ET.fromstring(archive.read(sheet_path))
            for cell in sheet_root.findall(f".//{{{MAIN_NS}}}c"):
                cell_type = cell.attrib.get("t")
                value_node = cell.find(f"{{{MAIN_NS}}}v")
                inline_node = cell.find(f"{{{MAIN_NS}}}is")
                if cell_type == "s" and value_node is not None:
                    value = shared[int(value_node.text or "0")]
                elif cell_type == "inlineStr" and inline_node is not None:
                    value = _xml_text(inline_node)
                elif value_node is not None:
                    value = value_node.text or ""
                else:
                    continue
                value = value.strip()
                if value:
                    values.append(value)
        return values
```

- [ ] **Step 4: Run the test and verify it passes**

Run:

```bash
python3 -m unittest make_replace.tests.test_lexicon_mapping -v
```

Expected: all tests `ok`.

- [ ] **Step 5: Check file size and prepare the review checkpoint**

Run:

```bash
wc -l make_replace/lexicon_mapping.py make_replace/tests/test_lexicon_mapping.py
git diff -- make_replace/lexicon_mapping.py make_replace/tests/test_lexicon_mapping.py
```

Expected: each implementation file is at most 300 lines. Proposed commit: `feat(rules): 复刻词典分词与拼音生成`。Do not commit without user approval.

---

### Task 2: Generate a Reviewable Ultrasound Mapping

**Files:**
- Create: `make_replace/generate_mapping.py`
- Modify: `make_replace/tests/test_lexicon_mapping.py`

**Interfaces:**
- Consumes: `read_xlsx_terms()`, `Lexicon.from_file()`, `prepare_mapping()`
- Produces: `write_mapping(result: MappingPreparation, output: Path) -> None`
- Produces: `write_preparation_report(result: MappingPreparation, output: Path) -> None`
- Produces CLI arguments: `--excel`, `--lexicon`, `--mapping-output`, `--report-output`

- [ ] **Step 1: Add failing atomic-output and report tests**

```python
from make_replace.generate_mapping import write_mapping, write_preparation_report
from make_replace.lexicon_mapping import MappingPreparation


def test_mapping_output_is_sorted_and_atomic(self):
    result = MappingPreparation(
        source_term_count=2,
        unique_term_count=2,
        rules=[("fei4jing4mai4", "肺静脉"), ("luan3chao2", "卵巢")],
        excluded_terms=["NT筛查"],
        missing_characters={},
        invalid_pronunciations=[],
    )
    with tempfile.TemporaryDirectory() as directory:
        output = Path(directory) / "mapping.txt"
        write_mapping(result, output)
        self.assertEqual(
            output.read_text(encoding="utf-8"),
            "fei4jing4mai4=肺静脉\nluan3chao2=卵巢\n",
        )
        self.assertFalse(output.with_name(output.name + ".tmp").exists())


def test_report_contains_excluded_terms(self):
    result = MappingPreparation(1, 1, [], ["NT筛查"], {}, [])
    with tempfile.TemporaryDirectory() as directory:
        output = Path(directory) / "mapping-report.txt"
        write_preparation_report(result, output)
        self.assertIn("NT筛查", output.read_text(encoding="utf-8"))
```

- [ ] **Step 2: Run the focused tests and verify they fail**

Run:

```bash
python3 -m unittest make_replace.tests.test_lexicon_mapping -v
```

Expected: import or name failures for `write_mapping` and `write_preparation_report`.

- [ ] **Step 3: Implement the CLI and atomic writers**

```python
# make_replace/generate_mapping.py
import argparse
from pathlib import Path

from make_replace.lexicon_mapping import Lexicon, MappingPreparation, prepare_mapping, read_xlsx_terms


def atomic_write(path: Path, content: str) -> None:
    temporary = path.with_name(path.name + ".tmp")
    temporary.write_text(content, encoding="utf-8")
    temporary.replace(path)


def write_mapping(result: MappingPreparation, output: Path) -> None:
    lines = [f"{pinyin}={target}" for pinyin, target in sorted(result.rules)]
    atomic_write(output, "\n".join(lines) + ("\n" if lines else ""))


def write_preparation_report(result: MappingPreparation, output: Path) -> None:
    lines = [
        f"Excel 非空词条: {result.source_term_count}",
        f"Excel 去重词条: {result.unique_term_count}",
        f"有效纯中文规则: {len(result.rules)}",
        f"排除词条: {len(result.excluded_terms)}",
        f"lexicon 缺失词条: {len(result.missing_characters)}",
        f"拼音异常词条: {len(result.invalid_pronunciations)}",
        "",
        "排除词条明细:",
        *result.excluded_terms,
    ]
    atomic_write(output, "\n".join(lines) + "\n")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--excel", type=Path, required=True)
    parser.add_argument("--lexicon", type=Path, required=True)
    parser.add_argument("--mapping-output", type=Path, required=True)
    parser.add_argument("--report-output", type=Path, required=True)
    args = parser.parse_args()
    result = prepare_mapping(read_xlsx_terms(args.excel), Lexicon.from_file(args.lexicon))
    write_mapping(result, args.mapping_output)
    write_preparation_report(result, args.report_output)
    return 1 if result.missing_characters or result.invalid_pronunciations else 0


if __name__ == "__main__":
    raise SystemExit(main())
```

- [ ] **Step 4: Run unit tests and a real-corpus dry run**

Run:

```bash
python3 -m unittest make_replace.tests.test_lexicon_mapping -v
python3 -m make_replace.generate_mapping \
  --excel 超声词汇.xlsx \
  --lexicon data/hr-files/lexicon.txt \
  --mapping-output /tmp/ultrasound-mapping.generated.txt \
  --report-output /tmp/ultrasound-mapping-report.txt
wc -l /tmp/ultrasound-mapping.generated.txt
```

Expected: tests pass; dry run exits 0; generated mapping contains 1,960 pure-Chinese rules before exact-pinyin conflict review; 41 mixed/non-Chinese terms are excluded and reported.

- [ ] **Step 5: Review the generated mapping without replacing project data**

Run:

```bash
rg -n '^(zhang3jing4|chang2jing4)=' /tmp/ultrasound-mapping.generated.txt
sed -n '1,220p' /tmp/ultrasound-mapping-report.txt
git diff -- make_replace/generate_mapping.py make_replace/tests/test_lexicon_mapping.py
```

Expected: `长径` initially exposes the current lexicon pronunciation in the generated mapping and is explicitly included in the human review notes. Proposed commit: `feat(rules): 从超声词汇生成候选 mapping`。Do not commit without user approval.

---

### Task 3: Plan Exact and Tone-Flexible Rules Safely

**Files:**
- Create: `make_replace/rule_planner.py`
- Create: `make_replace/tests/test_rule_planner.py`

**Interfaces:**
- Produces: `parse_toned_pinyin(key: str) -> tuple[PinyinSyllable, ...]`
- Produces: `load_mapping(path: Path) -> list[MappingRule]`
- Produces: `plan_rules(rules: Iterable[MappingRule]) -> RulePlan`
- Produces data: `RulePlan.exact_rules`, `RulePlan.flexible_rules`, `RulePlan.toneless_conflicts`

- [ ] **Step 1: Write failing rule-planning tests**

```python
# make_replace/tests/test_rule_planner.py
import unittest

from make_replace.rule_planner import MappingConflictError, MappingRule, parse_toned_pinyin, plan_rules


class RulePlannerTest(unittest.TestCase):
    def test_parses_syllables(self):
        syllables = parse_toned_pinyin("luan3chao2")
        self.assertEqual([(item.base, item.tone) for item in syllables], [("luan", "3"), ("chao", "2")])

    def test_unique_toneless_target_gets_flexible_rule(self):
        plan = plan_rules([MappingRule("luan3chao2", "卵巢", 1)])
        self.assertEqual(len(plan.exact_rules), 1)
        self.assertEqual(len(plan.flexible_rules), 1)
        self.assertEqual(plan.flexible_rules[0].toneless_key, "luanchao")

    def test_multiple_toneless_targets_disable_flexible_rule(self):
        plan = plan_rules([
            MappingRule("fei4jing4mai4", "肺静脉", 1),
            MappingRule("fei2jing4mai4", "腓静脉", 2),
        ])
        self.assertEqual(plan.flexible_rules, ())
        self.assertEqual(plan.toneless_conflicts[0].toneless_key, "feijingmai")

    def test_same_exact_key_with_different_targets_fails(self):
        with self.assertRaises(MappingConflictError):
            plan_rules([
                MappingRule("qian2bi4", "前臂", 1),
                MappingRule("qian2bi4", "前壁", 2),
            ])
```

- [ ] **Step 2: Run the tests and verify they fail**

Run:

```bash
python3 -m unittest make_replace.tests.test_rule_planner -v
```

Expected: missing-module failure.

- [ ] **Step 3: Implement the rule planner**

```python
# make_replace/rule_planner.py
from collections import defaultdict
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable
import re

PINYIN_RE = re.compile(r"(?:[a-z]+[1-4])+")
SYLLABLE_RE = re.compile(r"([a-z]+)([1-4])")


@dataclass(frozen=True)
class PinyinSyllable:
    base: str
    tone: str


@dataclass(frozen=True)
class MappingRule:
    pinyin: str
    target: str
    line_number: int


@dataclass(frozen=True)
class FlexibleRule:
    syllables: tuple[PinyinSyllable, ...]
    target: str
    toneless_key: str


@dataclass(frozen=True)
class TonelessConflict:
    toneless_key: str
    targets: tuple[str, ...]


@dataclass(frozen=True)
class RulePlan:
    exact_rules: tuple[MappingRule, ...]
    flexible_rules: tuple[FlexibleRule, ...]
    toneless_conflicts: tuple[TonelessConflict, ...]


class MappingConflictError(ValueError):
    pass


def parse_toned_pinyin(key: str) -> tuple[PinyinSyllable, ...]:
    if not PINYIN_RE.fullmatch(key):
        raise ValueError(f"invalid toned pinyin: {key}")
    return tuple(PinyinSyllable(base, tone) for base, tone in SYLLABLE_RE.findall(key))


def load_mapping(path: Path) -> list[MappingRule]:
    rules: list[MappingRule] = []
    for line_number, raw_line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        line = raw_line.strip()
        if not line or line.startswith("#"):
            continue
        if "=" not in line:
            raise ValueError(f"line {line_number}: missing '=': {raw_line}")
        key, target = (part.strip() for part in line.split("=", 1))
        if not key or not target:
            raise ValueError(f"line {line_number}: empty key or target: {raw_line}")
        parse_toned_pinyin(key)
        rules.append(MappingRule(key, target, line_number))
    return rules


def plan_rules(rules: Iterable[MappingRule]) -> RulePlan:
    exact_targets: dict[str, set[str]] = defaultdict(set)
    first_rule: dict[tuple[str, str], MappingRule] = {}
    for rule in rules:
        parse_toned_pinyin(rule.pinyin)
        exact_targets[rule.pinyin].add(rule.target)
        first_rule.setdefault((rule.pinyin, rule.target), rule)
    conflicts = {key: values for key, values in exact_targets.items() if len(values) > 1}
    if conflicts:
        details = "; ".join(f"{key}={sorted(values)}" for key, values in sorted(conflicts.items()))
        raise MappingConflictError(details)
    exact = tuple(sorted(first_rule.values(), key=lambda item: (item.pinyin, item.target)))
    groups: dict[str, list[MappingRule]] = defaultdict(list)
    for rule in exact:
        groups[re.sub(r"[1-4]", "", rule.pinyin)].append(rule)
    flexible: list[FlexibleRule] = []
    toneless_conflicts: list[TonelessConflict] = []
    for key, group in sorted(groups.items()):
        targets = tuple(sorted({rule.target for rule in group}))
        if len(targets) == 1:
            flexible.append(FlexibleRule(parse_toned_pinyin(group[0].pinyin), targets[0], key))
        else:
            toneless_conflicts.append(TonelessConflict(key, targets))
    return RulePlan(exact, tuple(flexible), tuple(toneless_conflicts))
```

- [ ] **Step 4: Run the planner tests and real mapping analysis**

Run:

```bash
python3 -m unittest make_replace.tests.test_rule_planner -v
python3 - <<'PY'
from pathlib import Path
from make_replace.rule_planner import load_mapping, plan_rules
plan = plan_rules(load_mapping(Path('/tmp/ultrasound-mapping.generated.txt')))
print(len(plan.exact_rules), len(plan.flexible_rules), len(plan.toneless_conflicts))
PY
```

Expected: unit tests pass. The real generated mapping initially raises an exact-key conflict for the three known groups `前臂/前壁`、`尺骨/耻骨`、`脐静脉/奇静脉`; this is the required human-decision gate, not an implementation failure.

- [ ] **Step 5: Prepare the review checkpoint**

Run:

```bash
git diff -- make_replace/rule_planner.py make_replace/tests/test_rule_planner.py
```

Expected: no placeholders and no silent target selection. Proposed commit: `feat(rules): 检测精确与无调拼音冲突`。Do not commit without user approval.

---

### Task 4: Compile the Single Weighted FST

**Files:**
- Create: `make_replace/build_replace_fst.py`
- Create: `make_replace/tests/test_build_replace_fst.py`

**Interfaces:**
- Consumes: `RulePlan` from Task 3
- Produces: `build_fst(plan: RulePlan, output: Path) -> BuildStats`
- Produces CLI arguments: `--mapping`, `--fst-output`, `--report-output`, `--exact-only`

- [ ] **Step 1: Write failing compiler tests guarded by Pynini availability**

```python
# make_replace/tests/test_build_replace_fst.py
import importlib.util
import tempfile
import unittest
from pathlib import Path

from make_replace.rule_planner import MappingRule, plan_rules

PINYINI_AVAILABLE = importlib.util.find_spec("pynini") is not None


@unittest.skipUnless(PINYINI_AVAILABLE, "Pynini is required")
class BuildReplaceFstTest(unittest.TestCase):
    def test_exact_and_different_tone_produce_same_target(self):
        from make_replace.build_replace_fst import build_fst, rewrite_text

        plan = plan_rules([MappingRule("luan3chao2", "卵巢", 1)])
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "replace.fst"
            build_fst(plan, output)
            self.assertEqual(rewrite_text(output, "luan3chao2"), "卵巢")
            self.assertEqual(rewrite_text(output, "luan4chao2"), "卵巢")

    def test_toneless_conflict_has_no_flexible_rewrite(self):
        from make_replace.build_replace_fst import build_fst, rewrite_text

        plan = plan_rules([
            MappingRule("fei4jing4mai4", "肺静脉", 1),
            MappingRule("fei2jing4mai4", "腓静脉", 2),
        ])
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "replace.fst"
            build_fst(plan, output)
            self.assertEqual(rewrite_text(output, "fei4jing4mai4"), "肺静脉")
            self.assertEqual(rewrite_text(output, "fei2jing4mai4"), "腓静脉")
            self.assertEqual(rewrite_text(output, "fei3jing4mai4"), "fei3jing4mai4")
```

- [ ] **Step 2: Run tests before implementation**

Run:

```bash
python3 -m unittest make_replace.tests.test_build_replace_fst -v
```

Expected: skipped when Pynini is unavailable; missing-module failure when Pynini is installed.

- [ ] **Step 3: Implement exact and flexible transducers**

```python
# make_replace/build_replace_fst.py
from dataclasses import dataclass
from pathlib import Path
import argparse
import time

import pynini
from pynini import cdrewrite
from pynini.lib import pynutil, rewrite, utf8

from make_replace.rule_planner import FlexibleRule, MappingConflictError, RulePlan, load_mapping, plan_rules

FLEXIBLE_WEIGHT = 1.0


@dataclass(frozen=True)
class BuildStats:
    exact_rules: int
    flexible_rules: int
    conflicts: int
    output_bytes: int


def flexible_acceptor(rule: FlexibleRule) -> pynini.Fst:
    tone = pynini.union("1", "2", "3", "4").optimize()
    parts = [pynini.accep(item.base) + tone for item in rule.syllables]
    result = parts[0]
    for part in parts[1:]:
        result += part
    return result.optimize()


def build_fst(plan: RulePlan, output: Path, exact_only: bool = False) -> BuildStats:
    rules = [pynini.cross(rule.pinyin, rule.target) for rule in plan.exact_rules]
    if not rules:
        raise ValueError("mapping contains no valid rules")
    if not exact_only:
        rules.extend(
            pynutil.add_weight(pynini.cross(flexible_acceptor(rule), rule.target), FLEXIBLE_WEIGHT)
            for rule in plan.flexible_rules
        )
    union = pynini.union(*rules).optimize()
    compiled = cdrewrite(union, "", "", utf8.VALID_UTF8_CHAR.star).optimize()
    temporary = output.with_name(output.name + ".tmp")
    compiled.write(str(temporary))
    temporary.replace(output)
    return BuildStats(len(plan.exact_rules), 0 if exact_only else len(plan.flexible_rules),
                      len(plan.toneless_conflicts), output.stat().st_size)


def rewrite_text(fst_path: Path, text: str) -> str:
    rule = pynini.Fst.read(str(fst_path))
    return rewrite.one_top_rewrite(text, rule)


def write_build_report(path: Path, stats: BuildStats, plan: RulePlan, elapsed: float) -> None:
    lines = [
        f"精确规则: {stats.exact_rules}",
        f"宽松规则: {stats.flexible_rules}",
        f"纯拼音冲突: {stats.conflicts}",
        f"FST 字节数: {stats.output_bytes}",
        f"构建秒数: {elapsed:.3f}",
        "",
        "纯拼音冲突明细:",
    ]
    lines.extend(
        f"{item.toneless_key}={' / '.join(item.targets)}"
        for item in plan.toneless_conflicts
    )
    temporary = path.with_name(path.name + ".tmp")
    temporary.write_text("\n".join(lines) + "\n", encoding="utf-8")
    temporary.replace(path)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--mapping", type=Path, required=True)
    parser.add_argument("--fst-output", type=Path, required=True)
    parser.add_argument("--report-output", type=Path, required=True)
    parser.add_argument("--exact-only", action="store_true")
    args = parser.parse_args(argv)
    try:
        plan = plan_rules(load_mapping(args.mapping))
        started = time.perf_counter()
        fst_candidate = args.fst_output.with_name(args.fst_output.name + ".candidate")
        report_candidate = args.report_output.with_name(args.report_output.name + ".candidate")
        stats = build_fst(plan, fst_candidate, args.exact_only)
        write_build_report(report_candidate, stats, plan, time.perf_counter() - started)
        fst_candidate.replace(args.fst_output)
        report_candidate.replace(args.report_output)
    except (OSError, ValueError, MappingConflictError) as error:
        print(f"FST build failed: {error}")
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
```

- [ ] **Step 4: Run compiler tests in a Pynini environment**

Run after obtaining approval for dependency installation if Pynini is absent:

```bash
python3 -m pip install --only-binary :all: pynini
python3 -m unittest make_replace.tests.test_build_replace_fst -v
```

Expected: both tests pass. If the current platform has no compatible wheel, run the same tests in the Colab notebook created in Task 7 and record the output.

- [ ] **Step 5: Verify safe failure and prepare review**

Run:

```bash
python3 -m unittest discover -s make_replace/tests -v
git diff -- make_replace/build_replace_fst.py make_replace/tests/test_build_replace_fst.py
```

Expected: all available tests pass; Pynini tests are either passing or explicitly skipped because the module is absent. Proposed commit: `feat(fst): 生成精确优先的单一规则文件`。Do not commit without user approval.

---

### Task 5: Replace the Hard-Coded Generator Entry Point

**Files:**
- Modify: `make_replace/main.py`
- Create: `make_replace/tests/test_main_cli.py`

**Interfaces:**
- Produces command: `python3 -m make_replace.main prepare ...`
- Produces command: `python3 -m make_replace.main build ...`

- [ ] **Step 1: Write failing CLI dispatch tests**

```python
# make_replace/tests/test_main_cli.py
import unittest
from unittest.mock import patch

from make_replace import main


class MainCliTest(unittest.TestCase):
    @patch("make_replace.main.generate_mapping_main", return_value=0)
    def test_prepare_dispatches_to_mapping_generator(self, generate):
        self.assertEqual(main.run(["prepare", "--help"]), 0)
        generate.assert_called_once()

    @patch("make_replace.main.build_fst_main", return_value=0)
    def test_build_dispatches_to_fst_builder(self, build):
        self.assertEqual(main.run(["build", "--help"]), 0)
        build.assert_called_once()
```

- [ ] **Step 2: Run the test and verify it fails**

Run:

```bash
python3 -m unittest make_replace.tests.test_main_cli -v
```

Expected: the current hard-coded module has no `run()` dispatcher.

- [ ] **Step 3: Replace hard-coded rules with a thin dispatcher**

```python
# make_replace/main.py
import sys

from make_replace.build_replace_fst import main as build_fst_main
from make_replace.generate_mapping import main as generate_mapping_main


def run(argv: list[str] | None = None) -> int:
    args = list(sys.argv[1:] if argv is None else argv)
    if not args or args[0] not in {"prepare", "build"}:
        print("Usage: python3 -m make_replace.main {prepare|build} [options]")
        return 2
    command = args.pop(0)
    return generate_mapping_main(args) if command == "prepare" else build_fst_main(args)


if __name__ == "__main__":
    raise SystemExit(run())
```

Adjust Task 2 and Task 4 `main()` functions to accept `argv: list[str] | None = None` so the dispatcher does not mutate global `sys.argv`.

- [ ] **Step 4: Run all Python tests**

Run:

```bash
python3 -m unittest discover -s make_replace/tests -v
```

Expected: all non-Pynini tests pass; Pynini integration tests pass or are explicitly skipped.

- [ ] **Step 5: Prepare review checkpoint**

Run:

```bash
git diff -- make_replace/main.py make_replace/tests/test_main_cli.py
```

Proposed commit: `refactor(fst): 统一规则准备与编译入口`。Do not commit without user approval.

---

### Task 6: Add Exact-First Runtime Matching for Temporary Rules

**Files:**
- Create: `src/runtime-rule-matcher.h`
- Create: `src/runtime-rule-matcher.cc`
- Create: `tests/runtime-rule-matcher-test.cc`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Produces: `RuntimeRuleMatcher::Reset(const std::unordered_map<std::string, std::string>&)`
- Produces: `RuntimeRuleMatcher::FindBest(const std::vector<std::string>&, size_t) -> std::optional<RuntimeRuleMatch>`
- Produces: `RemoveToneDigits(std::string_view) -> std::string`

- [ ] **Step 1: Obtain explicit approval for the root CMake change**

Explain that the new `.cc` file and standalone test executable require adding sources and `add_test()` entries. Do not continue this task until the user approves the root configuration edit.

- [ ] **Step 2: Write the failing standalone C++ test**

```cpp
// tests/runtime-rule-matcher-test.cc
#include <cassert>
#include <string>
#include <unordered_map>
#include <vector>

#include "runtime-rule-matcher.h"

using hr_standalone::RuntimeRuleMatcher;

int main() {
  RuntimeRuleMatcher matcher;
  matcher.Reset({{"luan3chao2", "卵巢"}});

  auto exact = matcher.FindBest({"luan3", "chao2"}, 0);
  assert(exact && exact->replacement == "卵巢" && exact->exact);

  auto flexible = matcher.FindBest({"luan4", "chao2"}, 0);
  assert(flexible && flexible->replacement == "卵巢" && !flexible->exact);

  matcher.Reset({
      {"fei4jing4mai4", "肺静脉"},
      {"fei2jing4mai4", "腓静脉"},
  });
  assert(matcher.FindBest({"fei4", "jing4", "mai4"}, 0)->replacement == "肺静脉");
  assert(matcher.FindBest({"fei2", "jing4", "mai4"}, 0)->replacement == "腓静脉");
  assert(!matcher.FindBest({"fei3", "jing4", "mai4"}, 0));
  return 0;
}
```

- [ ] **Step 3: Register the test and verify it fails**

Apply these exact CMake additions after approval:

```cmake
set(CORE_SOURCES
  src/homophone-replacer.cc
  src/runtime-rule-matcher.cc
)

include(CTest)
if(BUILD_TESTING)
  add_executable(runtime-rule-matcher-test
    tests/runtime-rule-matcher-test.cc
    src/runtime-rule-matcher.cc
  )
  target_include_directories(runtime-rule-matcher-test PRIVATE ${PROJECT_SOURCE_DIR}/src)
  target_compile_features(runtime-rule-matcher-test PRIVATE cxx_std_17)
  add_test(NAME runtime-rule-matcher COMMAND runtime-rule-matcher-test)
endif()
```

Run:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j4
ctest --test-dir build --output-on-failure -R runtime-rule-matcher
```

Expected: compilation fails because `runtime-rule-matcher.h` and its implementation do not yet exist.

- [ ] **Step 4: Implement the runtime rule matcher**

```cpp
// src/runtime-rule-matcher.h
#ifndef RUNTIME_RULE_MATCHER_H_
#define RUNTIME_RULE_MATCHER_H_

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace hr_standalone {

struct RuntimeRuleMatch {
  size_t end_index;
  std::string replacement;
  bool exact;
};

std::string RemoveToneDigits(std::string_view pinyin);

class RuntimeRuleMatcher {
 public:
  void Reset(const std::unordered_map<std::string, std::string> &rules);
  std::optional<RuntimeRuleMatch> FindBest(
      const std::vector<std::string> &pronunciations, size_t begin_index) const;

 private:
  std::unordered_map<std::string, std::string> exact_rules_;
  std::unordered_map<std::string, std::string> flexible_rules_;
  size_t max_exact_key_length_ = 0;
  size_t max_flexible_key_length_ = 0;
};

}  // namespace hr_standalone
#endif
```

Implement the source file as follows:

```cpp
// src/runtime-rule-matcher.cc
#include "runtime-rule-matcher.h"

#include <algorithm>
#include <set>
#include <utility>

namespace hr_standalone {
namespace {

std::optional<RuntimeRuleMatch> FindLongest(
    const std::unordered_map<std::string, std::string> &rules,
    const std::vector<std::string> &pronunciations, size_t begin,
    size_t max_key_length, bool remove_tones, bool exact) {
  std::string key;
  std::vector<std::pair<size_t, std::string>> candidates;
  for (size_t end = begin; end < pronunciations.size(); ++end) {
    key += remove_tones ? RemoveToneDigits(pronunciations[end]) : pronunciations[end];
    if (key.size() > max_key_length) break;
    candidates.emplace_back(end + 1, key);
  }
  for (auto it = candidates.rbegin(); it != candidates.rend(); ++it) {
    auto found = rules.find(it->second);
    if (found != rules.end()) {
      return RuntimeRuleMatch{it->first, found->second, exact};
    }
  }
  return std::nullopt;
}

}  // namespace

std::string RemoveToneDigits(std::string_view pinyin) {
  std::string result;
  result.reserve(pinyin.size());
  for (char value : pinyin) {
    if (value < '1' || value > '4') result.push_back(value);
  }
  return result;
}

void RuntimeRuleMatcher::Reset(
    const std::unordered_map<std::string, std::string> &rules) {
  exact_rules_ = rules;
  flexible_rules_.clear();
  max_exact_key_length_ = 0;
  max_flexible_key_length_ = 0;
  std::unordered_map<std::string, std::set<std::string>> groups;
  for (const auto &entry : exact_rules_) {
    max_exact_key_length_ = std::max(max_exact_key_length_, entry.first.size());
    groups[RemoveToneDigits(entry.first)].insert(entry.second);
  }
  for (const auto &entry : groups) {
    if (entry.second.size() != 1) continue;
    flexible_rules_[entry.first] = *entry.second.begin();
    max_flexible_key_length_ = std::max(max_flexible_key_length_, entry.first.size());
  }
}

std::optional<RuntimeRuleMatch> RuntimeRuleMatcher::FindBest(
    const std::vector<std::string> &pronunciations, size_t begin_index) const {
  auto exact = FindLongest(exact_rules_, pronunciations, begin_index,
                           max_exact_key_length_, false, true);
  if (exact) return exact;
  return FindLongest(flexible_rules_, pronunciations, begin_index,
                     max_flexible_key_length_, true, false);
}

}  // namespace hr_standalone
```

- [ ] **Step 5: Run the standalone test**

Run:

```bash
cmake --build build -j4
ctest --test-dir build --output-on-failure -R runtime-rule-matcher
```

Expected: one test passes, zero failures.

- [ ] **Step 6: Check project limits and prepare review**

Run:

```bash
wc -l src/runtime-rule-matcher.h src/runtime-rule-matcher.cc tests/runtime-rule-matcher-test.cc
git diff -- CMakeLists.txt src/runtime-rule-matcher.h src/runtime-rule-matcher.cc tests/runtime-rule-matcher-test.cc
```

Expected: functions are at most 50 lines, files at most 300 lines, nesting at most three levels. Proposed commit: `feat(runtime): 支持临时规则不限声调匹配`。Do not commit without user approval.

---

### Task 7: Integrate the Runtime Matcher into HomophoneReplacer

**Files:**
- Modify: `src/homophone-replacer.cc`
- Modify: `src/homophone-replacer.h` only if a public diagnostic option is proven necessary; otherwise leave unchanged.
- Create: `make_replace/tests/test_runtime_cli.py`

**Interfaces:**
- Consumes: `RuntimeRuleMatcher` from Task 6
- Preserves: `--add-rule`, `--del-rule`, `--rules-file` CLI behavior and priority above FST

- [ ] **Step 1: Write failing CLI integration tests**

```python
# make_replace/tests/test_runtime_cli.py
import subprocess
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
EXE = ROOT / "build/bin/homophone-replacer-standalone"


@unittest.skipUnless(EXE.exists(), "build the C++ executable first")
class RuntimeCliTest(unittest.TestCase):
    def run_tool(self, text: str, *rules: str) -> str:
        args = [str(EXE), "--text", text]
        for rule in rules:
            args.extend(["--add-rule", rule])
        completed = subprocess.run(args, cwd=ROOT, text=True, capture_output=True, check=True)
        return completed.stdout

    def test_different_tone_uses_unique_runtime_fallback(self):
        output = self.run_tool("左侧乱潮", "luan3chao2=卵巢")
        self.assertIn("Result: 左侧卵巢", output)

    def test_runtime_toneless_conflict_does_not_guess(self):
        output = self.run_tool(
            "匪静脉",
            "fei4jing4mai4=肺静脉",
            "fei2jing4mai4=腓静脉",
        )
        self.assertIn("Result: 匪静脉", output)
```

- [ ] **Step 2: Build and verify the first test fails on current behavior**

Run:

```bash
cmake --build build -j4
python3 -m unittest make_replace.tests.test_runtime_cli -v
```

Expected: `左侧乱潮` remains unchanged before integration.

- [ ] **Step 3: Replace the current runtime map scan with RuntimeRuleMatcher**

Add `#include "runtime-rule-matcher.h"`. In `BuildRuntimeRuleMap()`, preserve current add/delete/file parsing semantics to build one final exact map, then call:

```cpp
runtime_rule_matcher_.Reset(runtime_rule_map_);
```

Replace `ApplyRuntimeOverrides()` with:

```cpp
std::string ApplyRuntimeOverrides(const std::string &text) const {
  if (runtime_rule_map_.empty()) return text;
  std::vector<std::string> words = Segment(SplitUtf8(text));
  std::vector<std::string> prons;
  prons.reserve(words.size());
  for (const auto &word : words) {
    if (word.size() < 3 ||
        reinterpret_cast<const uint8_t *>(word.data())[0] < 128) {
      prons.push_back(word);
    } else {
      prons.push_back(ConvertWordToPronunciation(word));
    }
  }

  std::string out;
  for (size_t index = 0; index < words.size();) {
    auto match = runtime_rule_matcher_.FindBest(prons, index);
    if (match) {
      out += match->replacement;
      index = match->end_index;
    } else {
      out += words[index];
      ++index;
    }
  }
  return out;
}
```

Add this private member next to `runtime_rule_map_`:

```cpp
RuntimeRuleMatcher runtime_rule_matcher_;
```

Keep exact matches higher priority than flexible matches and retain the current rule-file priority above FST.

- [ ] **Step 4: Run focused and regression checks**

Run:

```bash
cmake --build build -j4
ctest --test-dir build --output-on-failure
python3 -m unittest make_replace.tests.test_runtime_cli -v
./build/bin/homophone-replacer-standalone --text "他想知道玄界芯片问题的答案" --debug
```

Expected: all tests pass; `左侧乱潮` becomes `左侧卵巢` with the temporary rule; the existing `玄界芯片` regression still produces `玄戒芯片`.

- [ ] **Step 5: Prepare review checkpoint**

Run:

```bash
git diff -- src/homophone-replacer.cc make_replace/tests/test_runtime_cli.py
```

Proposed commit: `feat(runtime): 统一精确与宽松临时规则行为`。Do not commit without user approval.

---

### Task 8: Historical Colab Wrapper (Superseded by Local Docker)

> 本节是原实施记录，最终交付已改为本地 Docker，Notebook 已删除。当前操作方式以 README 和上方 Decision Amendment 为准。

**Files:**
- Create: `make_replace/generate_replace_fst_colab.ipynb`
- Modify: `README.md`

**Interfaces:**
- Colab invokes the same `make_replace.main prepare` and `make_replace.main build` entry points.
- Colab downloads generated mapping, preparation report, FST, and FST build report.

- [ ] **Step 1: Create the minimal notebook**

The notebook must contain these executable cells in order:

```python
!pip install --only-binary :all: pynini
!git clone https://github.com/KK-KANGKANG/homophone-replacer.git
%cd homophone-replacer
```

```python
from google.colab import files
uploaded = files.upload()  # upload 超声词汇.xlsx
```

```python
!python3 -m make_replace.main prepare \
  --excel 超声词汇.xlsx \
  --lexicon data/hr-files/lexicon.txt \
  --mapping-output /content/ultrasound-mapping.generated.txt \
  --report-output /content/ultrasound-mapping-report.txt
```

After the user reviews pronunciation and checks the automatically discarded exact-pinyin entries, the build cell must run:

```python
!python3 -m make_replace.main build \
  --mapping /content/ultrasound-mapping.reviewed.txt \
  --fst-output /content/replace.fst \
  --report-output /content/fst-build-report.txt
```

The final cell downloads all outputs with `files.download()`.

- [ ] **Step 2: Update README generation instructions**

Document:

- Excel and lexicon are the source for the new mapping.
- Existing historical mapping is not used.
- `prepare` must be reviewed before `build`.
- Professional polyphonic corrections belong in lexicon.
- Exact-pinyin conflicts stop compilation.
- Toneless conflicts keep exact rules and disable flexible rules.
- Production loads only the resulting FST.
- Link to the repository notebook using `https://colab.research.google.com/github/KK-KANGKANG/homophone-replacer/blob/main/make_replace/generate_replace_fst_colab.ipynb`.

- [ ] **Step 3: Validate notebook JSON and README commands**

Run:

```bash
python3 -m json.tool make_replace/generate_replace_fst_colab.ipynb >/dev/null
python3 -m make_replace.main prepare --help
python3 -m make_replace.main build --help
```

Expected: valid JSON and both commands exit 0 after printing help.

- [ ] **Step 4: Prepare review checkpoint**

Run:

```bash
git diff -- README.md make_replace/generate_replace_fst_colab.ipynb
```

Proposed commit: `docs(fst): 增加超声规则生成与 Colab 流程`。Do not commit without user approval.

---

### Task 9: Generate and Review the Real Ultrasound Rule Set

**Files:**
- Modify only after explicit data review: `data/hr-files/lexicon.txt`
- Modify after explicit data review: `data/hr-files/mapping.txt`
- Modify after successful build and benchmark: `data/hr-files/replace.fst`
- Generate: `data/hr-files/fst-build-report.txt`

**Interfaces:**
- Consumes the completed tools from Tasks 1–8.
- Produces the reviewed production data artifacts.

- [ ] **Step 1: Generate candidate data outside tracked project files**

Run:

```bash
python3 -m make_replace.main prepare \
  --excel 超声词汇.xlsx \
  --lexicon data/hr-files/lexicon.txt \
  --mapping-output /tmp/ultrasound-mapping.generated.txt \
  --report-output /tmp/ultrasound-mapping-report.txt
```

Expected: 2,001 unique terms, 1,960 pure-Chinese rules, 41 excluded mixed/non-Chinese terms, and no missing Chinese characters.

- [ ] **Step 2: Produce exact and toneless conflict reports**

Run:

```bash
python3 - <<'PY'
from pathlib import Path
from make_replace.rule_planner import load_mapping, plan_rules, MappingConflictError
try:
    plan = plan_rules(load_mapping(Path('/tmp/ultrasound-mapping.generated.txt')))
    print('exact', len(plan.exact_rules))
    print('flexible', len(plan.flexible_rules))
    print('toneless conflicts', len(plan.toneless_conflicts))
except MappingConflictError as error:
    print(error)
PY
```

Expected: the first review identifies the exact-pinyin groups `前臂/前壁`、`尺骨/耻骨`、`脐静脉/奇静脉`. No target is selected automatically.

- [ ] **Step 3: Stop for user review of pronunciation and discarded exact conflicts**

Provide the generated mapping, excluded-term report, known `长径` pronunciation issue, and the exact-conflict discard report. Exact conflicts retain the first mapping entry automatically. Ask the user only which professional pronunciations require lexicon entries and whether any discarded target should be reordered to become the retained rule.

Do not edit `lexicon.txt`, replace `mapping.txt`, or build the production FST before this approval.

- [ ] **Step 4: Apply only approved lexicon corrections and regenerate mapping**

Use `apply_patch` for approved text changes. Re-run `prepare` until the mapping pronunciation review passes; verify `plan_rules()` reports every discarded exact-key target.

- [ ] **Step 5: Present the new mapping replacement for approval**

Run:

```bash
diff -u data/hr-files/mapping.txt /tmp/ultrasound-mapping.reviewed.txt | sed -n '1,240p'
```

Explain that the existing mapping is unrelated historical data and will be replaced only after explicit approval. After approval, use `apply_patch` or a safe mechanical replacement to update `data/hr-files/mapping.txt`.

- [ ] **Step 6: Build candidate exact-only and target FST files outside tracked data**

Run in a Pynini environment:

```bash
python3 -m make_replace.main build \
  --mapping data/hr-files/mapping.txt \
  --fst-output /tmp/replace-exact.fst \
  --report-output /tmp/exact-report.txt \
  --exact-only
python3 -m make_replace.main build \
  --mapping data/hr-files/mapping.txt \
  --fst-output /tmp/replace-tone-aware.fst \
  --report-output /tmp/fst-build-report.txt
```

Expected: both builds complete; the target report lists exact rules, flexible rules, and disabled toneless conflict groups.

- [ ] **Step 7: Run functional examples with a temporary runtime directory**

Copy the executable, lexicon, and candidate FST into a clearly named temporary directory and verify:

```text
左侧卵巢 → 左侧卵巢
左侧乱潮 → 左侧卵巢
肺静脉 → 肺静脉
腓静脉 → 腓静脉
冲突组的错误声调输入 → 保留原文
```

- [ ] **Step 8: Replace production FST only after user approval**

Present file size, build report, functional output, and benchmark from Task 10. Replace `data/hr-files/replace.fst` and add `data/hr-files/fst-build-report.txt` only after explicit approval.

Proposed commit after all three data files are approved: `feat(data): 更新超声同音词规则`。Do not commit without user approval.

---

### Task 10: Performance and Completion Verification

**Files:**
- Create or keep temporary benchmark output outside tracked files.
- Modify no source unless a measured regression requires a focused optimization task.

- [ ] **Step 1: Run complete Python and C++ verification**

Run:

```bash
python3 -m unittest discover -s make_replace/tests -v
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j4
ctest --test-dir build --output-on-failure
```

Expected: zero test failures; Pynini tests must pass in the build environment used to create the production FST.

- [ ] **Step 2: Measure exact-only and tone-aware FST size and loading**

Run:

```bash
ls -lh /tmp/replace-exact.fst /tmp/replace-tone-aware.fst
/usr/bin/time -p ./build/bin/homophone-replacer-standalone --text "左侧卵巢"
```

Run each candidate from a temporary directory where it is named `data/hr-files/replace.fst`, and record initialization time from program output.

- [ ] **Step 3: Measure warm long-text throughput**

Run:

```bash
python3 - <<'PY'
import re
import shutil
import statistics
import subprocess
import tempfile
from pathlib import Path

root = Path.cwd()
exe = root / "build/bin/homophone-replacer-standalone"
lexicon = root / "data/hr-files/lexicon.txt"
text = "他想知道玄界芯片问题的答案" * 4000
candidates = {
    "exact": Path("/tmp/replace-exact.fst"),
    "tone-aware": Path("/tmp/replace-tone-aware.fst"),
}

for name, fst in candidates.items():
    with tempfile.TemporaryDirectory(prefix=f"hr-{name}-") as directory:
        work = Path(directory)
        data = work / "data/hr-files"
        data.mkdir(parents=True)
        shutil.copy2(lexicon, data / "lexicon.txt")
        shutil.copy2(fst, data / "replace.fst")
        timings = []
        for _ in range(6):
            completed = subprocess.run(
                [str(exe), "--text", text], cwd=work, text=True,
                capture_output=True, check=True,
            )
            match = re.search(r"Processing time: ([0-9.]+)s", completed.stdout)
            timings.append(float(match.group(1)))
        measured = timings[1:]
        median = statistics.median(measured)
        print(name, "median_s=", median, "chars_per_s=", round(len(text) / median))
PY
```

Acceptance:

```text
tone-aware median throughput >= exact-only median throughput * 0.80
```

- [ ] **Step 4: Run final functional smoke tests**

Run:

```bash
./build/bin/homophone-replacer-standalone --text "左侧乱潮" --debug
./build/bin/homophone-replacer-standalone --text "左侧卵巢" --debug
./build/bin/homophone-replacer-standalone --text "他想知道玄界芯片问题的答案" --debug
```

Expected: ultrasound exact and different-tone cases produce approved targets; unrelated text remains correct or unchanged according to the new rule corpus.

- [ ] **Step 5: Verify the delivery diff and report limitations**

Run:

```bash
git diff --check
git status --short
git diff --stat
```

Report:

- exact/flexible rule counts;
- exact and toneless conflict counts;
- excluded mixed terms;
- lexicon corrections;
- FST size and load time;
- exact-only versus tone-aware throughput;
- commands and exit status;
- any verification that could not run locally.

- [ ] **Step 6: Prepare final commit proposal without committing**

List the exact files and propose commit messages following repository convention, for example:

```text
feat(fst): 支持声调精确与无调唯一匹配
feat(data): 更新超声同音词规则
docs(fst): 补充规则生成与验证流程
```

Wait for explicit user approval before any `git add` or `git commit`.
