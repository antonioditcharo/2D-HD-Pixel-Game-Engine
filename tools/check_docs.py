#!/usr/bin/env python3
"""Check the Markdown documentation for broken references.

Checks:
  1. Every source citation such as [S12] used in a document is defined in
     docs/sources.md, every defined source is cited at least once, and no
     source ID is defined twice.
  2. Every relative Markdown link points at a file that exists, and every
     "#anchor" matches a heading in the target file.

Run from anywhere:  python3 tools/check_docs.py
Exit code 0 means no problems were found.
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
SOURCES_FILE = REPO_ROOT / "docs" / "sources.md"

CITATION = re.compile(r"\[(S\d+)\]")
SOURCE_ROW = re.compile(r"^\| (S\d+) \|", re.MULTILINE)
LINK = re.compile(r"\]\(([^)\s]+)\)")
HEADING = re.compile(r"^#{1,6}\s+(.*)$", re.MULTILINE)
CODE_BLOCK = re.compile(r"```.*?```", re.DOTALL)
SKIP_DIRS = {".git", "build", "out", "node_modules", "vcpkg_installed", "third_party"}


def markdown_files() -> list[Path]:
    files = []
    for path in sorted(REPO_ROOT.rglob("*.md")):
        if not SKIP_DIRS.intersection(path.relative_to(REPO_ROOT).parts):
            files.append(path)
    return files


def heading_anchors(path: Path) -> set[str]:
    """Return the anchors GitHub generates for the headings in a file."""
    text = CODE_BLOCK.sub("", path.read_text(encoding="utf-8"))
    anchors = set()
    for match in HEADING.finditer(text):
        title = match.group(1).strip().lower()
        title = re.sub(r"[^\w\- ]", "", title)
        anchors.add(title.replace(" ", "-"))
    return anchors


def check_citations(files: list[Path]) -> list[str]:
    problems = []
    defined_list = SOURCE_ROW.findall(SOURCES_FILE.read_text(encoding="utf-8"))
    defined = set(defined_list)
    for source_id in sorted({s for s in defined_list if defined_list.count(s) > 1}):
        problems.append(f"docs/sources.md: {source_id} is defined more than once")

    cited: set[str] = set()
    for path in files:
        if path == SOURCES_FILE:
            continue
        found = set(CITATION.findall(path.read_text(encoding="utf-8")))
        cited |= found
        for source_id in sorted(found - defined):
            rel = path.relative_to(REPO_ROOT)
            problems.append(f"{rel}: cites {source_id}, which is not in docs/sources.md")

    for source_id in sorted(defined - cited, key=lambda s: int(s[1:])):
        problems.append(f"docs/sources.md: {source_id} is never cited")
    return problems


def check_links(files: list[Path]) -> list[str]:
    problems = []
    for path in files:
        text = CODE_BLOCK.sub("", path.read_text(encoding="utf-8"))
        rel = path.relative_to(REPO_ROOT)
        for link in LINK.findall(text):
            if re.match(r"^[a-z][a-z0-9+.-]*:", link):  # http:, https:, mailto:
                continue
            target_part, _, anchor = link.partition("#")
            target = (path.parent / target_part).resolve() if target_part else path
            if not target.exists():
                problems.append(f"{rel}: link target does not exist: {link}")
                continue
            if anchor and target.suffix == ".md" and anchor not in heading_anchors(target):
                problems.append(f"{rel}: no heading matches the anchor in: {link}")
    return problems


def main() -> int:
    files = markdown_files()
    problems = check_citations(files) + check_links(files)
    for problem in problems:
        print(problem)
    print(f"Checked {len(files)} Markdown files: {len(problems)} problem(s) found.")
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
