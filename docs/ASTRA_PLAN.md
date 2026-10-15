# Astra update plan

1. Inspect the supplied Luau style reference and current Volt API documentation. Preserve the silent bridge, bounded requests, snapshots, reconnection, and hook restoration.
2. Expose a vertical `getgenv().AstraOptions` table using readable PascalCase fields. Accept those same fields in the backend, normalize aliases before duplicate/reserved-key validation, and retain existing CLI spellings.
3. Change capture comments to valid Luau line comments: `-- Upvalues`, `-- [1] Name (copy)`, and `-- [2] Name (ref)`. Keep independent indexes and optional compact comments.
4. Compact proven array prefixes without reordering fields, hiding gaps, changing duplicate-key behavior, or expanding final call results. Keep the option enabled by default.
5. Add explicit tab indentation, group-blank-line, and table-brace spacing controls. Format code lexically so strings and comments keep their contents.
6. Rebrand tracked project code, API namespace, build targets, tools, protocol, and documentation as Astra. Update the generated header to `Decompiled with Astra | Bytecode Version: #`. Leave the current workspace location and the user's GitHub account identity alone.
7. Generate a small static documentation site and a GitHub Pages deployment workflow. Include all repository documentation, correct project-subpath links, and a repository rename/setup guide. No publishing or remote changes are performed.
8. Build and run the regression suites, bridge mocks, site checks, and live MCP compilation checks. Commit complete local milestones and record actual results and limitations; do not push.

The implementation is split between the bridge, output reconstruction/formatting, and documentation site. The primary integration owns the shared option schema, protocol validation, complete rename, tests, and live verification. Mechanical renaming happens after parallel file edits are complete.
