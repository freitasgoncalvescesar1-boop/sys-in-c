#ifndef KRTC_H
#define KRTC_H

#include <stdint.h>
#include <stddef.h>

typedef struct {
    uint8_t  second;
    uint8_t  minute;
    uint8_t  hour;
    uint8_t  day;
    uint8_t  month;
    uint16_t year;
} krtc_time_t;

void krtc_get_time(krtc_time_t *t);
void krtc_format_time(const krtc_time_t *t, char *buf, size_t max_len);
void krtc_format_date(const krtc_time_t *t, char *buf, size_t max_len);
void krtc_format_datetime(const krtc_time_t *t, char *buf, size_t max_len);

#endif
