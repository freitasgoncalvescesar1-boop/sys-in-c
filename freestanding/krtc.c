#ifndef KRTC_C_INCLUDED
#define KRTC_C_INCLUDED

#include "krtc.h"

#define CMOS_PORT_INDEX 0x70
#define CMOS_PORT_DATA  0x71

#if defined(__x86_64__) || defined(__i386__)
static inline void cmos_outb(uint16_t port, uint8_t val) {
    __asm__ __volatile__ ("outb %0, %1" : : "a"(val), "Nd"(port));
}
static inline uint8_t cmos_inb(uint16_t port) {
    uint8_t ret;
    __asm__ __volatile__ ("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}
#else
static inline void cmos_outb(uint16_t port, uint8_t val) { (void)port; (void)val; }
static inline uint8_t cmos_inb(uint16_t port) { (void)port; return 0; }
#endif

static uint8_t cmos_read(uint8_t reg) {
    cmos_outb(CMOS_PORT_INDEX, (uint8_t)(reg | 0x80)); // 0x80 desativa NMI temporariamente
    return cmos_inb(CMOS_PORT_DATA);
}

static int cmos_is_updating(void) {
    return (cmos_read(0x0A) & 0x80);
}

#define BCD_TO_BIN(val) ((uint8_t)(((val) & 0x0F) + (((val) >> 4) * 10)))

void krtc_get_time(krtc_time_t *t) {
    if (!t) return;

    // Aguarda se o chip estiver no meio do ciclo de incremento de segundo
    int timeout = 10000;
    while (timeout-- && cmos_is_updating());

    uint8_t sec   = cmos_read(0x00);
    uint8_t min   = cmos_read(0x02);
    uint8_t hour  = cmos_read(0x04);
    uint8_t day   = cmos_read(0x07);
    uint8_t month = cmos_read(0x08);
    uint8_t year  = cmos_read(0x09);
    uint8_t reg_b = cmos_read(0x0B);

    // Se estiver em formato BCD, converte para binário puro
    if (!(reg_b & 0x04)) {
        sec   = BCD_TO_BIN(sec);
        min   = BCD_TO_BIN(min);
        hour  = (uint8_t)(((hour & 0x0F) + (((hour & 0x70) >> 4) * 10)) | (hour & 0x80));
        day   = BCD_TO_BIN(day);
        month = BCD_TO_BIN(month);
        year  = BCD_TO_BIN(year);
    }

    // Se o relógio estiver em formato de 12 horas, converte para 24 horas
    if (!(reg_b & 0x02) && (hour & 0x80)) {
        hour = (uint8_t)(((hour & 0x7F) + 12) % 24);
    }

    t->second = sec;
    t->minute = min;
    t->hour   = hour;
    t->day    = day;
    t->month  = month;
    t->year   = (uint16_t)(2000 + year);
}

void krtc_format_time(const krtc_time_t *t, char *buf, size_t max_len) {
    if (!t || !buf || max_len < 9) return;
    buf[0] = (char)('0' + (t->hour / 10));
    buf[1] = (char)('0' + (t->hour % 10));
    buf[2] = ':';
    buf[3] = (char)('0' + (t->minute / 10));
    buf[4] = (char)('0' + (t->minute % 10));
    buf[5] = ':';
    buf[6] = (char)('0' + (t->second / 10));
    buf[7] = (char)('0' + (t->second % 10));
    buf[8] = '\0';
}

void krtc_format_date(const krtc_time_t *t, char *buf, size_t max_len) {
    if (!t || !buf || max_len < 11) return;
    buf[0] = (char)('0' + (t->day / 10));
    buf[1] = (char)('0' + (t->day % 10));
    buf[2] = '/';
    buf[3] = (char)('0' + (t->month / 10));
    buf[4] = (char)('0' + (t->month % 10));
    buf[5] = '/';
    buf[6] = '2';
    buf[7] = '0';
    uint16_t y = t->year % 100;
    buf[8] = (char)('0' + (y / 10));
    buf[9] = (char)('0' + (y % 10));
    buf[10] = '\0';
}

void krtc_format_datetime(const krtc_time_t *t, char *buf, size_t max_len) {
    if (!t || !buf || max_len < 22) return;
    char d[12], h[10];
    krtc_format_date(t, d, sizeof(d));
    krtc_format_time(t, h, sizeof(h));
    size_t pos = 0;
    for (size_t i = 0; d[i] && pos < max_len - 1; i++) buf[pos++] = d[i];
    if (pos < max_len - 3) { buf[pos++] = ' '; buf[pos++] = '|'; buf[pos++] = ' '; }
    for (size_t i = 0; h[i] && pos < max_len - 1; i++) buf[pos++] = h[i];
    buf[pos] = '\0';
}

#endif
