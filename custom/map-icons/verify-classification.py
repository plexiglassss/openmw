#!/usr/bin/env python3
"""Check the compiled native classifier, catalogue, original glyphs and resources.

Uses Python's standard library only. Build test-classification.cpp together with
components/mapmarkers/classification.cpp using C++20 before running this script.
"""
from __future__ import annotations

import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import platform
import re
import struct
import subprocess
import tempfile
import zlib

ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent
REFERENCE_SHA256 = '0de7fe3dcf226fb9bcf7e2a30772fb0eef1c9b9f79e8ba9c49aeae7496b1ac96'


def sha256(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def require(value, message):
    if not value:
        raise ValueError(message)


def generator():
    spec = importlib.util.spec_from_file_location('generate_known', HERE / 'generate-known.py')
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def png_metadata(path):
    data = path.read_bytes()
    require(data[:8] == b'\x89PNG\r\n\x1a\n', f'{path}: invalid PNG signature')
    offset = 8
    types = []
    compressed = []
    header = None
    while offset < len(data):
        require(offset + 12 <= len(data), f'{path}: truncated chunk')
        size = struct.unpack_from('>I', data, offset)[0]
        kind = data[offset + 4:offset + 8]
        content = data[offset + 8:offset + 8 + size]
        end = offset + 12 + size
        require(end <= len(data), f'{path}: truncated chunk payload')
        expected_crc = struct.unpack_from('>I', data, offset + 8 + size)[0]
        require(zlib.crc32(kind + content) & 0xffffffff == expected_crc, f'{path}: CRC mismatch')
        if kind == b'IHDR':
            require(header is None and not types and size == 13, f'{path}: invalid IHDR')
            header = struct.unpack('>IIBBBBB', content)
        elif kind == b'IDAT':
            compressed.append(content)
        elif kind == b'IEND':
            require(size == 0 and end == len(data), f'{path}: invalid IEND')
        types.append(kind)
        offset = end
    require(header == (64, 64, 8, 6, 0, 0, 0), f'{path}: expected 64x64, 8-bit noninterlaced RGBA')
    require(types[-1] == b'IEND' and compressed, f'{path}: missing pixels or IEND')
    raw = zlib.decompress(b''.join(compressed))
    require(len(raw) == 64 * (1 + 64 * 4), f'{path}: invalid decoded scanline size')
    require(all(raw[row * 257] <= 4 for row in range(64)), f'{path}: invalid row filter')
    return {'bytes': len(data), 'sha256': sha256(path), 'width': 64, 'height': 64,
            'rgba_base_level_bytes': 64 * 64 * 4, 'chunk_crc_checks': len(types)}


def check_assets(resources=None):
    manifest = json.loads((HERE / 'assets.json').read_text(encoding='utf-8'))
    expected_ids = {category.lower() for category in generator().CATEGORIES}
    require({row['category'] for row in manifest['files']} == expected_ids, 'Incomplete icon category set')
    require(len(manifest['files']) == 19, 'Unexpected icon asset count')
    cmake = (ROOT / 'files/data/CMakeLists.txt').read_text(encoding='utf-8')
    definitions = (ROOT / 'components/mapmarkers/classification.cpp').read_text(encoding='utf-8')
    result = {}
    for row in manifest['files']:
        relative = row['path']
        require(relative == f'textures/mapmarkers/{row["category"]}.png', 'Unexpected resource path')
        require(cmake.count('    ' + relative + '\n') == 1, f'{relative}: absent or duplicate CMake entry')
        require('"' + relative + '"' in definitions, f'{relative}: absent category definition')
        path = ROOT / 'files/data' / relative
        metadata = png_metadata(path)
        require(metadata['sha256'] == row['sha256'] and metadata['bytes'] == row['bytes'],
                f'{relative}: differs from original glyph')
        if resources is not None:
            require(sha256(resources / relative) == row['sha256'], f'{relative}: packaged resource mismatch')
        result[relative] = metadata
    return result


def check_cmake_copy():
    # Execute the repository's real resource-copy macro and actual data list;
    # the standalone configure requires none of OpenMW's engine dependencies.
    with tempfile.TemporaryDirectory(prefix='openmw-map-icons-') as temporary:
        base = Path(temporary)
        source, build = base / 'source', base / 'build'
        source.mkdir()
        quote = lambda path: '"' + path.as_posix().replace('"', '\\"') + '"'
        (source / 'CMakeLists.txt').write_text(
            'cmake_minimum_required(VERSION 3.16)\nproject(MapIconResourceCheck NONE)\n'
            + f'include({quote(ROOT / "cmake/OpenMWMacros.cmake")})\n'
            + 'set(OPENMW_RESOURCES_ROOT "${CMAKE_BINARY_DIR}/output")\n'
            + f'add_subdirectory({quote(ROOT / "files/data")} builtin)\n', encoding='utf-8')
        command = ['cmake', '-S', str(source), '-B', str(build)]
        process = subprocess.run(command, capture_output=True, text=True)
        require(process.returncode == 0, 'CMake copy validation failed:\n' + process.stdout + process.stderr)
        candidates = list((build / 'output').glob('resources/vfs'))
        candidates.extend((build / 'output').glob('*/resources/vfs'))
        require(candidates, 'CMake did not create a resource VFS')
        for resources in candidates:
            check_assets(resources)
        return {'passed': True, 'configurations_checked': len(candidates), 'glyphs_per_configuration': 19}


def check_reference(path, facts):
    require(sha256(path) == REFERENCE_SHA256, 'Supplied reference source differs from inspected source')
    text = path.read_text(encoding='utf-8')
    block = text.split('allExtCells.extCells = {', 1)[1].split('\n}', 1)[0]
    rows = re.findall(r'\["([^"]*)"\]\s*=\s*"([^"]*)"', block)
    primary = {name: category.lower() for name, category in rows}
    require(len(rows) == 1522 and len(primary) == 1517, 'Reference primary table changed')
    for name, category in primary.items():
        require(facts.get(name) == category, f'Reference primary mismatch: {name}')
    return {'source_sha256': REFERENCE_SHA256, 'primary_assignments': len(rows),
            'primary_names_verified': len(primary)}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', required=True, type=Path, help='compiled standalone C++ test executable')
    parser.add_argument('--receipt', type=Path, help='write full verification JSON')
    parser.add_argument('--iterations', type=int, default=100000)
    parser.add_argument('--resources', type=Path, help='verify installed resources/vfs directory')
    parser.add_argument('--cmake-copy', action='store_true', help='run the actual CMake asset-copy path in isolation')
    parser.add_argument('--reference', type=Path, help='optional original supplied allExtCells.lua for provenance check')
    args = parser.parse_args()
    require(args.iterations > 0, 'Iteration count must be positive')
    module = generator()
    facts = module.read_facts()
    require(len(facts) == 2690, 'Unexpected catalogue size')
    require(module.OUTPUT.read_text(encoding='utf-8') == module.generate(), 'Native catalogue requires regeneration')
    with tempfile.TemporaryDirectory(prefix='openmw-map-facts-') as temporary:
        crlf = Path(temporary) / 'known-locations.tsv'
        crlf.write_bytes(module.FACTS.read_text(encoding='utf-8').replace('\n', '\r\n').encode('utf-8'))
        require(module.generate(crlf) == module.generate(), 'Catalogue generation depends on checkout line endings')
    assets = check_assets(args.resources)
    command = [str(args.binary.resolve()), str(module.FACTS), str(args.iterations)]
    result = subprocess.run(command, capture_output=True, text=True, cwd=ROOT)
    require(result.returncode == 0, 'Native classifier test failed:\n' + result.stdout + result.stderr)
    native = json.loads(result.stdout)
    require(native['passed'] and native['catalogue_context_checks'] == 5380
            and native['routine_heap_allocations'] == 0, 'Incomplete native test receipt')
    spatial = native['spatial_index']
    require(spatial['markers'] == 100000 and spatial['small_view_x_buckets'] == 5
            and spatial['small_view_y_buckets'] == 15 and spatial['small_view_keys'] == 15
            and spatial['query_heap_allocations'] == 0, 'Incomplete selective-query test receipt')
    inputs = [ROOT / 'components/mapmarkers/classification.hpp', ROOT / 'components/mapmarkers/classification.cpp',
              ROOT / 'components/mapmarkers/spatialindex.hpp',
              module.OUTPUT, module.FACTS, HERE / 'generate-known.py', HERE / 'assets.json',
              HERE / 'test-classification.cpp', Path(__file__),
              ROOT / 'apps/components_tests/mapmarkers/cases.hpp',
              ROOT / 'apps/components_tests/mapmarkers/testclassification.cpp',
              ROOT / 'apps/components_tests/mapmarkers/testspatialindex.cpp']
    receipt = {
        'passed': True,
        'scope': 'Native pure classification, spatial queries, factual catalogue and resource packaging; no live-game or FPS claim',
        'platform': platform.platform(),
        'native_executable_sha256': sha256(args.binary),
        'native': native,
        'catalogue': {'names': len(facts), 'reproducible': True, 'crlf_checkout_verified': True,
                      'string_pool_bytes_including_final_null': sum(len(name) + 1 for name in facts) + 1,
                      'index_bytes': 8 * len(facts)},
        'assets': assets,
        'asset_totals': {'files': len(assets), 'png_bytes': sum(row['bytes'] for row in assets.values()),
                         'rgba_base_level_bytes': sum(row['rgba_base_level_bytes'] for row in assets.values()),
                         'installed_resources_verified': args.resources is not None},
        'inputs': {str(path.relative_to(ROOT)): sha256(path) for path in inputs},
    }
    if args.reference:
        receipt['reference'] = check_reference(args.reference, facts)
    if args.cmake_copy:
        receipt['cmake_resource_copy'] = check_cmake_copy()
    if args.receipt:
        args.receipt.parent.mkdir(parents=True, exist_ok=True)
        args.receipt.write_text(json.dumps(receipt, indent=2) + '\n', encoding='utf-8')
    print(json.dumps({'passed': True, 'native': native, 'asset_totals': receipt['asset_totals'],
                      'cmake_resource_copy': receipt.get('cmake_resource_copy'),
                      'receipt': str(args.receipt) if args.receipt else None}, indent=2))


if __name__ == '__main__':
    main()
