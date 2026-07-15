from dataclasses import dataclass
from pathlib import Path, PurePosixPath
from typing import Iterable
from xml.etree import ElementTree as ET
from zipfile import ZipFile
import re

MAIN_NS = "http://schemas.openxmlformats.org/spreadsheetml/2006/main"
REL_NS = "http://schemas.openxmlformats.org/officeDocument/2006/relationships"
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
            if len(fields) < 2:
                continue
            word = fields[0].lower()
            if word in entries:
                continue
            tokens = [cls._normalize_token(token) for token in fields[1:]]
            entries[word] = "".join(tokens)
        return cls(entries)

    @staticmethod
    def _normalize_token(token: str) -> str:
        return token if token[-1] <= "4" else token + "1"

    def contains(self, word: str) -> bool:
        return word in self._word_to_pronunciation

    def segment(self, text: str) -> list[str]:
        chars = list(text)
        words: list[str] = []
        index = 0
        while index < len(chars):
            matched = self._longest_word(chars, index)
            if matched:
                words.append(matched)
                index += len(matched)
            else:
                words.append(chars[index])
                index += 1
        return words

    def _longest_word(self, chars: list[str], index: int) -> str:
        max_length = min(10, len(chars) - index)
        for length in range(max_length, 1, -1):
            candidate = "".join(chars[index : index + length])
            if candidate in self._all_words:
                return candidate
        return ""

    def pronounce_word(self, word: str) -> str:
        if word in self._word_to_pronunciation:
            return self._word_to_pronunciation[word]
        if len(word) == 1:
            return word
        return "".join(self._word_to_pronunciation.get(char, char) for char in word)

    def pronounce_text(self, text: str) -> PronunciationResult:
        words = self.segment(text)
        pinyin = "".join(self.pronounce_word(word) for word in words)
        return PronunciationResult(words, pinyin)


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
        unknown = [char for char in term if not lexicon.contains(char)]
        if unknown:
            missing[term] = unknown
            continue
        result = lexicon.pronounce_text(term)
        if sum(char.isdigit() for char in result.pinyin) != len(term):
            invalid.append(term)
            continue
        rules.append((result.pinyin, term))
    return MappingPreparation(
        len(source_terms), len(unique_terms), rules, excluded, missing, invalid
    )


def _xml_text(element: ET.Element) -> str:
    return "".join(node.text or "" for node in element.iter(f"{{{MAIN_NS}}}t"))


def _shared_strings(archive: ZipFile) -> list[str]:
    if "xl/sharedStrings.xml" not in archive.namelist():
        return []
    root = ET.fromstring(archive.read("xl/sharedStrings.xml"))
    return [_xml_text(item) for item in root.findall(f"{{{MAIN_NS}}}si")]


def _sheet_paths(archive: ZipFile) -> list[str]:
    workbook = ET.fromstring(archive.read("xl/workbook.xml"))
    relationships = ET.fromstring(archive.read("xl/_rels/workbook.xml.rels"))
    targets = {item.attrib["Id"]: item.attrib["Target"] for item in relationships}
    sheets = workbook.find(f"{{{MAIN_NS}}}sheets")
    if sheets is None:
        return []
    paths: list[str] = []
    for sheet in sheets:
        target = targets[sheet.attrib[f"{{{REL_NS}}}id"]]
        paths.append(
            target.lstrip("/")
            if target.startswith("/")
            else str(PurePosixPath("xl") / target)
        )
    return paths


def _cell_value(cell: ET.Element, shared: list[str]) -> str | None:
    cell_type = cell.attrib.get("t")
    value_node = cell.find(f"{{{MAIN_NS}}}v")
    inline_node = cell.find(f"{{{MAIN_NS}}}is")
    if cell_type == "s" and value_node is not None:
        return shared[int(value_node.text or "0")]
    if cell_type == "inlineStr" and inline_node is not None:
        return _xml_text(inline_node)
    return value_node.text if value_node is not None else None


def read_xlsx_terms(path: Path) -> list[str]:
    with ZipFile(path) as archive:
        shared = _shared_strings(archive)
        values: list[str] = []
        for sheet_path in _sheet_paths(archive):
            sheet = ET.fromstring(archive.read(sheet_path))
            for cell in sheet.findall(f".//{{{MAIN_NS}}}c"):
                value = _cell_value(cell, shared)
                if value and value.strip():
                    values.append(value.strip())
        return values
