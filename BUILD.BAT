@echo off
REM ======================================================================
REM  BUILD.BAT - one-step build of CASTALIA/386 with Open Watcom C
REM ----------------------------------------------------------------------
REM  Prerequisite: the Open Watcom environment must be active so that
REM  WATCOM, PATH and INCLUDE are set.  The Open Watcom installer creates
REM  OWSETENV.BAT for exactly this; run it once per session, e.g.:
REM
REM      C:\WATCOM\OWSETENV.BAT
REM      CD \CASTALIA
REM      BUILD
REM
REM  MS-DOS COMMAND.COM cuts every batch line at 127 characters - after
REM  %CF% is expanded - so the shared flags live in CF and the modules are
REM  compiled in short FOR groups.  Lines that ran past the limit used to
REM  lose their tail on real DOS - seventeen modules of the longest group
REM  were never compiled - and the build carried on to "Build OK".  CI
REM  (ci/consistency.sh) now fails any line over the limit.
REM
REM  Toolchain: 16-bit real mode, medium model (-mm), 386 code (-3),
REM             size optimised (-os), maximum AND fatal warnings (-wx -we).
REM
REM  video, font, window and ui are the inner loop - every pixel, every
REM  character and every window compose goes through them, up to 18 times
REM  a second - so those four are built for SPEED (-otexan) instead.  Keep
REM  this in step with CFLAGS_FAST in the Makefile.
REM
REM  Failure is detected by what is MISSING: every object and both
REM  executables are deleted first, the compiler writes no object for a
REM  file with an error (or, under -we, a warning), so no EXE is linked.
REM ======================================================================
set CF=-bt=dos -mm -3 -zq -wx
if exist src\*.obj del src\*.obj
if exist install\*.obj del install\*.obj
if exist CASTALIA.EXE del CASTALIA.EXE
if exist INSTALL.EXE del INSTALL.EXE
echo Compiling CASTALIA/386 ...
for %%f in (video font window ui) do wcc %CF% -we -otexan -fo=src\%%f.obj src\%%f.c
for %%f in (main mouse keyboard config system menu files) do wcc %CF% -we -os -fo=src\%%f.obj src\%%f.c
for %%f in (launcher desktop dialog icon calc scrap) do wcc %CF% -we -os -fo=src\%%f.obj src\%%f.c
for %%f in (clock paint drawer inspect bench music) do wcc %CF% -we -os -fo=src\%%f.obj src\%%f.c
for %%f in (puzzle ttt mines reversi snake breaker echo) do wcc %CF% -we -os -fo=src\%%f.obj src\%%f.c
for %%f in (fractal charmap colors card group demo) do wcc %CF% -we -os -fo=src\%%f.obj src\%%f.c
for %%f in (splash quadrix depot timer eyes patience) do wcc %CF% -we -os -fo=src\%%f.obj src\%%f.c
for %%f in (lights settings oracle peek agenda media) do wcc %CF% -we -os -fo=src\%%f.obj src\%%f.c
for %%f in (flic sblaster opl lptdac gif about pong) do wcc %CF% -we -os -fo=src\%%f.obj src\%%f.c
for %%f in (calendar g2048 hiscore find recent filedlg) do wcc %CF% -we -os -fo=src\%%f.obj src\%%f.c
for %%f in (picshow corral typist textscan recycle props) do wcc %CF% -we -os -fo=src\%%f.obj src\%%f.c
echo Linking ...
wlink @castalia.lnk
if not exist CASTALIA.EXE goto fail
echo Build OK.  Run CASTALIA to start the desktop.
echo Building the graphical installer ...
wcc %CF% -we -os -i=src -fo=install\insmain.obj install\insmain.c
wlink @install\install.lnk
if not exist INSTALL.EXE goto fail
echo INSTALL.EXE built (the graphical setup).
goto done

:fail
echo.
echo BUILD FAILED - see the compiler/linker messages above.

:done
set CF=
