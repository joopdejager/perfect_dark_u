"""Minimal verified Debian package fetcher.
usage: debfetch.py index <InRelease> <base_url> <dist> <relpath> <out>
       debfetch.py pkgs <Packages> <base_url> <outdir> <pkg>...   (resolves Depends recursively)
"""
import sys, hashlib, lzma, re, urllib.request, os

def fetch(url):
    with urllib.request.urlopen(url, timeout=120) as r:
        return r.read()

def sha256(b): return hashlib.sha256(b).hexdigest()

def index(inrelease, base, dist, rel, out):
    text = open(inrelease, encoding="utf-8", errors="replace").read()
    sect = text.split("SHA256:")[1]
    want = None
    for line in sect.splitlines()[1:]:
        if not line.startswith(" "): break
        h, size, name = line.split()
        if name == rel: want = h
    if not want: sys.exit(f"{rel} not listed in InRelease")
    data = fetch(f"{base}/dists/{dist}/{rel}")
    if sha256(data) != want: sys.exit(f"hash mismatch for {rel}")
    if rel.endswith(".xz"): data = lzma.decompress(data)
    open(out, "wb").write(data)
    print(f"verified {rel}")

def parse(packages):
    pk = {}
    for stanza in open(packages, encoding="utf-8", errors="replace").read().split("\n\n"):
        f = {}
        key = None
        for line in stanza.splitlines():
            if line.startswith(" ") and key: f[key] += "\n" + line; continue
            if ":" in line:
                key, v = line.split(":", 1); f[key] = v.strip()
        if "Package" in f:
            pk.setdefault(f["Package"], f)
            for p in f.get("Provides", "").split(","):
                p = p.strip().split(" ")[0]
                if p: pk.setdefault("virtual:" + p, f)
    return pk

SKIP = {"debconf", "perl-base", "dpkg", "install-info", "libc-bin", "libgcc-s1-dummy"}

def pkgs(packages, base, outdir, names):
    pk = parse(packages); os.makedirs(outdir, exist_ok=True)
    todo, done = list(names), set()
    while todo:
        n = todo.pop()
        if n in done or n in SKIP: continue
        f = pk.get(n) or pk.get("virtual:" + n)
        if not f: print(f"  (missing {n})"); done.add(n); continue
        done.add(n); done.add(f["Package"])
        deb = os.path.join(outdir, os.path.basename(f["Filename"]))
        if not os.path.exists(deb):
            data = fetch(f"{base}/{f['Filename']}")
            if sha256(data) != f["SHA256"]: sys.exit(f"hash mismatch {n}")
            open(deb, "wb").write(data)
        for field in ("Pre-Depends", "Depends"):
            for alt in f.get(field, "").split(","):
                alt = alt.strip()
                if not alt: continue
                first = alt.split("|")[0].strip()
                todo.append(re.split(r"[ (:]", first)[0])
    print(f"fetched {len([d for d in os.listdir(outdir) if d.endswith('.deb')])} packages")

if sys.argv[1] == "index": index(*sys.argv[2:7])
else: pkgs(sys.argv[2], sys.argv[3], sys.argv[4], sys.argv[5:])
