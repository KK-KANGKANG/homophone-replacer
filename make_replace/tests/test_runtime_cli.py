import subprocess
import sys
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
EXE = ROOT / "build/bin/homophone-replacer-standalone"


def native_executable_exists() -> bool:
    if not EXE.exists():
        return False
    with EXE.open("rb") as stream:
        magic = stream.read(4)
    if sys.platform.startswith("linux"):
        return magic == b"\x7fELF"
    if sys.platform == "darwin":
        return magic in {
            b"\xca\xfe\xba\xbe",
            b"\xbe\xba\xfe\xca",
            b"\xcf\xfa\xed\xfe",
            b"\xfe\xed\xfa\xcf",
        }
    if sys.platform == "win32":
        return magic[:2] == b"MZ"
    return False


@unittest.skipUnless(native_executable_exists(), "build a native C++ executable first")
class RuntimeCliTest(unittest.TestCase):
    def run_tool(self, text: str, *rules: str) -> str:
        args = [str(EXE), "--text", text]
        for rule in rules:
            args.extend(["--add-rule", rule])
        completed = subprocess.run(
            args,
            cwd=ROOT,
            text=True,
            capture_output=True,
            check=True,
        )
        return completed.stdout

    def test_different_tone_uses_unique_runtime_fallback(self):
        output = self.run_tool("左侧乱潮", "luan3chao2=卵巢")
        self.assertIn("Result: 左侧卵巢", output)

    def test_runtime_toneless_conflict_does_not_guess(self):
        output = self.run_tool(
            "匪静脉",
            "fei4jing4mai4=肺静脉",
            "fei2jing4mai4=腓静脉",
        )
        self.assertIn("Result: 匪静脉", output)


if __name__ == "__main__":
    unittest.main()
