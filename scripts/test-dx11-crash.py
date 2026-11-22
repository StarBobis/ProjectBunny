"""Run isolated faults against the production crash writer; inspect dump streams."""
import pathlib
import struct
import subprocess
import sys
import tempfile

host = pathlib.Path(sys.argv[1]).resolve()
root = pathlib.Path(sys.argv[2]).resolve()
root.mkdir(parents=True, exist_ok=True)
for mode in ('normal', 'handled', 'crash', 'disabled', 'legacy', 'fallback', 'write-failure'):
    with tempfile.TemporaryDirectory(dir=root, prefix='崩溃-' + mode) as temporary:
        folder = pathlib.Path(temporary)
        ini = folder / 'd3dx.ini'
        ini.write_text('[Logging]\n' + ('crash_dump=0' if mode == 'disabled' else 'crash=1' if mode == 'legacy' else ''), encoding='utf-8')
        if mode == 'fallback':
            # A missing parent exercises the real TEMP fallback.
            log = folder / 'missing' / 'd3d11_log.txt'
            dumps = pathlib.Path(tempfile.gettempdir()) / '3Dmigoto-DX11-CrashDumps'
        else:
            log = folder / 'd3d11_log.txt'
            dumps = folder / 'CrashDumps'
            if mode == 'write-failure':
                dumps.write_text('Blocks report and dump creation')
        before = set(dumps.glob('*')) if dumps.exists() else set()
        result = subprocess.run([str(host), str(ini), str(log), mode], cwd=folder, timeout=25)
        created = set(dumps.glob('*')) - before
        if mode in ('normal', 'handled', 'disabled', 'legacy', 'write-failure'):
            assert not created, (mode, created)
            assert result.returncode == (0 if mode in ('normal', 'handled') else 0xC0000005), result.returncode
        else:
            assert result.returncode == 0xC0000005, result.returncode
            dump, = [p for p in created if p.suffix == '.dmp']
            data = dump.read_bytes()
            signature, version, streams, rva = struct.unpack_from('<IIII', data)
            assert signature == 0x504D444D
            entries = [struct.unpack_from('<III', data, rva + i * 12) for i in range(streams)]
            exception, = [item for item in entries if item[0] == 6]
            assert struct.unpack_from('<I', data, exception[2] + 8)[0] == 0xC0000005
            assert any(item[0] == 4 for item in entries), 'module stream missing'
            report, = [p for p in created if p.suffix == '.txt']
            assert 'error: 0x00000000' in report.read_text(encoding='utf-8')
        if mode not in ('normal', 'handled'):
            assert (folder / 'previous-filter-called').exists(), 'previous filter lost'
        for path in created:
            path.unlink()
        print(mode, 'PASS')
