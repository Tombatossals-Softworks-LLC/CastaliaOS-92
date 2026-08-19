# ======================================================================
# Makefile - CASTALIA/386  (Open Watcom wmake)
# ----------------------------------------------------------------------
# Build a real MS-DOS executable for a 386SX-class machine.
#
#   Target  : DOS, real mode, 16-bit
#   Model   : medium  (-mm : far code, near data; explicit far for VGA)
#   CPU     : 386     (-3  : 386 instruction scheduling)
#   Optimise: size    (-os)
#
# Usage (from this directory, with Open Watcom on PATH):
#
#       wmake              build CASTALIA.EXE
#       wmake clean        delete objects and the executable
#
# A one-shot alternative that needs no make is BUILD.BAT.
# Forward slashes are used in paths; Open Watcom accepts them on DOS too.
# ======================================================================

CC      = wcc
# -os (optimise for SIZE) everywhere: the .EXE has to fit conventional
# memory alongside DOS, and most of these 62 modules are applet code that
# runs once per user action, where size beats speed every time.
# -we: warnings ARE errors.  -wx alone is only the maximum warning
# LEVEL - it prints W112 "Pointer truncated" and then exits 0.  W112 is
# Watcom's own diagnostic for the medium-model near/far trap that has
# bitten this project four times, and for four years of commits it could
# not fail the build.  Three documents claimed otherwise.
CFLAGS  = -bt=dos -mm -3 -os -zq -wx -we
# ...except the four modules that ARE the inner loop.  Every pixel the
# shell draws goes through video.c, every character through font.c, every
# window compose through window.c and ui.c, up to 18 times a second.  For
# those, -otexan (favour time, expand inline, no aliasing assumptions)
# buys loop unrolling and strength reduction that -os declines, and the
# handful of kilobytes it costs is the best-spent space in the build.
CFLAGS_FAST = -bt=dos -mm -3 -otexan -zq -wx -we
LINKER  = wlink
SRC     = src

HDRS = $(SRC)/castalia.h $(SRC)/video.h $(SRC)/font.h $(SRC)/mouse.h &
       $(SRC)/keyboard.h $(SRC)/ui.h $(SRC)/config.h $(SRC)/system.h &
       $(SRC)/window.h $(SRC)/menu.h $(SRC)/files.h $(SRC)/launcher.h &
       $(SRC)/desktop.h $(SRC)/dialog.h $(SRC)/icon.h $(SRC)/calc.h &
       $(SRC)/scrap.h $(SRC)/clock.h $(SRC)/paint.h $(SRC)/drawer.h &
       $(SRC)/inspect.h $(SRC)/bench.h $(SRC)/music.h $(SRC)/puzzle.h &
       $(SRC)/ttt.h $(SRC)/mines.h $(SRC)/reversi.h $(SRC)/snake.h &
       $(SRC)/breaker.h $(SRC)/echo.h $(SRC)/fractal.h &
       $(SRC)/charmap.h $(SRC)/colors.h $(SRC)/card.h $(SRC)/group.h &
       $(SRC)/quadrix.h $(SRC)/depot.h $(SRC)/timer.h $(SRC)/eyes.h &
       $(SRC)/patience.h $(SRC)/lights.h $(SRC)/settings.h &
       $(SRC)/oracle.h $(SRC)/peek.h $(SRC)/agenda.h $(SRC)/media.h $(SRC)/gif.h $(SRC)/about.h &
       $(SRC)/flic.h $(SRC)/sblaster.h $(SRC)/opl.h $(SRC)/lptdac.h $(SRC)/demo.h $(SRC)/splash.h &
       $(SRC)/pong.h $(SRC)/calendar.h $(SRC)/g2048.h $(SRC)/hiscore.h &
       $(SRC)/find.h $(SRC)/recent.h $(SRC)/filedlg.h $(SRC)/picshow.h $(SRC)/corral.h $(SRC)/typist.h &
       $(SRC)/textscan.h

