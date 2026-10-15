#!/usr/bin/env python3
"""Build Astra's documentation with Python 3.11+ and no third-party packages.

The renderer intentionally supports the Markdown used in this repository:
headings, paragraphs, fenced code, lists, quotes, tables, links, and emphasis.
Raw HTML is escaped. Markdown stays the single source for GitHub and Pages.
"""

from __future__ import annotations

import argparse
import html
from html.parser import HTMLParser
import json
import os
from pathlib import Path
import re
import shutil
from urllib.parse import quote, unquote, urlsplit, urlunsplit


ROOT = Path(__file__).resolve().parent.parent
# Only user guides are published. Internal reports stay in the repository.
PUBLIC_DOCUMENTS = ("INDEX", "GETTING_STARTED", "CUSTOMIZATION", "SETUPS", "OPTIONS", "BUILDING")
DISCORD_URL = "https://discord.gg/FjUbXtdCzU"
LIST_ITEM = re.compile(r"^(\s*)([-+*]|\d+[.)])\s+(.+)$")
FENCE = re.compile(r"^\s*(`{3,}|~{3,})([^`~]*)$")


def table_cells(line: str) -> list[str]:
    """Keep escaped pipes and pipes inside code spans in their original cell."""
    line = line.strip().removeprefix("|").removesuffix("|")
    cells, current, code_ticks = [], [], 0
    index = 0
    while index < len(line):
        char = line[index]
        if char == "\\" and index + 1 < len(line):
            current.extend(line[index:index + 2])
            index += 2
            continue
        if char == "`":
            end = index + 1
            while end < len(line) and line[end] == "`":
                end += 1
            count = end - index
            code_ticks = count if code_ticks == 0 else (0 if count == code_ticks else code_ticks)
            current.append(line[index:end])
            index = end
            continue
        if char == "|" and code_ticks == 0:
            cells.append("".join(current).strip())
            current = []
        else:
            current.append(char)
        index += 1
    return cells + ["".join(current).strip()]


def table_separator(line: str) -> bool:
    return "|" in line and all(re.fullmatch(r":?-{3,}:?", cell) for cell in table_cells(line))


def plain_text(text: str) -> str:
    text = re.sub(r"!?\[([^\]]+)\]\([^)]*\)", r"\1", text)
    return re.sub(r"[`*_]", "", text)


