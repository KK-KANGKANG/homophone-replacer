import unittest
from unittest.mock import patch

from make_replace import main


class MainCliTest(unittest.TestCase):
    @patch("make_replace.main.generate_mapping_main", return_value=0)
    def test_prepare_dispatches_to_mapping_generator(self, generate):
        self.assertEqual(main.run(["prepare", "--excel", "words.xlsx"]), 0)
        generate.assert_called_once_with(["--excel", "words.xlsx"])

    @patch("make_replace.main.build_fst_main", return_value=0)
    def test_build_dispatches_to_fst_builder(self, build):
        self.assertEqual(main.run(["build", "--mapping", "mapping.txt"]), 0)
        build.assert_called_once_with(["--mapping", "mapping.txt"])

    def test_unknown_command_returns_usage_error(self):
        self.assertEqual(main.run(["unknown"]), 2)
