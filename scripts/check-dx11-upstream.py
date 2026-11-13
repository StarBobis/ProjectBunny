"""Fetch upstream and report every commit after the recorded import baseline.

This checker never modifies production code or advances synchronization status.
Run from any directory with Python 3 and Git installed.
"""
import json
import pathlib
import subprocess


ROOT = pathlib.Path(__file__).resolve().parents[1]
CHECKOUT = ROOT / 'build/upstream/XXMI-Libs-Package'


def git(*args):
    return subprocess.check_output(
        ['git', '-C', str(CHECKOUT), *args], text=True, encoding='utf-8'
    ).strip()


def main():
    ledger = json.loads((ROOT / 'docs/dx11-upstream-sync.json').read_text(encoding='utf-8'))
    CHECKOUT.parent.mkdir(parents=True, exist_ok=True)
    if not CHECKOUT.exists():
        subprocess.run(['git', 'clone', '--no-checkout', ledger['repository'], str(CHECKOUT)], check=True)
    if git('remote', 'get-url', 'origin') != ledger['repository']:
        raise RuntimeError('Unexpected upstream origin; refusing to fetch')
    git('fetch', 'origin', ledger['branch'])
    tip = git('rev-parse', 'FETCH_HEAD')
    subprocess.run(['git', '-C', str(CHECKOUT), 'merge-base', '--is-ancestor', ledger['baseline'], tip], check=True)
    known = {entry['sha']: entry for entry in ledger['commits']}
    commits = []
    for sha in git('rev-list', '--reverse', '--topo-order', ledger['baseline'] + '..' + tip).splitlines():
        entry = dict(known.get(sha, {'sha': sha, 'status': 'pending'}))
        entry['subject'] = git('show', '-s', '--format=%s', sha)
        commits.append(entry)
    report = {'repository': ledger['repository'], 'tip': tip, 'commits': commits}
    destination = ROOT / 'build/upstream/dx11-check.json'
    destination.write_text(json.dumps(report, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    print(f'Upstream: {tip}; commits: {len(commits)}; new: {sum(c["sha"] not in known for c in commits)}')
    print(f'Report: {destination}')


if __name__ == '__main__':
    main()
