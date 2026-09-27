@echo off
rem ===========================================================================
rem  FT891Remote - complete Windows build
rem
rem  Configures, compiles, deploys the runtime libraries and builds the
rem  installer, in one go.
rem
rem  Run it from the project root, in a x64 Native Tools Command Prompt
rem  for VS 2022 or VS 2026.
rem
rem  Requirements: Visual Studio 2022 or 2026 with the C++ workload, Qt for
rem  MSVC 64-bit, git, and Inno Setup 6 for the installer. PortAudio and Opus
rem  come from vcpkg; /deps installs vcpkg and both of them if they are missing.
rem
rem  Options:
rem    /deps         install vcpkg, PortAudio and Opus first if missing
rem    /clean        wipe the build directory first
rem    /nobuild      skip configure and compile, deploy and package only
rem    /noinstaller  stop after staging, do not run Inno Setup
rem    /help         print the options
rem
rem  Note: these comments deliberately avoid the help switch and the
rem  characters cmd treats specially. A rem line carrying the help switch
rem  makes cmd print the help for REM and then reject the words after it.
rem ===========================================================================

setlocal enabledelayedexpansion
title FT891Remote - complete build

rem ------------------------------------------------------- paths to adjust
set "QT_DIR=C:\Qt\6.11.2\msvc2022_64"
set "VCPKG_ROOT=C:\vcpkg"

set "BUILD_DIR=build"
set "CONFIG=Release"
set "DIST=installer\dist"

rem ------------------------------------------------------------- arguments
set DO_CLEAN=0
set DO_DEPS=0
set DO_BUILD=1
set DO_INSTALLER=1

:parse
if "%~1"=="" goto parsed
if /i "%~1"=="/deps" ( set "DO_DEPS=1" & shift & goto parse )
if /i "%~1"=="/clean" ( set "DO_CLEAN=1" & shift & goto parse )
if /i "%~1"=="/nobuild" ( set "DO_BUILD=0" & shift & goto parse )
if /i "%~1"=="/noinstaller" ( set "DO_INSTALLER=0" & shift & goto parse )
if /i "%~1"=="/?" goto usage
if /i "%~1"=="/help" goto usage
if /i "%~1"=="-h" goto usage
echo Unknown option: %~1
goto usage
:parsed

if not exist "CMakeLists.txt" (
    echo [X] Run this script from the project root, next to CMakeLists.txt.
    goto fail
)

rem ---------------------------------------------------------- prerequisites
echo.
echo === Checking prerequisites ===

where cmake >nul 2>&1
if errorlevel 1 (
    echo [X] cmake not found in PATH. Open a "x64 Native Tools Command Prompt".
    goto fail
)

where cl >nul 2>&1
if errorlevel 1 (
    echo [X] cl.exe not found. This must run in a "x64 Native Tools Command Prompt".
    goto fail
)

if not exist "%QT_DIR%\bin\windeployqt.exe" (
    echo [X] Qt not found at %QT_DIR%
    echo     Edit QT_DIR at the top of this script.
    goto fail
)
rem The client needs Qt Quick, the server Qt Serial Port: both must be part
rem of this Qt, the one the programs are built and deployed with.
if not exist "%QT_DIR%\lib\cmake\Qt6Quick\Qt6QuickConfig.cmake" (
    echo [X] Qt Quick is missing from the Qt at %QT_DIR%
    echo     Add it with the Qt Maintenance Tool: Qt Quick comes with the
    echo     MSVC 64-bit build of Qt.
    goto fail
)
if not exist "%QT_DIR%\lib\cmake\Qt6SerialPort\Qt6SerialPortConfig.cmake" (
    echo [X] Qt Serial Port is missing from the Qt at %QT_DIR%
    echo     Add it with the Qt Maintenance Tool, under Additional Libraries.
    goto fail
)
echo [ok] Qt          %QT_DIR%

set "VCPKG_TOOLCHAIN=%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake"
set "VCPKG_BIN=%VCPKG_ROOT%\installed\x64-windows\bin"

rem The double bar tests the exit code of the subroutine itself, not an
rem errorlevel left over by an earlier command.
if "%DO_DEPS%"=="1" call :deps || goto fail

if not exist "%VCPKG_TOOLCHAIN%" (
    echo [X] vcpkg not found at %VCPKG_ROOT%
    echo     Run build_all.bat /deps to install it, or edit VCPKG_ROOT.
    goto fail
)
if not exist "%VCPKG_BIN%\portaudio.dll" (
    echo [X] portaudio.dll missing. Run build_all.bat /deps, or:
    echo     "%VCPKG_ROOT%\vcpkg.exe" install portaudio:x64-windows opus:x64-windows
    goto fail
)
if not exist "%VCPKG_BIN%\opus.dll" (
    echo [X] opus.dll missing. Run build_all.bat /deps, or:
    echo     "%VCPKG_ROOT%\vcpkg.exe" install opus:x64-windows
    goto fail
)
echo [ok] vcpkg       %VCPKG_ROOT%

