import sys

from make_replace.build_replace_fst import main as build_fst_main
from make_replace.generate_mapping import main as generate_mapping_main


def run(argv: list[str] | None = None) -> int:
    args = list(sys.argv[1:] if argv is None else argv)
    if not args or args[0] not in {"prepare", "build"}:
        print("Usage: python3 -m make_replace.main {prepare|build} [options]")
        return 2
    command = args.pop(0)
    if command == "prepare":
        return generate_mapping_main(args)
    return build_fst_main(args)


if __name__ == "__main__":
    raise SystemExit(run())
