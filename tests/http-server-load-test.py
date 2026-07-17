import argparse
import concurrent.futures
import json
import socket
import statistics
import subprocess
import tempfile
import time
import urllib.request
from pathlib import Path


def free_port():
    with socket.socket() as sock:
        sock.bind(("127.0.0.1", 0))
        return sock.getsockname()[1]


def send(url):
    started = time.perf_counter()
    body = json.dumps({"text": "左侧乱潮"}, ensure_ascii=False).encode()
    request = urllib.request.Request(
        url, body, {"Content-Type": "application/json"})
    with urllib.request.urlopen(request, timeout=10) as response:
        result = json.loads(response.read())
    assert result["result"] == "左侧卵巢"
    return (time.perf_counter() - started) * 1000


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--server", required=True)
    parser.add_argument("--root", default=".")
    parser.add_argument("--requests", type=int, default=10000)
    parser.add_argument("--concurrency", type=int, default=50)
    args = parser.parse_args()
    root = Path(args.root).resolve()
    port = free_port()
    with tempfile.TemporaryDirectory() as directory:
        config = Path(directory) / "service.json"
        config.write_text(json.dumps({
            "server": {"host": "127.0.0.1", "port": port,
                       "worker_count": 2, "queue_capacity": 100,
                       "max_text_characters": 10000},
            "rules": {"lexicon": str(root / "data/hr-files/lexicon.txt"),
                      "fst": str(root / "data/hr-files/replace.fst"),
                      "mapping_enabled": False},
            "logging": {"level": "warning",
                        "directory": str(Path(directory) / "logs")}
        }), encoding="utf-8")
        process = subprocess.Popen([args.server, "--config", str(config)], cwd=root)
        try:
            health = f"http://127.0.0.1:{port}/health"
            for _ in range(100):
                try:
                    urllib.request.urlopen(health, timeout=1).close()
                    break
                except Exception:
                    time.sleep(0.1)
            url = f"http://127.0.0.1:{port}/replace"
            with concurrent.futures.ThreadPoolExecutor(
                    max_workers=args.concurrency) as executor:
                values = list(executor.map(lambda _: send(url), range(args.requests)))
        finally:
            process.terminate()
            process.wait(timeout=10)
    ordered = sorted(values)
    percentile = lambda p: ordered[min(len(ordered) - 1, int(len(ordered) * p))]
    print(f"requests={len(values)} errors=0 average_ms={statistics.mean(values):.3f} "
          f"p95_ms={percentile(.95):.3f} p99_ms={percentile(.99):.3f}")


if __name__ == "__main__":
    main()
