# Publishing the docs

Astra's documentation uses Markdown, a small Python generator, and GitHub Pages. The dark theme, navigation, search, code copying, and SVG logo are included. **No npm packages, custom token, or separate Pages branch are required.**

## Deploy on GitHub

For the main repository, Pages is already configured to use GitHub Actions. Push reviewed documentation changes to the default branch; the **Astra documentation** workflow builds and deploys them.

For a fresh fork or repository:

1. Open **Settings → Pages**.
2. Under **Build and deployment → Source**, choose **GitHub Actions**.
3. Open **Actions → Astra documentation → Run workflow** and choose the default branch.
4. Wait for the build and deploy jobs to finish. Open the deployment URL shown by GitHub.

The main site is [taze292.github.io/astra-decompiler](https://taze292.github.io/astra-decompiler/). Forks use their own repository identity automatically; all internal links are relative so repository subpaths work.

Pull requests build and check the site without deploying it. GitHub's [publishing-source guide](https://docs.github.com/en/pages/getting-started-with-github-pages/configuring-a-publishing-source-for-your-github-pages-site) and [custom-workflow guide](https://docs.github.com/en/pages/getting-started-with-github-pages/using-custom-workflows-with-github-pages) describe the platform settings.

## Preview locally

Use Python 3.11 or newer:

```powershell
python site/build.py
python -m http.server 8000 --directory artifacts/pages
```

Open `http://localhost:8000`. Rebuild and reload after edits. Generated files live in ignored `artifacts/pages/`; commit the Markdown, styles, JavaScript, SVG, and generator instead.

## Change the content

| File | Purpose |
| --- | --- |
| `docs/INDEX.md` | Documentation introduction |
| `docs/GETTING_STARTED.md` | Build, install, and troubleshoot |
| `docs/CUSTOMIZATION.md` | Common output settings |
| `docs/SETUPS.md` | CLI, batch, library, and platform setups |
| `docs/OPTIONS.md` | Every option and advanced behavior |
| `site/styles.css` | Theme and responsive layout |
| `site/app.js` | Local search, code copying, and mobile navigation |
| `site/assets/astra.svg` | Gradient A logo and favicon |

Every Markdown file in `docs/` is built automatically, along with the root README, `dev/README.md`, and third-party notices. Essentials and reference pages have dedicated sidebar groups; developer guides and reports remain accessible under **Developer notes**. Edit those groups in `site/build.py` when adding another primary guide. The source files remain readable on GitHub using GitHub-flavored Markdown.

The build checks local links and heading anchors, escapes raw HTML and code, and produces a local search index. JavaScript improves navigation; page content and links work without it.

## Troubleshooting

**`configure-pages` returns “Not Found”:** enable Pages and choose **GitHub Actions** under **Settings → Pages**, then rerun the failed job. The workflow does not automatically change repository settings. A separate token is normally unnecessary once Pages is configured.

**Deploy is waiting:** check the `github-pages` environment's protection rules. Restrict deployments to the default branch if needed.

**Build reports a broken link:** fix the Markdown path or heading anchor shown in the error, then rebuild.

**Pages is unavailable:** check the repository plan and visibility against GitHub's [Pages availability](https://docs.github.com/en/pages/getting-started-with-github-pages/configuring-a-publishing-source-for-your-github-pages-site). Published documentation is a public website.