OBJS = $(SRC)/main.obj $(SRC)/video.obj $(SRC)/font.obj $(SRC)/mouse.obj &
       $(SRC)/keyboard.obj $(SRC)/ui.obj $(SRC)/config.obj &
       $(SRC)/system.obj $(SRC)/window.obj $(SRC)/menu.obj &
       $(SRC)/files.obj $(SRC)/launcher.obj $(SRC)/desktop.obj &
       $(SRC)/dialog.obj $(SRC)/icon.obj $(SRC)/calc.obj &
       $(SRC)/scrap.obj $(SRC)/clock.obj $(SRC)/paint.obj &
       $(SRC)/drawer.obj $(SRC)/inspect.obj $(SRC)/bench.obj &
       $(SRC)/music.obj $(SRC)/puzzle.obj $(SRC)/ttt.obj &
       $(SRC)/mines.obj $(SRC)/reversi.obj $(SRC)/snake.obj &
       $(SRC)/breaker.obj $(SRC)/echo.obj $(SRC)/fractal.obj &
       $(SRC)/charmap.obj $(SRC)/colors.obj $(SRC)/card.obj &
       $(SRC)/group.obj $(SRC)/quadrix.obj $(SRC)/depot.obj &
       $(SRC)/timer.obj $(SRC)/eyes.obj &
       $(SRC)/patience.obj $(SRC)/lights.obj $(SRC)/settings.obj &
       $(SRC)/oracle.obj $(SRC)/peek.obj $(SRC)/agenda.obj $(SRC)/media.obj $(SRC)/gif.obj $(SRC)/about.obj &
       $(SRC)/flic.obj $(SRC)/sblaster.obj $(SRC)/opl.obj $(SRC)/lptdac.obj $(SRC)/demo.obj $(SRC)/splash.obj &
       $(SRC)/pong.obj $(SRC)/calendar.obj $(SRC)/g2048.obj $(SRC)/hiscore.obj &
       $(SRC)/find.obj      $(SRC)/recent.obj $(SRC)/filedlg.obj $(SRC)/picshow.obj $(SRC)/corral.obj $(SRC)/typist.obj &
       $(SRC)/textscan.obj

all : CASTALIA.EXE INSTALL.EXE

# ---- link ------------------------------------------------------------
CASTALIA.EXE : $(OBJS)
	$(LINKER) system dos option quiet,map=castalia.map,stack=8192 name $@ &
	  file $(SRC)/main.obj     file $(SRC)/video.obj    &
	  file $(SRC)/font.obj     file $(SRC)/mouse.obj    &
	  file $(SRC)/keyboard.obj file $(SRC)/ui.obj       &
	  file $(SRC)/config.obj   file $(SRC)/system.obj   &
	  file $(SRC)/window.obj   file $(SRC)/menu.obj     &
	  file $(SRC)/files.obj    file $(SRC)/launcher.obj &
	  file $(SRC)/desktop.obj  file $(SRC)/dialog.obj   &
	  file $(SRC)/icon.obj     file $(SRC)/calc.obj     &
	  file $(SRC)/scrap.obj    file $(SRC)/clock.obj    &
	  file $(SRC)/paint.obj    file $(SRC)/drawer.obj   &
	  file $(SRC)/inspect.obj  file $(SRC)/bench.obj     &
	  file $(SRC)/music.obj    file $(SRC)/puzzle.obj   &
	  file $(SRC)/ttt.obj      file $(SRC)/mines.obj    &
	  file $(SRC)/reversi.obj  file $(SRC)/snake.obj    &
	  file $(SRC)/breaker.obj  file $(SRC)/echo.obj     &
	  file $(SRC)/fractal.obj                           &
	  file $(SRC)/charmap.obj  file $(SRC)/colors.obj   &
	  file $(SRC)/card.obj     file $(SRC)/group.obj    &
	  file $(SRC)/quadrix.obj  file $(SRC)/depot.obj    &
	  file $(SRC)/timer.obj    file $(SRC)/eyes.obj     &
	  file $(SRC)/patience.obj file $(SRC)/lights.obj   &
	  file $(SRC)/settings.obj                          &
	  file $(SRC)/oracle.obj                            &
	  file $(SRC)/peek.obj                              &
	  file $(SRC)/agenda.obj                            &
	  file $(SRC)/media.obj                             &
	  file $(SRC)/gif.obj                               &
	  file $(SRC)/about.obj                             &
	  file $(SRC)/flic.obj                              &
	  file $(SRC)/sblaster.obj                          &
	  file $(SRC)/opl.obj                               &
	  file $(SRC)/lptdac.obj                            &
	  file $(SRC)/demo.obj     file $(SRC)/splash.obj  &
	  file $(SRC)/pong.obj     file $(SRC)/calendar.obj &
	  file $(SRC)/g2048.obj    file $(SRC)/hiscore.obj &
	  file $(SRC)/find.obj    file $(SRC)/picshow.obj &
	  file $(SRC)/recent.obj  file $(SRC)/filedlg.obj &
	  file $(SRC)/corral.obj  file $(SRC)/typist.obj &
	  file $(SRC)/textscan.obj

# ---- compile (explicit rules; any header change rebuilds all) --------
$(SRC)/main.obj : $(SRC)/main.c $(HDRS)
	$(CC) $(CFLAGS) -fo=$@ $(SRC)/main.c
