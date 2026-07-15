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
    syllables: tuple[PinyinSyllable, ...]


@dataclass(frozen=True)
class ExactConflict:
    pinyin: str
    kept_rule: MappingRule
    discarded_rules: tuple[MappingRule, ...]


@dataclass(frozen=True)
class RulePlan:
    exact_rules: tuple[MappingRule, ...]
    flexible_rules: tuple[FlexibleRule, ...]
    toneless_conflicts: tuple[TonelessConflict, ...]
    exact_conflicts: tuple[ExactConflict, ...]


def parse_toned_pinyin(key: str) -> tuple[PinyinSyllable, ...]:
    if not PINYIN_RE.fullmatch(key):
        raise ValueError(f"invalid toned pinyin: {key}")
    return tuple(
        PinyinSyllable(base, tone) for base, tone in SYLLABLE_RE.findall(key)
    )


def load_mapping(path: Path) -> list[MappingRule]:
    rules: list[MappingRule] = []
    for line_number, raw_line in enumerate(
        path.read_text(encoding="utf-8").splitlines(), 1
    ):
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


def _deduplicate(
    rules: Iterable[MappingRule],
) -> tuple[tuple[MappingRule, ...], tuple[ExactConflict, ...]]:
    first_rules: dict[str, MappingRule] = {}
    discarded: dict[str, list[MappingRule]] = defaultdict(list)
    for rule in rules:
        parse_toned_pinyin(rule.pinyin)
        kept = first_rules.setdefault(rule.pinyin, rule)
        if rule.target != kept.target:
            discarded[rule.pinyin].append(rule)
    exact = tuple(
        sorted(first_rules.values(), key=lambda item: (item.pinyin, item.target))
    )
    conflicts = tuple(
        ExactConflict(key, first_rules[key], tuple(discarded[key]))
        for key in sorted(discarded)
    )
    return exact, conflicts


def plan_rules(rules: Iterable[MappingRule]) -> RulePlan:
    exact, exact_conflicts = _deduplicate(rules)
    groups: dict[str, list[MappingRule]] = defaultdict(list)
    for rule in exact:
        groups[re.sub(r"[1-4]", "", rule.pinyin)].append(rule)

    flexible: list[FlexibleRule] = []
    conflicts: list[TonelessConflict] = []
    for toneless_key, group in sorted(groups.items()):
        targets = tuple(sorted({rule.target for rule in group}))
        if len(targets) == 1:
            flexible.append(
                FlexibleRule(
                    parse_toned_pinyin(group[0].pinyin), targets[0], toneless_key
                )
            )
        else:
            conflicts.append(
                TonelessConflict(
                    toneless_key, targets, parse_toned_pinyin(group[0].pinyin)
                )
            )
    return RulePlan(
        exact,
        tuple(flexible),
        tuple(conflicts),
        exact_conflicts,
    )
