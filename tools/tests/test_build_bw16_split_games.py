"""Host-only split build safety checks; no SDK/build/device commands."""
import importlib.util
import argparse
import contextlib
import fnmatch
import json
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
from unittest.mock import patch

SPEC = importlib.util.spec_from_file_location("split_games", Path(__file__).parents[1] / "build_bw16_split_games.py")
build = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(build)

# Read-only audit evidence, not a build:
# build/arduboy27-audio-final-20261116/audit-main2ext-20261117/q_assets.txt
AUDIT_RESOURCES = {
    ".rodata.cn_bitmap_pool": 15884,
    ".rodata.cn_glyph_index": 10260,
    ".rodata.cn_glyph_count": 2,
    ".rodata.asset_my_media": 3980,
    ".rodata._ZL7font5x7": 1280,
}
AUDIT_RESOURCE_TOTAL = 31406
# q_libm.txt: the 24 libm members left after the original five-member whitelist.
AUDIT_MATH_MEMBERS = {
    "lib_a-k_rem_pio2.o": ("__kernel_rem_pio2", 1772),
    "lib_a-ef_rem_pio2.o": ("__ieee754_rem_pio2f", 1520),
    "lib_a-e_rem_pio2.o": ("__ieee754_rem_pio2", 1388),
    "lib_a-kf_rem_pio2.o": ("__kernel_rem_pio2f", 1276),
    "lib_a-s_atan.o": ("atan", 908),
    "lib_a-k_cos.o": ("__kernel_cos", 460),
    "lib_a-e_atan2.o": ("__ieee754_atan2", 436),
    "lib_a-k_sin.o": ("__kernel_sin", 372),
    "lib_a-s_floor.o": ("floor", 264),
    "lib_a-s_scalbn.o": ("scalbn", 244),
    "lib_a-sf_scalbn.o": ("scalbnf", 192),
    "lib_a-kf_cos.o": ("__kernel_cosf", 188),
    "lib_a-s_sin.o": ("sin", 144),
    "lib_a-kf_sin.o": ("__kernel_sinf", 144),
    "lib_a-s_cos.o": ("cos", 136),
    "lib_a-sf_sin.o": ("sinf", 132),
    "lib_a-sf_cos.o": ("cosf", 128),
    "lib_a-s_copysign.o": ("copysign", 26),
    "lib_a-sf_copysign.o": ("copysignf", 24),
    "lib_a-s_fabs.o": ("fabs", 14),
    "lib_a-sf_fabs.o": ("fabsf", 14),
    "lib_a-sf_floor.o": ("floorf", 6),
    "lib_a-w_atan2.o": ("atan2", 4),
    "lib_a-s_matherr.o": ("matherr", 4),
}
AUDIT_MATH_TOTAL = 9796
AUDIT_LEGACY_MATH = (
    ("lib_a-sf_atan.o", "atanf", (".text*", ".rodata*")),
    ("lib_a-ef_atan2.o", "__ieee754_atan2f", (".text*",)),
    ("lib_a-wf_atan2.o", "atan2f", (".text*",)),
    ("lib_a-e_sqrt.o", "__ieee754_sqrt", (".text*",)),
    ("lib_a-w_sqrt.o", "sqrt", (".text*", ".rodata*")),
)
AUDIT_DIR = build.REPO / "build/arduboy27-audio-final-20261116/audit-main2ext-20261117"
AUDIT_MAP = AUDIT_DIR.parent / "compile/application.map"


def fake_tree(root):
    """Minimal repo/SDK layout for failure-injection tests; no embedded tools."""
    repo, sdk, tools, binaries = [root / x for x in ("repo", "sdk", "tools", "bin")]
    source = repo / "firmware/rtl8720dn/NanoUI"
    (source / "src").mkdir(parents=True)
    (source / "src/CrabitBuildProfile.h").write_text(
        (build.REPO / "firmware/rtl8720dn/NanoUI/src/CrabitBuildProfile.h").read_text(encoding="utf-8"), encoding="utf-8")
    (source / "ble_auth.local.example.h").write_text('#define CRABIT_BLE_AUTH_CODE "REPLACE"\n')
    shared = repo / "firmware/shared"
    for header in ("command_router.h", "comms_link.h", "topics.h", "weather_contract.h"):
        shared.mkdir(parents=True, exist_ok=True)
        (shared / header).write_text(f"// source of truth {header}\n", encoding="utf-8")
        (source / "src/crabit_shared").mkdir(parents=True, exist_ok=True)
        (source / "src/crabit_shared" / header).write_text(
            f'#include "../../../../shared/{header}"\n', encoding="utf-8")
    (repo / "tools").mkdir(parents=True)
    shutil.copy2(build.REPO / "tools/bw16_shared_headers.json",
                 repo / "tools/bw16_shared_headers.json")
    # The builder validates the one registry against the catalogue on every
    # run, including a build with no catalogue ports selected.
    (repo / "docs/nanoui").mkdir(parents=True)
    shutil.copy2(build.REPO / build.ARDUBOY30_CATALOG, repo / build.ARDUBOY30_CATALOG)
    (repo / build.ARDUBOY30_REGISTRY).parent.mkdir(parents=True)
    shutil.copy2(build.REPO / build.ARDUBOY30_REGISTRY, repo / build.ARDUBOY30_REGISTRY)
    (sdk / "variants/common_libs").mkdir(parents=True)
    archive = sdk / "variants/common_libs/lib_arduino.a"
    archive.write_bytes(b"archive")
    (sdk / "variants/common_libs/amebad_img2_is_arduino_std.ld").write_text("MEMORY\n{\n}\nSECTIONS\n{\n}\n")
    (sdk / "platform.txt").write_text("recipe.c.combine.pattern={runtime.platform.path}/variants/common_libs/{build.ldscript}\n")
    tools.mkdir()
    (tools / "postbuild_img2_arduino_windows.exe").write_bytes(b"fake")
    binaries.mkdir()
    for name in ("nm", "objcopy", "objdump"):
        (binaries / f"arm-none-eabi-{name}.exe").write_bytes(b"fake")
    return repo, sdk, tools, binaries, shared


