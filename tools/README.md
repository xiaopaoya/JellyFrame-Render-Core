# Tools

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