$(SRC)/video.obj : $(SRC)/video.c $(HDRS)
	$(CC) $(CFLAGS_FAST) -fo=$@ $(SRC)/video.c
$(SRC)/font.obj : $(SRC)/font.c $(HDRS)
	$(CC) $(CFLAGS_FAST) -fo=$@ $(SRC)/font.c
$(SRC)/mouse.obj : $(SRC)/mouse.c $(HDRS)
	$(CC) $(CFLAGS) -fo=$@ $(SRC)/mouse.c
$(SRC)/keyboard.obj : $(SRC)/keyboard.c $(HDRS)
	$(CC) $(CFLAGS) -fo=$@ $(SRC)/keyboard.c
$(SRC)/ui.obj : $(SRC)/ui.c $(HDRS)
	$(CC) $(CFLAGS_FAST) -fo=$@ $(SRC)/ui.c
$(SRC)/config.obj : $(SRC)/config.c $(HDRS)
	$(CC) $(CFLAGS) -fo=$@ $(SRC)/config.c
$(SRC)/system.obj : $(SRC)/system.c $(HDRS)
	$(CC) $(CFLAGS) -fo=$@ $(SRC)/system.c
$(SRC)/window.obj : $(SRC)/window.c $(HDRS)
	$(CC) $(CFLAGS_FAST) -fo=$@ $(SRC)/window.c
$(SRC)/menu.obj : $(SRC)/menu.c $(HDRS)
	$(CC) $(CFLAGS) -fo=$@ $(SRC)/menu.c
$(SRC)/files.obj : $(SRC)/files.c $(HDRS)
	$(CC) $(CFLAGS) -fo=$@ $(SRC)/files.c
$(SRC)/launcher.obj : $(SRC)/launcher.c $(HDRS)
	$(CC) $(CFLAGS) -fo=$@ $(SRC)/launcher.c
$(SRC)/desktop.obj : $(SRC)/desktop.c $(HDRS)
	$(CC) $(CFLAGS) -fo=$@ $(SRC)/desktop.c
$(SRC)/dialog.obj : $(SRC)/dialog.c $(HDRS)
	$(CC) $(CFLAGS) -fo=$@ $(SRC)/dialog.c
$(SRC)/icon.obj : $(SRC)/icon.c $(HDRS)
	$(CC) $(CFLAGS) -fo=$@ $(SRC)/icon.c
$(SRC)/calc.obj : $(SRC)/calc.c $(HDRS)
	$(CC) $(CFLAGS) -fo=$@ $(SRC)/calc.c
$(SRC)/scrap.obj : $(SRC)/scrap.c $(HDRS)
	$(CC) $(CFLAGS) -fo=$@ $(SRC)/scrap.c
$(SRC)/clock.obj : $(SRC)/clock.c $(HDRS)
	$(CC) $(CFLAGS) -fo=$@ $(SRC)/clock.c
$(SRC)/paint.obj : $(SRC)/paint.c $(HDRS)
	$(CC) $(CFLAGS) -fo=$@ $(SRC)/paint.c
$(SRC)/drawer.obj : $(SRC)/drawer.c $(HDRS)
	$(CC) $(CFLAGS) -fo=$@ $(SRC)/drawer.c
$(SRC)/inspect.obj : $(SRC)/inspect.c $(HDRS)
	$(CC) $(CFLAGS) -fo=$@ $(SRC)/inspect.c
$(SRC)/bench.obj : $(SRC)/bench.c $(HDRS)
	$(CC) $(CFLAGS) -fo=$@ $(SRC)/bench.c
$(SRC)/music.obj : $(SRC)/music.c $(HDRS)
	$(CC) $(CFLAGS) -fo=$@ $(SRC)/music.c
$(SRC)/puzzle.obj : $(SRC)/puzzle.c $(HDRS)
	$(CC) $(CFLAGS) -fo=$@ $(SRC)/puzzle.c
$(SRC)/ttt.obj : $(SRC)/ttt.c $(HDRS)
	$(CC) $(CFLAGS) -fo=$@ $(SRC)/ttt.c
$(SRC)/charmap.obj : $(SRC)/charmap.c $(HDRS)
	$(CC) $(CFLAGS) -fo=$@ $(SRC)/charmap.c
$(SRC)/mines.obj : $(SRC)/mines.c $(HDRS)
	$(CC) $(CFLAGS) -fo=$@ $(SRC)/mines.c
$(SRC)/reversi.obj : $(SRC)/reversi.c $(HDRS)
	$(CC) $(CFLAGS) -fo=$@ $(SRC)/reversi.c
