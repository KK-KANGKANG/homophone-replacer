import argparse
import json
import socket
import subprocess
import tempfile
import time
import urllib.error
import urllib.request
from pathlib import Path


def free_port():
    with socket.socket() as probe:
        probe.bind(("127.0.0.1", 0))
        return probe.getsockname()[1]


def request(url, data=None):
    headers = {"Content-Type": "application/json"}
    body = json.dumps(data, ensure_ascii=False).encode() if data else None
    with urllib.request.urlopen(
        urllib.request.Request(url, body, headers), timeout=3
    ) as response:
        return response.status, json.loads(response.read())


def wait_ready(url, process):
    for _ in range(100):
        if process.poll() is not None:
            raise RuntimeError("server exited before becoming ready")
        try:
            return request(url)
        except (urllib.error.URLError, ConnectionError):
            time.sleep(0.1)
    raise RuntimeError("server did not become ready")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--server", required=True)
    parser.add_argument("--root", required=True)
    args = parser.parse_args()
    root = Path(args.root).resolve()
    port = free_port()
    with tempfile.TemporaryDirectory() as directory:
        config = Path(directory) / "service.json"
        config.write_text(json.dumps({
            "server": {"host": "127.0.0.1", "port": port,
                       "worker_count": 1, "queue_capacity": 10,
                       "max_text_characters": 10000},
            "rules": {"lexicon": str(root / "data/hr-files/lexicon.txt"),
                      "fst": str(root / "data/hr-files/replace.fst"),
                      "mapping_enabled": False,
                      "mapping": str(root / "data/hr-files/mapping.txt")},
            "logging": {"level": "info", "directory": str(Path(directory) / "logs"),
                        "max_file_mb": 5, "max_files": 2}
        }), encoding="utf-8")
        process = subprocess.Popen(
            [args.server, "--config", str(config)], cwd=root,
            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        try:
            status, health = wait_ready(f"http://127.0.0.1:{port}/health", process)
            assert status == 200 and health["status"] == "ok"
            status, result = request(f"http://127.0.0.1:{port}/replace",
                                     {"text": "左侧乱潮"})
            assert status == 200 and result["result"] == "左侧卵巢"
        finally:
            process.terminate()
            try:
                process.wait(timeout=10)
            except subprocess.TimeoutExpired:
                process.kill()
                raise
        assert process.returncode == 0


if __name__ == "__main__":
    main()
