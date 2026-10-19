#!/usr/bin/env python3
"""Install or run the repository's clang-format Git clean filter."""

import argparse
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import sys


def find_clang_format():
    override = os.environ.get("CLANG_FORMAT")
    if override:
        executable = shutil.which(override)
        if not executable:
            raise RuntimeError("CLANG_FORMAT is not an executable: " + override)
        return executable

    executable = shutil.which("clang-format")
    if executable:
        return executable

    # Apple's Command Line Tools can supply clang-format outside PATH.
    xcrun = shutil.which("xcrun")
    if xcrun:
        result = subprocess.run(
            [xcrun, "--find", "clang-format"],
            stdout=subprocess.PIPE,
            stderr=subprocess.DEVNULL,
            text=True,
        )
        if result.returncode == 0:
            executable = shutil.which(result.stdout.strip())
            if executable:
                return executable

    for candidate in (
        "/opt/homebrew/opt/llvm/bin/clang-format",
        "/usr/local/opt/llvm/bin/clang-format",
        "/Library/Developer/CommandLineTools/usr/bin/clang-format",
    ):
        executable = shutil.which(candidate)
        if executable:
            return executable

    raise RuntimeError(
        "clang-format was not found; install it or set CLANG_FORMAT to its executable path"
    )


def format_contents(filename, contents):
    # %f supplies only the language/style lookup path. Always format stdin:
    # the worktree file may differ from what Git is currently staging.
    return subprocess.run(
        [find_clang_format(), "--style=file", "--assume-filename=" + filename],
        input=contents,
        stdout=subprocess.PIPE,
        check=True,
    ).stdout


def configure(install):
    root = Path(__file__).resolve().parent.parent
    git = ["git", "-C", str(root), "config", "--local"]
    if not install:
        for key in ("clean", "smudge", "required"):
            result = subprocess.run(git + ["--unset-all", "filter.clang-format." + key])
            if result.returncode not in (0, 5):
                raise RuntimeError("could not remove filter.clang-format." + key)
        print("Disabled the repository-local clang-format filter.")
        return

    # Validate both the executable and the existing style before changing Git.
    format_contents(str(root / "format-check.cpp"), b"int  main(){return 0;}\n")
    command = "{} {} --clean=%f".format(
        shlex.quote(sys.executable), shlex.quote(str(Path(__file__).resolve()))
    )
    subprocess.run(git + ["filter.clang-format.clean", command], check=True)
    subprocess.run(git + ["filter.clang-format.smudge", "cat"], check=True)
    # Reject staging if formatting fails instead of silently adding raw input.
    subprocess.run(git + ["filter.clang-format.required", "true"], check=True)
    print("Enabled clang-format on git add for this repository.")
    print("Formatter: " + find_clang_format())
    print("Formatted contents enter the index; worktree files are not rewritten.")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    action = parser.add_mutually_exclusive_group(required=True)
    action.add_argument("--install", action="store_true")
    action.add_argument("--uninstall", action="store_true")
    action.add_argument("--clean", metavar="PATH", help="filter stdin for Git")
    args = parser.parse_args()
    try:
        if args.clean is not None:
            sys.stdout.buffer.write(format_contents(args.clean, sys.stdin.buffer.read()))
        else:
            configure(args.install)
    except (OSError, RuntimeError, subprocess.CalledProcessError) as error:
        print("[clang-format filter] " + str(error), file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
