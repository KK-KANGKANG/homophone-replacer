#ifdef _WIN32

#include "server/windows-service.h"

#include <windows.h>

#include <exception>

#include "server/service-runner.h"

namespace hr_standalone {
namespace {

SERVICE_STATUS_HANDLE g_status_handle = nullptr;
SERVICE_STATUS g_status{};
ServiceOverrides g_overrides;
StopController g_stop;

void SetStatus(DWORD state, DWORD error = NO_ERROR) {
  g_status.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
  g_status.dwCurrentState = state;
  g_status.dwControlsAccepted =
      state == SERVICE_RUNNING ? SERVICE_ACCEPT_STOP : 0;
  g_status.dwWin32ExitCode = error;
  SetServiceStatus(g_status_handle, &g_status);
}

DWORD WINAPI ControlHandler(DWORD control, DWORD, LPVOID, LPVOID) {
  if (control == SERVICE_CONTROL_STOP) {
    SetStatus(SERVICE_STOP_PENDING);
    g_stop.RequestStop();
  }
  return NO_ERROR;
}

void WINAPI ServiceMain(DWORD, LPSTR *) {
  g_status_handle = RegisterServiceCtrlHandlerExA(
      "HomophoneReplacer", ControlHandler, nullptr);
  if (!g_status_handle) return;
  SetStatus(SERVICE_START_PENDING);
  try {
    auto path = g_overrides.config.value_or("config/service.json");
    auto config = LoadServiceConfig(path, g_overrides);
    SetStatus(SERVICE_RUNNING);
    int result = RunService(config, g_stop);
    SetStatus(SERVICE_STOPPED, result == 0 ? NO_ERROR : ERROR_SERVICE_SPECIFIC_ERROR);
  } catch (...) {
    SetStatus(SERVICE_STOPPED, ERROR_SERVICE_SPECIFIC_ERROR);
  }
}

}  // namespace

int RunWindowsService(const ServiceOverrides &overrides) {
  g_overrides = overrides;
  SERVICE_TABLE_ENTRYA table[] = {
      {const_cast<LPSTR>("HomophoneReplacer"), ServiceMain}, {nullptr, nullptr}};
  return StartServiceCtrlDispatcherA(table) ? 0 : 2;
}

}  // namespace hr_standalone

#endif
