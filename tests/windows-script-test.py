from pathlib import Path


root = Path(__file__).resolve().parents[1]
required = {
    "install-service.bat": ["sc.exe create HomophoneReplacer", "--service"],
    "uninstall-service.bat": ["sc.exe delete HomophoneReplacer"],
    "start-service.bat": ["sc.exe start HomophoneReplacer"],
    "stop-service.bat": ["sc.exe stop HomophoneReplacer"],
    "run-server.bat": ["homophone-replacer-server.exe", "--config"],
}
for name, fragments in required.items():
    text = (root / "scripts" / name).read_text(encoding="utf-8")
    for fragment in fragments:
        assert fragment in text, f"{name} missing {fragment}"
