#pragma once

// Called at the logging/config safe point, never under the loader lock.
// Process-lifetime registration: changes require a restart.
void InitializeCrashDiagnostics(const wchar_t* ini_path, const wchar_t* log_path) noexcept;
