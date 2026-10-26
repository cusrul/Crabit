"""Compile the public callback contract using the existing C++11 host compiler."""

import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


class CallbackCompileContracts(unittest.TestCase):
    def compiler(self):
        compiler = next(
            (
                path
                for path in (r"C:\w64dev\w64devkit\bin\g++.exe", shutil.which("g++"))
                if path and Path(path).is_file()
            ),
            None,
        )
        self.assertIsNotNone(compiler, "an existing C++ compiler is required")
        return compiler

    def compile(self, source):
        return subprocess.run(
            [
                self.compiler(),
                "-std=c++11",
                "-I",
                str(ROOT / "libraries/NanoUI/src"),
                "-x",
                "c++",
                "-fsyntax-only",
                "-",
            ],
            input="#include <NanoUI/product/core/callback.h>\n" + source,
            text=True,
            capture_output=True,
        )

    def test_function_pointer_constexpr_null_and_this_capture(self):
        result = self.compile(
            """
void action() {}
constexpr NanoUI::Callback<void()> fixed(action);
struct App {
    int value = 0;
    NanoUI::Callback<void()> callback() { return [this]() { ++value; }; }
};
void probe() {
    App app;
    auto callback = app.callback();
    callback();
    callback = nullptr;
    NanoUI::Callback<void()> legacyNull = 0;
}
static_assert(sizeof(NanoUI::Callback<void()>) <= 5 * sizeof(void*), "bounded callback");
"""
        )
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_large_capture_is_rejected(self):
        result = self.compile(
            """
struct Large { char data[64]; void operator()() const {} };
NanoUI::Callback<void()> callback = Large{};
"""
        )
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("callback capture is too large", result.stderr)

    def test_nontrivial_capture_is_rejected(self):
        result = self.compile(
            """
struct Owning {
    int* value;
    Owning() : value(nullptr) {}
    ~Owning() {}
    void operator()() const {}
};
NanoUI::Callback<void()> callback = Owning{};
"""
        )
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("nontrivial captured resources", result.stderr)

    def test_overaligned_capture_is_rejected(self):
        result = self.compile(
            """
struct alignas(32) Aligned { void operator()() const {} };
NanoUI::Callback<void()> callback = Aligned{};
"""
        )
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("callback capture alignment is unsupported", result.stderr)

    def test_mutable_closure_state_is_rejected(self):
        result = self.compile(
            """
struct Mutable { int value = 0; void operator()() { ++value; } };
NanoUI::Callback<void()> callback = Mutable{};
"""
        )
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("const Mutable", result.stderr)

    def test_product_action_button_matches_legacy_render_in_both_profiles(self):
        for profile in (0, 1):
            with (
                self.subTest(storage=profile),
                tempfile.TemporaryDirectory(prefix="nanoui-button-") as temp,
            ):
                binary = Path(temp) / "product-button.exe"
                result = subprocess.run(
                    [
                        self.compiler(),
                        "-std=c++11",
                        f"-DNANOUI_DYNAMIC_STORAGE={profile}",
                        "-I",
                        str(ROOT / "libraries/NanoUI/src"),
                        str(ROOT / "tools/tests/nanoui_action_button.cpp"),
                        str(ROOT / "libraries/NanoUI/src/NanoUI/product/runtime.cpp"),
                        str(ROOT / "libraries/NanoUI/tests/font_stub.cpp"),
                        "-o",
                        str(binary),
                    ],
                    capture_output=True,
                    text=True,
                )
                self.assertEqual(result.returncode, 0, result.stderr)
                run = subprocess.run([str(binary)], capture_output=True, text=True)
                self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
                self.assertIn("product action button failures=0", run.stdout)


if __name__ == "__main__":
    unittest.main()