class Markdown:
    def __init__(self, source: Path, pages: dict[Path, str], repository: str, ref: str):
        self.source = source
        self.pages = pages
        self.repository = repository
        self.ref = ref
        self.headings: list[tuple[int, str, str]] = []
        self.slug_counts: dict[str, int] = {}

    def source_url(self, path: Path) -> str:
        relative = path.relative_to(ROOT).as_posix()
        return f"https://github.com/{self.repository}/blob/{quote(self.ref, safe='')}/{quote(relative, safe='/')}"

    def link(self, target: str) -> str:
        target = target.strip().removeprefix("<").removesuffix(">")
        parts = urlsplit(target)
        if parts.scheme:
            return target if parts.scheme.lower() in {"https", "http", "mailto"} else "#"
        if parts.netloc:
            return "#"
        if not parts.path:
            return target
        # Resolve against the Markdown file, not the generated page's directory.
        path = (self.source.parent / unquote(parts.path)).resolve()
        if not path.is_relative_to(ROOT):
            raise ValueError(f"Link leaves the repository in {self.source}: {target}")
        destination = self.pages.get(path)
        if destination:
            return urlunsplit(("", "", destination, parts.query, parts.fragment))
        if path == ROOT / "docs" / "Luau-LICENSE.txt":
            return "luau-license.txt"
        if path.is_relative_to(ROOT / "site" / "assets"):
            if not path.is_file():
                raise ValueError(f"Missing site asset: {path}")
            return "assets/" + quote(path.relative_to(ROOT / "site" / "assets").as_posix(), safe="/")
        if not path.exists():
            raise ValueError(f"Missing local link in {self.source.relative_to(ROOT)}: {target}")
        return self.source_url(path) + (f"#{parts.fragment}" if parts.fragment else "")

    def inline(self, text: str) -> str:
        # Protect code and links before emphasis processing so source stays literal.
        tokens: list[str] = []

        def protect(value: str) -> str:
            tokens.append(value)
            return f"\x00{len(tokens) - 1}\x00"

        text = re.sub(
            r"(`+)(.+?)\1",
            lambda match: protect(f"<code>{html.escape(match[2])}</code>"), text,
        )

        def render_link(match: re.Match[str]) -> str:
            image, label, target = match[1], match[2], match[3]
            destination = html.escape(self.link(target), quote=True)
            if image:
                return protect(f'<img src="{destination}" alt="{html.escape(plain_text(label), quote=True)}" loading="lazy">')
            # Code spans in labels already belong to this invocation's tokens.
            # Format the label here instead of creating a second token namespace.
            label = html.escape(label)
            label = re.sub(r"\*\*(.+?)\*\*", r"<strong>\1</strong>", label)
            label = re.sub(r"(?<!\*)\*([^*]+)\*(?!\*)", r"<em>\1</em>", label)
            return protect(f'<a href="{destination}">{label}</a>')

        text = re.sub(r"(!?)\[([^\]]+)\]\((<[^>]+>|[^\s)]+)\)", render_link, text)
        text = html.escape(text)
        text = re.sub(r"\*\*(.+?)\*\*", r"<strong>\1</strong>", text)
        text = re.sub(r"(?<!\*)\*([^*]+)\*(?!\*)", r"<em>\1</em>", text)
        text = re.sub(r"\\([\\`*_{}\[\]()#+\-.!|>])", r"\1", text)
        # Links may themselves contain protected inline code in their labels.
        while re.search(r"\x00(\d+)\x00", text):
            text = re.sub(r"\x00(\d+)\x00", lambda match: tokens[int(match[1])], text)
        return text

    def slug(self, title: str) -> str:
        base = re.sub(r"[^\w\- ]", "", plain_text(title).lower()).replace(" ", "-") or "section"
        count = self.slug_counts.get(base, 0)
        self.slug_counts[base] = count + 1
        return base if count == 0 else f"{base}-{count}"

    def render(self, text: str) -> str:
        lines = text.splitlines()
        output: list[str] = []
        index = 0
        while index < len(lines):
            line = lines[index]
            if not line.strip():
                index += 1
                continue
            fence = FENCE.match(line)
            if fence:
                marker, language = fence[1], fence[2].strip().split(" ", 1)[0]
                code: list[str] = []
                index += 1
                while index < len(lines) and not re.fullmatch(r"\s*" + re.escape(marker[0]) + "{" + str(len(marker)) + r",}\s*", lines[index]):
                    code.append(lines[index])
                    index += 1
                if index == len(lines):
                    raise ValueError(f"Unclosed code fence in {self.source}")
                language_class = f' class="language-{html.escape(language, quote=True)}"' if re.fullmatch(r"[\w+-]+", language) else ""
                label = html.escape(language or "text")
                output.append(f'<div class="code-block"><div class="code-toolbar"><span>{label}</span><button type="button" class="copy-code" aria-label="Copy {label} code" hidden>Copy</button></div><pre><code{language_class}>{html.escape(chr(10).join(code))}\n</code></pre></div>')
                index += 1
                continue
            heading = re.match(r"^(#{1,6})\s+(.+?)(?:\s+#+)?$", line)
            if heading:
                level, title = len(heading[1]), heading[2]
                slug = self.slug(title)
                self.headings.append((level, plain_text(title), slug))
                output.append(f'<h{level} id="{slug}">{self.inline(title)}</h{level}>')
                index += 1
                continue
            if re.fullmatch(r"\s*(?:-{3,}|\*{3,}|_{3,})\s*", line):
                output.append("<hr>")
                index += 1
                continue
            if index + 1 < len(lines) and table_separator(lines[index + 1]):
                headers = table_cells(line)
                alignments = table_cells(lines[index + 1])
                if len(headers) != len(alignments):
                    raise ValueError(f"Mismatched table header in {self.source}")
                output.append('<div class="table-scroll" tabindex="0" role="region" aria-label="Scrollable table"><table><thead><tr>')
                output.extend(f'<th scope="col">{self.inline(cell)}</th>' for cell in headers)
                output.append("</tr></thead><tbody>")
                index += 2
                while index < len(lines) and lines[index].strip() and "|" in lines[index]:
                    cells = table_cells(lines[index])
                    if len(cells) != len(headers):
                        raise ValueError(f"Mismatched table row in {self.source}: {lines[index]}")
                    output.append("<tr>" + "".join(f"<td>{self.inline(cell)}</td>" for cell in cells) + "</tr>")
                    index += 1
                output.append("</tbody></table></div>")
                continue
            item = LIST_ITEM.match(line)
            if item:
                base_indent = len(item[1])
                ordered = item[2][0].isdigit()
                tag = "ol" if ordered else "ul"
                output.append(f"<{tag}>")
                while index < len(lines):
                    item = LIST_ITEM.match(lines[index])
                    if not item or len(item[1]) != base_indent or item[2][0].isdigit() != ordered:
                        break
                    body = [item[3]]
                    index += 1
                    while index < len(lines):
                        next_line = lines[index]
                        if next_line and len(next_line) - len(next_line.lstrip()) > base_indent:
                            body.append(next_line[min(len(item[1]) + len(item[2]) + 1, len(next_line) - len(next_line.lstrip())):])
                            index += 1
                        elif not next_line.strip() and index + 1 < len(lines) and LIST_ITEM.match(lines[index + 1]):
                            index += 1
                            break
                        else:
                            break
                    output.append("<li>" + self.render("\n".join(body)) + "</li>")
                output.append(f"</{tag}>")
                continue
            if line.startswith(">"):
                body = []
                while index < len(lines) and lines[index].startswith(">"):
                    body.append(lines[index].removeprefix(">").removeprefix(" "))
                    index += 1
                output.append("<blockquote>" + self.render("\n".join(body)) + "</blockquote>")
                continue
            paragraph = [line.strip()]
            index += 1
            while index < len(lines) and lines[index].strip():
                candidate = lines[index]
                if FENCE.match(candidate) or re.match(r"^#{1,6}\s", candidate) or LIST_ITEM.match(candidate) or candidate.startswith(">"):
                    break
                if index + 1 < len(lines) and table_separator(lines[index + 1]):
                    break
                paragraph.append(candidate.strip())
                index += 1
            output.append("<p>" + self.inline(" ".join(paragraph)) + "</p>")
        return "\n".join(output)


