import tempfile
import unittest
from pathlib import Path

from make_replace.rule_planner import (
    MappingRule,
    load_mapping,
    parse_toned_pinyin,
    plan_rules,
)


class RulePlannerTest(unittest.TestCase):
    def test_parses_syllables(self):
        syllables = parse_toned_pinyin("luan3chao2")
        self.assertEqual(
            [(item.base, item.tone) for item in syllables],
            [("luan", "3"), ("chao", "2")],
        )

    def test_unique_toneless_target_gets_flexible_rule(self):
        plan = plan_rules([MappingRule("luan3chao2", "卵巢", 1)])
        self.assertEqual(len(plan.exact_rules), 1)
        self.assertEqual(len(plan.flexible_rules), 1)
        self.assertEqual(plan.flexible_rules[0].toneless_key, "luanchao")

    def test_multiple_toneless_targets_disable_flexible_rule(self):
        plan = plan_rules(
            [
                MappingRule("fei4jing4mai4", "肺静脉", 1),
                MappingRule("fei2jing4mai4", "腓静脉", 2),
            ]
        )
        self.assertEqual(plan.flexible_rules, ())
        self.assertEqual(plan.toneless_conflicts[0].toneless_key, "feijingmai")
        self.assertEqual(
            [(item.base, item.tone) for item in plan.toneless_conflicts[0].syllables],
            [("fei", "2"), ("jing", "4"), ("mai", "4")],
        )

    def test_same_exact_key_keeps_first_target_and_reports_discarded_rule(self):
        plan = plan_rules(
            [
                MappingRule("qian2bi4", "前臂", 1),
                MappingRule("qian2bi4", "前壁", 2),
            ]
        )
        self.assertEqual(plan.exact_rules, (MappingRule("qian2bi4", "前臂", 1),))
        self.assertEqual(plan.exact_conflicts[0].kept_rule.target, "前臂")
        self.assertEqual(
            plan.exact_conflicts[0].discarded_rules,
            (MappingRule("qian2bi4", "前壁", 2),),
        )

    def test_load_mapping_ignores_comments_and_keeps_line_numbers(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "mapping.txt"
            path.write_text(
                "# 妇科\n\nluan3chao2 = 卵巢\n",
                encoding="utf-8",
            )
            self.assertEqual(load_mapping(path), [MappingRule("luan3chao2", "卵巢", 3)])
