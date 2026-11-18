import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import xml.etree.ElementTree as ET

p = argparse.ArgumentParser()
p.add_argument('--snapshot', type=Path, required=True)
p.add_argument('--capture', type=Path, required=True)
p.add_argument('--output', type=Path, required=True)
a = p.parse_args()
sha = lambda path: hashlib.sha256(path.read_bytes()).hexdigest()
base = a.snapshot.resolve()
m = json.loads((base / 'manifest.json').read_text())
assert m['complete'] and len({e['path'] for e in m['files']}) == len(m['files'])
for entry in m['files']:
    assert sha(base / 'source' / entry['path']) == entry['final_sha256'], entry['path']
marker = m['diagnostic_marker']
assert sha(base / 'source' / marker['path']) == marker['sha256']
ns = {'m': 'http://schemas.microsoft.com/developer/msbuild/2003'}
flags = {}
for name in ['Anyvoc', 'AnyvocMidiAllocationProbe']:
    tree = ET.parse(base / 'build' / f'{name}.vcxproj')
    groups = [g for g in tree.findall('m:ItemDefinitionGroup', ns) if 'Release|x64' in g.get('Condition', '')]
    assert len(groups) == 1
    cc = groups[0].find('m:ClCompile', ns)
    runtime = cc.find('m:RuntimeLibrary', ns).text
    forced = cc.find('m:ForcedIncludeFiles', ns).text
    definitions = cc.find('m:PreprocessorDefinitions', ns).text.split(';')
    assert runtime == 'MultiThreaded'
    assert Path(forced).resolve() == (base / 'source/Tests/MidiAllocationRecorder.h').resolve()
    assert 'ANYVOC_MIDI_ALLOCATION_DIAGNOSTIC=1' in definitions and 'NDEBUG=1' in definitions and 'DEBUG=1' not in definitions
    flags[name] = {'runtime': runtime, 'forced_include': forced, 'definitions': definitions}
cache = (base / 'build/CMakeCache.txt').read_text()
for expected in ['ANYVOC_BUILD_MIDI_ALLOCATION_PROBE:BOOL=ON', 'ANYVOC_BUILD_TESTS:BOOL=OFF', 'ANYVOC_BUILD_REALTIME_PROBE:BOOL=OFF']:
    assert expected in cache
spec = importlib.util.spec_from_file_location('comparison', Path(__file__).resolve().parents[2] / 'scripts/compare-midi-allocation-probe.py')
comparison = importlib.util.module_from_spec(spec)
spec.loader.exec_module(comparison)
_, fixtures, cases, audio = comparison.load_capture(a.capture)
exe = base / 'build/AnyvocMidiAllocationProbe_artefacts/Release/AnyvocMidiAllocationProbe.exe'
result = {'schema': 1, 'passed': True, 'snapshot_file_count': len(m['files']),
          'manifest_sha256': sha(base / 'manifest.json'), 'marker_sha256': marker['sha256'],
          'compiler_flags': flags, 'exe': {'path': str(exe), 'bytes': exe.stat().st_size, 'sha256': sha(exe)},
          'source_files': [{k: v for k, v in e.items() if k in ('path', 'final_sha256')}
                           for e in m['files'] if e['path'].startswith('Source/')],
          'capture': {'path': str(a.capture.resolve()), 'case_count': len(cases), 'audio_file_count': len(audio),
                      'files': [{'name': f.name, 'bytes': f.stat().st_size, 'sha256': sha(f)} for f in sorted(a.capture.iterdir())]},
          'cases': [{'id': cid, 'calls': case['allocation_count'], 'requested_bytes': case['requested_bytes'],
                     'by_site_owner': case['by_site_owner']} for cid, case in cases.items()],
          'helper_sha256': sha(Path(__file__))}
with a.output.open('x', encoding='utf-8') as f:
    json.dump(result, f, indent=2)
    f.write('\n')
print('PASS: all snapshot hashes, marker, Release/MT macro/forced include, EXE and full capture:', result['exe']['sha256'])
