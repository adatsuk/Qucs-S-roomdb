@echo off
rem LibMan / Tool Manager launcher for Qucs-S with ROOM + IHP PDK + ngspice.
setlocal EnableExtensions
set "ROOT=%~dp0"
set "EXE=%ROOT%build\qucs\qucs-s.exe"
if not exist "%EXE%" (
  echo ERROR: Build qucs-s first. See docs\ROOM.md
  exit /b 1
)

rem Prefer ngspice-46 (OSDI v0.4 / IHP Windows PE plugins). Fall back to ngspice-42.
if exist "%USERPROFILE%\Documents\VBIC_cryo\tools\ngspice\Spice64\bin\ngspice_con.exe" (
  set "PATH=%USERPROFILE%\Documents\VBIC_cryo\tools\ngspice\Spice64\bin;%PATH%"
) else if exist "%USERPROFILE%\Spice64\Spice64\bin\ngspice_con.exe" (
  set "PATH=%USERPROFILE%\Spice64\Spice64\bin;%PATH%"
) else if exist "%USERPROFILE%\Spice64\bin\ngspice_con.exe" (
  set "PATH=%USERPROFILE%\Spice64\bin;%PATH%"
) else if exist "C:\Spice64\bin\ngspice_con.exe" (
  set "PATH=C:\Spice64\bin;%PATH%"
)

rem IHP Open PDK models (prefer C:\Work\IHP, then Documents copy, then WSL UNC).
if not defined PDK_ROOT (
  if exist "C:\Work\IHP\ihp-sg13g2\libs.tech\ngspice\models" (
    set "PDK_ROOT=C:\Work\IHP"
  ) else if exist "%USERPROFILE%\Documents\IHP-Open-PDK\ihp-sg13g2\libs.tech\ngspice\models" (
    set "PDK_ROOT=%USERPROFILE%\Documents\IHP-Open-PDK"
  ) else if exist "\\wsl$\Ubuntu\home\adatsuk\IHP-Open-PDK\ihp-sg13g2\libs.tech\ngspice\models" (
    set "PDK_ROOT=\\wsl$\Ubuntu\home\adatsuk\IHP-Open-PDK"
  )
)
if not defined PDK set "PDK=ihp-sg13g2"
set "PDK_ROOT=%PDK_ROOT%"
set "PDK=%PDK%"

rem Sync User Libraries: IHP + QUCS_EXTRA_USER_LIBS + design stubs from LibMan env / opened view.
rem Qucs-S reads User Libraries from %%USERPROFILE%%\QucsWorkspace\user_lib
rem Pass CORE_PRIMITIVE_LIBS_FILE explicitly — child powershell sometimes misses bat env.
if exist "%ROOT%sync-qucs-user-libs.ps1" (
  powershell -NoProfile -ExecutionPolicy Bypass -File "%ROOT%sync-qucs-user-libs.ps1" -OpenedView "%~1" -PdkRoot "%PDK_ROOT%" -Pdk "%PDK%" -PrimitiveLibsFile "%CORE_PRIMITIVE_LIBS_FILE%"
)

start "" "%EXE%" %*
