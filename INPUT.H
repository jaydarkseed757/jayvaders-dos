#ifndef INPUT_H
#define INPUT_H

/*
 * INPUT.H -- direct port-60h keyboard layer.
 * Open Watcom wcc, medium model (-ml), 16-bit real-mode DOS.
 *
 * Hooks IRQ1 (INT 9h) for low-latency key state. Use kbhit()/getch() for
 * menus (TITLE.C); use this module in the game loop (GAME.C, HISCORE.C).
 */

/* -------------------------------------------------------------------------
 * Logical key indices
 * -------------------------------------------------------------------------*/
#define KEY_LEFT   0  /* Left arrow, kp-4, Z    */
#define KEY_RIGHT  1  /* Right arrow, kp-6, X   */
#define KEY_FIRE   2  /* Space, Left/Right Ctrl  */
#define KEY_UP     3  /* Up arrow, kp-8 (initials entry) */
#define KEY_DOWN   4  /* Down arrow, kp-2 (initials entry) */
#define KEY_ESC    5  /* Escape */
#define KEY_ENTER  6  /* Enter (initials entry confirm) */
#define KEY_COUNT  7

/* Set to 1 when the player requests quit-to-DOS (ESC). The main loop checks
 * this after title_run()/game_run() and breaks; cleanup() then shows stats. */
extern int g_quit_requested;

/* -------------------------------------------------------------------------
 * API
 * -------------------------------------------------------------------------*/

/* Hook IRQ1 (INT 9h). Call once before the game loop; zeroes all key state. */
void inp_install(void);

/* Restore the original IRQ1 vector. Call before program exit. */
void inp_remove(void);

/* Returns 1 if the logical key is currently held down, 0 otherwise. */
int inp_pressed(int key);

/*
 * Returns 1 if the key transitioned from up to down since the last call to
 * inp_clear_edge(). Use for single-fire actions (shooting, menu confirm).
 */
int inp_just_pressed(int key);

/* Clear all just-pressed edge flags. Call once per frame after reading. */
void inp_clear_edge(void);

#endif /* INPUT_H */
