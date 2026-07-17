"""Analyze OVA2 canonical terms against the project's pronunciation rules."""

import argparse
import json
import re
from collections import Counter, defaultdict
from pathlib import Path

from make_replace.lexicon_mapping import Lexicon, PURE_HAN


def remove_tones(pinyin: str) -> str:
    return re.sub(r"[1-5]", "", pinyin)


def load_existing_targets(path: Path) -> set[str]:
    targets = set()
    for line in path.read_text(encoding="utf-8").splitlines():
        if "=" in line and not line.lstrip().startswith("#"):
            targets.add(line.split("=", 1)[1].strip())
    return targets


def pronounce(term: str, lexicon: Lexicon) -> tuple[str | None, str]:
    if not PURE_HAN.fullmatch(term):
        return None, "non_han"
    result = lexicon.pronounce_text(term)
    if sum(char.isdigit() for char in result.pinyin) != len(term):
        return None, "missing_pinyin"
    return result.pinyin, "ok"


def analyze(json_path: Path, lexicon_path: Path, mapping_path: Path) -> tuple[str, str]:
    entries = json.loads(json_path.read_text(encoding="utf-8"))
    lexicon = Lexicon.from_file(lexicon_path)
    existing = load_existing_targets(mapping_path)
    canonical_entries: dict[str, list[dict]] = defaultdict(list)
    for entry in entries:
        canonical_entries[entry["canonical"]].append(entry)

    candidates: dict[str, str] = {}
    status_counts = Counter()
    relation_counts = Counter()
    source_type_counts = Counter()
    conflicts: dict[str, set[str]] = defaultdict(set)
    report_rows: list[str] = []

    for canonical, variants in sorted(canonical_entries.items()):
        canonical_pinyin, canonical_status = pronounce(canonical, lexicon)
        if canonical_status != "ok":
            status_counts[f"canonical_{canonical_status}"] += 1
            report_rows.append(f"{canonical}\t{canonical_status}")
            continue
        status_counts["canonical_ok"] += 1
        if canonical in existing:
            status_counts["already_in_mapping"] += 1
        else:
            candidates.setdefault(canonical_pinyin, canonical)
        for variant in variants:
            source_type_counts[variant.get("source_type", "unknown")] += 1
            src_pinyin, src_status = pronounce(variant["src"], lexicon)
            if src_status != "ok":
                status_counts[f"src_{src_status}"] += 1
                continue
            relation = "same_tone" if src_pinyin == canonical_pinyin else (
                "same_toneless" if remove_tones(src_pinyin) ==
                remove_tones(canonical_pinyin) else "different_toneless"
            )
            relation_counts[relation] += 1
            conflicts[remove_tones(canonical_pinyin)].add(canonical)

    conflict_items = {
        key: sorted(values) for key, values in conflicts.items() if len(values) > 1
    }
    conflicted_targets = {
        target for values in conflict_items.values() for target in values
    }
    candidate_lines = [
        f"{key}={value}"
        for key, value in sorted(candidates.items())
        if value not in existing and value not in conflicted_targets
    ]
    report = [
        "OVA2 canonical 词表分析报告",
        f"JSON 记录数: {len(entries)}",
        f"canonical 去重数: {len(canonical_entries)}",
        f"当前 mapping 已有目标数: {len(existing)}",
        f"可生成 canonical 规则数: {len(candidate_lines)}",
        f"因纯拼音冲突排除: {len(conflicted_targets)}",
        f"纯拼音冲突数: {len(conflict_items)}",
        "",
        "状态统计:",
    ]
    report.extend(f"{key}: {value}" for key, value in sorted(status_counts.items()))
    report.extend(["", "src/canonical 拼音关系:"])
    report.extend(f"{key}: {value}" for key, value in sorted(relation_counts.items()))
    report.extend(["", "source_type:"])
    report.extend(f"{key}: {value}" for key, value in sorted(source_type_counts.items()))
    report.extend(["", "纯拼音冲突明细:"])
    report.extend(f"{key}: {' / '.join(values)}" for key, values in sorted(conflict_items.items()))
    report.extend(["", "无法生成规则的 canonical:"])
    report.extend(report_rows)
    return "\n".join(candidate_lines) + "\n", "\n".join(report) + "\n"


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--json", type=Path, required=True)
    parser.add_argument("--lexicon", type=Path, required=True)
    parser.add_argument("--mapping", type=Path, required=True)
    parser.add_argument("--candidate-output", type=Path, required=True)
    parser.add_argument("--report-output", type=Path, required=True)
    args = parser.parse_args()
    candidate, report = analyze(args.json, args.lexicon, args.mapping)
    args.candidate_output.write_text(candidate, encoding="utf-8")
    args.report_output.write_text(report, encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
