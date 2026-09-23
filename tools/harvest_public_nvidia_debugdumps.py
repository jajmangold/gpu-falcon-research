#!/usr/bin/env python3
import argparse
import html
import json
import re
import subprocess
import sys
from pathlib import Path
from urllib.parse import urljoin, urlparse

import requests


TOPICS = [
    "https://forums.developer.nvidia.com/t/gpu-has-fallen-off-the-bus/261735",
    "https://forums.developer.nvidia.com/t/red-dead-redemption-2-game-crashes-gfx-error/202571",
    "https://forums.developer.nvidia.com/t/bug-570-124-04-freeze-on-monitor-wakeup-flip-event-timeout/325659/34",
    "https://forums.developer.nvidia.com/t/unable-to-determine-the-device-handle-for-gpu-000000-0-unknown-error-after-executing-nvidia-smi/294342",
    "https://forums.developer.nvidia.com/t/unable-to-determine-the-device-handle-for-gpu-000000-0-unknown-error/205143",
    "https://forums.developer.nvidia.com/t/black-screen-after-login-with-wayland-gtx750i-through-displayport-to-a-dell-p2715q-under-fedora-27-unable-to-handle-kernel-null-pointer-dereference-at-00000000000000c0/59693",
    "https://forums.developer.nvidia.com/t/unable-to-determine-the-device-handle-for-gpu-000000-0-unknown-error/197974",
    "https://forums.developer.nvidia.com/t/problem-with-opengl-visualization-without-an-x-server/73204",
]

EXTERNAL_ARCHIVES = [
    "https://sliedes.kapsi.fi/nv/system-hang-seq.tar.zst",
]


def topic_json_url(url: str) -> str:
    m = re.search(r"/t/[^/]+/(\d+)", url)
    if not m:
        return url
    return f"https://forums.developer.nvidia.com/t/{m.group(1)}.json"


def attachment_urls_from_topic(session: requests.Session, url: str) -> list[str]:
    res = session.get(topic_json_url(url), timeout=30)
    res.raise_for_status()
    data = res.json()
    found: set[str] = set()
    for post in data.get("post_stream", {}).get("posts", []):
        cooked = html.unescape(post.get("cooked", ""))
        for match in re.finditer(r'href="([^"]+)"', cooked):
            href = match.group(1)
            if re.search(r"\.(zip|tar\.zst|tgz|tar\.gz|gz)(?:\?|$)", href, re.I):
                found.add(urljoin(url, href))
        for upload in post.get("uploads", []) or []:
            original = upload.get("original_filename", "")
            short = upload.get("short_url") or upload.get("url")
            if short and re.search(r"\.(zip|tar\.zst|tgz|tar\.gz|gz)$", original, re.I):
                found.add(urljoin(url, short))
    return sorted(found)


def safe_name(url: str, seen: set[str]) -> str:
    path = urlparse(url).path
    name = Path(path).name or "download.bin"
    name = re.sub(r"[^A-Za-z0-9._-]+", "_", name)
    if name in seen:
        stem = Path(name).stem
        suffix = "".join(Path(name).suffixes)
        i = 2
        while f"{stem}-{i}{suffix}" in seen:
            i += 1
        name = f"{stem}-{i}{suffix}"
    seen.add(name)
    return name


def download(session: requests.Session, url: str, dest: Path) -> dict:
    r = session.get(url, timeout=120, allow_redirects=True)
    item = {
        "url": url,
        "status_code": r.status_code,
        "content_type": r.headers.get("content-type", ""),
        "path": str(dest),
        "bytes": 0,
    }
    if r.status_code == 200:
        dest.write_bytes(r.content)
        item["bytes"] = dest.stat().st_size
    return item


def extract_archive(path: Path, outdir: Path) -> list[Path]:
    outdir.mkdir(parents=True, exist_ok=True)
    if path.name.endswith(".tar.zst"):
        subprocess.run(["tar", "--use-compress-program=unzstd", "-xf", str(path), "-C", str(outdir)], check=False)
    elif path.suffix == ".zip":
        subprocess.run(["unzip", "-q", "-o", str(path), "-d", str(outdir)], check=False)
    elif path.suffix == ".gz":
        subprocess.run(["cp", str(path), str(outdir / path.name)], check=False)
    return [p for p in outdir.rglob("*") if p.is_file()]


