import tempfile
import unittest
from pathlib import Path
from zipfile import ZipFile

from make_replace.generate_mapping import write_mapping, write_preparation_report
from make_replace.lexicon_mapping import Lexicon, prepare_mapping, read_xlsx_terms
from make_replace.lexicon_mapping import MappingPreparation


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
            "</Relationships>",
        )
        archive.writestr(
            "xl/worksheets/sheet1.xml",
            '<?xml version="1.0" encoding="UTF-8"?>'
            '<worksheet xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main">'
            f"<sheetData>{rows}</sheetData>"
            "</worksheet>",
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
            self.assertEqual(
                lexicon.segment("卵巢肺静脉"), ["卵", "巢", "肺", "静脉"]
            )
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
