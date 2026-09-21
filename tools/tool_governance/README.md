# Tool governance

Repository-side static inventory, registry validation and generated navigation.
This tool never imports registered tools, runs their help commands or accesses
boards/scopes. Governance rules and rollout tasks live in
[docs/tools](../../docs/tools/DOCS_TOOLS_ARCHITECTURE.md).

Requires Python 3.10+, Git on PATH and the packages in `requirements.txt`:

```powershell
python -m pip install -r tools/tool_governance/requirements.txt
python tools/tool_governance/tool_governance.py inventory
python tools/tool_governance/tool_governance.py check --check-catalog
python tools/tool_governance/tool_governance.py catalog
```

All commands write stdout only. `inventory` uses Python's standard library;
`check` and `catalog` also validate with jsonschema. Use Python subprocess
capture with UTF-8 to save stdout under a new `out/` run directory on Windows.
`--root` selects another checkout; it must contain a Git repository. Registry,
schema and layout paths are fixed relative to that root.

`tools/tool_registry.json` is the only tool-family registration source. A family
may contain several explicit executable entrypoints and libraries. Root-level
compatibility wrappers belong to their destination family. File ownership is
exact, not a directory glob: new nonignored files under `tools/` are reported
until assigned. Working-tree additions are included; this is not a staged
source fingerprint or a replacement for P3.

`tools/tool_layout.json` is the versioned top-level layout baseline. Classified
roots may accept registered files, while the exact paths under legacy roots are
frozen. The check rejects new root files, new top-level directories and new
files inside legacy roots even when registry ownership was added. Removing a
legacy path and shrinking the baseline remains valid; expanding an exception
requires an explicit, reviewable baseline change.

Lifecycle `UNASSESSED` records existing tools awaiting qualification; it does
not mean deprecated or known broken. Static import candidates, absolute-path
and `out/` hints are review inputs, not proof of executable dependencies or
unsafe operations. Relative/dynamic imports and shell/subprocess calls require
manual review. Empty `depends_on` does not imply independence while
`dependency_review` is PENDING. A SUPPORTED declaration requires explicit
operation, completed dependency review and validation/handoff references;
checking file existence alone cannot establish that those proofs are adequate.

`catalog` prints the generated block for `tools/README.md`. Keep hand-written
operating instructions outside its BEGIN/END markers. `check --check-catalog`
also detects missing, duplicated or stale catalog blocks. Registry/checker changes
need relevant host tests and the repository's existing P3 gate before commit.
This first rollout is an explicit check; pre-commit integration is a later task.

Exit codes: 0 = requested check completed without errors (warnings may remain),
1 = inventory/registry errors, 2 = usage, dependency or input failure. Preserve
JSON error output. No device cleanup is needed because no hardware is opened.
