"""Regression checks for the Release AVX/AVX2 boundary (standard library only).

Run after an x64 Release build:
    python tools/test_avx2_boundary.py
Use --objects to inspect another build's VideoProcessor-Lib object directory.
"""

import argparse
from pathlib import Path
import re
import struct
import unittest
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[1]
NS = {"m": "http://schemas.microsoft.com/developer/msbuild/2003"}
LIB = ROOT / "src/VideoProcessor-Lib"
OBJECTS = LIB / "x64/Release"


def project(path):
    return ET.parse(path).getroot()


def kernels():
    return [item for item in project(LIB / "VideoProcessor-Lib.vcxproj").findall(
        "m:ItemGroup/m:ClCompile", NS) if item.attrib["Include"].endswith(".AVX2.cpp")]


def coff(path):
    data = path.read_bytes()
    machine, sections, _, symbols_at, symbol_count, optional_size, _ = struct.unpack_from(
        "<HHIIIHH", data)
    # LTCG objects have a different header. Reject them, not just their sections.
    if machine != 0x8664 or optional_size != 0:
        raise AssertionError(f"{path.name}: expected native x64 COFF, not LTCG")
    strings_at = symbols_at + symbol_count * 18

    def string_at(offset):
        start = strings_at + offset
        return data[start:data.index(b"\0", start)].decode("ascii")

    names = []
    for index in range(sections):
        name = data[20 + index * 40:28 + index * 40].rstrip(b"\0").decode("ascii")
        names.append(string_at(int(name[1:])) if name.startswith("/") else name)
    functions = []
    index = 0
    while index < symbol_count:
        offset = symbols_at + index * 18
        name_bytes = data[offset:offset + 8]
        name = (string_at(struct.unpack_from("<I", name_bytes, 4)[0])
                if name_bytes[:4] == b"\0" * 4 else name_bytes.rstrip(b"\0").decode("ascii"))
        _, section, kind, storage, auxiliary = struct.unpack_from("<IhHBB", data, offset + 8)
        if section > 0 and kind & 0x20 and storage == 2:
            functions.append(name)
        index += 1 + auxiliary
    return names, functions


class Avx2BoundaryTests(unittest.TestCase):
    def test_no_project_wide_avx2(self):
        for path in (ROOT / "src").rglob("*.vcxproj*"):
            if path.suffix not in (".vcxproj",) and not path.name.endswith(".vcxproj.vcxproj"):
                continue
            for group in project(path).findall("m:ItemDefinitionGroup", NS):
                for setting in group.findall("m:ClCompile/m:EnableEnhancedInstructionSet", NS):
                    self.assertNotIn(setting.text, ("AdvancedVectorExtensions2", "AdvancedVectorExtensions512"), str(path))

    def test_kernels_disable_ltcg_and_pch(self):
        self.assertEqual(9, len(kernels()))
        for item in kernels():
            for name, value in (("EnableEnhancedInstructionSet", "AdvancedVectorExtensions2"),
                                ("WholeProgramOptimization", "false"), ("PrecompiledHeader", "NotUsing")):
                setting = item.find("m:" + name, NS)
                self.assertIsNotNone(setting, item.attrib["Include"])
                self.assertEqual(value, setting.text, item.attrib["Include"])
                self.assertNotIn("Condition", setting.attrib)

    def test_no_avx2_intrinsics_in_baseline_sources(self):
        for item in project(LIB / "VideoProcessor-Lib.vcxproj").findall("m:ItemGroup/m:ClCompile", NS):
            source = LIB / Path(item.attrib["Include"].replace("\\", "/"))
            if source.name.endswith(".AVX2.cpp"):
                continue
            text = source.read_text(encoding="utf-8", errors="replace")
            text = re.sub(r"//[^\n]*|/\*.*?\*/", "", text, flags=re.S)
            self.assertIsNone(re.search(r"\b_mm256_\w+\s*\(", text), str(source))

    def test_release_objects_have_no_startup_initializers(self):
        for item in kernels():
            name = Path(item.attrib["Include"].replace("\\", "/")).stem + ".obj"
            sections, _ = coff(OBJECTS / name)
            self.assertFalse(any(s.startswith(".CRT") or s.startswith(".tls") for s in sections), name)

    def test_release_objects_only_publish_dispatched_kernels(self):
        # Shared inline COMDAT helpers compiled for AVX2 must not be selected for
        # baseline callers by the linker. These are the only allowed functions.
        allowed = re.compile(r"(?:^\?(?:ExecuteAvx2Probe|ConvertAVX2|ConvertRowsAVX2|ConvertLimited10RowPairsAVX2|"
                             r"ConvertR12RowPairsAVX2|ProcessAdvancedSegmentAVX2)@|"
                             r"^\?\?\$(?:ProcessLineSegmentImpl|ConvertV210ToP010_SIMDImpl)@)")
        for item in kernels():
            name = Path(item.attrib["Include"].replace("\\", "/")).stem + ".obj"
            _, functions = coff(OBJECTS / name)
            self.assertTrue(functions, name)
            for function in functions:
                self.assertRegex(function, allowed, name)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--objects", type=Path, default=OBJECTS)
    options, remaining = parser.parse_known_args()
    OBJECTS = options.objects
    unittest.main(argv=[__file__] + remaining)
