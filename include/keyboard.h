#ifndef KEYBOARD_H
#define KEYBOARD_H

/* =============================================================================
 * keyboard.h  —  I/O Management Module (Input Side)
 * ============================================================================= */

void keyboard_init   (void);                    /* enter raw/non-blocking mode */
void keyboard_restore(void);                    /* restore normal terminal     */
char keyPressed      (void);                    /* non-blocking single key read */
void readLine        (char *buffer, int max_len); /* blocking line read         */

#endif /* KEYBOARD_H */