rem The application version, read from CMakeLists.txt and passed to Inno
rem Setup. This line stays at top level: the searched string holds a
rem parenthesis, which would break a parenthesised block.
set "APPVER="
for /f "tokens=3 delims= " %%V in ('findstr /b /c:"project(FT891Remote VERSION" CMakeLists.txt') do set "APPVER=%%V"

rem Inno Setup, looked up in the usual places then in PATH.
rem Careful: the ProgramFiles x86 variable carries parentheses, which break
rem a parenthesised block. These lines stay at top level on purpose.
set "ISCC="
set "PF86=%ProgramFiles(x86)%"
if not defined PF86 set "PF86=C:\Program Files (x86)"
if exist "%PF86%\Inno Setup 6\ISCC.exe" set "ISCC=%PF86%\Inno Setup 6\ISCC.exe"
if not defined ISCC if exist "%ProgramFiles%\Inno Setup 6\ISCC.exe" set "ISCC=%ProgramFiles%\Inno Setup 6\ISCC.exe"
if not defined ISCC if exist "%PF86%\Inno Setup 7\ISCC.exe" set "ISCC=%PF86%\Inno Setup 7\ISCC.exe"
if not defined ISCC if exist "%ProgramFiles%\Inno Setup 7\ISCC.exe" set "ISCC=%ProgramFiles%\Inno Setup 7\ISCC.exe"
if not defined ISCC for /f "delims=" %%P in ('where ISCC 2^>nul') do if not defined ISCC set "ISCC=%%P"

if defined APPVER echo [ok] Version     !APPVER!

if "%DO_INSTALLER%"=="1" (
    if defined ISCC (
        echo [ok] Inno Setup  !ISCC!
    ) else (
        echo [--] Inno Setup not found, the installer step will be skipped.
        set DO_INSTALLER=0
    )
)

rem ------------------------------------------------------------------ clean
if "%DO_CLEAN%"=="1" (
    echo.
    echo === Cleaning ===
    if exist "%BUILD_DIR%" rmdir /s /q "%BUILD_DIR%"
    if exist "%DIST%"      rmdir /s /q "%DIST%"
    if exist "installer\output" rmdir /s /q "installer\output"
)

rem -------------------------------------------------------------- configure
if "%DO_BUILD%"=="1" (
    echo.
    echo === Configuring ===
    rem Qt6_DIR names this Qt outright. The vcpkg toolchain puts its own
    rem packages first in the search: a Qt installed in vcpkg for another
    rem project, without Qt Quick, was found instead, and configuration failed
    rem on the missing Quick component. Once Qt6_DIR is set, Qt loads every
    rem module from its own folder.
    rem VCPKG_APPLOCAL_DEPS is off: vcpkg would copy its own Qt DLLs next to
    rem the programs, and the deployment below brings the right ones.
    rem A cache made while the Qt of vcpkg was found still points at it,
    rem and CMake reuses those paths first: that cache is started afresh.
    if exist "%BUILD_DIR%\CMakeCache.txt" findstr /i /r /c:"^Qt6.*_DIR:.*vcpkg" "%BUILD_DIR%\CMakeCache.txt" >nul && del /q "%BUILD_DIR%\CMakeCache.txt"
    cmake -B "%BUILD_DIR%" ^
        -DCMAKE_TOOLCHAIN_FILE="%VCPKG_TOOLCHAIN%" ^
        -DCMAKE_PREFIX_PATH="%QT_DIR%" ^
        -DQt6_DIR="%QT_DIR%\lib\cmake\Qt6" ^
        -DVCPKG_APPLOCAL_DEPS=OFF
    if errorlevel 1 (
        echo [X] Configuration failed.
        goto fail
    )

    echo.
    echo === Compiling %CONFIG% ===
    cmake --build "%BUILD_DIR%" --config %CONFIG%
    if errorlevel 1 (
        echo [X] Compilation failed.
        goto fail
    )
)

set "OUT=%BUILD_DIR%\%CONFIG%"
if not exist "%OUT%\ft891remote.exe" (
    echo [X] %OUT%\ft891remote.exe missing. Compile without /nobuild first.
    goto fail
)

rem ------------------------------------------------------------------ stage
echo.
echo === Gathering into %DIST% ===
if exist "%DIST%" rmdir /s /q "%DIST%"
mkdir "%DIST%" 2>nul

copy /y "%OUT%\ft891remote.exe" "%DIST%\" >nul
if exist "%OUT%\ft891remote-server.exe" copy /y "%OUT%\ft891remote-server.exe" "%DIST%\" >nul

echo   Qt runtime...
"%QT_DIR%\bin\windeployqt.exe" --release --no-system-d3d-compiler --no-opengl-sw ^
    --qmldir client\qml "%DIST%\ft891remote.exe" >nul
