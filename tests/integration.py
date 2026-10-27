import csv
import io
import json
import math
import os
import subprocess
import sys

exe = sys.argv[1]
count = 0

def run(*args, ok=True):
    global count
    p = subprocess.run([exe, *args], text=True, capture_output=True, timeout=90)
    count += 1
    assert (p.returncode == 0) == ok, (args, p.returncode, p.stderr)
    if ok:
        assert not p.stderr, p.stderr
    else:
        assert p.stderr
    return p.stdout

base = ['--size', '4K', '--warmup', '0', '--repeat', '2', '--iterations', '3']
data = json.loads(run('--test', 'all', '--threads', '3', '--json', *base))
assert len(data['results']) == 6
for result in data['results']:
    assert result['verified']
    for rnd in result['rounds']:
        total = rnd['total']
        workers = rnd['workers']
        assert len(workers) == 3
        assert total['logical_bytes'] == sum(w['logical_bytes'] for w in workers)
        assert total['accesses'] == 3 * 512 * 3
        assert total['iterations'] == 9
        assert total['gbps'] > 0 and math.isfinite(total['gbps'])
        assert all(w['seconds'] > 0 and w['iterations'] == 3 for w in workers)
        assert (total['ns_access'] is not None) == (result['test'] == 'latency')

rows = list(csv.DictReader(io.StringIO(run('--test', 'all', '--csv', *base))))
assert len(rows) == 24
assert all(None not in row and None not in row.values() for row in rows)
assert {row['verified'] for row in rows} == {'true'}

for test in ['random-read', 'random-write', 'latency']:
    args = ['--test', test, '--accesses', '777', '--stride', '64', '--json', *base]
    one = json.loads(run(*args))['results'][0]
    two = json.loads(run(*args))['results'][0]
    assert one['sequence_hash'] == two['sequence_hash']
    assert one['rounds'][0]['total']['checksum'] == two['rounds'][0]['total']['checksum']
    assert one['rounds'][0]['total']['accesses'] == 777 * 3

for ratio in ['0', '30', '50', '70', '100']:
    run('--test', 'mix', '--read-ratio', ratio, *base)

result = json.loads(run('--min-size', '4K', '--max-size', '16K', '--scale', '2',
                        '--threads', '3', '--thread-sweep', '--json', *base))['results']
assert len(result) == 9
assert {r['size_per_thread'] for r in result} == {4096, 8192, 16384}
assert all(r['scaling_efficiency'] > 0 for r in result)
linear = json.loads(run('--min-size', '4K', '--max-size', '10K', '--step', '4K', '--json', *base))
assert [r['size_per_thread'] for r in linear['results']] == [4096, 8192, 10240]
run('--test', 'all', '--size', '8', '--stride', '8', '--warmup', '0', '--repeat', '1', '--iterations', '1')
run('--test', 'all', '--size', '4104', '--stride', '24', '--accesses', '1003', '--warmup', '0', '--repeat', '2', '--iterations', '3', '--threads', '4')
run('--test', 'all', '--size', '64M', '--threads', '2', '--warmup', '0', '--repeat', '1', '--iterations', '1')
timed = json.loads(run('--duration', '.01', '--warmup', '.001', '--repeat', '2', '--size', '16K', '--json'))
assert all(r['total']['seconds'] >= .01 for r in timed['results'][0]['rounds'])
for args in [ ['--unknown'], ['--threads', '0'], ['--threads', '-1'], ['--threads', '257'],
              ['--size', '0'], ['--size', '7'], ['--size', '999999999999999999999G'],
              ['--stride', '3'], ['--size', '8', '--stride', '16'], ['--duration', 'nan'],
              ['--duration', '0'], ['--warmup', '-1'], ['--repeat', '101'], ['--scale', '1'],
              ['--min-size', '8K', '--max-size', '4K'], ['--iterations', '0'],
              ['--read-ratio', '101'], ['--seed', '-1'], ['--accesses', '0'],
              ['--json', '--csv'], ['--cpu-affinity', '0,'], ['--size'],
              ['--size', '1G', '--memory-limit', '1M'],
              ['--iterations', '18446744073709551615'], ['--cpu-affinity', '999999'] ]:
    run(*args, ok=False)
run('--score', '--duration', '.01', ok=False)
run('--score', '--test', 'all', ok=False)
run('--score', '--threads', '2', ok=False)
run('--score', '--cpu-affinity', '0,1', ok=False)
run('--score', '--memory-limit', '255M', ok=False)
run('--score-quick', '--duration', '.01', ok=False)
run('--score', '--score-quick', ok=False)
if hasattr(os, 'sched_getaffinity'):
    cpu = min(os.sched_getaffinity(0))
    run('--cpu-affinity', str(cpu), *base)
partial = json.loads(run('--min-size', '4K', '--max-size', '16K', '--memory-limit', '8K', '--json', *base, ok=False))
assert [r['size_per_thread'] for r in partial['results']] == [4096, 8192]
if sys.platform.startswith('linux'):
    import resource
    # Sanitizer virtual address reservations are incompatible with RLIMIT_AS.
    sanitizer = subprocess.run(['ldd', exe], capture_output=True, text=True).stdout
    # Clang may link sanitizer runtimes statically; inspect symbols as well.
    symbols = subprocess.run(['nm', exe], capture_output=True, text=True).stdout
    if not any(s in sanitizer + symbols for s in ['asan', 'tsan', 'ubsan']):
        def limit_address_space():
            resource.setrlimit(resource.RLIMIT_AS, (128 * 1024 * 1024,) * 2)
        p = subprocess.run([exe, '--size', '256M', '--memory-limit', '512M',
                            '--iterations', '1', '--warmup', '0'],
                           preexec_fn=limit_address_space, capture_output=True, text=True, timeout=30)
        assert p.returncode == 1 and 'Benchmark failed' in p.stderr, p
        count += 1
run('--help')
run('--version')
print(f'integration tests passed ({count} processes)')
