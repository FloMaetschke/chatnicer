@echo off
REM =====================================================================================
REM  ChatNicer - Build ohne Visual Studio IDE
REM
REM  Aufruf:  build.bat           Standard: kleinste EXE (~110 KB), keine Redist noetig
REM           build.bat compat    maximal kompatibel: komplett statische CRT (~203 KB)
REM
REM  Das Skript sucht die VS-Buildtools selbst; es muss KEIN Developer Prompt offen sein.
REM =====================================================================================
setlocal

set "MODE=%~1"
if /i "%MODE%"=="compat" (
    set "OUTNAME=ChatNicer-compat.exe"
    set "CLEXTRA=/EHsc"
    set "LINKEXTRA="
) else (
    set "OUTNAME=ChatNicer.exe"
    REM  /EHs-c- + _HAS_EXCEPTIONS=0 : ohne Exception-Maschinerie der CRT
    REM  /NODEFAULTLIB:libucrt + ucrt.lib : vcruntime statisch, UCRT dynamisch.
    REM  ucrtbase.dll gehoert ab Windows 10 zum Betriebssystem -> kein VC++-Redist noetig.
    set "CLEXTRA=/EHs-c- /D_HAS_EXCEPTIONS=0"
    set "LINKEXTRA=/NODEFAULTLIB:libucrt.lib /DEFAULTLIB:ucrt.lib"
)

REM --- Visual-Studio-Umgebung laden ---------------------------------------------------
where cl.exe >nul 2>&1
if %errorlevel%==0 goto :have_cl

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
    echo [Fehler] vswhere.exe nicht gefunden - ist Visual Studio installiert?
    exit /b 1
)

REM Umweg ueber eine Temp-Datei: robuster als "for /f" mit Pfaden voller Leerzeichen
"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath > "%TEMP%\chatnicer_vspath.txt"
set "VSPATH="
set /p VSPATH=<"%TEMP%\chatnicer_vspath.txt"
del "%TEMP%\chatnicer_vspath.txt" >nul 2>&1

if not defined VSPATH (
    echo [Fehler] Keine VC++-Buildtools ^(x64^) gefunden.
    exit /b 1
)
call "%VSPATH%\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 (
    echo [Fehler] vcvars64.bat konnte nicht geladen werden.
    exit /b 1
)

:have_cl
if not exist build mkdir build

echo.
echo === Kompiliere %OUTNAME% ===
cl /nologo /std:c++17 /permissive- /W4 /MT /utf-8 %CLEXTRA% ^
   /O1 /Os /Oi /Oy /Gy /Gw /GL /GR- /GS- /Zc:inline /Zc:threadSafeInit- ^
   /DNDEBUG /DUNICODE /D_UNICODE /DWIN32_LEAN_AND_MEAN /D_WIN32_WINNT=0x0A00 ^
   /Fobuild\ /Fdbuild\ main.cpp ^
   /link /LTCG /OPT:REF /OPT:ICF /INCREMENTAL:NO /SUBSYSTEM:WINDOWS /RELEASE ^
   /MANIFEST:EMBED %LINKEXTRA% /OUT:build\%OUTNAME% ^
   user32.lib gdi32.lib shell32.lib comctl32.lib winhttp.lib advapi32.lib ^
   oleacc.lib oleaut32.lib ole32.lib

if errorlevel 1 (
    echo.
    echo [Fehler] Build fehlgeschlagen.
    exit /b 1
)

echo.
for %%F in (build\%OUTNAME%) do echo === Fertig: %%~fF  ^(%%~zF Bytes^) ===

REM --- SHA256 -------------------------------------------------------------------------
REM  Die EXE wird unsigniert ausgeliefert; der Hash gehoert deshalb in die Release-Notes
REM  und ist fuer den Nutzer der einzige Weg, den Download gegen das Repo zu pruefen.
set "SHA256="
for /f "skip=1 tokens=*" %%H in ('certutil -hashfile "build\%OUTNAME%" SHA256') do (
    if not defined SHA256 set "SHA256=%%H"
)
echo === SHA256: %SHA256%
endlocal