class LinkInventory(HTMLParser):
    def __init__(self):
        super().__init__()
        self.links: list[str] = []
        self.ids: set[str] = set()

    def handle_starttag(self, tag, attrs):
        values = dict(attrs)
        if "id" in values:
            if values["id"] in self.ids:
                raise ValueError(f'Duplicate HTML id: {values["id"]}')
            self.ids.add(values["id"])
        for attribute in ("href", "src"):
            if values.get(attribute):
                self.links.append(values[attribute])


def check_links(output: Path) -> int:
    inventories = {}
    for page in output.glob("*.html"):
        inventory = LinkInventory()
        inventory.feed(page.read_text(encoding="utf-8"))
        inventories[page.resolve()] = inventory
    checked = 0
    for page, inventory in inventories.items():
        for target in inventory.links:
            parts = urlsplit(target)
            if parts.scheme or parts.netloc:
                continue
            if parts.path.startswith("/"):
                raise ValueError(f"Root-relative link breaks project Pages: {target}")
            destination = (page.parent / unquote(parts.path)).resolve() if parts.path else page
            if not destination.is_relative_to(output.resolve()) or not destination.is_file():
                raise ValueError(f"Broken site link in {page.name}: {target}")
            if parts.fragment and destination in inventories and unquote(parts.fragment) not in inventories[destination].ids:
                raise ValueError(f"Broken site anchor in {page.name}: {target}")
            checked += 1
    return checked


