# Documentation site

The Astra site is generated from Markdown with Python 3.11+ and no third-party packages. [Preview and publishing instructions](../docs/GITHUB_PAGES.md) cover local use and GitHub Pages.

```powershell
python site/build.py
python -m http.server 8000 --directory artifacts/pages
```

Open `http://localhost:8000`. Rebuild and reload after changing source. Generated HTML belongs in ignored `artifacts/pages/`.

## Content and navigation

`site/landing.html` is the landing page at `index.html`; `docs/INDEX.md` is the documentation introduction at `docs.html`. The public site publishes only the six user guides listed in `PUBLIC_DOCUMENTS` in `build.py`. Repository-only developer reports, publishing instructions, migration history, and README files are excluded from both output and search. Previously generated private routes are removed when rebuilding an existing output directory.

Supported Markdown includes headings, paragraphs, fenced code, tables, nested lists, quotes, inline code, links, images, bold, and italic. This covers the GFM used by the repository. Raw HTML and source snippets are escaped, never executed.

## Theme and assets

- `styles.css`: Astra's dark responsive layout, print styles, and reduced-motion support.
- `landing.html`: a lightweight home page linking to the docs, latest release, and Discord.
- `app.js`: mobile navigation, local full-text search, copy buttons, Luau highlighting, and table of contents tracking.
- `assets/astra.svg`: gradient A logo, also used as the favicon and README mark.

Content and navigation remain available without JavaScript. Search uses a generated local JSON file; it does not call an external service. Code highlighting inserts text nodes and never interprets source as HTML. No CDN or framework runtime is required.

Hover, focus, dialogs, and navigation use short CSS transitions. Motion is disabled when the visitor requests reduced motion. Release links follow the repository's latest GitHub release; the ZIP must be uploaded separately when publishing a release.

## Build checks

Each build verifies local page and asset links, heading anchors, and repository file references. URLs are relative, so project Pages subpaths and local previews use the same output. GitHub Actions supplies `GITHUB_REPOSITORY` and `GITHUB_SHA` for source links. To override them locally:

```powershell
python site/build.py --repository YourName/astra-decompiler --ref main
```

The workflow builds pull requests and deploys only the default branch. It never commits generated files.
