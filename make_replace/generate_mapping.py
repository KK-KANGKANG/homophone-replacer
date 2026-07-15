import argparse
from pathlib import Path

from make_replace.lexicon_mapping import (
    Lexicon,
    MappingPreparation,
    prepare_mapping,
    read_xlsx_terms,
)


def atomic_write(path: Path, content: str) -> None:
    temporary = path.with_name(path.name + ".tmp")
    temporary.write_text(content, encoding="utf-8")
    temporary.replace(path)


def write_mapping(result: MappingPreparation, output: Path) -> None:
    lines = [f"{pinyin}={target}" for pinyin, target in sorted(result.rules)]
    atomic_write(output, "\n".join(lines) + ("\n" if lines else ""))


def _append_section(lines: list[str], title: str, values: list[str]) -> None:
    lines.extend(["", title, *values])


def write_preparation_report(result: MappingPreparation, output: Path) -> None:
    lines = [
        f"Excel 非空词条: {result.source_term_count}",
        f"Excel 去重词条: {result.unique_term_count}",
        f"有效纯中文规则: {len(result.rules)}",
        f"排除词条: {len(result.excluded_terms)}",
        f"lexicon 缺失词条: {len(result.missing_characters)}",
        f"拼音异常词条: {len(result.invalid_pronunciations)}",
    ]
    _append_section(lines, "排除词条明细:", result.excluded_terms)
    missing = [
        f"{term}: {''.join(characters)}"
        for term, characters in result.missing_characters.items()
    ]
    _append_section(lines, "lexicon 缺失明细:", missing)
    _append_section(lines, "拼音异常明细:", result.invalid_pronunciations)
    atomic_write(output, "\n".join(lines) + "\n")


def create_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser()
    parser.add_argument("--excel", type=Path, required=True)
    parser.add_argument("--lexicon", type=Path, required=True)
    parser.add_argument("--mapping-output", type=Path, required=True)
    parser.add_argument("--report-output", type=Path, required=True)
    return parser


def main(argv: list[str] | None = None) -> int:
    args = create_parser().parse_args(argv)
    terms = read_xlsx_terms(args.excel)
    lexicon = Lexicon.from_file(args.lexicon)
    result = prepare_mapping(terms, lexicon)
    write_mapping(result, args.mapping_output)
    write_preparation_report(result, args.report_output)
    return 1 if result.missing_characters or result.invalid_pronunciations else 0


if __name__ == "__main__":
    raise SystemExit(main())
