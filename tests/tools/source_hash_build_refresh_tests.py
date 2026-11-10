"""Existing hashed inputs must refresh identity through cmake --build alone."""

import argparse
from pathlib import Path
import subprocess
import tempfile
import time


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cmake', default='cmake')
    parser.add_argument('--generator')
    args = parser.parse_args()
    module = Path(__file__).resolve().parents[2] / 'cmake/render_core_source_hash.cmake'

    def run(command):
        result = subprocess.run(command, capture_output=True, timeout=30)
        if result.returncode:
            output = (result.stdout + result.stderr).decode('utf8', errors='replace')
            raise RuntimeError(
                f'Command failed: {command}\n{output}')

    with tempfile.TemporaryDirectory(prefix='core-source-hash-build-refresh-') as directory:
        root = Path(directory)
        source = root / 'source'
        build = root / 'build'
        inputs = (
            'src/probe.cpp',
            'src/private.h',
            'src/private.hpp',
            'include/render_core/public.h',
            'include/render_core/public.hpp',
            'cmake/render_core_probe.csv',
            'cmake/render_core_probe.json.in',
        )
        for relative in inputs:
            path = source / relative
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text('// original\n', encoding='utf8')

        # A compiler-free project keeps the regression independent of ordinary
        # object/header rebuild dependencies. Only configure inputs can refresh
        # the fingerprint and configure counter.
        (source / 'CMakeLists.txt').write_text(
            'cmake_minimum_required(VERSION 3.16)\nproject(HashRefreshProbe NONE)\n'
            f'include("{module.as_posix()}")\n'
            'jellyframe_compute_render_core_source_hash("${CMAKE_CURRENT_SOURCE_DIR}")\n'
            'file(WRITE "${CMAKE_BINARY_DIR}/hash.txt" "${JELLYFRAME_RENDER_CORE_SOURCE_HASH}")\n'
            'set(configure_count 0)\n'
            'if(EXISTS "${CMAKE_BINARY_DIR}/configure_count.txt")\n'
            '  file(READ "${CMAKE_BINARY_DIR}/configure_count.txt" configure_count)\n'
            'endif()\n'
            'math(EXPR configure_count "${configure_count} + 1")\n'
            'file(WRITE "${CMAKE_BINARY_DIR}/configure_count.txt" "${configure_count}")\n'
            'add_custom_target(hash_probe ALL)\n',
            encoding='utf8')
        configure = [args.cmake, '-S', str(source), '-B', str(build)]
        if args.generator:
            configure.extend(['-G', args.generator])
        run(configure)
        build_command = [args.cmake, '--build', str(build), '--config', 'Debug']

        def snapshot():
            return ((build / 'hash.txt').read_text(encoding='utf8'),
                    int((build / 'configure_count.txt').read_text(encoding='utf8')))

        def unchanged_rebuild(previous):
            run(build_command)
            if snapshot() != previous:
                raise RuntimeError('unchanged build unexpectedly reconfigured or changed identity')

        previous = snapshot()
        unchanged_rebuild(previous)
        for relative in inputs:
            # Also cover filesystems/build tools with one-second timestamps.
            time.sleep(1.1)
            (source / relative).write_text('// modified\n', encoding='utf8')
            run(build_command)
            current = snapshot()
            if current[0] == previous[0]:
                raise RuntimeError(f'build-only {relative} edit did not refresh source identity')
            if current[1] != previous[1] + 1:
                raise RuntimeError(f'{relative} edit did not trigger exactly one reconfigure')
            unchanged_rebuild(current)
            previous = current

    print('existing source/header/metadata edits refresh identity through build alone')


if __name__ == '__main__':
    main()
