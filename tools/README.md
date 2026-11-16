# Tools

## Shaping normalization evidence

The private shaping normalizer uses Unicode 14.0.0 data pinned by SHA-256 in
`tests/fixtures/shaping_normalization/manifest.json`. With Python 3.11 carrying
Unicode 14.0.0 `unicodedata`, FontTools installed, the official
`DerivedCoreProperties-14.0.0.txt` and `NormalizationTest-14.0.0.txt` files in
that fixture directory, and the exact source faces available locally, run:

```powershell
python tools\unicode14_normalization_evidence.py --font-dir <NotoSansSC-2.002-directory>
```

The generator refuses changed Unicode data, Python Unicode versions or source
font bytes. Synthetic catalog data exists only to exercise the Unicode
algorithm oracle; it is not evidence that the product font contains those
glyphs. Per-face eligibility is derived from the pinned source cmap.

`package_render_core_source.py` creates the deterministic Render Core source
archive and its SHA-256 sidecar. The archive contains only tracked repository
inputs, so local build directories and editor artifacts cannot enter a release
candidate.

```powershell
python tools\package_render_core_source.py --output-dir build\source-dist
```

## Release Signing

Release signing is local-only. `initialize_release_signing.ps1` creates a
passphrase-protected Ed25519 signing key below
`%LOCALAPPDATA%\JellyFrame\release-signing\gnupg`, exports only its public
key, and writes repository-local Git configuration. It never writes a private
key or passphrase into the checkout.

```powershell
.\tools\initialize_release_signing.ps1 -Identity "Maintainer Name <verified-email@example.com>"
```

Upload the emitted armored public key to GitHub's **SSH and GPG keys** settings.
Keep the private key, passphrase and revocation certificate offline and out of
the repository.

After the release candidate is reviewed and the checkout is clean, prepare a
signed annotated tag plus deterministic archive locally:

```powershell
.\tools\prepare_signed_release.ps1 -Version 0.6.0
```

This command does not push by default. Review the tag and archive checksum,
then use `-PushTag` only when the release is approved.
