# Publishing Checklist

Before putting this folder on GitHub, keep the repo clean and documentation
only.

## Include

```text
Markdown summaries
derived observations
register names and offsets
small hand-written diagrams
links to public comparator sources only if independently legal to share
```

## Do Not Include

```text
NVIDIA source files
leaked archives
firmware blobs
VBIOS ROM images unless you have redistribution rights
private keys or signatures
host debugdump zips
compiled probing binaries
crash or exploit payloads
```

## Before Push

Run:

```bash
find . -type f | sort
git status --short
```

Then check for accidental binary blobs:

```bash
find . -type f -size +1M -print
```

The intended public shape is:

```text
README.md
LICENSE-NOTE.md
docs/*.md
data/key-observations.md
```
