# HTML dimension ABI 3 replay

This is a computed-value diagnostic against the independently authored frozen
2026-10-16 corpus, not a new browser collection or device acceptance.
`tests/fixtures/html_dimensions/frozen-cases.json` is a byte copy of the original
case file; its SHA-256 is recorded in both reports.

`before.json` records candidate 64f0cf94: 173/183 scalar-projected checks match.
Ten failures expose tiny/large ratio handling, CSS reset keywords and inline
`!important`. `after.json` records the fixes by source and built-library hashes:
183/183 scalar-projected checks match. The report's coreHead identifies the
parent commit used while developing; source/library hashes identify tested bytes.

The scalar projection is explicit: ABI 3 rounds fractional pixel and percentage
dimensions into existing integer fields. Eighteen values differ from the frozen
fractional expectations and remain listed, even though their scalar projections
match. For example, 37.5% becomes 38%, which can produce a visible size difference
on large containers. This does not qualify Q1000 fractional layout or complete
experimental feature parity. ABI 3 also cannot retain the distinction between
automatic and author-specified aspect ratios; that flag is explicitly untested.
The baseline corpus contains no inherited dimensions, so inheritance is outside
this replay's acceptance.

The fixes reduce exact decimal coefficients before bounded integer ratio storage,
avoid string construction/reparsing for HTML ratios, clear width/height hints on
`initial`/`unset`, clear ratios on `auto`/`initial`/`unset`, and handle the same
terminal `!important` spelling already recognized by the stylesheet parser in
inline declarations. No new public Style fields or ABI changes are introduced.
This does not extend the inline parser to full token grammar or CSS inheritance.

Core Release full and minimal (Canvas/modern-paint/Flex-Grid/advanced-forms off)
builds and all three CTest targets pass. Resource costs and MCU behavior have
not been measured for this port. Runtime source-override regression uses a clean
6ad38f4f worktree; the separately known external-trial path failure is excluded
from the repeated run and remains unresolved.

Reproduce on Windows with Clang and a matching already-built Core library:

```powershell
python tests/tools/replay_html_dimension_oracle.py --cases tests/fixtures/html_dimensions/frozen-cases.json --build-dir E:/Builds/html-dimension-hints-mainline --output E:/Builds/html-dimension-hints-mainline/oracle/new-report.json
```

The script returns nonzero for scalar-projected differences. The original frozen
browser observations and disagreements remain in the experiment checkout and
are not overwritten by this driver.
