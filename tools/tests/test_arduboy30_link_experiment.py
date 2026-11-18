"""Bounded Arduboy30 link experiment: folder selector + exact libm members."""
import importlib.util
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location("bw16_split", ROOT / "tools/build_bw16_split_games.py")
build = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(build)

SCRIPT = "MEMORY\n{\n}\nSECTIONS\n{\n}\n"
OLD_DEFAULT = (
    "MEMORY\n{\n EXTFLASH (rx) : ORIGIN = 0x08107000, LENGTH = 0xF1000\n}\n"
    "SECTIONS\n{\n .extflash : {\n *(.text.*Foo* .rodata.*Foo*)\n"
    " . = ALIGN(16);\n } > EXTFLASH\n"
    ' ASSERT(SIZEOF(.extflash) <= LENGTH(EXTFLASH), "Extension overflow")\n\n}\n'
)
ARM_CANDIDATES = (
    Path(os.environ["CRABIT_ARM_BIN"]) if os.environ.get("CRABIT_ARM_BIN") else None,
    build.DEFAULT_SDK.parents[2] / "tools/ameba_d_asdk_toolchain/1.0.1/bin",
)


def arm_bin():
    """Locate the real ARM binutils used by the embedded build; skip if absent."""
    for directory in ARM_CANDIDATES:
        if directory and (directory / "arm-none-eabi-ld.exe").is_file():
            return directory
    return None