$(SRC)/snake.obj : $(SRC)/snake.c $(HDRS)
	$(CC) $(CFLAGS) -fo=$@ $(SRC)/snake.c
$(SRC)/breaker.obj : $(SRC)/breaker.c $(HDRS)
	$(CC) $(CFLAGS) -fo=$@ $(SRC)/breaker.c
$(SRC)/echo.obj : $(SRC)/echo.c $(HDRS)
	$(CC) $(CFLAGS) -fo=$@ $(SRC)/echo.c
$(SRC)/fractal.obj : $(SRC)/fractal.c $(HDRS)
	$(CC) $(CFLAGS) -fo=$@ $(SRC)/fractal.c
$(SRC)/colors.obj : $(SRC)/colors.c $(HDRS)
	$(CC) $(CFLAGS) -fo=$@ $(SRC)/colors.c
$(SRC)/card.obj : $(SRC)/card.c $(HDRS)
	$(CC) $(CFLAGS) -fo=$@ $(SRC)/card.c
$(SRC)/group.obj : $(SRC)/group.c $(HDRS)
	$(CC) $(CFLAGS) -fo=$@ $(SRC)/group.c
$(SRC)/demo.obj : $(SRC)/demo.c $(HDRS)
	$(CC) $(CFLAGS) -fo=$@ $(SRC)/demo.c
$(SRC)/splash.obj : $(SRC)/splash.c $(HDRS)
	$(CC) $(CFLAGS) -fo=$@ $(SRC)/splash.c
$(SRC)/quadrix.obj : $(SRC)/quadrix.c $(HDRS)
	$(CC) $(CFLAGS) -fo=$@ $(SRC)/quadrix.c
$(SRC)/depot.obj : $(SRC)/depot.c $(HDRS)
	$(CC) $(CFLAGS) -fo=$@ $(SRC)/depot.c
$(SRC)/timer.obj : $(SRC)/timer.c $(HDRS)
	$(CC) $(CFLAGS) -fo=$@ $(SRC)/timer.c
$(SRC)/eyes.obj : $(SRC)/eyes.c $(HDRS)
	$(CC) $(CFLAGS) -fo=$@ $(SRC)/eyes.c
$(SRC)/patience.obj : $(SRC)/patience.c $(HDRS)
	$(CC) $(CFLAGS) -fo=$@ $(SRC)/patience.c
$(SRC)/lights.obj : $(SRC)/lights.c $(HDRS)
	$(CC) $(CFLAGS) -fo=$@ $(SRC)/lights.c
$(SRC)/settings.obj : $(SRC)/settings.c $(HDRS)
	$(CC) $(CFLAGS) -fo=$@ $(SRC)/settings.c

$(SRC)/oracle.obj : $(SRC)/oracle.c $(HDRS)
	$(CC) $(CFLAGS) -fo=$@ $(SRC)/oracle.c

$(SRC)/peek.obj : $(SRC)/peek.c $(HDRS)
	$(CC) $(CFLAGS) -fo=$@ $(SRC)/peek.c

$(SRC)/agenda.obj : $(SRC)/agenda.c $(HDRS)
	$(CC) $(CFLAGS) -fo=$@ $(SRC)/agenda.c

$(SRC)/media.obj : $(SRC)/media.c $(HDRS)
	$(CC) $(CFLAGS) -fo=$@ $(SRC)/media.c

$(SRC)/flic.obj : $(SRC)/flic.c $(HDRS)
	$(CC) $(CFLAGS) -fo=$@ $(SRC)/flic.c

$(SRC)/sblaster.obj : $(SRC)/sblaster.c $(HDRS)
	$(CC) $(CFLAGS) -fo=$@ $(SRC)/sblaster.c

$(SRC)/opl.obj : $(SRC)/opl.c $(HDRS)
	$(CC) $(CFLAGS) -fo=$@ $(SRC)/opl.c

$(SRC)/lptdac.obj : $(SRC)/lptdac.c $(HDRS)
	$(CC) $(CFLAGS) -fo=$@ $(SRC)/lptdac.c

$(SRC)/gif.obj : $(SRC)/gif.c $(HDRS)
	$(CC) $(CFLAGS) -fo=$@ $(SRC)/gif.c

$(SRC)/about.obj : $(SRC)/about.c $(HDRS)
	$(CC) $(CFLAGS) -fo=$@ $(SRC)/about.c

$(SRC)/pong.obj : $(SRC)/pong.c $(HDRS)
	$(CC) $(CFLAGS) -fo=$@ $(SRC)/pong.c

