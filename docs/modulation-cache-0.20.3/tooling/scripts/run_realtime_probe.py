"""Run three rotated rounds of silent real-device captures; preserve every result."""
import argparse
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import subprocess
import time


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--exe', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--sample-rate', type=int, required=True)
    parser.add_argument('--buffer-size', type=int, required=True)
    parser.add_argument('--seconds', type=int, default=60)
    parser.add_argument('--warmup', type=int, default=3)
    parser.add_argument('--rounds', type=int, default=3)
    args = parser.parse_args()
    if not 1 <= args.seconds <= 3600 or not 0 <= args.warmup <= 60 or not 1 <= args.rounds <= 20:
        parser.error('seconds must be 1..3600, warmup 0..60, rounds 1..20')
    if not 8000 <= args.sample_rate <= 192000 or not 16 <= args.buffer_size <= 8192:
        parser.error('use an explicit supported sample rate and buffer size from --list')
    args.exe = args.exe.resolve(strict=True)
    args.output = args.output.resolve()
    args.output.mkdir(parents=True, exist_ok=True)
    manifest_path = args.output / 'suite-manifest.json'
    # Atomic reservation: two runners must not share an output namespace.
    with manifest_path.open('x', encoding='utf-8') as stream:
        stream.write('{"complete": false, "initialising": true}\n')
    repo = Path(__file__).resolve().parents[1]
    source_paths = [Path('Tests/RealtimeProbe.cpp'), Path('CMakeLists.txt')]
    source_paths.extend(sorted(p.relative_to(repo) for p in (repo / 'Source').glob('*.cpp')))
    source_paths.extend(sorted(p.relative_to(repo) for p in (repo / 'Source').glob('*.h')))
    cases = ['baseline', 'unison', 'matrix', 'chaos', 'chaos-rossler', 'granular']
    manifest = {
        'started_utc': datetime.now(timezone.utc).isoformat(),
        'source_base_commit': subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=repo, text=True).strip(),
        'juce_commit': subprocess.check_output(['git', '-C', 'third_party/JUCE', 'rev-parse', 'HEAD'], cwd=repo, text=True).strip(),
        'exe_sha256': digest(args.exe),
        'source_sha256': {p.as_posix(): digest(repo / p) for p in source_paths},
        'driver_sha256': digest(Path(__file__)),
        'requested': {k: v for k, v in vars(args).items() if k not in ('exe', 'output')},
        'cases': cases, 'runs': [], 'complete': False,
    }

    def save():
        pending = manifest_path.with_suffix('.pending')
        pending.write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
        pending.replace(manifest_path)

    save()
    for round_index in range(args.rounds):
        for case_index in range(len(cases)):
            case = cases[(case_index + round_index) % len(cases)]
            stem = args.output / f'round-{round_index + 1}-{case}'
            cmd = [str(args.exe), '--case', case, '--seconds', str(args.seconds),
                   '--warmup', str(args.warmup), '--sample-rate', str(args.sample_rate),
                   '--buffer-size', str(args.buffer_size), '--output', str(stem)]
            print(f'START round={round_index + 1} case={case} utc={datetime.now(timezone.utc).isoformat()}', flush=True)
            start = time.monotonic()
            item = {'round': round_index + 1, 'case': case, 'command': cmd,
                    'status': 'starting', 'exit_code': None,
                    'started_utc': datetime.now(timezone.utc).isoformat()}
            manifest['runs'].append(item)
            save()
            report = {}
            try:
                item['exe_sha256_before'] = digest(args.exe)
                if item['exe_sha256_before'] != manifest['exe_sha256']:
                    raise RuntimeError('Probe executable changed before capture')
                with stem.with_suffix('.stdout.txt').open('xb') as out, stem.with_suffix('.stderr.txt').open('xb') as err:
                    process = subprocess.run(cmd, cwd=repo, stdout=out, stderr=err,
                                             timeout=(args.seconds + args.warmup) * 4 + 60)
                item['exit_code'] = process.returncode
                report_path = stem.with_suffix('.json')
                if report_path.exists():
                    report = json.loads(report_path.read_text(encoding='utf-8-sig'))
                if process.returncode != 0 or report.get('status') != 'valid':
                    raise RuntimeError('Capture invalid; preserve this suite and investigate')
                item['status'] = 'valid'
            except Exception as error:
                item['status'] = 'invalid'
                item['failure'] = {'type': type(error).__name__, 'message': str(error)}
            finally:
                item['wall_seconds'] = time.monotonic() - start
                try:
                    item['exe_sha256_after'] = digest(args.exe)
                    if item['exe_sha256_after'] != manifest['exe_sha256']:
                        raise RuntimeError('Probe executable changed during capture')
                except Exception as error:
                    item['status'] = 'invalid'
                    item['binary_identity_failure'] = str(error)
                item['artifacts'] = {
                    p.name: {'bytes': p.stat().st_size, 'sha256': digest(p)}
                    for suffix in ('.json', '.csv', '.stdout.txt', '.stderr.txt')
                    if (p := stem.with_suffix(suffix)).is_file()
                }
                save()
            print('END ' + json.dumps(item), flush=True)
            print('SUMMARY ' + json.dumps(report.get('summary', {})), flush=True)
            if item['status'] != 'valid':
                raise RuntimeError('Capture invalid; see preserved suite manifest and logs')
    manifest['complete'] = True
    manifest['finished_utc'] = datetime.now(timezone.utc).isoformat()
    save()
    print('SUITE COMPLETE', flush=True)


if __name__ == '__main__':
    main()
