@echo off
REM ============================================================================
REM Put the MSVC x64 toolchain on PATH, or fail here saying so.
REM
REM Every Windows CI step used to open with this line, three times, verbatim:
REM
REM   for /f "usebackq tokens=*" %%i in (`"%ProgramFiles(x86)%\...\vswhere.exe"
REM       -latest -property installationPath`) do call "%%i\...\vcvars64.bat"
REM
REM A `for /f` whose command produces no output runs its body ZERO times and
REM succeeds. So when vswhere returned nothing, vcvars64.bat was never called,
REM the step carried on with no compiler and no Ninja, and the job died eight
REM minutes later somewhere else entirely.
REM
REM That is not hypothetical. Run 31959889506, job "Windows . build + test",
REM step "Build the shipping configurations": eighteen ports reported
REM "<port> does not exist" and then
REM
REM   CMake Error: CMake was unable to find a build program corresponding to
REM   "Ninja".  CMAKE_MAKE_PROGRAM is not set.
REM
REM Nothing in that output names Visual Studio, and the identical loop in the
REM tests step had SUCCEEDED eight minutes earlier in the same job -- so the
REM evidence pointed at vcpkg and at the ports, which were fine. The cost of
REM that failure was entirely in the diagnosis.
REM
REM No `setlocal` anywhere in this file: the whole point is to mutate the
REM caller's environment, and setlocal would discard it at `exit /b`.
REM ============================================================================

set "_vswhere=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%_vswhere%" (
    echo ERROR: vswhere.exe is not at "%_vswhere%".
    echo        Every Windows job needs it to find the C++ toolset.
    exit /b 1
)

REM A LADDER, not a stricter query. `-requires` is the query we want, but if
REM the image ever ships an instance that does not advertise that component we
REM must still accept what the old loop accepted -- this file is here to turn
REM silence into a message, not to start rejecting builds that used to work.
REM The real gate is the `where` check at the bottom, which tests the thing we
REM actually need rather than the mechanism we used to get it.
set "_vsroot="
for /f "usebackq tokens=*" %%i in (`"%_vswhere%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath 2^>nul`) do set "_vsroot=%%i"
if not defined _vsroot (
    for /f "usebackq tokens=*" %%i in (`"%_vswhere%" -latest -products * -property installationPath 2^>nul`) do set "_vsroot=%%i"
)
if not defined _vsroot (
    for /f "usebackq tokens=*" %%i in (`"%_vswhere%" -latest -property installationPath 2^>nul`) do set "_vsroot=%%i"
)

if not defined _vsroot (
    echo ERROR: vswhere found no Visual Studio instance.
    echo        This is the failure that used to arrive eight minutes later
    echo        wearing Ninja's name. Everything vswhere can see:
    "%_vswhere%" -all -prerelease -property installationPath
    exit /b 1
)

set "_vcvars=%_vsroot%\VC\Auxiliary\Build\vcvars64.bat"
if not exist "%_vcvars%" (
    echo ERROR: found Visual Studio at "%_vsroot%"
    echo        but no VC\Auxiliary\Build\vcvars64.bat under it, so that
    echo        instance has no C++ toolset installed.
    exit /b 1
)

REM vcvars64.bat prints "'vswhere.exe' is not recognized as an internal or
REM external command" to stderr on some installs (measured: VS 18 Community,
REM which reproduces it when run directly with this script out of the picture).
REM It is Microsoft's script failing to find its own helper on PATH, it is
REM cosmetic, and the environment initializes correctly anyway. Do not go
REM looking for it above -- the ladder does not shell out by that name.
call "%_vcvars%"
if errorlevel 1 (
    echo ERROR: vcvars64.bat exited %errorlevel%.
    exit /b 1
)

REM THE CHECK THAT MATTERS. Above this line we tested the mechanism; here we
REM test the invariant the caller depends on. It fires for a failure of any of
REM the four steps above, and for causes nobody has seen yet -- a vcvars that
REM reports success and sets nothing still gets caught here, one line into the
REM step, instead of at the far end of a 150-minute job.
where cl.exe >nul 2>&1
if errorlevel 1 (
    echo ERROR: vcvars64.bat succeeded but cl.exe is not on PATH.
    exit /b 1
)
where ninja.exe >nul 2>&1
if errorlevel 1 (
    echo ERROR: vcvars64.bat succeeded but ninja.exe is not on PATH.
    echo        The presets use the Ninja generator; without this, configure
    echo        fails with "unable to find a build program corresponding to
    echo        Ninja" and names nothing that would lead you back to here.
    exit /b 1
)

echo MSVC environment ready: %_vsroot%
exit /b 0
