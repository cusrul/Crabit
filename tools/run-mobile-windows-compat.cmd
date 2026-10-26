@echo off
rem Process-only workaround for the confirmed Flutter AccessibilityBridge crash.
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0run-mobile-windows.ps1" -CompatibilityMode -Configuration Release
if errorlevel 1 pause