$(SRC)/calendar.obj : $(SRC)/calendar.c $(HDRS)
	$(CC) $(CFLAGS) -fo=$@ $(SRC)/calendar.c

$(SRC)/g2048.obj : $(SRC)/g2048.c $(HDRS)
	$(CC) $(CFLAGS) -fo=$@ $(SRC)/g2048.c

$(SRC)/hiscore.obj : $(SRC)/hiscore.c $(HDRS)
	$(CC) $(CFLAGS) -fo=$@ $(SRC)/hiscore.c

$(SRC)/find.obj : $(SRC)/find.c $(HDRS)
	$(CC) $(CFLAGS) -fo=$@ $(SRC)/find.c

$(SRC)/recent.obj : $(SRC)/recent.c $(HDRS)
	$(CC) $(CFLAGS) -fo=$@ $(SRC)/recent.c

$(SRC)/textscan.obj : $(SRC)/textscan.c $(HDRS)
	$(CC) $(CFLAGS) -fo=$@ $(SRC)/textscan.c

$(SRC)/filedlg.obj : $(SRC)/filedlg.c $(HDRS)
	$(CC) $(CFLAGS) -fo=$@ $(SRC)/filedlg.c

$(SRC)/picshow.obj : $(SRC)/picshow.c $(HDRS)
	$(CC) $(CFLAGS) -fo=$@ $(SRC)/picshow.c

$(SRC)/corral.obj : $(SRC)/corral.c $(HDRS)
	$(CC) $(CFLAGS) -fo=$@ $(SRC)/corral.c

$(SRC)/typist.obj : $(SRC)/typist.c $(HDRS)
	$(CC) $(CFLAGS) -fo=$@ $(SRC)/typist.c

# ---- the graphical installer (a second, smaller executable) ---------
install/insmain.obj : install/insmain.c $(HDRS)
	$(CC) $(CFLAGS) -i=$(SRC) -fo=$@ install/insmain.c

INSTALL.EXE : install/insmain.obj $(SRC)/video.obj $(SRC)/font.obj &
              $(SRC)/ui.obj $(SRC)/keyboard.obj
	$(LINKER) system dos option quiet,stack=8192 name $@ &
	  file install/insmain.obj &
	  file $(SRC)/video.obj    file $(SRC)/font.obj &
	  file $(SRC)/ui.obj       file $(SRC)/keyboard.obj

# ---- housekeeping ----------------------------------------------------
# rm, not del: this repo is built on Linux (CI included), where `del`
# fails silently - so `wmake clean` exited 0 having cleaned nothing, and
# the next `wmake` was a no-op that handed back a stale binary.
clean : .SYMBOLIC
	rm -f $(SRC)/*.obj install/*.obj CASTALIA.EXE INSTALL.EXE castalia.map

# ---- staging the ready-to-run release --------------------------------
# release/ is committed so the repository can be unzipped and run without
# a Watcom toolchain.  Being a hand-made copy, it drifted: its README
# spent three versions telling people Alt+Tab cycles windows, long after
# the cycler had moved to Shift+Tab.  ci/release.sh now fails on that
# class of drift, and this is the target that fixes it.
release : CASTALIA.EXE .SYMBOLIC
	cp CASTALIA.EXE release/CASTALIA.EXE
	cp README.TXT   release/README.TXT
	rm -rf release/ASSETS
	/bin/mkdir -p release/ASSETS
	cp -r assets/icons  release/ASSETS/ICONS
	cp -r assets/media  release/ASSETS/MEDIA
	cp -r assets/themes release/ASSETS/THEMES
	bash ci/release.sh --strict

# ---- every gate, in one command --------------------------------------
# Each gate lives in its own script, so running them all is a list you
# have to remember - and BUILD.TXT's copy of that list had already gone
# stale, omitting nearfar and release.  One target, one order, stops at
# the first failure.  (ci/smoke.sh is deliberately not here: it wants
# DOSBox, Xvfb and ImageMagick, so it cannot be a precondition for
# committing on a machine that has none of them.  Run it by hand.)
# Every gate, then ONE line saying so.  wmake aborts on the first
# failure, so the absence of that last line is the signal - which
# matters because the natural way to read this target's output is to
# grep it for the gate you were thinking about, and a commit went out
# today past a "consistency: FAILED" that scrolled past exactly that
# way.  "check: ALL GATES PASSED" is the only line worth trusting.
check : CASTALIA.EXE .SYMBOLIC
	bash ci/lint.sh
	bash ci/nearfar.sh
	bash ci/consistency.sh
	bash ci/release.sh
	bash ci/memory.sh castalia.map
	make -C tests
	echo check: ALL GATES PASSED
