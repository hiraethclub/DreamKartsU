/* DKU shim: a keyboard as KOS reports it, fed by the platform (SDL keys,
   USB keyboard via nsyskbd, or the software keyboard's text). The queue
   holds key | mods << 8 | leds << 16, like KOS. */
#ifndef DKU_SHIM_DC_MAPLE_KEYBOARD_H
#define DKU_SHIM_DC_MAPLE_KEYBOARD_H

#include <stdbool.h>
#include <stdint.h>
#include <dc/maple.h>

typedef enum {
    KBD_KEY_NONE = 0x00,
    KBD_KEY_A = 0x04,   /* A..Z run 0x04..0x1d */
    KBD_KEY_1 = 0x1e,   /* 1..9 0x1e..0x26, 0 at 0x27 */
    KBD_KEY_0 = 0x27,
    KBD_KEY_ENTER = 0x28,
    KBD_KEY_ESCAPE = 0x29,
    KBD_KEY_BACKSPACE = 0x2a,
    KBD_KEY_TAB = 0x2b,
    KBD_KEY_SPACE = 0x2c,
    KBD_KEY_MINUS = 0x2d,
    KBD_KEY_PLUS = 0x2e,
    KBD_KEY_LBRACKET = 0x2f,
    KBD_KEY_RBRACKET = 0x30,
    KBD_KEY_BACKSLASH = 0x31,
    KBD_KEY_SEMICOLON = 0x33,
    KBD_KEY_QUOTE = 0x34,
    KBD_KEY_TILDE = 0x35,
    KBD_KEY_COMMA = 0x36,
    KBD_KEY_PERIOD = 0x37,
    KBD_KEY_SLASH = 0x38,
    KBD_KEY_RIGHT = 0x4f,
    KBD_KEY_LEFT = 0x50,
    KBD_KEY_DOWN = 0x51,
    KBD_KEY_UP = 0x52,
    KBD_KEY_PAD_ENTER = 0x58
} kbd_key_t;

typedef union {
    struct {
        uint8_t lctrl : 1, lshift : 1, lalt : 1, s1 : 1, rctrl : 1, rshift : 1, ralt : 1, s2 : 1;
    };
    uint8_t raw;
} kbd_mods_t;

typedef union {
    struct {
        uint8_t num_lock : 1, caps_lock : 1, scroll_lock : 1, unused : 5;
    };
    uint8_t raw;
} kbd_leds_t;

#define KBD_REGION_US 2

typedef struct {
    int region;
    kbd_mods_t last_modifiers;
} kbd_state_t;

#define KBD_QUEUE_END -1

kbd_state_t *kbd_get_state(maple_device_t *dev);
int kbd_queue_pop(maple_device_t *dev, bool xlat);
char kbd_key_to_ascii(kbd_key_t key, int region, kbd_mods_t mods, kbd_leds_t leds);

#endif
