"""Check that modifying a private implementation header changes package identity."""
from pathlib import Path
import subprocess
import tempfile

module = Path(__file__).resolve().parents[2] / 'cmake/render_core_source_hash.cmake'
with tempfile.TemporaryDirectory(prefix='core-private-header-hash-') as directory:
    root = Path(directory)
    source = root / 'source'
    for folder in ('src', 'include/render_core', 'cmake'):
        (source / folder).mkdir(parents=True, exist_ok=True)
    (source / 'src/probe.cpp').write_text('// implementation\n', encoding='utf8')
    header = source / 'src/private.h'
    header.write_text('// original\n', encoding='utf8')
    (source / 'CMakeLists.txt').write_text(
        'cmake_minimum_required(VERSION 3.16)\nproject(HashProbe NONE)\n'
        f'include("{module.as_posix()}")\n'
        'jellyframe_compute_render_core_source_hash("${CMAKE_CURRENT_SOURCE_DIR}")\n'
        'file(WRITE "${CMAKE_BINARY_DIR}/hash.txt" "${JELLYFRAME_RENDER_CORE_SOURCE_HASH}")\n',
        encoding='utf8')
    build = root / 'build'

    def fingerprint():
        subprocess.run(['cmake', '-S', str(source), '-B', str(build)], check=True, capture_output=True)
        return (build / 'hash.txt').read_text(encoding='utf8')

    before = fingerprint()
    header.write_text('// modified\n', encoding='utf8')
    after = fingerprint()
    if before == after:
        raise RuntimeError('private .h change did not alter source identity')
    header.rename(header.with_suffix('.hpp'))
    renamed = fingerprint()
    if renamed == after:
        raise RuntimeError('private .hpp path was not included in source identity')
    header = header.with_suffix('.hpp')
    header.write_text('// modified again\n', encoding='utf8')
    if fingerprint() == renamed:
        raise RuntimeError('private .hpp change did not alter source identity')
    print('private implementation header source identity tests passed')
