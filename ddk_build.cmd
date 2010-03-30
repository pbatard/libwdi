@echo off
rem default builds static library. Pass argument 'DLL' to build a DLL

if Test%BUILD_ALT_DIR%==Test goto usage

set DDK_DIR=%BASEDIR:\=\\%
set ORG_BUILD_ALT_DIR=%BUILD_ALT_DIR%
set ORG_BUILDARCH=%_BUILDARCH%
set ORG_PATH=%PATH%
set ORG_BUILD_DEFAULT_TARGETS=%BUILD_DEFAULT_TARGETS%

set version=1.0

set cpudir=i386
rem set BUILD_ALT_DIR=Win32
if %ORG_BUILDARCH%==x86 goto isI386
set cpudir=amd64
rem set BUILD_ALT_DIR=x64
:isI386

cd libwdi
set srcPath=obj%BUILD_ALT_DIR%\%cpudir%

del /F Makefile >NUL 2>&1
copy embedder_sources sources >NUL 2>&1
@echo on
build -cwgZ
@echo off
if errorlevel 1 goto builderror
copy %srcPath%\embedder.exe . >NUL 2>&1

set 386=1
set AMD64=
set BUILD_DEFAULT_TARGETS=-386
set _AMD64bit=
set _BUILDARCH=x86
set PATH=%BASEDIR%\bin\x86;%BASEDIR%\bin\x86\x86

copy installer_x86_sources sources >NUL 2>&1
@echo on
build -cwgZ
@echo off
if errorlevel 1 goto builderror
copy obj%BUILD_ALT_DIR%\i386\installer_x86.exe . >NUL 2>&1

set 386=
set AMD64=1
set BUILD_DEFAULT_TARGETS=-amd64
set _AMD64bit=true
set _BUILDARCH=AMD64
set PATH=%BASEDIR%\bin\x86\amd64;%BASEDIR%\bin\x86

copy installer_x64_sources sources >NUL 2>&1
@echo on
build -cwgZ
@echo off
if errorlevel 1 goto builderror
copy obj%BUILD_ALT_DIR%\amd64\installer_x64.exe . >NUL 2>&1

if %ORG_BUILDARCH%==AMD64 goto restorePath
set 386=1
set AMD64=
set BUILD_DEFAULT_TARGETS=-386
set _AMD64bit=
set _BUILDARCH=x86

:restorePath
set PATH=%ORG_PATH%

echo.
echo Embedding binary resources
embedder.exe resource.h

rem DLL or static lib selection (must use concatenation)
set TARGET=LIBRARY
if Test%1==TestDLL set TARGET=DYNLINK
echo TARGETTYPE=%TARGET% > target
copy target+libwdi_sources sources >NUL 2>&1
del target
@echo on
build -cwgZ
@echo off
if errorlevel 1 goto builderror
copy obj%BUILD_ALT_DIR%\%cpudir%\libwdi.lib . >NUL 2>&1

cd ..\examples

del /F Makefile  >NUL 2>&1
copy setdrv_sources sources >NUL 2>&1
@echo on
build -cwgZ
@echo off
if errorlevel 1 goto builderror
copy obj%BUILD_ALT_DIR%\%cpudir%\setdrv.exe . >NUL 2>&1

echo #include ^<windows.h^> > afxres.h
copy setdrv_gui_sources sources >NUL 2>&1
@echo on
build -cwgZ
@echo off
rem del afxres.h
if errorlevel 1 goto builderror
copy obj%BUILD_ALT_DIR%\%cpudir%\setdrv_gui.exe . >NUL 2>&1

cd ..

goto done

:builderror
cd ..
echo Build failed
goto done

:usage
echo ddk_build must be run in a Windows Driver Kit build environment
pause
goto done

:done
set BUILD_ALT_DIR=%ORG_BUILD_ALT_DIR%
set _BUILDARCH=%ORG_BUILDARCH%
set PATH=%ORG_PATH%
set BUILD_DEFAULT_TARGETS=%ORG_BUILD_DEFAULT_TARGETS%

if Test%DDK_TARGET_OS%==TestWinXP goto nowarn

echo.
echo.
echo WARNING: You do not seem to use the Windows XP DDK build environment.
echo Be mindful that using the Windows Vista or Windows 7 DDK build environments
echo will result in library and applications that do NOT run on Windows XP.
echo.

:nowarn
