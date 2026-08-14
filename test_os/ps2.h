#ifndef PS2_H
#define PS2_H

#include <stdint.h>
#include "../freestanding/kgfx.h"

void ps2_init(void);
void ps2_keyboard_handler(void);
void ps2_mouse_handler(void);

kgfx_mouse_t *ps2_get_mouse_state(void);
const char *ps2_get_input_buffer(void);
void ps2_clear_input_buffer(void);

#endif
