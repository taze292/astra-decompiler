# Astra documentation site

The site is built from the root README, **every `docs/*.md` document**, and the third-party notices. Edit those Markdown files to change the documentation. New files in `docs/` automatically appear in the navigation. `docs/Luau-LICENSE.txt` is included as a downloadable license notice.

Run with Python 3.11 or newer; no packages need installing:

```powershell
python site/build.py
python -m http.server 8000 --directory artifacts/pages
```

Open `http://localhost:8000`. Generated files live in ignored `artifacts/pages/`. The build checks all local page/asset links and heading anchors, rejects missing repository links, and escapes code and raw HTML. It supports the repository's headings, paragraphs, fenced code, tables, nested lists, quotes, inline code, links, bold, and italic Markdown. It does not execute JavaScript or raw HTML from Markdown.

Source links default to `taze292/astra-decompiler` on `main`. Override them for another fork or branch:

```powershell
python site/build.py --repository YourName/astra-decompiler --ref main
```

GitHub Actions supplies `GITHUB_REPOSITORY` and `GITHUB_SHA` automatically, so deployed source links use the actual repository and exact commit. All generated internal URLs are relative; the same output works at a repository Pages subpath, a custom domain, or a local server. Styling is intentionally small and includes mobile, dark-mode, keyboard navigation, scrollable code/tables, and print rules.

The workflow builds documentation for pull requests and deploys only pushes/manual runs on the default branch. Setup instructions are in [the Pages guide](../docs/GITHUB_PAGES.md).
