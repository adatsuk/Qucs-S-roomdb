@echo off
rem ngspice_con.exe shim for Qucs-S: run WSL ngspice with path translation.
rem Place this file as C:\Spice64\bin\ngspice_con.bat and point Qucs-S at it,
rem or copy as ngspice_con.cmd next to a dummy ngspice_con.exe — Qucs looks for exe.
rem Prefer a real Windows ngspice_con.exe when available.
setlocal EnableExtensions
set "ARGS="
:loop
if "%~1"=="" goto run
set "A=%~1"
rem Convert Windows paths that look like C:\... to /mnt/c/...
echo %A%| findstr /R /C:"^[A-Za-z]:\\" >nul
if not errorlevel 1 (
  for /f "delims=" %%P in ('wsl wslpath -u "%A%" 2^>nul') do set "A=%%P"
)
set "ARGS=%ARGS% %A%"
shift
goto loop
:run
wsl bash -lc "export PDK_ROOT=${PDK_ROOT:-$HOME/IHP-Open-PDK}; export PDK=${PDK:-ihp-sg13g2}; export PATH=$HOME/.local/bin:$PATH; exec ngspice%ARGS%"
exit /b %ERRORLEVEL%
