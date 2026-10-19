#!/usr/bin/env python3
"""Integration checks in temporary repositories; never stage this worktree."""

import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest


SOURCE_ROOT = Path(__file__).resolve().parent.parent


class FormatOnAddTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory(prefix="clang format repo ")
        self.addCleanup(self.directory.cleanup)
        self.root = Path(self.directory.name)
        (self.root / "scripts").mkdir()
        for relative in (
            ".gitattributes",
            ".clang-format",
            "scripts/git_clang_format.py",
        ):
            shutil.copyfile(SOURCE_ROOT / relative, self.root / relative)
        self.env = os.environ.copy()
        self.env["GIT_CONFIG_NOSYSTEM"] = "1"
        self.env["GIT_CONFIG_GLOBAL"] = os.devnull
        self.env["GIT_TERMINAL_PROMPT"] = "0"
        self.git("init", "-q")
        self.git("config", "user.name", "Format Test")
        self.git("config", "user.email", "format-test@example.invalid")
        self.setup_filter("--install")

    def git(self, *args, input=None, check=True, cwd=None, env=None):
        return subprocess.run(
            ["git", "-c", "core.hooksPath=/dev/null", *args],
            cwd=cwd or self.root,
            env=env or self.env,
            input=input,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            check=check,
        )

    def setup_filter(self, action):
        return subprocess.run(
            [sys.executable, str(self.root / "scripts/git_clang_format.py"), action],
            cwd=self.root,
            env=self.env,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            check=True,
        )

    def expected(self, name, contents):
        return subprocess.run(
            [
                sys.executable,
                str(self.root / "scripts/git_clang_format.py"),
                "--clean=" + name,
            ],
            input=contents,
            cwd=self.root,
            env=self.env,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            check=True,
        ).stdout

    def test_add_formats_index_without_rewriting_worktree(self):
        raw = b"int  main(){return 0;}\n"
        suffixes = (
            "c", "C", "cc", "cp", "cpp", "CPP", "cxx", "c++",
            "h", "H", "hh", "hpp", "hxx", "h++", "inc", "inl", "ipp", "tpp",
        )
        for index, suffix in enumerate(suffixes):
            # Distinct basenames also work on case-insensitive filesystems.
            name = "source_{}.{}".format(index, suffix)
            (self.root / name).write_bytes(raw)
            self.git("add", "--", name)
            staged = self.git("show", ":" + name).stdout
            self.assertEqual(staged, self.expected(name, raw))
            self.assertNotEqual(staged, raw)
            self.assertEqual((self.root / name).read_bytes(), raw)
        # Unselected files must not be staged by the filter.
        (self.root / "unselected.cpp").write_bytes(raw)
        self.assertNotIn(b"unselected.cpp", self.git("ls-files", "-z").stdout)

    def test_filenames_and_subdirectories_are_shell_safe(self):
        raw = b"int  answer(){return 42;}\n"
        names = (
            "nested dir/a file.cpp",
            "quoted'file.hpp",
            "$(touch INJECTED).cpp",
            "--leading-option.cc",
            "中文.cpp",
            "line\nbreak.h",
        )
        for name in names:
            path = self.root / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(raw)
            self.git("add", "--", path.name, cwd=path.parent)
            self.assertEqual(self.git("show", ":" + name).stdout, self.expected(name, raw))
        self.assertFalse((self.root / "INJECTED").exists())

    def test_non_cpp_files_are_unchanged(self):
        raw = b"int  main(){return 0;}\n"
        (self.root / "notes.txt").write_bytes(raw)
        self.git("add", "notes.txt")
        self.assertEqual(self.git("show", ":notes.txt").stdout, raw)

    def test_stdin_and_nested_style_are_used(self):
        nested = self.root / "nested"
        nested.mkdir()
        (nested / ".clang-format").write_text("BasedOnStyle: LLVM\nIndentWidth: 8\n")
        (nested / "source.cpp").write_text("THIS IS NOT THE STAGED CONTENT")
        raw = b"int  f(){int x=1;return x;}\n"
        formatted = self.expected("nested/source.cpp", raw)
        self.assertIn(b"\n        int x = 1;", formatted)
        self.assertNotIn(b"THIS IS NOT", formatted)
        self.assertEqual(self.expected("nested/source.cpp", formatted), formatted)

    def test_missing_formatter_rejects_add_and_preserves_index(self):
        path = self.root / "source.cpp"
        path.write_bytes(b"int  f(){return 1;}\n")
        self.git("add", "source.cpp")
        before = self.git("show", ":source.cpp").stdout
        path.write_bytes(b"int  f(){return 2;}\n")
        env = dict(self.env, CLANG_FORMAT="/nonexistent/clang-format")
        result = self.git("add", "source.cpp", env=env, check=False)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn(b"CLANG_FORMAT is not an executable", result.stderr)
        self.assertEqual(self.git("show", ":source.cpp").stdout, before)

    def test_patch_staging_does_not_add_unselected_changes(self):
        baseline = (
            "int first() {\n  return 1;\n}\n"
            + "// Separate the two diff hunks.\n" * 20
            + "int second() {\n  return 2;\n}\n"
        ).encode()
        path = self.root / "partial.cpp"
        path.write_bytes(baseline)
        self.git("add", ".")
        self.git("commit", "-qm", "baseline")
        changed = baseline.replace(b"return 1;", b"return 101;").replace(
            b"return 2;", b"return 202;"
        )
        path.write_bytes(changed)
        self.git("add", "-p", "--", "partial.cpp", input=b"y\nn\n")
        staged = self.git("show", ":partial.cpp").stdout
        self.assertIn(b"return 101;", staged)
        self.assertIn(b"return 2;", staged)
        self.assertNotIn(b"return 202;", staged)
        self.assertEqual(path.read_bytes(), changed)

    def test_formatter_failure_rejects_add_and_preserves_index(self):
        path = self.root / "source.cpp"
        path.write_bytes(b"int  f(){return 1;}\n")
        self.git("add", "source.cpp")
        before = self.git("show", ":source.cpp").stdout
        (self.root / ".clang-format").write_text("NotAClangFormatOption: true\n")
        path.write_bytes(b"int  f(){return 2;}\n")
        result = self.git("add", "source.cpp", check=False)
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(self.git("show", ":source.cpp").stdout, before)

    def test_install_is_repeatable_and_can_be_disabled(self):
        self.setup_filter("--install")
        self.setup_filter("--uninstall")
        self.setup_filter("--uninstall")
        raw = b"int  f(){return 0;}\n"
        (self.root / "source.cpp").write_bytes(raw)
        self.git("add", "source.cpp")
        self.assertEqual(self.git("show", ":source.cpp").stdout, raw)


if __name__ == "__main__":
    unittest.main()
