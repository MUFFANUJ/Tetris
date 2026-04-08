/* =============================================================================
 * keyboard.c  —  Input/Output Management Module
 * =============================================================================
 * OS MODULE: I/O Management
 *   Manages how this "computer" (our Tetris OS) talks to the world.
 *   Wraps the POSIX terminal so we get raw, non-blocking key reads —
 *   the same role a real keyboard driver plays in a proper OS kernel.
 *
 * RULES COMPLIANCE:
 *   - <stdio.h>  : allowed (terminal I/O only — getchar / EOF)
 *   - <stdlib.h> : allowed (system() for stty terminal control)
 *   - printf / scanf : NOT used here; screen.c owns all output.
 * ============================================================================= */

#include "../include/keyboard.h"
#include <stdio.h>   /* getchar(), EOF — terminal I/O exception allowed by spec */
#include <stdlib.h>  /* system() — process control exception allowed by spec    */

/* ---------------------------------------------------------------------------
 * keyboard_init()
 *   Switches the terminal into "raw" mode so every keypress is delivered
 *   immediately to our game loop without the OS line-buffering it.
 *   Uses stty to configure the terminal for raw, non-blocking input.
 * --------------------------------------------------------------------------- */
void keyboard_init(void) {
    setvbuf(stdin, NULL, _IONBF, 0);  /* disable stdio buffering for stdin */
    system("stty raw -echo -icanon min 0 time 0");
}

/* ---------------------------------------------------------------------------
 * keyboard_restore()
 *   Restores the terminal to normal (cooked) behaviour.
 *   MUST be called before the program exits; otherwise the shell is left in
 *   raw mode and becomes unusable after the game closes.
 * --------------------------------------------------------------------------- */
void keyboard_restore(void) {
    system("stty sane");
}

/* ---------------------------------------------------------------------------
 * keyPressed()
 *   Non-blocking single-character read with arrow key support.
 *   Arrow keys send escape sequences: ESC [ A/B/C/D
 *   Returns: 'w'/'s'/'a'/'d' for arrows, or the actual key pressed
 * --------------------------------------------------------------------------- */
char keyPressed(void) {
    int ch = getchar();          /* non-blocking in raw+min0+time0 mode */
    if (ch == EOF) {
        clearerr(stdin);         /* clear EOF so future keys are read */
        return '\0';
    }

    /* Check for escape sequence (arrow keys) */
    if (ch == '\033') {
        int seq1 = getchar();
        if (seq1 == EOF) { clearerr(stdin); return '\0'; }
        if (seq1 == '[') {
            int seq2 = getchar();
            if (seq2 == EOF) { clearerr(stdin); return '\0'; }
            switch (seq2) {
                case 'A': return 'w';  /* Up arrow -> rotate */
                case 'B': return 's';  /* Down arrow -> soft drop */
                case 'C': return 'd';  /* Right arrow -> move right */
                case 'D': return 'a';  /* Left arrow -> move left */
            }
        }
        return '\0';
    }

    return (char)ch;
}

/* ---------------------------------------------------------------------------
 * readLine(buffer, max_len)
 *   Blocking line read (used for menus / name entry, not the game loop).
 *
 *   Temporarily reverts the terminal to normal mode so the user gets normal
 *   echo + line-editing (backspace etc.), reads one line, then re-enables
 *   raw mode so the game loop continues to work correctly.
 *
 *   Parameters:
 *     buffer  — caller-supplied destination; must be at least max_len bytes.
 *     max_len — maximum characters to store (including null terminator).
 *
 *   Edge cases handled:
 *     - NULL buffer or non-positive max_len → returns immediately (no crash).
 *     - EOF before newline → loop stops; buffer is still null-terminated.
 * --------------------------------------------------------------------------- */
void readLine(char *buffer, int max_len) {
    if (!buffer || max_len <= 0) return;  /* guard: bad arguments */

    /* Temporarily restore normal terminal mode */
    system("stty sane");

    int i = 0;
    while (i < max_len - 1) {    /* leave room for '\0' */
        int ch = getchar();
        if (ch == '\n' || ch == EOF) break;   /* stop at enter or stream end */
        buffer[i++] = (char)ch;
    }
    buffer[i] = '\0';             /* always null-terminate */

    keyboard_init();              /* back to raw mode for the game loop */
}
