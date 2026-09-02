#ifndef KSOUND_C_INCLUDED
#define KSOUND_C_INCLUDED

#include "ksound.h"

#if defined(__x86_64__) || defined(__i386__)
static inline void snd_outb(uint16_t port, uint8_t val) {
    __asm__ __volatile__ ("outb %0, %1" : : "a"(val), "Nd"(port));
}
static inline uint8_t snd_inb(uint16_t port) {
    uint8_t ret;
    __asm__ __volatile__ ("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}
#else
static inline void snd_outb(uint16_t port, uint8_t val) { (void)port; (void)val; }
static inline uint8_t snd_inb(uint16_t port) { (void)port; return 0; }
#endif

static uint32_t sound_stop_tick = 0;
static int sound_playing = 0;

void ksound_init(void) {
    ksound_stop();
}

void ksound_play(uint32_t freq_hz) {
    if (freq_hz == 0) {
        ksound_stop();
        return;
    }
    uint32_t divisor = 1193180 / freq_hz;
    snd_outb(0x43, 0xB6); // Canal 2, onda quadrada
    snd_outb(0x42, (uint8_t)(divisor & 0xFF));
    snd_outb(0x42, (uint8_t)((divisor >> 8) & 0xFF));

    uint8_t tmp = snd_inb(0x61);
    if ((tmp & 3) != 3) {
        snd_outb(0x61, tmp | 3);
    }
    sound_playing = 1;
}

void ksound_stop(void) {
    uint8_t tmp = snd_inb(0x61) & 0xFC;
    snd_outb(0x61, tmp);
    sound_playing = 0;
}

void ksound_beep(uint32_t freq_hz, uint32_t duration_ticks, uint32_t current_tick) {
    ksound_play(freq_hz);
    sound_stop_tick = current_tick + duration_ticks;
}

void ksound_update(uint32_t current_tick) {
    if (sound_playing && sound_stop_tick > 0 && current_tick >= sound_stop_tick) {
        ksound_stop();
        sound_stop_tick = 0;
    }
}

#endif