class SplitGamesTests(unittest.TestCase):
    def test_arduboy30_selection_only_enables_ids(self):
        header = build.arduboy30_header([
            {"id": "pong", "app_class": "PongApp", "menu_label": "Pong"},
            {"id": "jumpboy", "app_class": "JumpBoyApp", "menu_label": "JumpBoy"},
        ])
        self.assertIn("#define CRABIT_ARDUBOY30_COUNT 2", header)
        self.assertIn("#define CRABIT_ARDUBOY30_ENABLE_PONG 1", header)
        self.assertIn("#define CRABIT_ARDUBOY30_ENABLE_JUMPBOY 1", header)
        self.assertNotIn("PongApp", header)
        self.assertNotIn('"Pong"', header)
        self.assertNotIn("CRABIT_ARDUBOY30_ENTRIES", header)
        empty = build.arduboy30_header([])
        self.assertIn("#define CRABIT_ARDUBOY30_COUNT 0", empty)
        self.assertNotIn("ENABLE_", empty)
        with self.assertRaisesRegex(ValueError, "collides after macro conversion"):
            build.arduboy30_header([
                {"id": "a-b", "app_class": "ABApp", "menu_label": "AB"},
                {"id": "a_b", "app_class": "AB2App", "menu_label": "AB2"},
            ])

    def test_registry_entries_are_checked_for_selected_ports(self):
        with tempfile.TemporaryDirectory() as tmp:
            repo = Path(tmp)
            folder = repo / "firmware/rtl8720dn/NanoUI/app/apps/games/pong"
            folder.mkdir(parents=True)
            (folder / "app.h").write_text("class PongApp {};\n", encoding="utf-8")
            (folder / "README.md").write_text("# Pong\n", encoding="utf-8")
            license_text = b"MIT\n"
            (folder / "LICENSE").write_bytes(license_text)
            registry = repo / build.ARDUBOY30_REGISTRY
            registry.parent.mkdir(parents=True, exist_ok=True)
            registry.write_text(
                '#include "apps/games/pong/app.h"\n'
                '#if defined(CRABIT_ARDUBOY30_ENABLE_PONG)\n'
                '    entryFor<PongApp>("pong", "Pong", Category::CasualRhythm),\n#endif\n',
                encoding="utf-8")
            game = {"id": "pong", "app_class": "PongApp", "menu_label": "Pong",
                    "source": {"license_sha256": build.license_sha256(license_text)}}
            self.assertEqual(build.arduboy30_paths(repo, [game])[0], folder / "app.h")
            _text, entries = build.load_arduboy30_registry_entries(repo)
            self.assertEqual(entries["pong"],
                             build.RegistryEntry("Pong", "PongApp", "CasualRhythm", False))
            legacy = build.load_arduboy30_registry(repo)[1]
            self.assertEqual(legacy, {"pong": ("Pong", "PongApp")})
            # A built-in game is never selectable through --arduboy30.
            registry.write_text(
                '#include "apps/games/hollow/app.h"\n'
                '    builtInEntryFor<HollowSeekerApp>("hollow", "Hollow Seeker", '
                "Category::JumpAction),\n", encoding="utf-8")
            built_in = dict(game, id="hollow", app_class="HollowSeekerApp",
                            menu_label="Hollow Seeker")
            with self.assertRaisesRegex(ValueError, "not registered as selectable"):
                build.arduboy30_paths(repo, [built_in])
            registry.write_text("", encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "no entries"):
                build.load_arduboy30_registry(repo)
            registry.write_text(
                '#include "apps/games/pong/app.h"\n'
                '#include "apps/games/other/app.h"\n'
                '#if defined(CRABIT_ARDUBOY30_ENABLE_OTHER)\n'
                '    entryFor<OtherApp>("other", "Other", Category::CasualRhythm),\n#endif\n',
                encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "not registered"):
                build.arduboy30_paths(repo, [game])
            for row, error in (
                    ('#include "apps/games/pong/app.h"\n'
                     '    entryFor<PongApp>("pong", "Pong", Category::CasualRhythm),\n',
                     "lacks its enable macro"),
                    ('#if defined(CRABIT_ARDUBOY30_ENABLE_PONG)\n'
                     '    entryFor<PongApp>("pong", "Pong", Category::CasualRhythm),\n'
                     "#endif\n", "lacks its app include"),
                    ('#include "apps/games/pong/app.h"\n'
                     '#if defined(CRABIT_ARDUBOY30_ENABLE_PONG)\n'
                     '    entryFor<PongApp>("pong", "Pong", Category::Platformer),\n'
                     "#endif\n", "category is unknown"),
                    ('#include "apps/games/pong/app.h"\n'
                     '    builtInEntryFor<PongApp>("pong", "Pong", '
                     "Category::CasualRhythm),\n", "does not match its id")):
                registry.write_text(row, encoding="utf-8")
                with self.assertRaisesRegex(ValueError, error):
                    build.arduboy30_paths(repo, [game])
            registry.write_text(
                '#include "apps/games/pong/app.h"\n'
                '#if defined(CRABIT_ARDUBOY30_ENABLE_PONG)\n'
                '    entryFor<WrongApp>("pong", "Pong", Category::CasualRhythm),\n#endif\n',
                encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "app class mismatch"):
                build.arduboy30_paths(repo, [game])

    def test_bounds(self):
        for size in (1, 0xF1000):
            build.check_extension(size)
        for size in (0, -1, 0xF1001):
            with self.assertRaises(ValueError):
                build.check_extension(size)
        with self.assertRaises(ValueError):
            build.check_extension(16, 0x08106000)

    def test_selectors_preserved_extended_and_guarded(self):
        self.assertEqual(build.selectors([]), build.DEFAULT_SELECTORS)
        names = build.selectors(["NewGame", "foo::Bar", "NewGame"])
        self.assertEqual(names[-2:], ("NewGame", "foo::Bar"))
        text = build.split_linker("MEMORY\n{\n}\nSECTIONS\n{\n}\n", names)
        for name in (*build.DEFAULT_SELECTORS, "NewGame", "3foo3Bar"):
            self.assertIn(f".text.*{name}* .rodata.*{name}*", text)
        self.assertIn("ORIGIN = 0x08107000, LENGTH = 0xF1000", text)
        for name in ("", "*", "foo.bar", "Foo); evil", "a/b", "3Bad"):
            with self.assertRaises(ValueError):
                build.selectors([name])
        with self.assertRaises(ValueError):
            build.split_linker("changed", names)

    def test_support_selectors_are_always_placed_but_optional(self):
        # The category menu and its registry table exist in every ports image,
        # so their narrow placements are always present and never broadened.
        self.assertEqual(build.arduboy30_support_selectors(),
                         build.ARDUBOY30_SUPPORT_SELECTORS)
        support = build.arduboy30_support_selectors()
        for name in support:
            self.assertNotIn(name, build.DEFAULT_SELECTORS)
        # Every expected compat/registry name is a real selector token.
        for expected in ("SpritesCompat", "CrabitArduboy::detail",
                          "arduboyFont5x7", "Arduboy30Registry", "CrabitArduboyAudio"):
            self.assertIn(expected, support)
        # The real mangled names must be caught by the generated wildcards.
        text = build.split_linker("MEMORY\n{\n}\nSECTIONS\n{\n}\n",
                                  (*build.DEFAULT_SELECTORS, *support))
        for pattern in ("*(.text.*SpritesCompat* .rodata.*SpritesCompat*)",
                        "*(.text.*13CrabitArduboy6detail* .rodata.*13CrabitArduboy6detail*)",
                        "*(.text.*arduboyFont5x7* .rodata.*arduboyFont5x7*)",
                         "*(.text.*Arduboy30Registry* .rodata.*Arduboy30Registry*)",
                         "*(.text.*CrabitArduboyAudio* .rodata.*CrabitArduboyAudio*)"):
            self.assertIn(pattern, text)
        # Mutable port state and unwind tables are never placed in flash.
        self.assertNotIn(".bss", text)
        self.assertNotIn(".data.*", text)
        self.assertNotIn(".ARM.exidx", text)
        self.assertNotIn(".ARM.extab", text)

    def test_support_symbols_are_optional_but_never_left_in_main(self):
        ext = "08107000 00000106 W SpritesCompat::draw(short, short, unsigned char const*, unsigned char, unsigned char, bool)\n"
        # Absence is allowed: not every selection emits every helper.
        build.check_symbols("", (), 32, optional=("SpritesCompat",))
        # Present in the extension is fine.
        build.check_symbols(ext, (), 0x200, optional=("SpritesCompat",))
        # Present in the main image is a failure, whatever its size.
        for line in ("0e00d7a0 00000106 W SpritesCompat::draw(short, short, unsigned char const*, unsigned char, unsigned char, bool)",
                     "0e08ff94 00000108 V Arduboy30Registry::entries(unsigned int&)::table",
                     "0e0913a8 000001e0 t arduboyFont5x7()::font",
                     "0e00d8ac 00000058 W CrabitArduboy::detail::runtime()"):
            with self.assertRaisesRegex(ValueError, "stayed in main"):
                build.check_symbols(line, (), 0x200,
                                    optional=("SpritesCompat", "Arduboy30Registry",
                                              "arduboyFont5x7", "CrabitArduboy::detail"))
        # A support name passed as a required selector is still required.
        with self.assertRaisesRegex(ValueError, "No real extension symbol"):
            build.check_symbols("", ("SpritesCompat",), 0x200)

    def test_support_wildcards_match_real_sections_and_nothing_else(self):
        """Section names below are copied from the retry2 link map (no SDK build).

        They are the complete Arduboy support residue in .xip_image2.text:
        build/arduboy21-ondemand-retry2-20261116/compile/application.map
        """
        moved = {
            ".text._ZN13SpritesCompat12drawPlusMaskEssPKhh": 264,
            ".text._ZN13SpritesCompat4drawEssPKhhhb": 268,
            ".text._ZN13CrabitArduboy6detail7runtimeEv": 88,
            ".text._ZN13CrabitArduboy6detail8allocateEj": 60,
            ".text._ZN13CrabitArduboy6detail7releaseEPvj": 52,
            ".text._ZN13CrabitArduboy6detail15platformReleaseEPv": 4,
            ".text._ZN13CrabitArduboy6detail16platformAllocateEj": 4,
            ".rodata._ZZN17Arduboy30Registry7entriesERjE5table": 264,
            ".rodata._ZZL14arduboyFont5x7vE4font": 480,
        }
        kept = (
            # NanoUI menu font, mutable port runtime state and unwind tables.
            ".rodata._ZL7font5x7",
            ".bss._ZZN13CrabitArduboy6detail7runtimeEvE8instance",
            ".bss._ZGVZN13CrabitArduboy6detail7runtimeEvE8instance",
            ".ARM.exidx.text._ZN13CrabitArduboy6detail7runtimeEv",
            ".rodata.str1.4",
            ".text._Z19buildModernAnimMenuPKhSt6vectorIPKcSaIS3_EES1_IPFvvESaIS7_EE",
        )
        patterns = []
        for name in build.arduboy30_support_selectors():
            token = name if "::" not in name else "".join(
                str(len(part)) + part for part in name.split("::"))
            patterns += [".text.*" + token + "*", ".rodata.*" + token + "*"]
        for section in moved:
            self.assertTrue(any(fnmatch.fnmatchcase(section, p) for p in patterns), section)
        for section in kept:
            self.assertFalse(any(fnmatch.fnmatchcase(section, p) for p in patterns), section)
        self.assertEqual(sum(moved.values()), 1484)

    def test_support_guard_allows_ram_state_but_not_main_readonly(self):
        # Retry3 evidence: the port runtime's lazy singleton and its guard are
        # weak objects in .ram_image2.bss (0x10024318/0x1002431c), not rodata.
        ram_nm = ("10024318 00000004 V guard variable for CrabitArduboy::detail::runtime()::instance\n"
                  "1002431c 00000078 V CrabitArduboy::detail::runtime()::instance\n")
        ram_t = ("10024318  w    O .ram_image2.bss\t00000004 guard variable for CrabitArduboy::detail::runtime()::instance\n"
                 "1002431c  w    O .ram_image2.bss\t00000078 CrabitArduboy::detail::runtime()::instance\n")
        build.check_symbols(ram_nm, (), 0x200, objects=ram_t,
                            optional=("CrabitArduboy::detail",))
        # Every RAM section objdump can report is equally mutable state.
        for section in (".ram_image2.data", ".ram_image2.nocache.data", ".ram_heap.data"):
            build.check_symbols(
                "1002431c 00000078 V CrabitArduboy::detail::runtime()::instance\n", (), 0x200,
                objects=f"1002431c  w    O {section}\t00000078 CrabitArduboy::detail::runtime()::instance\n",
                optional=("CrabitArduboy::detail",))
        # Readonly weak data left in the main xip image still fails.
        with self.assertRaisesRegex(ValueError, "stayed in main"):
            build.check_symbols(
                "0e08ff94 00000108 V Arduboy30Registry::entries(unsigned int&)::table\n", (), 0x200,
                objects="0e08ff94  w    O .xip_image2.text\t00000108 Arduboy30Registry::entries(unsigned int&)::table\n",
                optional=("Arduboy30Registry",))
        # Weak readonly data actually in the extension is the expected result.
        build.check_symbols(
            "08107100 00000108 V Arduboy30Registry::entries(unsigned int&)::table\n", (), 0x400,
            objects="08107100  w    O .extflash\t00000108 Arduboy30Registry::entries(unsigned int&)::table\n",
            optional=("Arduboy30Registry",))
        # Fail closed: a RAM-looking nm line without objdump evidence is rejected.
        with self.assertRaisesRegex(ValueError, "stayed in main"):
            build.check_symbols(ram_nm, (), 0x200, optional=("CrabitArduboy::detail",))
        # Code is never excused, even if a section lookup claims RAM.
        with self.assertRaisesRegex(ValueError, "stayed in main"):
            build.check_symbols(
                "0e00d8ac 00000058 W CrabitArduboy::detail::runtime()\n", (), 0x200,
                objects="0e00d8ac  w    F .ram_image2.bss\t00000058 CrabitArduboy::detail::runtime()\n",
                optional=("CrabitArduboy::detail",))
        # Required selectors keep the strict "must be in .extflash" semantics.
        with self.assertRaisesRegex(ValueError, "No real extension symbol"):
            build.check_symbols(ram_nm, ("CrabitArduboy::detail",), 0x200, objects=ram_t)

    def test_selected_ports_write_support_selectors_into_linker(self):
        with tempfile.TemporaryDirectory() as tmp:
            repo, sdk, tools, binaries, _shared = fake_tree(Path(tmp))
            output = repo / "build/with-ports"
            args = argparse.Namespace(sdk=sdk, toolchain=binaries, tools=tools, output=output,
                                      game_namespace=[], pwsh="mock-pwsh",
                                      arduino_cli="mock-cli", arduboy30=["pong"])

            def injected(command, log, *, cwd=None):
                raise RuntimeError("stop after staging")

            with patch.object(build, "REPO", repo), \
                 patch.object(build, "arduboy30_selection",
                              return_value=[{"id": "pong", "namespace": "PongSandbox",
                                             "app_class": "PongApp"}]), \
                 patch.object(build, "arduboy30_paths", return_value=[]), \
                 patch.object(build.shutil, "which", return_value="mock-pwsh"), \
                 patch.object(build, "run", side_effect=injected):
                with self.assertRaisesRegex(RuntimeError, "stop after staging"):
                    build.build(args)
            report = json.loads((output / "report.json").read_text())
            self.assertEqual(report["support_selectors"], list(build.ARDUBOY30_SUPPORT_SELECTORS))
            self.assertEqual(report["selectors"][-len(build.ARDUBOY30_SUPPORT_SELECTORS):],
                             list(build.ARDUBOY30_SUPPORT_SELECTORS))
            self.assertEqual(report["failed_stage"], "compile")
            linker = (output / "split-games.ld").read_text()
            for name in build.ARDUBOY30_SUPPORT_SELECTORS:
                token = name if "::" not in name else "".join(
                    str(len(part)) + part for part in name.split("::"))
                self.assertIn(f".text.*{token}* .rodata.*{token}*", linker)
            self.assertIn("LENGTH = 0xF1000", linker)
            self.assertIn('ASSERT(SIZEOF(.extflash) <= LENGTH(EXTFLASH)', linker)

    def test_symbols_must_exist_inside_extension(self):
        text = "08107000 00000010 t NewGame::tick()\r\n08107010 00000008 r vtable for NewApp\r\n"
        build.check_symbols(text, ("NewGame", "NewApp"), 32)
        for name in ("Missing", "Game"):
            with self.assertRaises(ValueError):
                build.check_symbols(text, (name,), 32)
        for bad in ("0e007000 00000010 t NewGame::tick()", "08107018 00000010 t NewGame::tick()", "08107000 00000000 t NewGame::tick()"):
            with self.assertRaises(ValueError):
                build.check_symbols(bad, ("NewGame",), 32)

    def test_game_requires_code_and_readonly_assets_and_weak_symbols_count(self):
        code = "08107000 00000008 W NewGame::tick()\n"
        assets = "08107008 00000008 r NewGame::sprites\n"
        build.check_symbols(code + assets, ("NewGame",), 16, asset_names=("NewGame",))
        for text in (code, assets, code + assets.replace("08107008", "0e007008")):
            with self.assertRaises(ValueError):
                build.check_symbols(text, ("NewGame",), 16, asset_names=("NewGame",))
        build.check_symbols("08107000 00000008 V vtable for NewApp", ("NewApp",), 16)
        namespace = build.GAME_NAMESPACES[-1]
        with self.assertRaises(ValueError):
            build.check_symbols(f"08107000 00000008 W {namespace}::tick()\n08107008 00000008 V vtable for {namespace}::App",
                                (namespace,), 16, asset_names=(namespace,))

    def test_section_parser(self):
        self.assertEqual(build.parse_sections(" 0 .extflash 00000020 08107000 08107000 00027000 2**3\r\n CONTENTS"),
                          {".extflash": (32, 0x08107000, 0x08107000)})

    def test_code_section_assets_require_elf_object_type(self):
        text = "08107000 00000008 T NewGame::tick()\n08107008 00000008 t NewGame::pixels\n"
        objects = "08107008 l     O .extflash\t00000008 NewGame::pixels\n"
        build.check_symbols(text, ("NewGame",), 16, asset_names=("NewGame",), objects=objects)
        for wrong in ("", objects.replace("O .extflash", "F .extflash"),
                      objects.replace("08107008", "0e007008")):
            with self.assertRaises(ValueError):
                build.check_symbols(text, ("NewGame",), 16, asset_names=("NewGame",), objects=wrong)

    def test_auth_only_from_template(self):
        template = '#pragma once\n#define CRABIT_BLE_AUTH_CODE "REPLACE"\n'
        self.assertIn('#define CRABIT_BLE_AUTH_CODE "000000"', build.public_header(template))
        for text in ("", template * 2, template.replace("REPLACE", "000000")):
            with self.assertRaises(ValueError):
                build.public_header(text)

    def test_profile_is_copy_only(self):
        path = build.REPO / "firmware/rtl8720dn/NanoUI/src/CrabitBuildProfile.h"
        original = path.read_text(encoding="utf-8")
        result = build.experimental_profile(original)
        self.assertIn("#define CRABIT_BW16_NETWORK_GAME 0", result)
        self.assertIn("#define CRABIT_BW16_ARDUBOY_PORTS 1", result)
        self.assertEqual(result.replace("\n#define CRABIT_BW16_ARDUBOY_PORTS 1\n#define CRABIT_BW16_NETWORK_GAME 0", "", 1), original)
        self.assertEqual(path.read_text(encoding="utf-8"), original)
        with self.assertRaises(ValueError):
            build.experimental_profile("changed")

    def test_output_path_safety(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            repo = root / "repo"
            repo.mkdir()
            sdk = root / "sdk"
            sdk.mkdir()
            safe = repo / "build/fresh"
            self.assertEqual(build.validate_output(safe, repo, [sdk]), safe.resolve())
            for path in (repo, root, repo / "tools/new", sdk / "new", root / "sdk"):
                with self.assertRaises(ValueError):
                    build.validate_output(path, repo, [sdk])
            safe.mkdir(parents=True)
            with self.assertRaises(ValueError):
                build.validate_output(safe, repo, [sdk])

    def test_copy_never_opens_auth_or_backups(self):
        with tempfile.TemporaryDirectory() as tmp:
            source, target = Path(tmp) / "source", Path(tmp) / "target"
            source.mkdir()
            for name in ("ble_auth.local.h", "BLE_AUTH.LOCAL.H", "font.bak", "good.cpp"):
                (source / name).touch()
            (source / "build-old").mkdir()
            (source / "nested").mkdir()
            (source / "nested/ble_auth.local.h").touch()
            import builtins
            forbidden = {str(p) for p in source.rglob("*") if p.is_file() and p.name != "good.cpp"}
            real_open = builtins.open
            def guarded_open(file, *args, **kwargs):
                if str(file) in forbidden:
                    raise AssertionError("Forbidden input opened")
                return real_open(file, *args, **kwargs)
            with patch.object(builtins, "open", side_effect=guarded_open):
                build.copy_clean(source, target)
            self.assertEqual(sorted(p.relative_to(target).as_posix() for p in target.rglob("*")), ["good.cpp", "nested"])

    def test_stock_copy_excludes_generated_not_dependencies(self):
        with tempfile.TemporaryDirectory() as tmp:
            source, target = Path(tmp) / "tools", Path(tmp) / "fresh"
            (source / "bsp/image/PMU_bins/NONE").mkdir(parents=True)
            for name in ("application.axf", "application.map", "km0_km4_image2.bin", "ram_2.bin", "postbuild_img2_arduino_windows.exe",
                         "bsp/image/km0_image2_all.bin", "bsp/image/PMU_bins/NONE/km0_image2_all.bin"):
                (source / name).touch()
            build.copy_clean(source, target, stock_tools=True)
            self.assertFalse((target / "application.axf").exists())
            self.assertFalse((target / "bsp/image/km0_image2_all.bin").exists())
            self.assertTrue((target / "bsp/image/PMU_bins/NONE/km0_image2_all.bin").exists())
            self.assertTrue((target / "postbuild_img2_arduino_windows.exe").exists())

    def test_zero_exit_failure_logs_rejected(self):
        with tempfile.TemporaryDirectory() as tmp:
            log = Path(tmp) / "command.log"
            for text in (b"The system cannot find the file specified", b"Invalid number of parameters", b"fatal error: missing", b"Access is denied"):
                with patch.object(build.subprocess, "run", return_value=subprocess.CompletedProcess([], 0, text)):
                    with self.assertRaises(RuntimeError):
                        build.run(["never-executed"], log)
                    self.assertEqual(log.read_bytes(), text)
            with patch.object(build.subprocess, "run", return_value=subprocess.CompletedProcess([], 0, b"1 file(s) copied.\nis_law = 1")):
                build.run(["never-executed"], log)

    def test_compile_evidence_survives_symbol_failure(self):
        with tempfile.TemporaryDirectory() as tmp:
            repo, sdk, tools, binaries, _shared = fake_tree(Path(tmp))
            output = repo / "build/symbol-fail"
            args = argparse.Namespace(sdk=sdk, toolchain=binaries, tools=tools, output=output,
                                      game_namespace=[], pwsh="mock-pwsh",
                                      arduino_cli="mock-cli", arduboy30=[])

            def injected(command, log, *, cwd=None):
                name = Path(command[0]).name
                if command[0] == "mock-cli":
                    elf = output / "compile/application.axf"
                    elf.parent.mkdir(parents=True, exist_ok=True)
                    elf.write_bytes(b"\x7fELF" + b"\0" * 60)
                    return ""
                if name == "arm-none-eabi-objdump.exe" and log.name == "sections.log":
                    return " 0 .extflash 00000020 08107000 08107000 00027000 2**4\n"
                if name == "arm-none-eabi-nm.exe":
                    raise RuntimeError("simulated symbol failure")
                return ""

            with patch.object(build, "REPO", repo), \
                 patch.object(build.shutil, "which", return_value="mock-pwsh"), \
                 patch.object(build, "run", side_effect=injected):
                with self.assertRaisesRegex(RuntimeError, "simulated symbol failure"):
                    build.build(args)
            report = json.loads((output / "report.json").read_text())
            self.assertEqual(report["compile_link"], "PASS")
            self.assertEqual(report["extension_checks"], "PASS")
            self.assertEqual(report["sections"][".extflash"]["bytes"], 32)
            self.assertEqual(report["failed_stage"], "symbols")
            self.assertIn("simulated symbol failure", report["failure"])
            self.assertNotIn("symbol_checks", report)
            self.assertNotIn("main_only_extract", report)
            self.assertNotIn("fresh_stock_pack", report)
            self.assertEqual(report["status"], "FAIL")
            self.assertTrue(report["original_sdk_archive_unchanged"])

    def test_archive_hash_finally_on_failure(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            repo, sdk, tools, binaries, shared = fake_tree(root)
            archive = sdk / "variants/common_libs/lib_arduino.a"
            for mutate in (False, True):
                output = repo / "build" / str(mutate)
                args = argparse.Namespace(sdk=sdk, toolchain=binaries, tools=tools, output=output,
                                          game_namespace=[], pwsh="mock-pwsh", arduino_cli="mock-cli")
                def fail(*args, **kwargs):
                    if mutate:
                        archive.write_bytes(b"changed")
                    raise RuntimeError("simulated compile failure; no command executed")
                with patch.object(build, "REPO", repo), patch.object(build.shutil, "which", return_value="mock-pwsh"), patch.object(build, "run", side_effect=fail):
                    with self.assertRaises(RuntimeError):
                        build.build(args)
                report = json.loads((output / "report.json").read_text())
                self.assertEqual(report["original_sdk_archive_unchanged"], not mutate)
                self.assertFalse(report["flashable"])
                self.assertEqual(report["hardware"], "NOT TESTED")
                self.assertEqual(report["status"], "FAIL")
                # Staging itself must already be manifest-driven and byte-exact.
                for header in ("command_router.h", "comms_link.h", "topics.h", "weather_contract.h"):
                    staged = output / "NanoUI/src/crabit_shared" / header
                    self.assertEqual(staged.read_bytes(), (shared / header).read_bytes())


class ResourceAndMathMigrationTests(unittest.TestCase):
    """Exact resource sections and the audited libm member whitelist."""

    def test_resource_table_is_exact_and_totals_31406(self):
        self.assertEqual({section for _symbol, section in build.RESOURCE_SECTIONS},
                         set(AUDIT_RESOURCES))
        self.assertEqual(sum(AUDIT_RESOURCES.values()), AUDIT_RESOURCE_TOTAL)
        patterns = build.resource_patterns()
        self.assertEqual(patterns, tuple(f"*({section})" for section in AUDIT_RESOURCES))
        for pattern in patterns:
            # Whole-name input-section selector: no glob, no code, no writable data.
            self.assertTrue(pattern.startswith("*(.rodata."), pattern)
            self.assertTrue(pattern.endswith(")"), pattern)
            self.assertEqual(pattern.count("*"), 1, pattern)
            self.assertNotIn(".text", pattern)
            self.assertNotIn(".data", pattern)
            self.assertNotIn(".bss", pattern)

    def test_resource_selectors_never_capture_game_fonts_or_other_assets(self):
        # Strip the linker's *( ... ) wrapper; the inside is the input-section glob.
        patterns = [pattern[2:-1] for pattern in build.resource_patterns()]
        moved = (".rodata.cn_bitmap_pool", ".rodata.cn_glyph_index",
                 ".rodata.cn_glyph_count", ".rodata.asset_my_media",
                 ".rodata._ZL7font5x7")
        kept = (
            # arduboyFont5x7()::font - the substring risk this table exists for.
            ".rodata._ZZL14arduboyFont5x7vE4font",
            ".rodata._ZZN17Arduboy30Registry7entriesERjE5table",
            ".rodata._ZL16icon_hacker_main",
            ".rodata._ZL18DEEP_SCAN_CHANNELS",
            ".rodata.str1.4",
            ".text._ZL7font5x7",
            ".bss._ZL7font5x7",
        )
        for section in moved:
            self.assertTrue(any(fnmatch.fnmatchcase(section, p) for p in patterns), section)
        for section in kept:
            self.assertFalse(any(fnmatch.fnmatchcase(section, p) for p in patterns), section)
        # A coarse token would blur a resource into its neighbours; exact input
        # section names keep them apart (and never touch arduboyFont5x7).
        self.assertTrue(fnmatch.fnmatchcase(".rodata._ZL7font5x7", ".rodata.*5x7*"))
        self.assertTrue(fnmatch.fnmatchcase(".rodata._ZZL14arduboyFont5x7vE4font",
                                            ".rodata.*5x7*"))
        self.assertTrue(fnmatch.fnmatchcase(".rodata.cn_glyph_count",
                                            ".rodata.*cn_glyph*"))
        self.assertTrue(fnmatch.fnmatchcase(".rodata.cn_glyph_index",
                                            ".rodata.*cn_glyph*"))
        self.assertNotIn("font5x7", build.selectors([]))
        self.assertNotIn("font5x7", build.arduboy30_support_selectors())
        self.assertNotIn("font5x7", build.DEFAULT_SELECTORS)
        for bad in ((".rodata.font5x7*",), (".rodata.*font*",), (".data.font5x7",),
                    (".text.font5x7",), (".rodata.",), ("..\\font5x7",)):
            with self.assertRaises(ValueError):
                build.resource_placements(
                    (("font5x7", section) for section in bad))
        with self.assertRaises(ValueError):
            build.resource_placements((("cn_bitmap_pool", ".rodata.font5x7"),
                                       ("cn_bitmap_pool", ".rodata.other")))
        with self.assertRaises(ValueError):
            build.resource_placements((("bad name", ".rodata.font5x7"),))

    def test_resource_guard_requires_readonly_extension_object(self):
        section = ".rodata.cn_bitmap_pool"
        nm = "08107000 00003e0c t cn_bitmap_pool\n"
        types = "08107000 l     O .extflash\t00003e0c cn_bitmap_pool\n"
        placed = build.check_resources(nm, types, 0x10000)
        self.assertEqual(placed, [{"symbol": "cn_bitmap_pool", "input_section": section,
                                   "output_section": ".extflash", "address": "0x8107000",
                                   "bytes": 15884}])
        # Truly absent (no nm record at all) is allowed: reduced builds may not
        # compile a resource, and the exact section selector then matches nothing.
        self.assertEqual(build.check_resources("", "", 0x10000), [])
        self.assertEqual(build.check_resources(
            "08107000 00000010 t other_asset\n", "", 0x10000), [])
        # Emitted with no bytes is NOT absence: it is a broken resource.
        for text, objects in (
                ("08107000 00000000 t cn_bitmap_pool\n", ""),
                ("08107000 00000000 t cn_bitmap_pool\n", types),
                (nm, types.replace("00003e0c", "00000000"))):
            with self.assertRaisesRegex(ValueError, "without a real read-only object"):
                build.check_resources(text, objects, 0x10000)
        # Anything still in main fails, whatever the evidence around it.
        with self.assertRaisesRegex(ValueError, "stayed in main"):
            build.check_resources(
                "0e0924a0 00003e0c t cn_bitmap_pool\n",
                "0e0924a0 l     O .xip_image2.text\t00003e0c cn_bitmap_pool\n", 0x10000)
        with self.assertRaisesRegex(ValueError, "stayed in main"):
            build.check_resources(nm + "0e0924a0 00003e0c t cn_bitmap_pool\n",
                                  types + "0e0924a0 l     O .xip_image2.text\t00003e0c cn_bitmap_pool\n",
                                  0x10000)
        with self.assertRaisesRegex(ValueError, "stayed in main"):
            build.check_resources("0e0924a0 00003e0c t cn_bitmap_pool\n", types, 0x10000)
        # Outside the declared extension window is not placement evidence.
        with self.assertRaisesRegex(ValueError, "stayed in main"):
            build.check_resources(nm, types, 0x1000)
        # Inside .extflash but the STT_OBJECT must agree on address and length.
        for objects in ("", types.replace("O .extflash", "F .extflash"),
                        types.replace("08107000", "08108000"),
                        types.replace("00003e0c", "00003e00")):
            with self.assertRaisesRegex(ValueError, "without a real read-only object"):
                build.check_resources(nm, objects, 0x10000)

    def _staged_report(self, root, arduboy30):
        repo, sdk, tools, binaries, _shared = fake_tree(Path(root))
        output = repo / "build" / (arduboy30[0] if arduboy30 else "default6")
        args = argparse.Namespace(sdk=sdk, toolchain=binaries, tools=tools, output=output,
                                  game_namespace=[], pwsh="mock-pwsh", arduino_cli="mock-cli",
                                  arduboy30=list(arduboy30))

        def injected(command, log, *, cwd=None):
            raise RuntimeError("stop after staging")

        with contextlib.ExitStack() as stack:
            stack.enter_context(patch.object(build, "REPO", repo))
            stack.enter_context(patch.object(build.shutil, "which", return_value="mock-pwsh"))
            stack.enter_context(patch.object(build, "run", side_effect=injected))
            stack.enter_context(patch.object(
                build, "arduboy30_selection",
                return_value=[{"id": "pong", "namespace": "PongSandbox",
                               "app_class": "PongApp"}] if arduboy30 else []))
            stack.enter_context(patch.object(build, "arduboy30_paths", return_value=[]))
            with self.assertRaisesRegex(RuntimeError, "stop after staging"):
                build.build(args)
        return json.loads((output / "report.json").read_text()), (output / "split-games.ld").read_text()

    def test_resources_are_placed_in_default_six_and_selected_profiles(self):
        # The generic class selectors are unchanged when no resource table is given.
        plain = build.split_linker("MEMORY\n{\n}\nSECTIONS\n{\n}\n", ("Foo",))
        self.assertNotIn(".rodata.cn_bitmap_pool", plain)
        for selection in ([], ["pong"]):
            with tempfile.TemporaryDirectory() as tmp:
                report, linker = self._staged_report(tmp, selection)
                for pattern in build.resource_patterns():
                    self.assertIn(pattern, linker)
                self.assertEqual([item["input_section"] for item in report["resource_sections"]],
                                 list(AUDIT_RESOURCES))
                # Resources are not reported as game namespaces.
                for item in report["resource_sections"]:
                    self.assertNotIn(item["symbol"], report["selectors"])
                self.assertEqual(report["failed_stage"], "compile")
                # Math members still follow the catalogue-selection gate.
                self.assertNotIn("arduboy30_math_symbols", report)

    def test_math_whitelist_is_the_audited_twenty_nine_members(self):
        table = build.ARDUBOY30_MATH_OBJECTS
        self.assertEqual(table[:len(AUDIT_LEGACY_MATH)], AUDIT_LEGACY_MATH)
        self.assertEqual(len(table), len(AUDIT_LEGACY_MATH) + len(AUDIT_MATH_MEMBERS))
        members = [member for member, _symbol, _sections in table]
        symbols = [symbol for _member, symbol, _sections in table]
        self.assertEqual(len(set(members)), len(members))
        self.assertEqual(len(set(symbols)), len(symbols))
        new = {member: (symbol, sections) for member, symbol, sections in table[len(AUDIT_LEGACY_MATH):]}
        self.assertEqual(set(new), set(AUDIT_MATH_MEMBERS))
        for member, (symbol, sections) in new.items():
            self.assertEqual(symbol, AUDIT_MATH_MEMBERS[member][0], member)
            # Read-only sections only; .data/.bss of every member is empty.
            self.assertEqual(sections, (".text*", ".rodata*"), member)
            self.assertFalse(symbol.startswith(("printf", "std::")), symbol)
        self.assertEqual(sum(bytes for _symbol, bytes in AUDIT_MATH_MEMBERS.values()),
                         AUDIT_MATH_TOTAL)
        # The five table-bearing members keep their .rodata tables moving too.
        for member in ("lib_a-k_rem_pio2.o", "lib_a-ef_rem_pio2.o", "lib_a-e_rem_pio2.o",
                       "lib_a-kf_rem_pio2.o", "lib_a-s_atan.o"):
            self.assertIn(".rodata*", new[member][1])

    def test_math_patterns_are_exact_members_and_tolerate_absence(self):
        patterns = build.arduboy30_math_patterns(build.ARDUBOY30_MATH_OBJECTS)
        self.assertEqual(len(patterns), len(build.ARDUBOY30_MATH_OBJECTS))
        for pattern, (member, _symbol, sections) in zip(patterns, build.ARDUBOY30_MATH_OBJECTS):
            self.assertEqual(pattern, f"*libm.a:{member}({' '.join(sections)})")
            self.assertNotIn(",", pattern)
            self.assertNotIn("libm.a:*(", pattern)
        text = build.split_linker("MEMORY\n{\n}\nSECTIONS\n{\n}\n",
                                  ("Foo",), build.ARDUBOY30_MATH_OBJECTS)
        self.assertNotIn("--whole-archive", text)
        self.assertNotIn("*sqrt*", text)
        self.assertIn("*libm.a:lib_a-s_matherr.o(.text* .rodata*)", text)
        # Absent members are never required: an empty image passes unchanged.
        self.assertEqual(build.check_arduboy30_math("", 0x1000, build.ARDUBOY30_MATH_OBJECTS), [])
        good = "08107010 00000010 T floor\n08107020 00000100 T __kernel_rem_pio2\n"
        self.assertEqual(build.check_arduboy30_math(good, 0x1000, build.ARDUBOY30_MATH_OBJECTS),
                         ["__kernel_rem_pio2", "floor"])
        with self.assertRaisesRegex(ValueError, "not in extension"):
            build.check_arduboy30_math("0e0840e0 00000108 T floor\n", 0x1000,
                                       build.ARDUBOY30_MATH_OBJECTS)

    def test_math_member_map_guard_rejects_residual_readonly_and_mutable_extension(self):
        header = (".extflash       0x08107000    0x2000\n"
                  ".xip_image2.text\n"
                  "                0x0e080000    0x4000\n")
        moved = (".extflash       0x08107000    0x2000\n"
                 " .text          0x08107000      0x69c libm.a(lib_a-k_rem_pio2.o)\n"
                 " .rodata        0x0810769c       0x50 libm.a(lib_a-k_rem_pio2.o)\n"
                 " .data          0x00000000        0x0 libm.a(lib_a-k_rem_pio2.o)\n")
        self.assertEqual(build.check_math_members(moved, build.ARDUBOY30_MATH_OBJECTS),
                         {"lib_a-k_rem_pio2.o": 0x69c + 0x50})
        # Individual absent members are allowed, but no evidence is not proof.
        with self.assertRaisesRegex(ValueError, "No whitelisted libm placement evidence"):
            build.check_math_members(header, build.ARDUBOY30_MATH_OBJECTS)
        residue = (".extflash       0x08107000    0x2000\n"
                   ".xip_image2.text\n"
                   "                0x0e080000    0x4000\n"
                   " .text          0x0e083558      0x69c libm.a(lib_a-k_rem_pio2.o)\n"
                   " .rodata        0x0e0b9a40       0x50 libm.a(lib_a-k_rem_pio2.o)\n")
        with self.assertRaisesRegex(ValueError, "stayed in .xip_image2.text"):
            build.check_math_members(residue, build.ARDUBOY30_MATH_OBJECTS)
        # A member's writable section must never be pulled into the extension.
        mutable = (".extflash       0x08107000    0x2000\n"
                   " .text          0x08107000      0x69c libm.a(lib_a-k_rem_pio2.o)\n"
                   " .bss           0x0810769c        0x4 libm.a(lib_a-k_rem_pio2.o)\n")
        with self.assertRaisesRegex(ValueError, "writable section moved to extension"):
            build.check_math_members(mutable, build.ARDUBOY30_MATH_OBJECTS)
        # Members outside the whitelist do not count as placement evidence.
        other = (".extflash       0x08107000    0x2000\n"
                 " .text          0x08107000      0x10 libm.a(lib_a-other.o)\n")
        with self.assertRaisesRegex(ValueError, "No whitelisted libm placement evidence"):
            build.check_math_members(other, build.ARDUBOY30_MATH_OBJECTS)

    def test_math_map_guard_reads_wrapped_input_sections_and_ignores_discarded(self):
        # Long mangled/qualified input sections print their name on one line and
        # the address/size/origin on the next; both must be read.
        wrapped = (".extflash       0x08107000    0x2000\n"
                   " .text.__kernel_rem_pio2\n"
                   "                0x08107000      0x69c libm.a(lib_a-k_rem_pio2.o)\n"
                   " .rodata.str1.1\n"
                   "                0x0810769c        0x5 libm.a(lib_a-k_rem_pio2.o)\n")
        self.assertEqual(build.check_math_members(wrapped, build.ARDUBOY30_MATH_OBJECTS),
                         {"lib_a-k_rem_pio2.o": 0x69c + 0x5})
        # The same wrapped shape left in main is still a residue.
        wrapped_residue = (".extflash       0x08107000    0x2000\n"
                           ".xip_image2.text\n"
                           "                0x0e080000    0x4000\n"
                           " .text._ZN18__kernel_rem_pio2v\n"
                           "                0x0e083558      0x69c libm.a(lib_a-k_rem_pio2.o)\n")
        with self.assertRaisesRegex(ValueError, "stayed in .xip_image2.text"):
            build.check_math_members(wrapped_residue, build.ARDUBOY30_MATH_OBJECTS)
        # Discarded inputs (before any output section, or address 0) are not
        # placement evidence: they must not count as moved nor as residue.
        discarded = ("Discarded input sections\n"
                     "\n"
                     " .text.__kernel_rem_pio2\n"
                     "                0x00000000      0x69c libm.a(lib_a-k_rem_pio2.o)\n"
                     " .data          0x00000000        0x0 libm.a(lib_a-k_rem_pio2.o)\n"
                     " .rodata       0x00000000        0x0 libm.a(lib_a-k_rem_pio2.o)\n"
                     ".extflash       0x08107000    0x2000\n"
                     " .text          0x00000000        0x0 libm.a(lib_a-ef_rem_pio2.o)\n")
        self.assertEqual(build.check_math_members(discarded + wrapped, build.ARDUBOY30_MATH_OBJECTS),
                         {"lib_a-k_rem_pio2.o": 0x69c + 0x5})
        # A discarded read-only section with a stale nonzero size is still
        # ignored: without an address it was never placed anywhere.
        stale = ("Discarded input sections\n"
                 " .rodata        0x00000000       0x50 libm.a(lib_a-s_atan.o)\n")
        self.assertEqual(build.check_math_members(stale + wrapped, build.ARDUBOY30_MATH_OBJECTS),
                         {"lib_a-k_rem_pio2.o": 0x69c + 0x5})

    def test_math_map_guard_rejects_missing_or_truncated_evidence(self):
        for text in ("", "Discarded input sections\n", ".extflash\n",
                     ".extflash 0x08107000 0x2000\n .text.__kernel_rem_pio2\n"):
            with self.subTest(text=text), self.assertRaises(ValueError):
                build.check_math_members(text, build.ARDUBOY30_MATH_OBJECTS)
        self.assertEqual(build.check_math_members("", ()), {})

    @unittest.skipUnless(AUDIT_MAP.is_file(), "audited link map is unavailable")
    def test_audited_map_keeps_the_original_five_in_extension_only(self):
        """Real GNU map evidence: the old five moved, the 24 new ones did not."""
        text = AUDIT_MAP.read_text(encoding="utf-8", errors="replace")
        moved = build.check_math_members(text, AUDIT_LEGACY_MATH)
        self.assertEqual(set(moved), {member for member, _s, _sec in AUDIT_LEGACY_MATH})
        # The wrapped `.rodata.str1.1` string of sqrt is real evidence of a
        # two-line input section; the same-line parser used to miss it.
        wrapped = [record for record in build.map_inputs(text)
                   if record[1].startswith(".rodata.str1.1")
                   and "lib_a-w_sqrt.o" in record[4]]
        self.assertEqual([(record[0], record[3]) for record in wrapped],
                         [(".extflash", 5)])
        # Discarded inputs (no output section) never count as moved.
        discarded = [record for record in build.map_inputs(text) if record[0] is None]
        self.assertTrue(discarded)
        self.assertTrue(all(record[2] == 0 for record in discarded))
        with self.assertRaisesRegex(ValueError, "stayed in \\.xip_image2\\.text"):
            build.check_math_members(text, build.ARDUBOY30_MATH_OBJECTS)


if __name__ == "__main__":
    unittest.main()
