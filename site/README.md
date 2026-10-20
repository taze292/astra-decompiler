# Website

The site uses Python 3.11+ and no third-party Python packages.

## Local preview

```powershell
python site/build.py
python -m http.server 8000 --directory artifacts/pages
```

Open `http://localhost:8000`. Rebuild and reload after edits. Generated pages, font caches, and local captures belong under ignored `artifacts/`.

The first build obtains Satoshi and Switzer from the official Fontshare packages and caches them in `artifacts/site-fonts/`. JetBrains Mono is bundled with its OFL notice. The generated site serves all fonts locally.

## Content

Only the six guides in `PUBLIC_DOCUMENTS` are published and searched. Source Markdown stays readable on GitHub. Old report routes are removed when rebuilding.

| Source | Purpose |
| --- | --- |
| `landing.html` | Hero, links, and source/output example |
| `../docs/` | Installation, customization, integrations, options, and building |
| `styles.css` | Shared themes, code colors, layout, and motion |
| `app.js` | Theme switch, example tabs, copy buttons, search, and navigation |
| `theme.js` | Apply the saved theme before painting |
| `assets/` | Astra star logo, official Luau logo, icons, fonts, and notices |
| `examples/` | Supplied sample source and output; displayed without execution |

Search runs locally against generated JSON. Internal navigation and search results stay in the current tab; external web links open in new tabs. Without JavaScript, documentation navigation and both example panels remain available.

Reduced-motion visitors get gentle fades. The toggle and hover states use short transitions.

## Checks and GitHub Pages

Every build validates local links, heading anchors, SVG symbols, and repository file references. All site links are relative so project Pages subpaths work.

The **Astra documentation** workflow builds pull requests and deploys the default branch. For a new repository, choose **GitHub Actions** in **Settings > Pages**, then run the workflow.

The build gets repository identity and source-link revisions from GitHub Actions. To override them locally:

```powershell
python site/build.py --repository YourName/astra-decompiler --ref main
```

Commit the sources and licenses. Generated HTML and Fontshare font binaries stay out of Git.
