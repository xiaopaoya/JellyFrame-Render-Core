#!/usr/bin/env python3
"""Create a deterministic standalone JellyFrame Render Core source archive."""

from __future__ import annotations

import argparse
import gzip
import hashlib
import io
import re
import subprocess
import tarfile
from pathlib import Path


ARCHIVE_PREFIX = "jellyframe-render-core"
VERSION_PATTERN = re.compile(
    r'set\(JELLYFRAME_RENDER_CORE_PACKAGE_VERSION\s+"([^"]+)"', re.MULTILINE
)
TEXT_ARCHIVE_SUFFIXES = frozenset(
    {
        ".bdf",
        ".c",
        ".cc",
        ".cmake",
        ".cpp",
        ".css",
        ".csv",
        ".h",
        ".hpp",
        ".html",
        ".in",
        ".json",
        ".md",
        ".ps1",
        ".py",
        ".txt",
    }
)
TEXT_ARCHIVE_FILENAMES = frozenset({"CMakeLists.txt", "LICENSE"})
ARCHIVE_INPUTS = (
    "LICENSE",
    "CMakeLists.txt",
    "CMakePresets.json",
    "README.md",
    "cmake",
    "include",
    "src",
    "tests",
    "docs",
    "benchmarks",
    "samples",
    "tools",
)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--source-root",
        type=Path,
        default=Path(__file__).resolve().parents[1],
        help="Render Core source checkout",
    )
    parser.add_argument("--output-dir", type=Path, required=True)
    return parser.parse_args()


def require_file(path: Path) -> Path:
    if not path.is_file():
        raise RuntimeError(f"required Render Core source file is missing: {path}")
    return path


def package_version(source_root: Path) -> str:
    version_file = require_file(source_root / "cmake" / "render_core_version.cmake")
    match = VERSION_PATTERN.search(version_file.read_text(encoding="utf-8"))
    if match is None:
        raise RuntimeError(f"could not read Render Core package version from: {version_file}")
    return match.group(1)


def git_tracked_files(source_root: Path) -> set[Path] | None:
    """Return tracked source paths when the archive is made from a checkout.

    A source archive is often made from a release checkout. In that case an
    editor backup or an accidental in-tree build product must not become part
    of the artifact. A standalone archive has no `.git` directory, so it
    remains self-packable and uses its complete source tree instead.
    """
    result = subprocess.run(
        ["git", "-C", str(source_root), "ls-files", "-z", "--",
         *ARCHIVE_INPUTS],
        text=False,
        capture_output=True,
        check=False,
    )
    if result.returncode != 0:
        return None
    return {
        source_root / Path(relative.decode("utf-8"))
        for relative in result.stdout.split(b"\0")
        if relative
    }


def source_files(source_root: Path) -> list[Path]:
    files: set[Path] = set()
    for relative in ARCHIVE_INPUTS:
        path = source_root / relative
        if path.is_file():
            files.add(path)
        elif path.is_dir():
            files.update(candidate for candidate in path.rglob("*") if candidate.is_file())
        else:
            raise RuntimeError(f"required Render Core archive input is missing: {path}")
    tracked_files = git_tracked_files(source_root)
    if tracked_files is not None:
        required_files = {source_root / relative for relative in ARCHIVE_INPUTS
                          if (source_root / relative).is_file()}
        missing_required = required_files - tracked_files
        if missing_required:
            missing = ", ".join(
                path.relative_to(source_root).as_posix() for path in sorted(missing_required)
            )
            raise RuntimeError(f"tracked Render Core checkout is missing required source files: {missing}")
        files.intersection_update(tracked_files)
    return sorted(files, key=lambda path: path.relative_to(source_root).as_posix())


def archive_member(root_name: str, source_root: Path, path: Path) -> str:
    return f"{root_name}/{path.relative_to(source_root).as_posix()}"


def add_file(archive: tarfile.TarFile, name: str, content: bytes) -> None:
    info = tarfile.TarInfo(name)
    info.size = len(content)
    info.mode = 0o644
    info.uid = 0
    info.gid = 0
    info.uname = ""
    info.gname = ""
    info.mtime = 0
    archive.addfile(info, fileobj=io.BytesIO(content))


def archive_content(path: Path) -> bytes:
    """Return the portable archive representation for one source member.

    Archive checksums are release-artifact identities, so they must not depend
    on a contributor's Git line-ending settings. Keep opaque future assets
    byte-for-byte: only declared text sources are normalized.
    """
    content = path.read_bytes()
    if path.name not in TEXT_ARCHIVE_FILENAMES and path.suffix.lower() not in TEXT_ARCHIVE_SUFFIXES:
        return content
    return content.replace(b"\r\n", b"\n").replace(b"\r", b"\n")


def create_archive(source_root: Path, output_dir: Path) -> tuple[Path, Path]:
    source_root = source_root.resolve()
    output_dir = output_dir.resolve()
    version = package_version(source_root)
    root_name = f"{ARCHIVE_PREFIX}-{version}"
    output_dir.mkdir(parents=True, exist_ok=True)
    archive_path = output_dir / f"{root_name}.tar.gz"
    checksum_path = archive_path.with_suffix(archive_path.suffix + ".sha256")
    archive_entries: dict[str, bytes] = {}
    for path in source_files(source_root):
        archive_entries[archive_member(root_name, source_root, path)] = archive_content(path)

    with archive_path.open("wb") as raw_file:
        with gzip.GzipFile(fileobj=raw_file, mode="wb", filename="", mtime=0) as compressed:
            with tarfile.open(fileobj=compressed, mode="w", format=tarfile.GNU_FORMAT) as archive:
                for name in sorted(archive_entries):
                    add_file(archive, name, archive_entries[name])

    digest = hashlib.sha256(archive_path.read_bytes()).hexdigest()
    checksum_path.write_text(
        f"{digest}  {archive_path.name}\n", encoding="ascii", newline="\n"
    )
    return archive_path, checksum_path


def main() -> int:
    args = parse_args()
    archive_path, checksum_path = create_archive(args.source_root, args.output_dir)
    print(archive_path)
    print(checksum_path)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
