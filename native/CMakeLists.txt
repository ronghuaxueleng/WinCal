cmake_minimum_required(VERSION 3.25)

project(WinCalNative VERSION 0.1.0 LANGUAGES CXX RC)

file(TO_CMAKE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../Assets/WinCal.ico" WINCAL_ICON_PATH)
file(TO_CMAKE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/resources/WinCal.manifest" WINCAL_MANIFEST_PATH)
configure_file(resources/WinCal.rc.in "${CMAKE_CURRENT_BINARY_DIR}/WinCal.rc" @ONLY)

add_executable(WinCalNative WIN32
    src/app_settings.cpp
    src/calendar_data.cpp
    src/china_holidays.cpp
    src/weather.cpp
    src/lunar_calendar.cpp
    src/main.cpp
    "${CMAKE_CURRENT_BINARY_DIR}/WinCal.rc"
)

add_executable(WinCalNativeSmoke
    src/app_settings.cpp
    src/lunar_calendar.cpp
    tests/smoke.cpp
)

set_target_properties(WinCalNative PROPERTIES
    OUTPUT_NAME "WinCal"
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
)

target_compile_features(WinCalNative PRIVATE cxx_std_20)
target_compile_features(WinCalNativeSmoke PRIVATE cxx_std_20)
target_include_directories(WinCalNativeSmoke PRIVATE src)
target_compile_definitions(WinCalNative PRIVATE
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
)
target_compile_definitions(WinCalNativeSmoke PRIVATE
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
)

if(MSVC)
    target_compile_options(WinCalNative PRIVATE /utf-8 /W4 /permissive- /EHsc)
    target_compile_options(WinCalNativeSmoke PRIVATE /utf-8 /W4 /permissive- /EHsc)
    target_link_options(WinCalNative PRIVATE /MANIFEST:NO)
endif()

target_link_libraries(WinCalNative PRIVATE
    advapi32
    bcrypt
    comctl32
    d2d1
    dwrite
    dwmapi
    gdi32
    imm32
    icu
    ole32
    oleaut32
    shell32
    shlwapi
    user32
    winhttp
    ws2_32
    windowsapp
    uiautomationcore
)

target_link_libraries(WinCalNativeSmoke PRIVATE
    icu
    ole32
    shell32
    windowsapp
)

install(TARGETS WinCalNative RUNTIME DESTINATION .)

# The shell clock is built from source; the x86 host is required by the engine
# even when Explorer and the calendar are x64. Neither component has a UI.
set(CLOCK_VENDOR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/windhawk-clock")
add_custom_command(OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/windhawk-import.lib"
    COMMAND "${CMAKE_AR}" /nologo "/def:${CLOCK_VENDOR}/windhawk_import.def" /machine:x64
        "/out:${CMAKE_CURRENT_BINARY_DIR}/windhawk-import.lib"
    DEPENDS "${CLOCK_VENDOR}/windhawk_import.def")
add_custom_target(WinCalClockImports DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/windhawk-import.lib")
add_library(WinCalTaskbarClock SHARED "${CLOCK_VENDOR}/entrypoints.cpp" "${CLOCK_VENDOR}/entrypoints.def")
add_dependencies(WinCalTaskbarClock WinCalClockImports)
target_compile_features(WinCalTaskbarClock PRIVATE cxx_std_20)
target_compile_definitions(WinCalTaskbarClock PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX WH_MOD
    WH_MOD_ID=L"taskbar-clock-customization" WH_MOD_VERSION=L"1.8.1")
target_include_directories(WinCalTaskbarClock PRIVATE "${CLOCK_VENDOR}")
target_compile_options(WinCalTaskbarClock PRIVATE /utf-8 /EHsc /bigobj /FIwindhawk_api.h)
set_target_properties(WinCalTaskbarClock PROPERTIES MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
target_link_libraries(WinCalTaskbarClock PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/windhawk-import.lib"
    dxgi ole32 oleaut32 pdh powrprof runtimeobject shlwapi version wininet windowsapp comsuppw comctl32)
add_custom_command(OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/host/WinCalTaskbarHost.exe"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/host"
    COMMAND cmd /c "${CMAKE_CURRENT_SOURCE_DIR}/tools/build-taskbar-host.cmd"
        "${CMAKE_GENERATOR_INSTANCE}" "${CMAKE_CURRENT_BINARY_DIR}/host" "${CMAKE_CURRENT_SOURCE_DIR}/src/taskbar_host.cpp"
    DEPENDS src/taskbar_host.cpp tools/build-taskbar-host.cmd)
add_custom_target(WinCalTaskbarHost ALL DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/host/WinCalTaskbarHost.exe")
add_dependencies(WinCalNative WinCalTaskbarClock WinCalTaskbarHost)
install(TARGETS WinCalTaskbarClock RUNTIME DESTINATION .)
install(PROGRAMS "${CMAKE_CURRENT_BINARY_DIR}/host/WinCalTaskbarHost.exe" DESTINATION .)
install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/third_party/windhawk-engine/TaskbarExtension/"
    DESTINATION TaskbarExtension
    PATTERN "*.lib" EXCLUDE)
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/third_party/windhawk-engine/LICENSE"
    DESTINATION TaskbarExtension)

# On-demand visual check; excluded from normal builds and installation.
add_executable(WinCalSettingsPreview EXCLUDE_FROM_ALL
    tests/settings_preview.cpp
    src/app_settings.cpp
    src/calendar_data.cpp
    src/china_holidays.cpp
    src/weather.cpp
    src/lunar_calendar.cpp
    "${CMAKE_CURRENT_BINARY_DIR}/WinCal.rc"
)
target_compile_features(WinCalSettingsPreview PRIVATE cxx_std_20)
target_compile_definitions(WinCalSettingsPreview PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
target_compile_options(WinCalSettingsPreview PRIVATE /utf-8 /W4 /permissive- /EHsc)
target_link_options(WinCalSettingsPreview PRIVATE /MANIFEST:NO)
target_link_libraries(WinCalSettingsPreview PRIVATE $<TARGET_PROPERTY:WinCalNative,LINK_LIBRARIES> d3d11 dxgi)

# Parser and refresh regression tests do not access user settings or services.
add_executable(WinCalCalendarTests tests/calendar_data_tests.cpp src/china_holidays.cpp)
target_compile_features(WinCalCalendarTests PRIVATE cxx_std_20)
target_compile_definitions(WinCalCalendarTests PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
target_compile_options(WinCalCalendarTests PRIVATE /utf-8 /W4 /permissive- /EHsc)
target_link_libraries(WinCalCalendarTests PRIVATE bcrypt ole32 shell32 winhttp windowsapp)
enable_testing()
add_executable(WinCalFortuneTests tests/fortune_tests.cpp)
target_compile_features(WinCalFortuneTests PRIVATE cxx_std_20)
target_compile_options(WinCalFortuneTests PRIVATE /utf-8 /W4 /permissive- /EHsc)
add_test(NAME FortuneRegression COMMAND WinCalFortuneTests)
add_executable(WinCalAlmanacTests tests/almanac_tests.cpp)
target_compile_features(WinCalAlmanacTests PRIVATE cxx_std_20)
target_compile_options(WinCalAlmanacTests PRIVATE /utf-8 /W4 /permissive- /EHsc)
add_test(NAME AlmanacRegression COMMAND WinCalAlmanacTests)
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/third_party/lunar/LICENSE" DESTINATION licenses RENAME lunar-LICENSE.txt)
add_test(NAME CalendarRegression COMMAND WinCalCalendarTests)

add_executable(WinCalCoreTests tests/core_tests.cpp src/lunar_calendar.cpp)
target_compile_features(WinCalCoreTests PRIVATE cxx_std_20)
target_compile_definitions(WinCalCoreTests PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
target_compile_options(WinCalCoreTests PRIVATE /utf-8 /W4 /permissive- /EHsc)
target_link_libraries(WinCalCoreTests PRIVATE advapi32 icu ole32 shell32 windowsapp)
add_test(NAME CoreRegression COMMAND WinCalCoreTests)

add_executable(WinCalWeatherTests tests/weather_tests.cpp src/weather.cpp)
target_compile_features(WinCalWeatherTests PRIVATE cxx_std_20)
target_compile_definitions(WinCalWeatherTests PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
target_compile_options(WinCalWeatherTests PRIVATE /utf-8 /W4 /permissive- /EHsc)
target_link_libraries(WinCalWeatherTests PRIVATE ole32 winhttp windowsapp)
add_test(NAME WeatherRegression COMMAND WinCalWeatherTests)
