#ifndef KSOUND_H
#define KSOUND_H

#include <stdint.h>

#define NOTE_C4  261
#define NOTE_D4  294
#define NOTE_E4  330
#define NOTE_F4  349
#define NOTE_G4  392
#define NOTE_A4  440
#define NOTE_B4  494
#define NOTE_C5  523
#define NOTE_E5  659
#define NOTE_G5  784

void ksound_init(void);
void ksound_play(uint32_t freq_hz);
void ksound_stop(void);
void ksound_beep(uint32_t freq_hz, uint32_t duration_ticks, uint32_t current_tick);
void ksound_update(uint32_t current_tick);

#endif
