# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026 MNE-CPP Authors
#   Christoph Dinh <christoph.dinh@mne-cpp.org>
"""Exported acq classes must sit inside ACQ_STD_MEMBERS_BEGIN/END.

MSVC reports C4251 for standard-library members of exported classes, and the
Windows build treats it as an error. Only the macOS and Linux builds run
locally, so this checks the bracketing statically.
"""

import re
import unittest
from pathlib import Path

ACQ = Path(__file__).resolve().parents[3] / "src" / "libraries" / "acq"
TOKENS = re.compile(r"^(ACQ_STD_MEMBERS_BEGIN|ACQ_STD_MEMBERS_END)$|^class ACQSHARED_EXPORT (\w+)", re.MULTILINE)


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


class TestAcqExportContract(unittest.TestCase):
    def test_detects_missing_bracket(self):
        self.assertEqual(unbracketed("class ACQSHARED_EXPORT A\n{\n};\n"), ["A"])
        self.assertEqual(unbracketed("ACQ_STD_MEMBERS_BEGIN\nclass ACQSHARED_EXPORT A\n"), ["<unbalanced>"])

    def test_every_exported_class_is_bracketed(self):
        headers = sorted(ACQ.rglob("*.h"))
        self.assertTrue(headers)
        for header in headers:
            with self.subTest(header=header.name):
                self.assertEqual(unbracketed(header.read_text(encoding="utf-8")), [])


if __name__ == "__main__":
    unittest.main()
