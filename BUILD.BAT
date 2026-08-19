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
REM  The command line is kept under the DOS 127-character limit by
REM  compiling in two FOR groups and linking from castalia.lnk.
REM
REM  Toolchain: 16-bit real mode, medium model (-mm), 386 code (-3),
REM             size optimised (-os), maximum AND fatal warnings (-wx -we).
REM
REM  video, font, window and ui are the inner loop - every pixel, every
REM  character and every window compose goes through them, up to 18 times
REM  a second - so those four are built for SPEED (-otexan) instead.  Keep
REM  this in step with CFLAGS_FAST in the Makefile.
REM ======================================================================
echo Compiling CASTALIA/386 ...
for %%f in (video font window ui) do wcc -bt=dos -mm -3 -otexan -zq -wx -we -fo=src\%%f.obj src\%%f.c
for %%f in (main mouse keyboard config) do wcc -bt=dos -mm -3 -os -zq -wx -we -fo=src\%%f.obj src\%%f.c
for %%f in (system menu files launcher desktop dialog icon) do wcc -bt=dos -mm -3 -os -zq -wx -we -fo=src\%%f.obj src\%%f.c
for %%f in (calc scrap clock paint drawer inspect bench music) do wcc -bt=dos -mm -3 -os -zq -wx -we -fo=src\%%f.obj src\%%f.c
for %%f in (puzzle ttt mines reversi snake breaker echo fractal) do wcc -bt=dos -mm -3 -os -zq -wx -we -fo=src\%%f.obj src\%%f.c
for %%f in (charmap colors card group demo splash) do wcc -bt=dos -mm -3 -os -zq -wx -we -fo=src\%%f.obj src\%%f.c
for %%f in (quadrix depot timer eyes patience lights settings oracle peek agenda media flic sblaster opl lptdac gif about) do wcc -bt=dos -mm -3 -os -zq -wx -we -fo=src\%%f.obj src\%%f.c
for %%f in (pong calendar g2048 hiscore find recent filedlg picshow corral typist textscan) do wcc -bt=dos -mm -3 -os -zq -wx -we -fo=src\%%f.obj src\%%f.c
echo Linking ...
wlink @castalia.lnk
if exist CASTALIA.EXE echo Build OK.  Run CASTALIA to start the desktop.
echo Building the graphical installer ...
wcc -bt=dos -mm -3 -os -zq -wx -we -i=src -fo=install\insmain.obj install\insmain.c
wlink @install\install.lnk
if exist INSTALL.EXE echo INSTALL.EXE built (the graphical setup).