def document_title(source: Path) -> str:
    if source.name == "THIRD_PARTY_NOTICES.md":
        return "Third-party notices"
    for line in source.read_text(encoding="utf-8-sig").splitlines():
        if line.startswith("# "):
            return plain_text(line[2:])
    return source.stem.replace("_", " ").title()


def build(output: Path, repository: str, ref: str) -> None:
    documents = [ROOT / "docs" / f"{name}.md" for name in PUBLIC_DOCUMENTS]
    pages = {source.resolve(): ("docs.html" if source.stem == "INDEX" else source.stem.lower().replace("_", "-") + ".html") for source in documents}
    if len(set(pages.values())) != len(pages):
        raise ValueError("Documentation filenames produce duplicate site routes")
    output.mkdir(parents=True, exist_ok=True)
    # Retire previously generated private pages, including in reused previews.
    # Touch only known generated routes directly inside the output directory.
    retired = {path.stem.lower().replace("_", "-") + ".html" for path in (ROOT / "docs").glob("*.md")}
    retired.update(("repository.html", "development.html", "third-party-notices.html"))
    for route in retired - set(pages.values()) - {"index.html"}:
        stale = output / route
        if stale.is_file() and stale.resolve().parent == output.resolve():
            stale.unlink()
    shutil.copyfile(ROOT / "site" / "styles.css", output / "styles.css")
    shutil.copyfile(ROOT / "site" / "app.js", output / "app.js")
    shutil.copytree(ROOT / "site" / "assets", output / "assets", dirs_exist_ok=True)
    shutil.copyfile(ROOT / "docs" / "Luau-LICENSE.txt", output / "luau-license.txt")
    (output / ".nojekyll").write_text("", encoding="utf-8")
    titles = {source: document_title(source) for source in documents}
    repository_url = f"https://github.com/{repository}"
    release_url = repository_url + "/releases/latest"
    landing = (ROOT / "site" / "landing.html").read_text(encoding="utf-8")
    for key, value in {"REPOSITORY_URL": repository_url, "RELEASE_URL": release_url, "DISCORD_URL": DISCORD_URL}.items():
        landing = landing.replace("@@" + key + "@@", html.escape(value, quote=True))
    (output / "index.html").write_text(landing, encoding="utf-8", newline="\n")
    guide_names = ("INDEX", "GETTING_STARTED", "CUSTOMIZATION", "SETUPS")
    reference_names = ("OPTIONS", "BUILDING")
    labels = {"INDEX": "Introduction", "GETTING_STARTED": "Installation", "CUSTOMIZATION": "Customization", "SETUPS": "CLI and integrations", "OPTIONS": "All options", "BUILDING": "Build from source"}
    search_index = []
    for source in documents:
        renderer = Markdown(source, pages, repository, ref)
        body = renderer.render(source.read_text(encoding="utf-8-sig"))
        def navigation(items: list[Path]) -> str:
            return "\n".join(f'<li><a href="{pages[document.resolve()]}"' + (' aria-current="page"' if document == source else "") + f'>{html.escape(labels[document.stem])}</a></li>' for document in items)

        guides = [document for name in guide_names for document in documents if document.stem == name]
        reference = [document for name in reference_names for document in documents if document.stem == name]
        links = f'<section class="nav-group"><h2>Get started</h2><ul>{navigation(guides)}</ul></section><section class="nav-group"><h2>Reference</h2><ul>{navigation(reference)}</ul></section>'
        toc = "\n".join(f'<li><a href="#{slug}">{html.escape(title)}</a></li>' for level, title, slug in renderer.headings if level == 2)
        toc_html = f'<aside class="contents" aria-label="On this page"><p>On this page</p><ul>{toc}</ul></aside>' if toc else ""
        search_text = re.sub(r"\s+", " ", plain_text(re.sub(r"```[\s\S]*?```", "", source.read_text(encoding="utf-8-sig"))))
        search_index.append({"title": titles[source], "url": pages[source.resolve()], "text": search_text[:18000], "headings": [title for level, title, slug in renderer.headings if level == 2]})
        group = "Get started" if source in guides else "Reference"
        position = documents.index(source)
        adjacent = []
        for offset, direction in ((-1, "Previous"), (1, "Next")):
            if 0 <= position + offset < len(documents):
                neighbor = documents[position + offset]
                adjacent.append(f'<a class="page-{direction.lower()}" href="{pages[neighbor.resolve()]}"><span>{direction}</span>{html.escape(labels[neighbor.stem])}</a>')
        pagination = '<nav class="page-pagination" aria-label="Documentation pages">' + "".join(adjacent) + "</nav>"
        page = f'''<!doctype html>
<html lang="en">
<head>
    <meta charset="utf-8">
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <meta name="description" content="Set up Astra and customize Luau decompilation. Guides, configuration, and developer documentation.">
    <title>{html.escape(titles[source])} · Astra</title>
    <link rel="stylesheet" href="styles.css">
    <link rel="icon" href="assets/astra.svg" type="image/svg+xml">
    <script src="app.js" defer></script>
</head>
<body>
    <a class="skip-link" href="#content">Skip to content</a>
    <header class="site-header"><a class="brand" href="index.html" aria-label="Astra home"><img src="assets/astra.svg" alt="" width="30" height="30">Astra<span class="brand-divider"></span><span class="brand-label">Documentation</span></a><div class="header-actions"><button type="button" class="search-trigger" hidden><span>Search documentation</span><kbd>Ctrl K</kbd></button><a class="github-link" href="{release_url}">Download<span aria-hidden="true"> ↗</span></a><button type="button" class="mobile-toggle" aria-controls="documentation-nav" aria-expanded="false" hidden>Menu</button></div></header>
    <div class="layout">
        <nav id="documentation-nav" class="sidebar" aria-label="Documentation">{links}<div class="sidebar-community"><a href="{release_url}">Download Astra <span aria-hidden="true">↗</span></a><a href="{DISCORD_URL}">Join Discord <span aria-hidden="true">↗</span></a></div></nav>
        <main id="content" tabindex="-1"><div class="eyebrow">{group}</div><article>{body}</article>
            {pagination}<footer><span>Astra documentation</span><a href="{DISCORD_URL}">Need a hand? Join Discord <span aria-hidden="true">↗</span></a></footer>
        </main>
        {toc_html}
    </div>
    <dialog class="search-dialog" aria-labelledby="search-title"><div class="search-heading"><h2 id="search-title">Search documentation</h2><button type="button" class="close-search" aria-label="Close search">Esc</button></div><label class="visually-hidden" for="search-input">Search pages and options</label><input id="search-input" type="search" placeholder="Search pages, options, and guides…" autocomplete="off"><p class="search-status" role="status" aria-live="polite"></p><ul class="search-results"></ul><p class="search-hint">Use Tab to choose a result. Escape closes search.</p></dialog>
</body>
</html>
'''
        (output / pages[source.resolve()]).write_text(page, encoding="utf-8", newline="\n")
    (output / "search-index.json").write_text(json.dumps(search_index, ensure_ascii=True, separators=(",", ":")), encoding="utf-8", newline="\n")
    checked = check_links(output)
    print(f"Built Astra landing page and {len(documents)} public guides; checked {checked} local links and anchors.")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, default=ROOT / "artifacts" / "pages")
    parser.add_argument("--repository", default=os.environ.get("GITHUB_REPOSITORY", "taze292/astra-decompiler"), help="GitHub owner/repository used for source links")
    parser.add_argument("--ref", default=os.environ.get("GITHUB_SHA", "main"), help="Branch or commit used for source links")
    arguments = parser.parse_args()
    if not re.fullmatch(r"[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+", arguments.repository):
        parser.error("--repository must be a GitHub owner/repository")
    if not arguments.ref or any(ord(char) < 32 for char in arguments.ref):
        parser.error("--ref must be a branch or commit")
    build(arguments.output.resolve(), arguments.repository, arguments.ref)


if __name__ == "__main__":
    main()