class Arduboy30LinkExperimentTests(unittest.TestCase):
    def test_no_selection_still_places_only_the_category_menu(self):
        # The menu exists in every ports image; math members only with ports.
        self.assertEqual(build.arduboy30_link_extras([]),
                         (("ArduboyCategoryFolderApp",), ()))
        # No selector, no math: the linker text stays byte-for-byte unchanged.
        self.assertEqual(build.split_linker(SCRIPT, ("Foo",)), OLD_DEFAULT)
        self.assertEqual(build.split_linker(SCRIPT, ("Foo",), ()), OLD_DEFAULT)

    def test_selection_adds_folder_and_exact_math_objects(self):
        folder, math = build.arduboy30_link_extras([{"id": "example"}])
        self.assertEqual(folder, ("ArduboyCategoryFolderApp",))
        # The original five stay first and unchanged; the audited remainder is
        # appended, still one exact archive member per entry.
        self.assertEqual([entry[1] for entry in math[:5]],
                         ["atanf", "__ieee754_atan2f", "atan2f", "__ieee754_sqrt", "sqrt"])
        self.assertEqual([entry[0] for entry in math[:5]],
                         ["lib_a-sf_atan.o", "lib_a-ef_atan2.o", "lib_a-wf_atan2.o",
                          "lib_a-e_sqrt.o", "lib_a-w_sqrt.o"])
        self.assertEqual(math, build.ARDUBOY30_MATH_OBJECTS)
        self.assertEqual(build.selectors(["ArduboyCategoryFolderApp"]),
                         (*build.DEFAULT_SELECTORS, "ArduboyCategoryFolderApp"))

    def test_linker_uses_archive_member_patterns_not_broad_matches(self):
        folder, math = build.arduboy30_link_extras([{"id": "example"}])
        text = build.split_linker(SCRIPT, ("Foo", *folder), math)
        for line in ("*libm.a:lib_a-sf_atan.o(.text* .rodata*)",
                     "*libm.a:lib_a-ef_atan2.o(.text*)",
                     "*libm.a:lib_a-wf_atan2.o(.text*)",
                     "*libm.a:lib_a-e_sqrt.o(.text*)",
                     "*libm.a:lib_a-w_sqrt.o(.text* .rodata*)"):
            self.assertIn(line, text)
        self.assertIn("*(.text.*ArduboyCategoryFolderApp* .rodata.*ArduboyCategoryFolderApp*)",
                      text)
        for forbidden in ("libm.a:*(", "libm.a:(", "--whole-archive", "*sqrt*",
                          "printf", "__errno", "WindowManager", "std::"):
            self.assertNotIn(forbidden, text)

    def test_math_verifier_requires_extension_and_ignores_missing(self):
        _, math = build.arduboy30_link_extras([{"id": "example"}])
        self.assertEqual(build.check_arduboy30_math("", 0x1000, math), [])
        good = "08107010 00000010 T sqrt\n08107020 00000010 T atanf\n"
        self.assertEqual(build.check_arduboy30_math(good, 0x1000, math), ["atanf", "sqrt"])
        wrong = "0e082650 000000b4 T sqrt\n"
        with self.assertRaisesRegex(ValueError, "not in extension"):
            build.check_arduboy30_math(wrong, 0x1000, math)
        undefined = "         U sqrt\n08107010 00000010 T sqrt_extra\n"
        self.assertEqual(build.check_arduboy30_math(undefined, 0x1000, math), [])

    def test_real_arm_ld_selects_archive_members_without_loose_objects(self):
        """Regression for the real failure: ld must resolve archive members via -lm.

        The old script line `lib_a-sf_atan.o(...)` was treated as a separate
        input file and failed with 'cannot find'. This test builds a real
        archive, keeps member objects in obj/, and only supplies `-lm`.
        """
        binaries = arm_bin()
        if not binaries:
            self.skipTest("ARM binutils are unavailable")
        gcc = binaries / "arm-none-eabi-gcc.exe"
        ar = binaries / "arm-none-eabi-ar.exe"
        ld = binaries / "arm-none-eabi-ld.exe"
        nm = binaries / "arm-none-eabi-nm.exe"
        objdump = binaries / "arm-none-eabi-objdump.exe"
        for tool in (gcc, ar, ld, nm, objdump):
            self.assertTrue(tool.is_file(), tool)
        # Real member names; atanf/sqrt carry rodata that must also move.
        sources = {
            "lib_a-sf_atan.o": ("const int atanf_tag = 0x1234;\n"
                                "int atanf(int x) { return x + atanf_tag; }\n"),
            "lib_a-ef_atan2.o": "int __ieee754_atan2f(int x) { return x + 1; }\n",
            "lib_a-wf_atan2.o": "int atan2f(int x) { return x + 2; }\n",
            "lib_a-e_sqrt.o": "int __ieee754_sqrt(int x) { return x + 3; }\n",
            "lib_a-w_sqrt.o": ("const char sqrt_tag[] = \"sq\";\n"
                               "int sqrt(int x) { return x + 4; }\n"),
            "lib_a-unused.o": "int unused_member(int x) { return x + 5; }\n",
        }
        with tempfile.TemporaryDirectory(prefix="crabit-ld-archive-") as directory:
            root = Path(directory)
            (root / "src").mkdir()
            (root / "obj").mkdir()
            (root / "lib").mkdir()
            for name, source in sources.items():
                (root / "src" / (name[:-2] + ".c")).write_text(source)
                result = subprocess.run(
                    [str(gcc), "-O1", "-fno-builtin", "-c", str(root / "src" / (name[:-2] + ".c")),
                     "-o", str(root / "obj" / name)], capture_output=True, text=True)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            (root / "main.c").write_text(
                "extern int atanf(int); extern int __ieee754_atan2f(int);\n"
                "extern int atan2f(int); extern int __ieee754_sqrt(int); extern int sqrt(int);\n"
                "int main(void) { return atanf(1) + __ieee754_atan2f(2) + atan2f(3) + "
                "__ieee754_sqrt(4) + sqrt(5); }\n")
            result = subprocess.run(
                [str(gcc), "-O1", "-c", str(root / "main.c"), "-o", str(root / "main.o")],
                capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            members = [str(root / "obj" / name) for name in sources]
            result = subprocess.run([str(ar), "rcs", str(root / "lib/libm.a"), *members],
                                    capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            # No member object is reachable as a loose input in the work directory.
            for name in sources:
                self.assertFalse((root / name).exists())
            _, math = build.arduboy30_link_extras([{"id": "example"}])
            script = "\n".join([
                "MEMORY", "{",
                " EXTFLASH (rx) : ORIGIN = 0x08107000, LENGTH = 0xF1000",
                " MAIN (rx) : ORIGIN = 0x08006000, LENGTH = 0x10000",
                "}", "SECTIONS", "{", " .extflash : {",
                *("  " + pattern for pattern in build.arduboy30_math_patterns(math)),
                "  *libm.a:lib_a-absent.o(.text*)",
                "  . = ALIGN(16);", " } > EXTFLASH",
                " .text : { *(.text*) *(.rodata*) } > MAIN",
                " /DISCARD/ : { *(.ARM.attributes) *(.comment) }", "}", ""])
            (root / "select.ld").write_text(script)
            result = subprocess.run(
                [str(ld), "-T", str(root / "select.ld"), "-o", str(root / "out.elf"),
                 str(root / "main.o"), "-L", str(root / "lib"), "-lm", "-e", "main"],
                capture_output=True, text=True, cwd=root)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            symbols = subprocess.run([str(nm), "-n", "-S", str(root / "out.elf")],
                                     capture_output=True, text=True, check=True).stdout
            sections = subprocess.run([str(objdump), "-h", str(root / "out.elf")],
                                      capture_output=True, text=True, check=True).stdout
            ext = next(line for line in sections.splitlines()
                       if line.strip().startswith("0 .extflash"))
            size = int(ext.split()[2], 16)
            self.assertGreater(size, 0)
            # Only the five members above exist in this synthetic archive; the
            # other whitelist members are absent inputs, not link failures.
            self.assertEqual(build.check_arduboy30_math(symbols, size, math),
                             ["atanf", "__ieee754_atan2f", "atan2f", "__ieee754_sqrt", "sqrt"])
            self.assertNotIn("unused_member", symbols)
            self.assertNotIn("lib_a-absent", symbols)
            addresses = {line.split()[-1]: int(line.split()[0], 16)
                         for line in symbols.splitlines() if len(line.split()) >= 4}
            for symbol in ("atanf_tag", "sqrt_tag", "main"):
                self.assertIn(symbol, addresses)
            for symbol in ("atanf_tag", "sqrt_tag"):
                self.assertGreaterEqual(addresses[symbol], build.XIP_BASE + build.EXT_OFFSET)
                self.assertLess(addresses[symbol], build.XIP_BASE + build.EXT_OFFSET + size)
            self.assertFalse(build.XIP_BASE + build.EXT_OFFSET
                             <= addresses["main"] < build.XIP_BASE + build.EXT_OFFSET + size)

    def test_real_libm_contains_whitelist_members(self):
        """Read-only check that the linked multilib still names these members."""
        binaries = arm_bin()
        if not binaries:
            self.skipTest("ARM binutils are unavailable")
        libm = (binaries.parent / "arm-none-eabi/lib/v8-m.main/fpu/fpv5-sp-d16/libm.a")
        if not libm.is_file():
            libm = (build.DEFAULT_SDK.parents[2] /
                    "tools/ameba_d_asdk_toolchain/1.0.1/arm-none-eabi/lib/"
                    "v8-m.main/fpu/fpv5-sp-d16/libm.a")
        if not libm.is_file():
            self.skipTest("real libm.a is unavailable")
        listed = subprocess.run([str(binaries / "arm-none-eabi-ar.exe"), "t", str(libm)],
                                capture_output=True, text=True, check=True).stdout.split()
        for member, _symbol, _sections in build.ARDUBOY30_MATH_OBJECTS:
            self.assertIn(member, listed)


if __name__ == "__main__":
    unittest.main()
