@echo off
REM ======================================================================
REM  CASTSHEL.BAT - start CASTALIA/386 with free-memory game launching
REM ----------------------------------------------------------------------
REM  Run Castalia through this wrapper (type CASTSHEL instead of CASTALIA)
REM  to enable "free-memory" launches: a [shortcut] marked  freemem=true
REM  unloads Castalia completely before the program runs, so a big DOS game
REM  gets ALL of conventional memory, and the desktop returns when it ends.
REM
REM  How it works: Castalia sets nothing permanent.  When you launch a
REM  freemem entry it writes CASTRUN.BAT and exits; the loop below runs
REM  that file (the game, with the whole machine to itself) and then starts
REM  Castalia again.  Plain CASTALIA still works - it just keeps the shell
REM  resident while a program runs (fine for editors and tools).
REM
REM  Run this from the Castalia directory (where CASTALIA.EXE lives).
REM ======================================================================
set CASTSHEL=1

:castloop
CASTALIA.EXE
if not exist CASTRUN.BAT goto castdone
call CASTRUN.BAT
if exist CASTRUN.BAT del CASTRUN.BAT
goto castloop

:castdone
set CASTSHEL=
