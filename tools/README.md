# Tools

`package_render_core_source.py` creates the deterministic Render Core source
archive and its SHA-256 sidecar. The archive contains only tracked repository
inputs, so local build directories and editor artifacts cannot enter a release
candidate.

```powershell
python tools\package_render_core_source.py --output-dir build\source-dist
```
