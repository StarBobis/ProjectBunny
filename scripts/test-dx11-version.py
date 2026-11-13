"""Exercise the real CMake generator using isolated Git repositories."""
import argparse
from concurrent.futures import ThreadPoolExecutor
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile


def run(args, *, env=None, success=True):
    result = subprocess.run(args, capture_output=True, text=True, encoding="utf-8", env=env)
    if success and result.returncode:
        raise RuntimeError(result.stderr or result.stdout)
    if not success:
        assert result.returncode, "invalid version unexpectedly succeeded"
    return result


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--source-root", type=Path, required=True)
    parser.add_argument("--work-dir", type=Path, required=True)
    args = parser.parse_args()
    root, work = args.source_root.resolve(), args.work_dir.resolve()
    try:
        work.relative_to(root / "build")
    except ValueError:
        raise AssertionError("version fixtures must remain inside workspace build") from None
    work.mkdir(parents=True, exist_ok=True)
    generator = root / "cmake" / "GenerateDX11Version.cmake"
    git, cmake = shutil.which("git"), shutil.which("cmake")
    assert git and cmake
    env = os.environ.copy()
    env["GIT_CEILING_DIRECTORIES"] = str(work)
    env.pop("GITHUB_ACTOR", None)
    with tempfile.TemporaryDirectory(prefix="dx11-version-", dir=work) as temporary:
        base = Path(temporary).resolve()
        assert base.parent == work
        repo, output = base / "repo with spaces", base / "generated.h"
        repo.mkdir()

        def g(*arguments):
            return run([git, "-C", str(repo), *arguments], env=env).stdout.strip()

        def generate(*, tag="", source=repo, git_path=git, builder="", success=True):
            run([cmake, f"-DSOURCE_DIR={source}", f"-DOUTPUT_FILE={output}",
                 f"-DGIT_EXECUTABLE={git_path}", f"-DVERSION_TAG={tag}",
                 f"-DBUILDER_OVERRIDE={builder}", "-DFALLBACK_VERSION=1.3.16",
                 "-P", str(generator)], env=env, success=success)
            return output.read_text(encoding="utf-8") if success else ""

        def number(text, name):
            return int(re.search(rf"^#define {name} (\d+)$", text, re.M)[1])

        def version(text):
            return tuple(number(text, "VERSION_" + part) for part in ("MAJOR", "MINOR", "REVISION", "BUILD"))

        def string(text, name):
            literal = re.search(rf"^#define {name} (\".*\")$", text, re.M)[1]
            return json.loads(literal)

        g("init", "-q")
        g("config", "user.name", "Version Test")
        g("config", "user.email", "version-test@example.invalid")
        (repo / "source.txt").write_text("first\n", encoding="utf-8")
        g("add", "source.txt")
        g("commit", "-qm", "initial")
        text = generate()
        assert version(text) == (1, 3, 16, 0) and number(text, "DX11_VERSION_IS_RELEASE") == 0
        assert "Version Test" in string(text, "VER_COMPANYNAME_STR")
        assert string(text, "VER_COMMENTS_STR") == ""
        g("tag", "v2.3.4")
        text = generate()
        assert version(text) == (2, 3, 4, 0) and number(text, "DX11_VERSION_IS_RELEASE") == 1
        g("tag", "v2.3.4.9")
        text = generate(tag="v2.3.4.9")
        assert version(text) == (2, 3, 4, 9)
        old_time = 1_600_000_000_000_000_000
        os.utime(output, ns=(old_time, old_time))
        assert generate(tag="v2.3.4.9") == text and output.stat().st_mtime_ns == old_time
        with ThreadPoolExecutor(max_workers=6) as workers:
            results = list(workers.map(lambda i: generate(tag="v2.3.4.9", builder=f"Concurrent{i}"), range(6)))
        assert all(version(value) == (2, 3, 4, 9) and "Built by Concurrent" in string(value, "VER_COMPANYNAME_STR")
                   for value in results), "concurrent generators must publish complete headers"
        (repo / "source.txt").write_text("dirty\n", encoding="utf-8")
        text = generate(tag="v2.3.4.9")
        assert "-dirty" in string(text, "VER_COMPANYNAME_STR") and number(text, "DX11_VERSION_IS_RELEASE") == 0
        g("add", "source.txt")
        g("commit", "-qm", "next")
        text = generate(tag="v2.3.4.9")
        assert version(text) == (2, 3, 4, 10) and number(text, "DX11_VERSION_IS_RELEASE") == 0
        assert "-dirty" not in string(text, "VER_COMPANYNAME_STR")
        builder = 'Builder "quoted" \\ path; segment end'
        text = generate(tag="v2.3.4.9", builder=builder)
        assert string(text, "VER_COMPANYNAME_STR").startswith('Built by ' + builder + ' @ '), (repr(builder), repr(string(text, "VER_COMPANYNAME_STR")))
        actual_newline = "Builder\nNext".replace("\\n", "\n")
        text = generate(tag="v2.3.4.9", builder=actual_newline)
        assert "Builder Next" in string(text, "VER_COMPANYNAME_STR")
        for remote, expected in [
            ("git@github.com:Owner/Repo.git", "https://github.com/Owner/Repo"),
            ("ssh://git@github.com/Owner/Repo.git", "https://github.com/Owner/Repo"),
            ("https://dummy:super-secret@github.com/Owner/Repo.git?access_token=secret#private", "https://github.com/Owner/Repo"),
            ("https://dummy:super-secret@code.example.invalid/Owner/Repo.git", "https://code.example.invalid/Owner/Repo"),
            ("file:///private/source.git", ""),
        ]:
            g("config", "remote.origin.url", remote)
            text = generate(tag="v2.3.4.9")
            assert string(text, "VER_COMMENTS_STR") == expected
            assert "super-secret" not in text and "access_token" not in text and "#private" not in text
        archive = base / "archive"
        archive.mkdir()
        text = generate(source=archive)
        assert version(text) == (1, 3, 16, 0) and "archive" in string(text, "VER_COMPANYNAME_STR")
        text = generate(git_path=base / "absent-git")
        assert version(text) == (1, 3, 16, 0) and number(text, "DX11_VERSION_IS_RELEASE") == 0
        text = generate(source=archive, tag="v0001.0002.0003.0004")
        assert version(text) == (1, 2, 3, 4)
        before = output.read_bytes()
        for invalid in ("v1.2", "v1.2.3.4.5", "v65536.0.0", "v1.2.3.65536"):
            generate(tag=invalid, success=False)
            assert output.read_bytes() == before
        g("tag", "v1.0.0.65535", "HEAD~1")
        generate(tag="v1.0.0.65535", success=False)
        assert output.read_bytes() == before
        orphan = g("commit-tree", g("rev-parse", "HEAD^{tree}"), "-m", "unrelated root")
        g("tag", "v9.0.0", orphan)
        generate(tag="v9.0.0", success=False)
        assert output.read_bytes() == before
    print("PASS: CMake Git version tags/distance/dirty, archive/noGit, escaping, safe remote, stable/concurrent output and16-bit bounds")


if __name__ == "__main__":
    main()
