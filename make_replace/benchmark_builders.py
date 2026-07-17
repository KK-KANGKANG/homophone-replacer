"""Compare the current cdrewrite builder with a direct replacement graph."""

import argparse
from pathlib import Path

from make_replace.build_replace_fst import (
    _encode_syllables,
    _length_penalty,
    _load_pynini,
    _tone_flexible_acceptor,
)
from make_replace.rule_planner import load_mapping, parse_toned_pinyin, plan_rules


def rule_union(mapping: Path):
    pynini, _, pynutil, _, _ = _load_pynini()
    plan = plan_rules(load_mapping(mapping))
    max_syllables = max(
        len(parse_toned_pinyin(rule.pinyin)) for rule in plan.exact_rules
    )
    blocker_base = float(max_syllables + 1)
    flexible_base = blocker_base * 2
    rules = [
        pynutil.add_weight(
            pynini.cross(
                _encode_syllables(parse_toned_pinyin(rule.pinyin), pynini),
                rule.target,
            ),
            _length_penalty(
                max_syllables, len(parse_toned_pinyin(rule.pinyin))
            ),
        )
        for rule in plan.exact_rules
    ]
    rules.extend(
        pynutil.add_weight(
            pynini.cross(_tone_flexible_acceptor(rule.syllables, pynini),
                         rule.target),
            flexible_base + _length_penalty(max_syllables, len(rule.syllables)),
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
    return pynini.union(*rules).optimize()


def build(mapping: Path, output: Path, mode: str) -> None:
    pynini, cdrewrite, pynutil, _, utf8 = _load_pynini()
    rules = rule_union(mapping)
    print(f"rule_union_states={rules.num_states()}", flush=True)
    if mode == "old":
        compiled = cdrewrite(
            rules, "", "", utf8.VALID_UTF8_CHAR.star
        ).optimize()
    else:
        # Unmatched characters remain unchanged, but cost more than a rule path.
        fallback = pynutil.add_weight(utf8.VALID_UTF8_CHAR, 1000.0)
        compiled = pynini.union(rules, fallback).closure().optimize()
    print(f"compiled_states={compiled.num_states()}", flush=True)
    compiled.write(str(output))


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--mapping", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--mode", choices=("old", "direct"), required=True)
    args = parser.parse_args()
    build(args.mapping, args.output, args.mode)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