if errorlevel 1 (
    echo [X] windeployqt failed on the client.
    goto fail
)
if exist "%DIST%\ft891remote-server.exe" (
    "%QT_DIR%\bin\windeployqt.exe" --release --no-system-d3d-compiler --no-opengl-sw ^
        "%DIST%\ft891remote-server.exe" >nul
    if errorlevel 1 (
        echo [X] windeployqt failed on the server.
        goto fail
    )
)

rem The QML modules are loaded at run time, never linked: without them the
rem client opens an empty window and says nothing. windeployqt finds them by
rem scanning client\qml; check that it did.
if not exist "%DIST%\qml\QtQuick\Controls" (
    echo [X] The Qt Quick modules were not deployed into %DIST%\qml.
    echo     Is Qt Quick Controls part of the Qt installed at %QT_DIR%?
    goto fail
)

echo   PortAudio and Opus...
copy /y "%VCPKG_BIN%\portaudio.dll" "%DIST%\" >nul
if errorlevel 1 (
    echo [X] Could not copy portaudio.dll
    goto fail
)
copy /y "%VCPKG_BIN%\opus.dll" "%DIST%\" >nul
if errorlevel 1 (
    echo [X] Could not copy opus.dll
    goto fail
)

rem The Visual C++ runtime. A fresh Windows does not have it, and both
rem programs then stop at start-up on a missing MSVCP140.dll. windeployqt
rem usually drops the redistributable next to the programs; if not, it comes
rem from the Visual Studio this prompt belongs to. The installer runs it.
echo   Visual C++ runtime...
if not exist "%DIST%\vc_redist.x64.exe" (
    if defined VCToolsRedistDir if exist "%VCToolsRedistDir%vc_redist.x64.exe" copy /y "%VCToolsRedistDir%vc_redist.x64.exe" "%DIST%\" >nul
)
if exist "%DIST%\vc_redist.x64.exe" (
    echo [ok] vc_redist.x64.exe bundled
) else (
    echo [--] vc_redist.x64.exe not found: the installer will rely on the runtime
    echo      already being present on the target machine.
)

rem -------------------------------------------------------------- installer
if "%DO_INSTALLER%"=="1" (
    echo.
    echo === Building the installer ===
    if not defined APPVER (
        echo [--] Version not found in CMakeLists.txt, the installer keeps its default.
        "%ISCC%" /Q "installer\FT891Remote.iss"
    ) else (
        echo [ok] Installer version !APPVER!
        "%ISCC%" /Q "/DAppVersion=!APPVER!" "installer\FT891Remote.iss"
    )
    if errorlevel 1 (
        echo [X] Inno Setup failed.
        goto fail
    )
)

rem ----------------------------------------------------------------- report
echo.
echo ===========================================================
echo  Done.
echo.
echo  Programs        %OUT%
echo  Ready to run    %DIST%
if "%DO_INSTALLER%"=="1" (
    rem No quotes around the pattern: cmd only expands an unquoted wildcard.
    for %%F in (installer\output\*.exe) do echo  Installer       installer\output\%%~nxF   %%~zF bytes
)
echo ===========================================================
endlocal
exit /b 0

:usage
echo.
echo Usage: build_all.bat [/deps] [/clean] [/nobuild] [/noinstaller]
echo.
echo   /deps         install vcpkg, PortAudio and Opus first if missing
echo   /clean        wipe the build directory first
echo   /nobuild      skip configure and compile, deploy and package only
echo   /noinstaller  stop after staging, do not run Inno Setup
echo.
echo Adjust QT_DIR and VCPKG_ROOT at the top of the file.
endlocal
exit /b 0

:deps
rem vcpkg and the two audio libraries. Qt, Visual Studio and Inno Setup have
rem installers of their own and are not handled here.
echo.
echo === Dependencies ===
if not exist "%VCPKG_ROOT%\vcpkg.exe" (
    where git >nul 2>&1
    if errorlevel 1 (
        echo [X] git is needed to fetch vcpkg: https://git-scm.com/download/win
        exit /b 1
    )
    if not exist "%VCPKG_ROOT%\.git" (
        echo   Cloning vcpkg into %VCPKG_ROOT%...
        git clone https://github.com/microsoft/vcpkg.git "%VCPKG_ROOT%"
        if errorlevel 1 (
            echo [X] Cloning vcpkg failed.
            exit /b 1
        )
    )
    echo   Bootstrapping vcpkg...
    call "%VCPKG_ROOT%\bootstrap-vcpkg.bat" -disableMetrics
    if errorlevel 1 (
        echo [X] Bootstrapping vcpkg failed.
        exit /b 1
    )
)
echo   Installing PortAudio and Opus, a few minutes the first time...
"%VCPKG_ROOT%\vcpkg.exe" install portaudio:x64-windows opus:x64-windows
if errorlevel 1 (
    echo [X] vcpkg could not install PortAudio and Opus.
    exit /b 1
)
echo [ok] PortAudio and Opus installed
exit /b 0

:fail
echo.
echo Build aborted.
endlocal
exit /b 1
