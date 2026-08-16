#!/usr/bin/env python3
import hashlib
import lzma
import pathlib
import re
import sys
import urllib.request

REPOS = [
    ("https://deb.debian.org/debian-security/dists/bookworm-security/main/binary-amd64/Packages.xz",
     "https://deb.debian.org/debian-security/"),
    ("https://deb.debian.org/debian/dists/bookworm/main/binary-amd64/Packages.xz",
     "https://deb.debian.org/debian/"),
    ("https://deb.debian.org/debian/dists/bookworm-updates/main/binary-amd64/Packages.xz",
     "https://deb.debian.org/debian/"),
]


def paragraphs(text):
    for block in text.split("\n\n"):
        fields = {}
        current = None
        for line in block.splitlines():
            if line.startswith((" ", "\t")) and current:
                fields[current] += " " + line.strip()
            elif ": " in line:
                current, value = line.split(": ", 1)
                fields[current] = value
        if fields.get("Package"):
            yield fields


def dep_names(value):
    for group in value.split(","):
        alternatives = []
        for item in group.split("|"):
            name = re.split(r"\s*\(|\s*\[|\s*<", item.strip(), 1)[0]
            name = name.split(":", 1)[0]
            if name:
                alternatives.append(name)
        if alternatives:
            yield alternatives


def main():
    if len(sys.argv) < 4:
        raise SystemExit("usage: resolver STATUS OUTDIR PACKAGE...")
    status_path = pathlib.Path(sys.argv[1])
    outdir = pathlib.Path(sys.argv[2])
    requested = sys.argv[3:]
    outdir.mkdir(parents=True, exist_ok=True)

    installed = set()
    provided = set()
    for pkg in paragraphs(status_path.read_text(errors="replace")):
        if pkg.get("Status") == "install ok installed":
            installed.add(pkg["Package"])
            provided.update(x.strip().split()[0] for x in
                            pkg.get("Provides", "").split(",") if x.strip())

    available = {}
    virtual = {}
    for url, base in REPOS:
        print("index", url, flush=True)
        raw = urllib.request.urlopen(url, timeout=60).read()
        for pkg in paragraphs(lzma.decompress(raw).decode(errors="replace")):
            pkg["_Base"] = base
            available[pkg["Package"]] = pkg
            for name in pkg.get("Provides", "").split(","):
                parts = name.strip().split()
                if parts:
                    virtual.setdefault(parts[0], pkg["Package"])

    selected = {}
    queue = list(requested)
    while queue:
        name = queue.pop(0)
        if name in installed or name in provided or name in selected:
            continue
        actual = name if name in available else virtual.get(name)
        if not actual:
            raise SystemExit(f"unresolved dependency: {name}")
        if actual in installed or actual in selected:
            continue
        pkg = available[actual]
        selected[actual] = pkg
        for key in ("Pre-Depends", "Depends"):
            for alternatives in dep_names(pkg.get(key, "")):
                choice = next((x for x in alternatives
                               if x in installed or x in provided), None)
                if not choice:
                    choice = next((x for x in alternatives
                                   if x in available or x in virtual), None)
                if not choice:
                    raise SystemExit(f"unresolved alternatives for {actual}: "
                                     f"{' | '.join(alternatives)}")
                queue.append(choice)

    manifest = []
    total = 0
    for name in sorted(selected):
        pkg = selected[name]
        filename = pkg["Filename"]
        target = outdir / pathlib.Path(filename).name
        expected = pkg["SHA256"]
        if not target.exists() or hashlib.sha256(target.read_bytes()).hexdigest() != expected:
            print("download", name, pkg.get("Version", ""), flush=True)
            data = urllib.request.urlopen(pkg["_Base"] + filename,
                                          timeout=120).read()
            if hashlib.sha256(data).hexdigest() != expected:
                raise SystemExit(f"SHA256 mismatch: {name}")
            target.write_bytes(data)
        size = target.stat().st_size
        total += size
        manifest.append(f"{expected}  {target.name}  {name}  {pkg.get('Version', '')}")
    (outdir / "SHA256SUMS.packages").write_text("\n".join(manifest) + "\n")
    print(f"packages={len(selected)} bytes={total}")


if __name__ == "__main__":
    main()
