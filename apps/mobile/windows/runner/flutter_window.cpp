#include "flutter_window.h"

#include <optional>
#include <commctrl.h>

#include "flutter/generated_plugin_registrant.h"

namespace {
constexpr UINT_PTR kAccessibilityCompatibilitySubclass = 1;

LRESULT CALLBACK AccessibilityCompatibilityProc(HWND window, UINT message,
                                               WPARAM wparam, LPARAM lparam,
                                               UINT_PTR subclass_id, DWORD_PTR) {
  // Opt-in workaround for the reproduced engine AccessibilityBridge crash.
  // WM_GETOBJECT enables the affected Windows semantics path. This process-only
  // mode deliberately declines UIA/MSAA providers; ordinary builds retain them.
  if (message == WM_GETOBJECT) return 0;
  if (message == WM_NCDESTROY) {
    RemoveWindowSubclass(window, AccessibilityCompatibilityProc, subclass_id);
  }
  return DefSubclassProc(window, message, wparam, lparam);
}
}  // namespace

FlutterWindow::FlutterWindow(const flutter::DartProject& project)
    : project_(project) {}

FlutterWindow::~FlutterWindow() {}

bool FlutterWindow::OnCreate() {
  if (!Win32Window::OnCreate()) {
    return false;
  }

  RECT frame = GetClientArea();

  // The size here must match the window dimensions to avoid unnecessary surface
  // creation / destruction in the startup path.
  flutter_controller_ = std::make_unique<flutter::FlutterViewController>(
      frame.right - frame.left, frame.bottom - frame.top, project_);
  // Ensure that basic setup of the controller was successful.
  if (!flutter_controller_->engine() || !flutter_controller_->view()) {
    return false;
  }
  wchar_t compatibility[2] = {};
  if (GetEnvironmentVariableW(L"CRABIT_WINDOWS_DISABLE_ACCESSIBILITY", compatibility, 2) == 1 &&
      compatibility[0] == L'1') {
    accessibility_compatibility_ = SetWindowSubclass(
        flutter_controller_->view()->GetNativeWindow(), AccessibilityCompatibilityProc,
        kAccessibilityCompatibilitySubclass, 0) != FALSE;
    if (!accessibility_compatibility_) return false;
  }
  RegisterPlugins(flutter_controller_->engine());
  SetChildContent(flutter_controller_->view()->GetNativeWindow());

  flutter_controller_->engine()->SetNextFrameCallback([&]() {
    this->Show();
  });

  // Flutter can complete the first frame before the "show window" callback is
  // registered. The following call ensures a frame is pending to ensure the
  // window is shown. It is a no-op if the first frame hasn't completed yet.
  flutter_controller_->ForceRedraw();

  return true;
}

void FlutterWindow::OnDestroy() {
  if (flutter_controller_) {
    if (accessibility_compatibility_) {
      RemoveWindowSubclass(flutter_controller_->view()->GetNativeWindow(),
                           AccessibilityCompatibilityProc,
                           kAccessibilityCompatibilitySubclass);
      accessibility_compatibility_ = false;
    }
    flutter_controller_ = nullptr;
  }

  Win32Window::OnDestroy();
}

LRESULT
FlutterWindow::MessageHandler(HWND hwnd, UINT const message,
                              WPARAM const wparam,
                              LPARAM const lparam) noexcept {
  // Give Flutter, including plugins, an opportunity to handle window messages.
  if (flutter_controller_) {
    std::optional<LRESULT> result =
        flutter_controller_->HandleTopLevelWindowProc(hwnd, message, wparam,
                                                      lparam);
    if (result) {
      return *result;
    }
  }

  switch (message) {
    case WM_FONTCHANGE:
      flutter_controller_->engine()->ReloadSystemFonts();
      break;
  }

  return Win32Window::MessageHandler(hwnd, message, wparam, lparam);
}
