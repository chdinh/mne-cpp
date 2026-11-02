# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026 MNE-CPP Authors
#   Christoph Dinh <christoph.dinh@mne-cpp.org>
"""Windows-only failure modes of mne_acq, checked statically.

Only the macOS and Linux builds run locally, so two MSVC failures are caught here:
exported classes outside ACQ_STD_MEMBERS_BEGIN/END (C4251 under /WX), and
identifiers that <windows.h> defines as macros (small, far, near, ...) in acq
sources and tests that include it.
"""

import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
ACQ = ROOT / "src" / "libraries" / "acq"
# Object-like macros from <windows.h> (minwindef.h, rpcndr.h) that silently replace an identifier.
WINDOWS_MACROS = ("small", "hyper", "far", "near", "pascal", "cdecl", "interface")
CODE_WORD = re.compile(r"\b(" + "|".join(WINDOWS_MACROS) + r")\b")
TOKENS = re.compile(r"^(ACQ_STD_MEMBERS_BEGIN|ACQ_STD_MEMBERS_END)$|^(?:class|struct) ACQSHARED_EXPORT (\w+)", re.MULTILINE)


def unbracketed(text: str) -> list[str]:
    depth, missing = 0, []
    for match in TOKENS.finditer(text):
        if match.group(1) == "ACQ_STD_MEMBERS_BEGIN":
            depth += 1
        elif match.group(1) == "ACQ_STD_MEMBERS_END":
            depth -= 1
        elif depth != 1:
            missing.append(match.group(2))
    return missing + (["<unbalanced>"] if depth else [])


def windows_macro_names(text: str) -> list[str]:
    """Identifiers of *text* that <windows.h> turns into macros; comments and strings are ignored."""
    if "<windows.h>" not in text:
        return []
    code = re.sub(r"//[^\n]*|/\*.*?\*/|\"(?:\\.|[^\"\\])*\"", " ", text, flags=re.DOTALL)
    return sorted(set(CODE_WORD.findall(code)))


class TestAcqExportContract(unittest.TestCase):
    def test_detects_missing_bracket(self):
        self.assertEqual(unbracketed("class ACQSHARED_EXPORT A\n{\n};\n"), ["A"])
        self.assertEqual(unbracketed("struct ACQSHARED_EXPORT B\n{\n};\n"), ["B"])
        self.assertEqual(unbracketed("ACQ_STD_MEMBERS_BEGIN\nclass ACQSHARED_EXPORT A\n"), ["<unbalanced>"])

    def test_every_exported_class_is_bracketed(self):
        headers = sorted(ACQ.rglob("*.h"))
        self.assertTrue(headers)
        for header in headers:
            with self.subTest(header=header.name):
                self.assertEqual(unbracketed(header.read_text(encoding="utf-8")), [])

    def test_detects_windows_macro_names(self):
        source = '#include <windows.h>\n// far away\nauto small = 1; auto far = "near";\n'
        self.assertEqual(windows_macro_names(source), ["far", "small"])
        self.assertEqual(windows_macro_names("auto small = 1;\n"), [])

    def test_no_windows_macro_names_in_acq_code(self):
        files = sorted(ACQ.rglob("*.[ch]*")) + sorted((ROOT / "src" / "testframes").glob("test_acq_*/*.cpp"))
        files += sorted((ROOT / "src" / "examples").glob("ex_acq_*/*.cpp"))
        for path in files:
            with self.subTest(file=path.name):
                self.assertEqual(windows_macro_names(path.read_text(encoding="utf-8")), [])


if __name__ == "__main__":
    unittest.main()
