import argparse
import time
from dataclasses import dataclass
from pathlib import Path
from typing import Any

from make_replace.rule_planner import (
    FlexibleRule,
    RulePlan,
    TonelessConflict,
    load_mapping,
    parse_toned_pinyin,
    plan_rules,
)

@dataclass(frozen=True)
class BuildStats:
    exact_rules: int
    flexible_rules: int
    conflicts: int
    output_bytes: int


def flexible_bases(rule: FlexibleRule) -> tuple[str, ...]:
    return tuple(syllable.base for syllable in rule.syllables)


def conflict_bases(conflict: TonelessConflict) -> tuple[str, ...]:
    return tuple(syllable.base for syllable in conflict.syllables)


def _length_penalty(max_syllables: int, syllables: int) -> float:
    return float(max_syllables - syllables)


def _load_pynini() -> tuple[Any, Any, Any, Any, Any]:
    try:
        import pynini
        from pynini import cdrewrite
        from pynini.lib import pynutil, rewrite, utf8
    except ImportError as error:
        raise RuntimeError(
            "Pynini is unavailable; run the build command in the project Colab"
        ) from error
    return pynini, cdrewrite, pynutil, rewrite, utf8


def _tone_flexible_acceptor(syllables: Any, pynini: Any) -> Any:
    tone = pynini.union("1", "2", "3", "4").optimize()
    parts = [pynini.accep(syllable.base) + tone for syllable in syllables]
    result = parts[0]
    for part in parts[1:]:
        result += part
    return result.optimize()


def _flexible_acceptor(rule: FlexibleRule, pynini: Any) -> Any:
    return _tone_flexible_acceptor(rule.syllables, pynini)


def build_fst(
    plan: RulePlan, output: Path, exact_only: bool = False
) -> BuildStats:
    pynini, cdrewrite, pynutil, _, utf8 = _load_pynini()
    if not plan.exact_rules:
        raise ValueError("mapping contains no valid rules")
    max_syllables = max(
        len(parse_toned_pinyin(rule.pinyin)) for rule in plan.exact_rules
    )
    blocker_base = float(max_syllables + 1)
    flexible_base = blocker_base * 2
    rules = [
        pynutil.add_weight(
            pynini.cross(rule.pinyin, rule.target),
            _length_penalty(
                max_syllables, len(parse_toned_pinyin(rule.pinyin))
            ),
        )
        for rule in plan.exact_rules
    ]
    if not exact_only:
        rules.extend(
            pynutil.add_weight(
                pynini.cross(_flexible_acceptor(rule, pynini), rule.target),
                flexible_base
                + _length_penalty(max_syllables, len(rule.syllables)),
            )
            for rule in plan.flexible_rules
        )
        rules.extend(
            pynutil.add_weight(
                _tone_flexible_acceptor(conflict.syllables, pynini),
                blocker_base
                + _length_penalty(max_syllables, len(conflict.syllables)),
            )
            for conflict in plan.toneless_conflicts
        )
    rule_union = pynini.union(*rules).optimize()
    compiled = cdrewrite(rule_union, "", "", utf8.VALID_UTF8_CHAR.star).optimize()
    compiled.write(str(output))
    return BuildStats(
        len(plan.exact_rules),
        0 if exact_only else len(plan.flexible_rules),
        len(plan.toneless_conflicts),
        output.stat().st_size,
    )


def rewrite_text(fst_path: Path, text: str) -> str:
    pynini, _, _, rewrite, _ = _load_pynini()
    rule = pynini.Fst.read(str(fst_path))
    return rewrite.one_top_rewrite(text, rule)


def _atomic_write(path: Path, content: str) -> None:
    temporary = path.with_name(path.name + ".tmp")
    temporary.write_text(content, encoding="utf-8")
    temporary.replace(path)


def write_build_report(
    path: Path, stats: BuildStats, plan: RulePlan, elapsed: float
) -> None:
    discarded_exact_rules = sum(
        len(item.discarded_rules) for item in plan.exact_conflicts
    )
    lines = [
        f"精确规则: {stats.exact_rules}",
        f"宽松规则: {stats.flexible_rules}",
        f"带调冲突丢弃: {discarded_exact_rules}",
        f"纯拼音冲突: {stats.conflicts}",
        f"FST 字节数: {stats.output_bytes}",
        f"构建秒数: {elapsed:.3f}",
        "",
        "带调冲突明细:",
    ]
    lines.extend(
        f"{item.pinyin}: 保留 {item.kept_rule.target}; 丢弃 "
        + " / ".join(rule.target for rule in item.discarded_rules)
        for item in plan.exact_conflicts
    )
    lines.extend([
        "",
        "纯拼音冲突明细:",
    ])
    lines.extend(
        f"{item.toneless_key}={' / '.join(item.targets)}"
        for item in plan.toneless_conflicts
    )
    _atomic_write(path, "\n".join(lines) + "\n")


def create_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser()
    parser.add_argument("--mapping", type=Path, required=True)
    parser.add_argument("--fst-output", type=Path, required=True)
    parser.add_argument("--report-output", type=Path, required=True)
    parser.add_argument("--exact-only", action="store_true")
    return parser


def _remove_if_exists(path: Path) -> None:
    if path.exists():
        path.unlink()


def main(argv: list[str] | None = None) -> int:
    args = create_parser().parse_args(argv)
    fst_candidate = args.fst_output.with_name(args.fst_output.name + ".candidate")
    report_candidate = args.report_output.with_name(
        args.report_output.name + ".candidate"
    )
    try:
        plan = plan_rules(load_mapping(args.mapping))
        started = time.perf_counter()
        stats = build_fst(plan, fst_candidate, args.exact_only)
        write_build_report(
            report_candidate, stats, plan, time.perf_counter() - started
        )
        fst_candidate.replace(args.fst_output)
        report_candidate.replace(args.report_output)
    except (OSError, RuntimeError, ValueError) as error:
        _remove_if_exists(fst_candidate)
        _remove_if_exists(report_candidate)
        print(f"FST build failed: {error}")
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
