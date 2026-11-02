# Taskbar clock source dependency

Clock source: m417z, Taskbar Clock Customization 1.8.1 (GPL-3.0).
https://github.com/ramensoftware/windhawk-mods/blob/main/mods/taskbar-clock-customization.wh.cpp

API headers: Windhawk 1.7.3 (GPL-3.0).
https://github.com/ramensoftware/windhawk/tree/v1.7.3

WinCal compiles the clock into WinCalTaskbarClock.dll with MSVC. The API header
exports InternalWhModPtr for the engine, and entrypoints.cpp supplies the engine's
expected entry names. FormatLineNoLock additionally handles WinCal's template
prefix with the shared src/taskbar_format.h formatter and WinCal's weather feed.
The host uses the Windhawk engine DLL; no Windhawk manager or editor is launched.
