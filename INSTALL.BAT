@echo off
REM ======================================================================
REM  INSTALL.BAT - install CASTALIA/386 onto a hard disk
REM ----------------------------------------------------------------------
REM  Run this from the distribution directory (the folder that holds
REM  CASTALIA.EXE).  It copies Castalia into a target directory - by
REM  default C:\CASTALIA, or a path you pass as the first argument:
REM
REM      INSTALL                 install to C:\CASTALIA
REM      INSTALL D:\APPS\CAST    install somewhere else
REM      INSTALL C:\CASTALIA PATH   ...and append the PATH line to
REM                                 C:\AUTOEXEC.BAT (only with this
REM                                 explicit second argument, and only
REM                                 if the line is not already there)
REM
REM  It is deliberately conservative: it only writes inside the target
REM  directory, never overwrites an existing CASTALIA.INI (so your settings
REM  survive a re-install), and touches AUTOEXEC.BAT ONLY when you ask
REM  for it with the explicit PATH argument.
REM
REM  Written for plain MS-DOS 6.22 / FreeDOS: no COPY /Y, no CHOICE; the
REM  only external tool used is FIND (shipped with every DOS), and only
REM  for the optional PATH step.
REM ======================================================================
set CTGT=%1
if "%CTGT%"=="" set CTGT=C:\CASTALIA

echo.
echo   CASTALIA 92  -  Graphical Environment for DOS
echo   Tombatossals Softworks
echo   ----------------------------------------------
echo.

if not exist CASTALIA.EXE goto noexe

echo   Installing to %CTGT% ...
echo.

if not exist %CTGT%\NUL md %CTGT%
if not exist %CTGT%\ASSETS\NUL md %CTGT%\ASSETS
if not exist %CTGT%\ASSETS\ICONS\NUL md %CTGT%\ASSETS\ICONS
if not exist %CTGT%\ASSETS\THEMES\NUL md %CTGT%\ASSETS\THEMES

copy CASTALIA.EXE %CTGT% > NUL
if exist CASTSHEL.BAT copy CASTSHEL.BAT %CTGT% > NUL
if not exist %CTGT%\CASTALIA.INI copy CASTALIA.INI %CTGT% > NUL
if exist README.TXT copy README.TXT %CTGT% > NUL
if exist ASSETS\ICONS\*.ICN copy ASSETS\ICONS\*.ICN %CTGT%\ASSETS\ICONS > NUL
if exist ASSETS\ICONS\*.ICO copy ASSETS\ICONS\*.ICO %CTGT%\ASSETS\ICONS > NUL
if exist ASSETS\ICONS\*.GIF copy ASSETS\ICONS\*.GIF %CTGT%\ASSETS\ICONS > NUL

echo   Installed files:
dir %CTGT% /w
echo.
echo   Installation complete.
echo.
echo   To start the desktop:
echo.
echo       %CTGT%
echo       CASTALIA           (or CASTSHEL, see below)
echo.
echo   Use CASTSHEL instead of CASTALIA to enable "free-memory" launches:
echo   a program marked  freemem=true  in CASTALIA.INI then runs with the
echo   whole machine to itself (Castalia unloads and comes back after) -
echo   handy for big games.
echo.
echo   (Load a mouse driver first - MOUSE.COM, CTMOUSE.EXE or similar - if
echo    you want to use the pointer.  Castalia needs at least 1 MB of RAM.)
echo.
if "%2"=="PATH" goto dopath
echo   To run CASTALIA from any directory, add this line to AUTOEXEC.BAT
echo   yourself, or re-run as  INSTALL %CTGT% PATH  to have it added:
echo.
echo       SET PATH=%%PATH%%;%CTGT%
echo.
goto end

:dopath
if not exist C:\AUTOEXEC.BAT goto addpath
find /I "%CTGT%" C:\AUTOEXEC.BAT > NUL
if not errorlevel 1 goto haspath
:addpath
echo SET PATH=%%PATH%%;%CTGT%>> C:\AUTOEXEC.BAT
echo   Added  SET PATH=%%PATH%%;%CTGT%  to C:\AUTOEXEC.BAT
echo   (takes effect on the next boot).
echo.
goto end
:haspath
echo   C:\AUTOEXEC.BAT already mentions %CTGT% - left untouched.
echo.
goto end

:noexe
echo   ERROR: CASTALIA.EXE was not found in the current directory.
echo   Change to the folder that contains CASTALIA.EXE and run INSTALL again.
echo.

:end
set CTGT=
