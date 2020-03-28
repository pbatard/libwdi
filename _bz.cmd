@echo off

call "C:\Program Files (x86)\Microsoft Visual Studio\2019\Community\Common7\Tools\VsDevCmd.bat" -arch=x86 -host_arch=amd64
cd /d "%~dp0"
rem *** Get the version
for /f "tokens=3" %%i in ('findstr FileVersion examples\zadig.rc') do set "ver=%%i"
set ver=%ver:"=%
for /f "tokens=1,2 delims=." %%i in ("%ver%") do set "ZADIG_VERSION=%%i.%%j"
msbuild libwdi.sln /m /p:Project=Zadig;Configuration=Release,Platform=Win32 /t:Rebuild
copy Win32\Release\examples\zadig.exe zadig-%ZADIG_VERSION%.exe
upx --lzma --best zadig-%ZADIG_VERSION%.exe
"C:\Program Files (x86)\Windows Kits\10\bin\10.0.18362.0\x64\signtool" sign /v /sha1 9ce9a71ccab3b38a74781b975f1c228222cf7d3b /fd SHA256 /tr http://sha256timestamp.ws.symantec.com/sha256/timestamp zadig-%ZADIG_VERSION%.exe
pause
