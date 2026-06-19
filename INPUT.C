/*
 * INPUT.C -- Direct port-60h keyboard layer (build step 5).
 * Open Watcom wcc, medium model (-ml), 16-bit real-mode DOS.
 *
 * Hooks INT 9h (IRQ1). Maintains two per-key arrays:
 *   s_key_down[] -- 1 while the key is physically held.
 *   s_key_edge[] -- 1 from key-press until inp_clear_edge().
 *
 * Does NOT chain the BIOS handler, suppressing type-ahead buffering during
 * gameplay. inp_remove() restores the original vector before program exit.
 */

#include <i86.h>    /* inp, outp                        */
#include <dos.h>    /* _dos_getvect, _dos_setvect        */
#include "INPUT.H"

/* -------------------------------------------------------------------------
 * Port constants
 * -------------------------------------------------------------------------*/
#define KBD_DATA    0x60   /* keyboard data / scan code port */
#define PIC_CMD     0x20   /* 8259A PIC command port         */
#define PIC_EOI     0x20   /* non-specific End of Interrupt  */

/* -------------------------------------------------------------------------
 * Static state
 * -------------------------------------------------------------------------*/
static unsigned char s_key_down[KEY_COUNT];
static unsigned char s_key_edge[KEY_COUNT];

/* Flat scan-code → logical key map; 0xFF = unmapped. */
static unsigned char s_scan_map[128];

/* 1 after receiving the 0xE0 extended-key prefix. */
static unsigned char s_e0_pending;

/* Saved BIOS IRQ1 vector, restored by inp_remove(). */
static void (__interrupt __far *s_old_irq1)(void);

/* -------------------------------------------------------------------------
 * Scan-code table (make codes; break = make | 0x80)
 * -------------------------------------------------------------------------*/
#define MAP_ENTRY(scan, key) { (unsigned char)(scan), (unsigned char)(key) }

static const struct { unsigned char scan; unsigned char key; } s_map_entries[] = {
    MAP_ENTRY(0x01, KEY_ESC),    /* Escape              */
    MAP_ENTRY(0x1C, KEY_ENTER),  /* Enter               */
    MAP_ENTRY(0x1D, KEY_FIRE),   /* Left Ctrl           */
    MAP_ENTRY(0x2C, KEY_LEFT),   /* Z  (alt-left)       */
    MAP_ENTRY(0x2D, KEY_RIGHT),  /* X  (alt-right)      */
    MAP_ENTRY(0x39, KEY_FIRE),   /* Space               */
    MAP_ENTRY(0x48, KEY_UP),     /* Up arrow / kp-8     */
    MAP_ENTRY(0x4B, KEY_LEFT),   /* Left arrow / kp-4   */
    MAP_ENTRY(0x4D, KEY_RIGHT),  /* Right arrow / kp-6  */
    MAP_ENTRY(0x50, KEY_DOWN),   /* Down arrow / kp-2   */
};

#define MAP_ENTRIES_COUNT  10

/* -------------------------------------------------------------------------
 * IRQ1 handler
 *
 * Extended-key protocol: dedicated cursor keys send 0xE0 then the same scan
 * code as the numeric-keypad equivalent. Set s_e0_pending on 0xE0; process
 * the following code normally (arrow scan codes are identical whether prefixed
 * or not). Break codes from extended keys arrive as E0 + (0x80 | scan).
 * -------------------------------------------------------------------------*/
static void __interrupt __far irq1_handler(void)
{
    unsigned char scan, scan7;
    unsigned char key;

    scan = (unsigned char)inp(KBD_DATA);

    if (scan == 0xE0) {
        s_e0_pending = 1;
        outp(PIC_CMD, PIC_EOI);
        return;
    }

    s_e0_pending = 0;         /* consume prefix regardless of this code */

    if (scan & 0x80) {
        /* Key release (break code): high bit set */
        scan7 = scan & 0x7F;
        if (scan7 < 128) {
            key = s_scan_map[scan7];
            if (key != 0xFF)
                s_key_down[key] = 0;
        }
    } else {
        /* Key press (make code) */
        if (scan < 128) {
            key = s_scan_map[scan];
            if (key != 0xFF) {
                if (!s_key_down[key])
                    s_key_edge[key] = 1;   /* rising edge */
                s_key_down[key] = 1;
            }
        }
    }

    outp(PIC_CMD, PIC_EOI);
}

/* -------------------------------------------------------------------------
 * Public API
 * -------------------------------------------------------------------------*/

void inp_install(void)
{
    int i;

    /* Zero all state */
    for (i = 0; i < KEY_COUNT; i++) {
        s_key_down[i] = 0;
        s_key_edge[i] = 0;
    }
    for (i = 0; i < 128; i++)
        s_scan_map[i] = 0xFF;
    s_e0_pending = 0;

    /* Populate scan map from table */
    for (i = 0; i < MAP_ENTRIES_COUNT; i++)
        s_scan_map[s_map_entries[i].scan] = s_map_entries[i].key;

    /* Save BIOS vector and install our handler */
    s_old_irq1 = _dos_getvect(0x09);
    _dos_setvect(0x09, irq1_handler);
}

void inp_remove(void)
{
    int i;

    _dos_setvect(0x09, s_old_irq1);

    for (i = 0; i < KEY_COUNT; i++) {
        s_key_down[i] = 0;
        s_key_edge[i] = 0;
    }
}

int inp_pressed(int key)
{
    if ((unsigned int)key >= KEY_COUNT) return 0;
    return s_key_down[key] != 0;
}

int inp_just_pressed(int key)
{
    if ((unsigned int)key >= KEY_COUNT) return 0;
    return s_key_edge[key] != 0;
}

void inp_clear_edge(void)
{
    int i;
    for (i = 0; i < KEY_COUNT; i++)
        s_key_edge[i] = 0;
}

/* =========================================================================
 * INPUT_TEST -- text-mode self-test, no EGA required.
 *
 * Compile:
 *   wcc -ml -zf -DINPUT_TEST INPUT.C
 *   wlink system dos file INPUT.obj name INPUTTEST
 *
 * Expected: press keys, their logical names print on screen. ESC exits.
 * =========================================================================*/
#ifdef INPUT_TEST

#include <stdio.h>

static const char *key_names[KEY_COUNT] = {
    "LEFT", "RIGHT", "FIRE", "UP", "DOWN", "ESC", "ENTER"
};

int main(void)
{
    int k, any;

    puts("INPUT TEST -- press keys (ESC to quit)");
    puts("Keys: arrows/ZX=move, Space/Ctrl=fire, Enter=confirm");
    puts("------------------------------------------------------");

    inp_install();

    while (!inp_pressed(KEY_ESC)) {
        any = 0;
        for (k = 0; k < KEY_COUNT; k++) {
            if (inp_just_pressed(k)) {
                if (!any) { any = 1; printf("PRESSED: "); }
                printf("%s ", key_names[k]);
            }
        }
        if (any) printf("\n");
        inp_clear_edge();
    }

    inp_remove();
    puts("ESC detected -- IRQ1 restored. Done.");
    return 0;
}

#endif /* INPUT_TEST */
