#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <string>

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int)
{
    int count{};
    auto arguments = CommandLineToArgvW(GetCommandLineW(), &count);
    if (!arguments || count != 4) return 1;
    const std::wstring library = std::wstring(arguments[1]) + L"\\Engine\\1.7.3\\32\\windhawk.dll";
    const DWORD owner = wcstoul(arguments[2], nullptr, 10);
    HANDLE stop = OpenEventW(SYNCHRONIZE, FALSE, arguments[3]);
    LocalFree(arguments);
    HANDLE parent = OpenProcess(SYNCHRONIZE, FALSE, owner);
    if (!stop || !parent) return 2;
    HMODULE module = LoadLibraryW(library.c_str());
    if (!module) return 3;
    const auto start = reinterpret_cast<HANDLE(*)()>(GetProcAddress(module, "GlobalHookSessionStart"));
    const auto update = reinterpret_cast<BOOL(*)(HANDLE)>(GetProcAddress(module, "GlobalHookSessionHandleNewProcesses"));
    const auto finish = reinterpret_cast<BOOL(*)(HANDLE)>(GetProcAddress(module, "GlobalHookSessionEnd"));
    if (!start || !update || !finish) return 4;
    HANDLE session = start();
    if (!session) return 5;
    HANDLE handles[]{parent, stop};
    while (WaitForMultipleObjects(2, handles, FALSE, 500) == WAIT_TIMEOUT) update(session);
    finish(session);
    FreeLibrary(module);
    CloseHandle(parent);
    CloseHandle(stop);
    return 0;
}
