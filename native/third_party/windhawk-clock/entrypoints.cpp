#include <winternl.h>
#include "clock_mod.cpp"

extern "C" BOOL WinCalModInit() { return Wh_ModInit(); }
extern "C" void WinCalModAfterInit() { Wh_ModAfterInit(); }
extern "C" void WinCalModBeforeUninit() { Wh_ModBeforeUninit(); }
extern "C" void WinCalModUninit() { Wh_ModUninit(); }
extern "C" BOOL WinCalModSettingsChanged(BOOL* reload) { return Wh_ModSettingsChanged(reload); }
