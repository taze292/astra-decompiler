# Publish the Astra documentation

The repository includes a complete static documentation site and `.github/workflows/pages.yml`. GitHub Actions builds the root README, all Markdown documents in `docs/`, and third-party notices. **No `gh-pages` branch is needed.** The Markdown files remain the source of truth.

## Rename the repository first

The current checkout's origin is `https://github.com/taze292/taze-decompiler.git`. A suggested new name is **`astra-decompiler`**.

1. Open the repository on GitHub, then **Settings → General**.
2. Change **Repository name** to `astra-decompiler`, and select **Rename**.
3. After GitHub completes the rename, update this checkout's remote:

```powershell
git remote set-url origin https://github.com/taze292/astra-decompiler.git
git remote -v
```

Renaming normally redirects repository pages and Git operations, but **GitHub Pages project-site URLs do not redirect**. Rename before publishing the site, or update every published Pages link afterward. Do not create another repository under the old name if you want its repository redirects to keep working. These are GitHub's documented [repository rename rules](https://docs.github.com/en/repositories/creating-and-managing-repositories/renaming-a-repository).

The local checkout folder can keep its existing name; it does not determine the executable, project branding, or Pages URL. Releases and release assets also need checking: a repository rename does not rebuild or rename old downloadable executables.

## Enable Pages

1. When you are ready to publish, push the reviewed commits to the repository's default branch.
2. Open **Settings → Pages** on GitHub.
3. Under **Build and deployment → Source**, select **GitHub Actions**.
4. Open **Actions → Astra documentation → Run workflow**, choose the default branch, and run it. Later changes to the README, `docs/`, or `site/` deploy automatically from that branch.
5. Wait for both the **build** and **deploy** jobs to complete. Open the URL shown by the deployment or by **Settings → Pages**.
6. Under **Settings → Environments → github-pages**, restrict deployment branches to the default branch if that restriction is not already present.

With the suggested name, the expected project-site address is:

```text
https://taze292.github.io/astra-decompiler/
```

The workflow obtains the repository identity from GitHub, so forks and future renames do not require hardcoded base-path changes. It checks local links before uploading the artifact. Pull requests build the site but do not deploy it. No custom token, npm installation, or separate site branch is required.

GitHub documents the setup in [Configuring a publishing source](https://docs.github.com/en/pages/getting-started-with-github-pages/configuring-a-publishing-source-for-your-github-pages-site) and the artifact/permission requirements in [Using custom workflows with GitHub Pages](https://docs.github.com/en/pages/getting-started-with-github-pages/using-custom-workflows-with-github-pages).

## Preview and maintain the site

Use Python 3.11 or newer:

```powershell
python site/build.py --repository taze292/astra-decompiler --ref main
python -m http.server 8000 --directory artifacts/pages
```

Open `http://localhost:8000`. The generated HTML lives in ignored `artifacts/pages/`; commit the Markdown, generator, styles, and workflow rather than generated pages. The generator uses only Python's standard library and checks local links and anchors during every build.

- Update `README.md` for the overview and getting started.
- Update `docs/OPTIONS.md` for the complete configuration reference.
- Add a Markdown file in `docs/` to add a page automatically.
- Edit `site/styles.css` for the small responsive theme.
- Keep source snippets inside fenced code blocks; the renderer escapes them instead of interpreting HTML.

## Troubleshooting

If the first deployment reports that Pages is not configured, select **GitHub Actions** in **Settings → Pages** and rerun the workflow. The workflow does not automatically change repository settings.

If a deployment waits for approval, inspect the `github-pages` environment's protection rules. If the build reports a missing link or anchor, fix the Markdown reference shown by the error and rerun the build. Links to generated pages are relative, so a project subpath should not cause missing CSS or navigation.

If Pages is unavailable, check the repository's plan and visibility against GitHub's [Pages availability](https://docs.github.com/en/pages/getting-started-with-github-pages/configuring-a-publishing-source-for-your-github-pages-site). The live site is public even when the repository's plan permits publishing from a private repository.
