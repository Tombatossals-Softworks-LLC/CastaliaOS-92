/* ======================================================================
 * launcher.c - External program launching for CASTALIA/386
 * ====================================================================== */
#include <stdlib.h>    /* system                                         */
#include <stdio.h>     /* printf                                         */
#include <conio.h>     /* getch                                          */
#include <dos.h>       /* _dos_getdrive / _dos_setdrive                  */
#include <direct.h>    /* getcwd / chdir                                 */
#include "launcher.h"
#include "video.h"
#include "system.h"    /* sys_home / sys_home_path                       */

int launcher_run(const char *path, const char *command, const char *theme)
{
    char     saved_dir[80];
    unsigned saved_drive, total;
    int      rc;

    if (command == NULL || command[0] == '\0')
        return 0;

    /* Remember where we are so the shell's own location is preserved. */
    _dos_getdrive(&saved_drive);
    getcwd(saved_dir, sizeof(saved_dir));

    /* Leave the GUI - fading to black first (no-op when animations are
       off), so the desktop dissolves instead of snapping to text mode. */
    video_fade_out();
    video_text_mode();

    /* Move to the requested working directory, if any. */
    if (path != NULL && path[0] != '\0') {
        if (path[1] == ':') {
            unsigned d = (unsigned)((path[0] | 0x20) - 'a');   /* 0 = A */
            _dos_setdrive(d + 1, &total);
        }
        chdir(path);
    }

    printf("\nCASTALIA/386 - launching: %s\n", command);
    printf("The desktop will return when the program exits.\n\n");

    rc = system(command);
    if (rc == -1)
        printf("\nCould not start '%s'.\n", command);

    printf("\nPress any key to return to Castalia...");
    (void)getch();

    /* Restore our original drive and directory. */
    _dos_setdrive(saved_drive, &total);
    chdir(saved_dir);

    /* Re-enter graphics and re-apply the theme (the DAC was reset), then
       black the palette out again: main repaints the desktop and its
       present path fades it back in - the return mirrors the departure. */
    video_graphics_mode();
    video_set_theme(theme);
    video_blackout();
    return rc;
}

/* ----------------------------------------------------------------------
 * Free-memory launch.  For a memory-hungry program (a big game) we do NOT
 * stay resident - that would cost it ~120 KB of conventional memory.
 * Instead we write CASTRUN.BAT and exit; the CASTSHEL.BAT wrapper that
 * launched us runs that file (so the program has ALL of conventional
 * memory) and then relaunches Castalia.  The generated batch returns to
 * our home drive/directory afterwards so the shell comes back in place.
 * -------------------------------------------------------------------- */
/* TRUE when CASTRUN.BAT really reached the disk.  The caller unloads the
   whole shell on the strength of this, so a silent failure on read-only
   media closed the user's desktop, never ran their program, and explained
   nothing. */
bool_t launcher_write_runfile(const char *path, const char *command)
{
    FILE       *f;
    char        runp[80];
    const char *home = sys_home();       /* e.g. "C:\CASTALIA" - captured
                                            at start-up, NOT the browsed
                                            directory we may stand in now */

    if (command == NULL || command[0] == '\0')
        return FALSE;

    /* CASTSHEL.BAT looks for CASTRUN.BAT next to CASTALIA.EXE, so write
       it there explicitly; the current directory could be anywhere. */
    sys_home_path(runp, (int)sizeof(runp), "CASTRUN.BAT");
    f = fopen(runp, "w");
    if (f == NULL)
        return FALSE;

    fprintf(f, "@echo off\n");
    if (path != NULL && path[0] != '\0') {
        if (path[1] == ':')
            fprintf(f, "%c:\n", path[0]);          /* switch to its drive  */
        fprintf(f, "cd %s\n", path);
    }
    fprintf(f, "%s\n", command);                   /* run the program      */
    if (home[1] == ':')
        fprintf(f, "%c:\n", home[0]);              /* back home drive      */
    fprintf(f, "cd %s\n", home);                   /* back home directory  */
    if (ferror(f)) { fclose(f); return FALSE; }
    return (fclose(f) == 0) ? TRUE : FALSE;
}
