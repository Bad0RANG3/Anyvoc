import hashlib
import json
from pathlib import Path
import subprocess

repo = Path(__file__).resolve().parents[2]
exe = repo / 'build/AnyvocRealtimeProbe_artefacts/Release/AnyvocRealtimeProbe.exe'
out = repo / 'build/realtime-probe-0.20.2/guard-checks'
out.mkdir(exist_ok=False)
base = out / 'simultaneous'
cmd = [str(exe), '--case', 'baseline', '--seconds', '1', '--warmup', '.2',
       '--sample-rate', '0', '--buffer-size', '0', '--output', str(base)]
results = []
processes = [subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE) for _ in range(2)]
for i, process in enumerate(processes):
    stdout, stderr = process.communicate(timeout=20)
    (out / f'concurrent-{i}.stdout.txt').write_bytes(stdout)
    (out / f'concurrent-{i}.stderr.txt').write_bytes(stderr)
    results.append({'exit_code': process.returncode, 'stderr': stderr.decode('utf-8', errors='replace')})
assert sorted(r['exit_code'] for r in results) == [0, 1]
assert 'namespace is occupied' in next(r['stderr'] for r in results if r['exit_code'] == 1)
assert json.loads(base.with_suffix('.json').read_text())['status'] == 'valid'

def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

before = {suffix: sha(base.with_suffix(suffix)) for suffix in ('.json', '.csv')}
retry = subprocess.run(cmd, capture_output=True, timeout=10)
(out / 'retry.stdout.txt').write_bytes(retry.stdout)
(out / 'retry.stderr.txt').write_bytes(retry.stderr)
assert retry.returncode == 1 and b'Output already exists' in retry.stderr
assert before == {suffix: sha(base.with_suffix(suffix)) for suffix in before}

stale = out / 'stale'
lock = Path(str(stale) + '.capture-lock')
lock.mkdir()
try:
    refused = subprocess.run(cmd[:-1] + [str(stale)], capture_output=True, timeout=10)
    (out / 'stale.stderr.txt').write_bytes(refused.stderr)
    assert refused.returncode == 1 and b'namespace is occupied' in refused.stderr
    assert not stale.with_suffix('.json').exists() and not stale.with_suffix('.csv').exists()
finally:
    lock.rmdir()

invalid = out / 'invalid-cli'
cli = subprocess.run([str(exe), '--seconds', 'nan', '--output', str(invalid)], capture_output=True, timeout=10)
(out / 'invalid-cli.stdout.txt').write_bytes(cli.stdout)
(out / 'invalid-cli.stderr.txt').write_bytes(cli.stderr)
assert cli.returncode == 2
assert not invalid.with_suffix('.json').exists() and not invalid.with_suffix('.csv').exists()
proof = {'status': 'PASS', 'exe_sha256': sha(exe), 'concurrent_results': results,
         'completed_files_unchanged': before, 'checks': ['one winner, one atomic-lock rejection',
         'winner report valid', 'completed capture refuses overwrite with byte-identical files',
         'stale lock refuses without writing', 'invalid CLI exits before creating artifacts'],
         'scope': 'Tool guards plus one 1-second silent device capture, excluded from formal performance data'}
(out / 'guard-verification.json').write_text(json.dumps(proof, indent=2) + '\n', encoding='utf-8')
print(json.dumps(proof, indent=2))
