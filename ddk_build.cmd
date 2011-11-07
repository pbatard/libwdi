@echo off
::# default builds static library. 
::# you can pass the following arguments (case insensitive):
::# - "DLL" to build a DLL instead of a static library
::# - "no_samples" to build the library only

if Test%BUILD_ALT_DIR%==Test goto usage
::# /M 2 for multiple cores
set BUILD_CMD=build -bcwgZ -M2
set PWD=%~dp0

::# process commandline parameters
set TARGET=LIBRARY
set BUILD_SAMPLES=YES

:more_args
if "%1" == "" goto no_more_args
::# /I for case insensitive
if /I Test%1==TestDLL set TARGET=DYNLINK
if /I Test%1==Testno_samples set BUILD_SAMPLES=NO
::# - shift the arguments and examine %1 again
shift
goto more_args
:no_more_args

::# Set DDK_DIR (=BASEDIR with escaped backslashes)
set DDK_DIR=%BASEDIR:\=\\%
::# Set target platform type
set ARCH_DIR=%_BUILDARCH%
if /I Test%_BUILDARCH%==Testx86 set ARCH_DIR=i386

if /I %_BUILDARCH%==amd64 goto x86_64
echo #define NO_BUILD64> libwdi\build64.h
goto main_start
:x86_64
echo #define BUILD64> libwdi\build64.h

:main_start
cd libwdi
set srcPath=obj%BUILD_ALT_DIR%\%cpudir%

del Makefile.hide >NUL 2>&1
if EXIST Makefile ren Makefile Makefile.hide
copy .msvc\embedder_sources sources >NUL 2>&1
@echo on
%BUILD_CMD% -x86
@echo off
if errorlevel 1 goto builderror
copy obj%BUILD_ALT_DIR%\i386\embedder.exe . >NUL 2>&1

copy .msvc\installer_x86_sources sources >NUL 2>&1
@echo on
%BUILD_CMD% -x86
@echo off
if errorlevel 1 goto builderror
copy obj%BUILD_ALT_DIR%\i386\installer_x86.exe . >NUL 2>&1

copy .msvc\installer_x64_sources sources >NUL 2>&1
@echo on
%BUILD_CMD% -amd64
@echo off
if errorlevel 1 goto builderror
copy obj%BUILD_ALT_DIR%\amd64\installer_x64.exe . >NUL 2>&1

echo.
echo Embedding binary resources
embedder.exe embedded.h

::# DLL or static lib selection (must use concatenation)
echo TARGETTYPE=%TARGET% > target
copy target+.msvc\libwdi_sources sources >NUL 2>&1
del target
@echo on
%BUILD_CMD%
@echo off
if errorlevel 1 goto builderror
copy obj%BUILD_ALT_DIR%\%ARCH_DIR%\libwdi.lib . >NUL 2>&1
copy obj%BUILD_ALT_DIR%\%ARCH_DIR%\libwdi.dll . >NUL 2>&1

if EXIST Makefile.hide ren Makefile.hide Makefile
cd ..
if Test%BUILD_SAMPLES%==TestNO goto done
cd examples\getopt

del Makefile.hide >NUL 2>&1
if EXIST Makefile ren Makefile Makefile.hide
copy .msvc\getopt_sources sources >NUL 2>&1
@echo on
%BUILD_CMD%
@echo off
if errorlevel 1 goto builderror
copy obj%BUILD_ALT_DIR%\%ARCH_DIR%\getopt.lib . >NUL 2>&1

if EXIST Makefile.hide ren Makefile.hide Makefile
cd ..

del Makefile.hide >NUL 2>&1
if EXIST Makefile ren Makefile Makefile.hide
::# Work around MS's VC++ and DDK weird icompatibilities with regards to rc files
echo #include ^<windows.h^> > afxres.h
echo #ifndef IDC_STATIC >> afxres.h
echo #define IDC_STATIC -1 >> afxres.h
echo #endif >> afxres.h
copy .msvc\zadic_sources sources >NUL 2>&1
@echo on
%BUILD_CMD%
@echo off
if errorlevel 1 goto builderror
copy obj%BUILD_ALT_DIR%\%ARCH_DIR%\zadic.exe . >NUL 2>&1

copy .msvc\zadig_sources sources >NUL 2>&1
@echo on
%BUILD_CMD%
@echo off
if errorlevel 1 goto builderror
del afxres.h
copy obj%BUILD_ALT_DIR%\%ARCH_DIR%\zadig.exe . >NUL 2>&1

copy .msvc\inf_wizard_sources sources >NUL 2>&1
@echo on
%BUILD_CMD%
@echo off
if errorlevel 1 goto builderror
copy obj%BUILD_ALT_DIR%\%ARCH_DIR%\inf-wizard.exe . >NUL 2>&1

copy .msvc\wdi-simple_sources sources >NUL 2>&1
@echo on
%BUILD_CMD%
@echo off
if errorlevel 1 goto builderror
copy obj%BUILD_ALT_DIR%\%ARCH_DIR%\wdi-simple.exe . >NUL 2>&1

if EXIST Makefile.hide ren Makefile.hide Makefile
cd ..

goto done

:builderror
if EXIST Makefile.hide ren Makefile.hide Makefile
if EXIST afxres.h del afxres.h
echo Build failed
goto done

:usage
echo ddk_build must be run in a Windows Driver Kit build environment
pause
goto done

:done

if Test%DDK_TARGET_OS%==TestWinXP goto nowarn

echo.
echo.
echo WARNING: You do not seem to use the Windows XP DDK build environment.
echo Be mindful that using the Windows Vista or Windows 7 DDK build environments
echo will result in library and applications that do NOT run on Windows XP.
echo.

:nowarn
cd %PWD%