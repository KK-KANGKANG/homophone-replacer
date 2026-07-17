import importlib.util
import tempfile
import unittest
from pathlib import Path

from make_replace.rule_planner import MappingRule, plan_rules

PINYINI_AVAILABLE = importlib.util.find_spec("pynini") is not None


class BuildReplaceFstPureTest(unittest.TestCase):
    def test_flexible_bases_preserve_syllable_order(self):
        from make_replace.build_replace_fst import conflict_bases, flexible_bases

        plan = plan_rules([MappingRule("luan3chao2", "卵巢", 1)])
        self.assertEqual(flexible_bases(plan.flexible_rules[0]), ("luan", "chao"))
        conflict_plan = plan_rules(
            [
                MappingRule("fei4jing4mai4", "肺静脉", 1),
                MappingRule("fei2jing4mai4", "腓静脉", 2),
            ]
        )
        self.assertEqual(
            conflict_bases(conflict_plan.toneless_conflicts[0]),
            ("fei", "jing", "mai"),
        )

    def test_build_report_lists_discarded_exact_targets(self):
        from make_replace.build_replace_fst import BuildStats, write_build_report

        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            report = root / "report.txt"
            plan = plan_rules(
                [
                    MappingRule("qian2bi4", "前臂", 1),
                    MappingRule("qian2bi4", "前壁", 2),
                ]
            )
            write_build_report(
                report,
                BuildStats(1, 1, 0, 123),
                plan,
                0.25,
            )
            content = report.read_text(encoding="utf-8")
            self.assertIn("带调冲突丢弃: 1", content)
            self.assertIn("qian2bi4: 保留 前臂; 丢弃 前壁", content)


@unittest.skipUnless(PINYINI_AVAILABLE, "Pynini is required")
class BuildReplaceFstIntegrationTest(unittest.TestCase):
    def test_exact_and_different_tone_produce_same_target(self):
        from make_replace.build_replace_fst import build_fst, rewrite_text

        plan = plan_rules([MappingRule("luan3chao2", "卵巢", 1)])
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "replace.fst"
            build_fst(plan, output)
            self.assertEqual(rewrite_text(output, "#luan3##chao2#"), "卵巢")
            self.assertEqual(rewrite_text(output, "#luan4##chao2#"), "卵巢")
            self.assertEqual(
                rewrite_text(output, "未知#luan4##chao2#"), "未知卵巢"
            )

    def test_flexible_rule_does_not_match_inside_another_syllable(self):
        from make_replace.build_replace_fst import build_fst, rewrite_text

        plan = plan_rules([MappingRule("hou2", "喉", 1)])
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "replace.fst"
            build_fst(plan, output)
            self.assertEqual(rewrite_text(output, "#zhou1#"), "#zhou1#")
            self.assertEqual(rewrite_text(output, "#hou1#"), "喉")

    def test_toneless_conflict_has_no_flexible_rewrite(self):
        from make_replace.build_replace_fst import build_fst, rewrite_text

        plan = plan_rules(
            [
                MappingRule("fei4", "肺", 1),
                MappingRule("fei4jing4mai4", "肺静脉", 2),
                MappingRule("fei2jing4mai4", "腓静脉", 3),
            ]
        )
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "replace.fst"
            build_fst(plan, output)
            self.assertEqual(
                rewrite_text(output, "#fei4##jing4##mai4#"), "肺静脉"
            )
            self.assertEqual(
                rewrite_text(output, "#fei2##jing4##mai4#"), "腓静脉"
            )
            self.assertEqual(
                rewrite_text(output, "#fei3##jing4##mai4#"),
                "#fei3##jing4##mai4#",
            )