def try_decrypt(extractor: Path, archive: Path, outdir: Path) -> dict:
    outdir.mkdir(parents=True, exist_ok=True)
    proc = subprocess.run([str(extractor), str(archive), str(outdir)], text=True, capture_output=True)
    files = [str(p.relative_to(outdir)) for p in outdir.rglob("*") if p.is_file()]
    return {
        "archive": str(archive),
        "returncode": proc.returncode,
        "stdout_tail": proc.stdout[-2000:],
        "stderr_tail": proc.stderr[-2000:],
        "files": files,
    }


def printable_hits(path: Path) -> list[str]:
    data = path.read_bytes()
    hits = []
    for pat in [b"NVIDIA", b"Tesla", b"V100", b"Quadro", b"GeForce", b"RTX", b"GTX", b"CMP", b"InfoROM", b"VBIOS"]:
        if pat in data:
            hits.append(pat.decode())
    return hits


def classify_tree(tree: Path) -> dict:
    files = [p for p in tree.rglob("*") if p.is_file()]
    hitmap = {}
    for p in files:
        hits = printable_hits(p)
        if hits:
            hitmap[str(p.relative_to(tree))] = hits
    names = [str(p.relative_to(tree)) for p in files]
    return {"file_count": len(files), "files": names[:200], "printable_hits": hitmap}


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", required=True, type=Path)
    ap.add_argument("--extractor", required=True, type=Path)
    args = ap.parse_args()

    out = args.out
    downloads = out / "downloads"
    raw_extract = out / "raw-extract"
    decrypted = out / "decrypted"
    downloads.mkdir(parents=True, exist_ok=True)

    session = requests.Session()
    session.headers["User-Agent"] = "gpu-falcon-research public debugdump classifier"

    candidates: set[str] = set(EXTERNAL_ARCHIVES)
    topic_errors = {}
    for topic in TOPICS:
        try:
            candidates.update(attachment_urls_from_topic(session, topic))
        except Exception as exc:
            topic_errors[topic] = repr(exc)

    seen_names: set[str] = set()
    downloads_meta = []
    for url in sorted(candidates):
        name = safe_name(url, seen_names)
        downloads_meta.append(download(session, url, downloads / name))

    decrypt_meta = []
    raw_meta = {}
    for item in downloads_meta:
        p = Path(item["path"])
        if not p.exists() or item["bytes"] == 0:
            continue
        if p.name.endswith(".tar.zst") or p.suffix in {".zip", ".gz"}:
            files = extract_archive(p, raw_extract / p.stem)
            raw_meta[p.name] = [str(f.relative_to(raw_extract / p.stem)) for f in files[:500]]
        if p.suffix == ".zip":
            decrypt_meta.append(try_decrypt(args.extractor, p, decrypted / p.stem))
        for zp in (raw_extract / p.stem).rglob("*.zip") if (raw_extract / p.stem).exists() else []:
            decrypt_meta.append(try_decrypt(args.extractor, zp, decrypted / p.stem / zp.stem))

    report = {
        "topics": TOPICS,
        "topic_errors": topic_errors,
        "candidate_urls": sorted(candidates),
        "downloads": downloads_meta,
        "raw_extract": raw_meta,
        "decrypt": decrypt_meta,
        "classifications": {
            "raw_extract": classify_tree(raw_extract) if raw_extract.exists() else {},
            "decrypted": classify_tree(decrypted) if decrypted.exists() else {},
        },
    }
    (out / "harvest-report.json").write_text(json.dumps(report, indent=2))
    print(json.dumps({
        "candidates": len(candidates),
        "downloads": sum(1 for d in downloads_meta if d["bytes"] > 0),
        "decrypt_attempts": len(decrypt_meta),
        "report": str(out / "harvest-report.json"),
    }, indent=2))
    return 0


if __name__ == "__main__":
    sys.exit(main())
