#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdint.h>
#include <math.h>
#include <errno.h>
#include "../libutilipc/utilipc.h"

#define COLOR_RESET   "\033[0m"
#define COLOR_TITLE   "\033[1;35m"
#define COLOR_OK      "\033[1;32m"
#define COLOR_TAG     "\033[1;33m"
#define COLOR_VAL     "\033[1;36m"
#define COLOR_MUTED   "\033[0;90m"

#define PI 3.14159265358979323846

#pragma pack(push, 1)
typedef struct {
    char     riff[4];        // "RIFF"
    uint32_t chunk_size;     // 36 + data_size
    char     wave[4];        // "WAVE"
    char     fmt[4];         // "fmt "
    uint32_t subchunk1_size; // 16 para PCM
    uint16_t audio_format;   // 1 para PCM
    uint16_t num_channels;   // 1 (Mono) ou 2 (Stereo)
    uint32_t sample_rate;    // 8000, 22050, 44100, 48000 Hz
    uint32_t byte_rate;      // sample_rate * channels * (bits/8)
    uint16_t block_align;    // channels * (bits/8)
    uint16_t bits_per_sample;// 8, 16 ou 24 bits
    char     data[4];        // "data"
    uint32_t data_size;      // total de bytes do payload
} WavHeader;
#pragma pack(pop)

// --- SÍNTESE FÍSICA DE CORDAS (KARPLUS-STRONG) ---
typedef struct {
    double buffer[2048];
    int length;
    int index;
    double decay;
    int active;
} KarplusVoice;

static KarplusVoice ks_voices[8];

static void ks_init_voices(void) {
    memset(ks_voices, 0, sizeof(ks_voices));
}

static void ks_pluck(int voice_idx, double freq_hz, double decay, uint32_t sample_rate) {
    if (voice_idx < 0 || voice_idx >= 8 || freq_hz <= 20.0) return;
    KarplusVoice *v = &ks_voices[voice_idx];
    v->length = (int)(sample_rate / freq_hz);
    if (v->length > 2048) v->length = 2048;
    if (v->length < 4) v->length = 4;
    v->index = 0;
    v->decay = decay;
    v->active = 1;

    for (int i = 0; i < v->length; i++) {
        v->buffer[i] = (((double)rand() / (double)RAND_MAX) * 2.0 - 1.0);
    }
}

static double ks_process(int voice_idx) {
    if (voice_idx < 0 || voice_idx >= 8) return 0.0;
    KarplusVoice *v = &ks_voices[voice_idx];
    if (!v->active || v->length == 0) return 0.0;

    double curr = v->buffer[v->index];
    int next_idx = (v->index + 1) % v->length;
    double filtered = 0.5 * (curr + v->buffer[next_idx]) * v->decay;
    v->buffer[v->index] = filtered;
    v->index = next_idx;
    return curr;
}

static inline double guitar_distortion(double in, double drive) {
    double x = in * drive;
    return tanh(x);
}

// --- ESTRUTURA PARA SÍNTESE DE ÁGUA / GOTAS ---
typedef struct {
    double freq;
    double start_time;
    double amp;
    double pan;
    int active;
} WaterDrop;

static WaterDrop drops[12];
static double water_stream_noise_l = 0.0;
static double water_stream_noise_r = 0.0;

static void init_water_synth(void) {
    memset(drops, 0, sizeof(drops));
    water_stream_noise_l = 0.0;
    water_stream_noise_r = 0.0;
}

// Preset: Som da Água e Rio Zen com Pingos Procedurais (24-bit)
static void gen_water_sound_24(uint32_t t, uint32_t sr, double *l, double *r) {
    double time_s = (double)t / (double)sr;

    // 1. Gerador do fluxo contínuo de água (Ruído Browniano Filtrado + LFOs)
    double white_l = (((double)rand() / (double)RAND_MAX) * 2.0 - 1.0);
    double white_r = (((double)rand() / (double)RAND_MAX) * 2.0 - 1.0);

    // Filtro passa-baixas duplo em cascata
    water_stream_noise_l = water_stream_noise_l * 0.94 + white_l * 0.06;
    water_stream_noise_r = water_stream_noise_r * 0.94 + white_r * 0.06;

    // Modulação de correnteza com LFO lento
    double current_mod_l = 0.6 + 0.4 * sin(time_s * 0.7 * 2.0 * PI) * cos(time_s * 0.23 * 2.0 * PI);
    double current_mod_r = 0.6 + 0.4 * sin(time_s * 0.75 * 2.0 * PI) * cos(time_s * 0.21 * 2.0 * PI);

    double stream_l = water_stream_noise_l * current_mod_l * 0.35;
    double stream_r = water_stream_noise_r * current_mod_r * 0.35;

    // 2. Disparador de Gotas e Bolhas Aleatórias (Minnebaert/van den Doel model)
    if (rand() % (sr / 14) == 0) {
        for (int i = 0; i < 12; i++) {
            if (!drops[i].active) {
                drops[i].freq = 400.0 + ((double)rand() / (double)RAND_MAX) * 700.0; // 400Hz a 1100Hz
                drops[i].start_time = time_s;
                drops[i].amp = 0.3 + ((double)rand() / (double)RAND_MAX) * 0.5;
                drops[i].pan = ((double)rand() / (double)RAND_MAX); // 0.0 (esq) a 1.0 (dir)
                drops[i].active = 1;
                break;
            }
        }
    }

    double droplets_l = 0.0;
    double droplets_r = 0.0;

    for (int i = 0; i < 12; i++) {
        if (drops[i].active) {
            double dt = time_s - drops[i].start_time;
            if (dt > 0.25) {
                drops[i].active = 0;
                continue;
            }
            // A frequência da gota sobe ligeiramente e o volume decai exponencialmente
            double f = drops[i].freq * (1.0 + 1.2 * dt);
            double env = drops[i].amp * exp(-dt * 28.0);
            double wave = sin(dt * f * 2.0 * PI) * env;

            droplets_l += wave * (1.0 - drops[i].pan);
            droplets_r += wave * drops[i].pan;
        }
    }

    *l = (stream_l + droplets_l) * 0.95;
    *r = (stream_r + droplets_r) * 0.95;
}

// Preset: Cyberpunk / Synthwave Beat Drop (24-bit)
static void init_beat_drop(void) {
    // Nada a inicializar
}

static void gen_beat_drop_24(uint32_t t, uint32_t sr, double *l, double *r) {
    double time_s = (double)t / (double)sr;
    double bpm = 128.0;
    double beat_duration = 60.0 / bpm;        // ~0.46875s por batida
    double bar_duration = beat_duration * 4.0; // 1 compasso

    int bar_idx = (int)(time_s / bar_duration);
    double bar_time = fmod(time_s, bar_duration);
    double beat_time = fmod(time_s, beat_duration);
    int beat_idx = (int)(bar_time / beat_duration); // 0, 1, 2, 3

    // Introdução / Buildup (Compassos 0 e 1) vs THE DROP (Compasso 2 em diante)
    int is_drop = (bar_idx >= 2);

    double kick = 0.0;
    double snare = 0.0;
    double hihat = 0.0;
    double bass = 0.0;
    double synth_lead = 0.0;

    if (!is_drop) {
        // --- BUILDUP / SUBIDA ---
        // Snare roll acelerando
        double roll_interval = (bar_idx == 0) ? (beat_duration / 2.0) : (beat_duration / 4.0);
        double roll_time = fmod(time_s, roll_interval);
        double snare_noise = (((double)rand() / (double)RAND_MAX) * 2.0 - 1.0);
        snare = snare_noise * exp(-roll_time * 30.0) * (0.3 + 0.4 * (bar_time / bar_duration));

        // Riser de frequência senoidal subindo
        double riser_f = 200.0 + (time_s / (bar_duration * 2.0)) * 600.0;
        synth_lead = sin(time_s * riser_f * 2.0 * PI) * 0.3 * (time_s / (bar_duration * 2.0));
    } else {
        // --- THE HEAVY DROP ---
        // 1. Kick 808 Punch (Batidas 0, 1, 2, 3)
        double kick_env = exp(-beat_time * 16.0);
        double kick_pitch = 150.0 * exp(-beat_time * 35.0) + 42.0;
        kick = sin(beat_time * kick_pitch * 2.0 * PI) * kick_env * 1.4;
        kick = guitar_distortion(kick, 1.3);

        // 2. Snare Pesado (Batidas 1 e 3)
        if (beat_idx == 1 || beat_idx == 3) {
            double s_noise = (((double)rand() / (double)RAND_MAX) * 2.0 - 1.0);
            double s_body = sin(beat_time * 180.0 * 2.0 * PI);
            snare = (s_noise * 0.7 + s_body * 0.4) * exp(-beat_time * 18.0) * 1.1;
        }

        // 3. Hi-Hat 16th notes (a cada 1/4 de batida)
        double hat_time = fmod(time_s, beat_duration / 4.0);
        double hat_noise = (((double)rand() / (double)RAND_MAX) * 2.0 - 1.0);
        hihat = hat_noise * exp(-hat_time * 85.0) * 0.25;

        // 4. Bassline Pesada (Sub-Bass + Sawtooth sintetizado)
        int note_step = (int)(time_s / (beat_duration / 2.0)) % 8;
        static const double bass_scale[] = {55.0, 55.0, 65.41, 55.0, 73.42, 55.0, 82.41, 48.99}; // A1 scale
        double bass_f = bass_scale[note_step];
        double saw = 2.0 * fmod(time_s * bass_f, 1.0) - 1.0;
        double sub = sin(time_s * bass_f * 2.0 * PI);
        bass = guitar_distortion(saw * 0.6 + sub * 0.8, 2.2) * 0.5;

        // 5. Synth Lead Arpeggiator Estéreo
        int lead_step = (int)(time_s / (beat_duration / 4.0)) % 16;
        static const double lead_notes[] = {
            440.0, 523.25, 659.25, 783.99, 880.0, 783.99, 659.25, 523.25,
            440.0, 659.25, 783.99, 880.0,  987.77, 880.0, 783.99, 659.25
        };
        double lead_f = lead_notes[lead_step];
        synth_lead = sin(time_s * lead_f * 2.0 * PI) * 0.35;
    }

    double master_l = (kick * 0.9) + (snare * 0.7) + (hihat * 0.6) + (bass * 0.8) + (synth_lead * 0.7);
    double master_r = (kick * 0.9) + (snare * 0.7) + (hihat * 0.4) + (bass * 0.8) + (synth_lead * 0.5);

    // Limitador suave tipo Master Bus
    *l = tanh(master_l * 0.9);
    *r = tanh(master_r * 0.9);
}

// Presets de Áudio
typedef struct {
    int id;
    int bit_depth;
    int is_stereo;
    uint32_t default_rate;
    const char *name;
    const char *author;
    const char *formula_desc;
    void (*init)(void);
    void (*render)(uint32_t t, uint32_t sample_rate, double *left, double *right);
} AudioPreset;

// Presets Clássicos
static void gen_viznut(uint32_t t, uint32_t sr, double *l, double *r) { (void)sr; *l = *r = (double)((uint8_t)(t * (((t >> 12) | (t >> 8)) & (63 & (t >> 4))))); }
static void gen_symphony(uint32_t t, uint32_t sr, double *l, double *r) { (void)sr; *l = *r = (double)((uint8_t)(((t >> 6 | t | t >> (t >> 16)) * 10 + ((t >> 11) & 7)) & 0xFF)); }
static void gen_cosmic(uint32_t t, uint32_t sr, double *l, double *r) { (void)sr; *l = *r = (double)((uint8_t)(((t * (t >> 5 | t >> 8)) >> (t >> 16)) & 0xFF)); }
static void gen_techno(uint32_t t, uint32_t sr, double *l, double *r) { (void)sr; *l = *r = (double)((uint8_t)(((t * 5 & t >> 7) | (t * 3 & t >> 10)) & 0xFF)); }
static void gen_sierra(uint32_t t, uint32_t sr, double *l, double *r) { (void)sr; *l = *r = (double)((uint8_t)((((t * (t >> 8 | t >> 9) & 46 & t >> 8)) ^ (t & t >> 13 | t >> 6)) & 0xFF)); }
static void gen_harmony(uint32_t t, uint32_t sr, double *l, double *r) { (void)sr; *l = *r = (double)((uint8_t)((t * (t ^ t + (t >> 15 | 1) ^ (t - 1280 ^ t) >> 10)) & 0xFF)); }
static void gen_alien(uint32_t t, uint32_t sr, double *l, double *r) { (void)sr; *l = *r = (double)((uint8_t)((((t * (t >> 11 & t >> 8 & 123 & t >> 3)) + (t >> 7 & t >> 10))) & 0xFF)); }

static void gen_ambient_pad_16(uint32_t t, uint32_t sr, double *l, double *r) {
    double time_s = (double)t / (double)sr;
    *l = sin(time_s * 220.0 * 2.0 * PI) * cos(time_s * 0.5 * 2.0 * PI) * 0.5 + sin(time_s * 440.0 * 2.0 * PI + sin(time_s * 2.0 * PI) * 4.0) * 0.4;
    *r = sin(time_s * 222.0 * 2.0 * PI) * cos(time_s * 0.52 * 2.0 * PI) * 0.5 + sin(time_s * 444.0 * 2.0 * PI + cos(time_s * 2.1 * PI) * 4.0) * 0.4;
}

static void gen_snes_fm_16(uint32_t t, uint32_t sr, double *l, double *r) {
    double time_s = (double)t / (double)sr;
    int step = (int)(time_s * 8.0) % 8;
    static const double notes[] = {220.0, 261.63, 329.63, 392.0, 440.0, 523.25, 659.25, 783.99};
    double f = notes[step];
    double wave = sin(time_s * f * 2.0 * PI + sin(time_s * f * 4.0 * PI) * 2.5) * 0.7;
    double bass = sin(time_s * (f / 2.0) * 2.0 * PI) * 0.3;
    *l = wave * 0.8 + bass * 0.5; *r = wave * 0.5 + bass * 0.8;
}

static void gen_alien_pulsar_16(uint32_t t, uint32_t sr, double *l, double *r) {
    double time_s = (double)t / (double)sr;
    double sound = sin(time_s * 330.0 * 2.0 * PI * (1.0 + 0.1 * sin(time_s * 4.0 * PI))) * cos(time_s * 8.0 * PI) * 0.8;
    *l = *r = sound;
}

static void gen_retrowave_16(uint32_t t, uint32_t sr, double *l, double *r) {
    double time_s = (double)t / (double)sr;
    int chord = (int)(time_s * 2.0) % 4;
    double root = (chord == 0) ? 130.81 : (chord == 1) ? 164.81 : (chord == 2) ? 174.61 : 196.0;
    double lead = (sin(time_s * root * 2.0 * PI) + sin(time_s * root * 1.5 * 2.0 * PI) * 0.5 + sin(time_s * root * 2.0 * 2.0 * PI) * 0.25) * 0.6;
    *l = lead * 0.9; *r = lead * 0.7;
}

static void gen_guitar_acoustic_24(uint32_t t, uint32_t sr, double *l, double *r) {
    static const double melody[] = { 164.81, 220.00, 261.63, 329.63, 392.00, 329.63, 261.63, 220.00, 146.83, 220.00, 293.66, 369.99, 440.00, 369.99, 293.66, 220.00 };
    uint32_t step_interval = sr / 5;
    if (t % step_interval == 0) {
        int note_idx = (t / step_interval) % 16;
        int voice = (t / step_interval) % 4;
        ks_pluck(voice, melody[note_idx], 0.995, sr);
    }
    double mix = ks_process(0) + ks_process(1) + ks_process(2) + ks_process(3);
    *l = mix * 0.85; *r = mix * 0.75;
}

static void gen_guitar_rock_24(uint32_t t, uint32_t sr, double *l, double *r) {
    static const double riffs[] = { 82.41, 123.47, 110.0, 164.81, 98.00, 146.83, 82.41, 123.47 };
    uint32_t step_interval = sr / 3;
    if (t % step_interval == 0) {
        int chord_idx = ((t / step_interval) % 4) * 2;
        ks_pluck(0, riffs[chord_idx], 0.991, sr);
        ks_pluck(1, riffs[chord_idx + 1], 0.991, sr);
    }
    double raw = ks_process(0) * 0.6 + ks_process(1) * 0.5;
    double lead_solo = sin(((double)t / sr) * 329.63 * 2.0 * PI) * 0.2;
    double dist = guitar_distortion(raw + lead_solo, 4.5);
    *l = dist * 0.8; *r = dist * 0.85;
}

static void gen_funk_bass_24(uint32_t t, uint32_t sr, double *l, double *r) {
    static const double bass_notes[] = { 55.00, 55.00, 110.00, 73.42, 82.41, 55.00, 98.00, 103.83 };
    uint32_t step_interval = sr / 6;
    if (t % step_interval == 0) {
        int note_idx = (t / step_interval) % 8;
        ks_pluck(0, bass_notes[note_idx], 0.988, sr);
    }
    double punch = sin(((double)t / sr) * 55.0 * 2.0 * PI) * 0.3;
    double bass_out = guitar_distortion(ks_process(0) * 1.5 + punch, 1.8);
    *l = bass_out * 0.9; *r = bass_out * 0.9;
}

static const AudioPreset presets[] = {
    // 8-bit Clássicos (1 - 7)
    {1,  8,  0, 8000,  "Viznut Classic (8-bit)",        "Ville-Matias Heikkilä", "t * (((t>>12)|(t>>8))&(63&(t>>4)))",                       NULL, gen_viznut},
    {2,  8,  0, 8000,  "Bit Symphony / 42 (8-bit)",     "Rygan & Kragen",        "(t>>6|t|t>>(t>>16))*10+((t>>11)&7)",                        NULL, gen_symphony},
    {3,  8,  0, 8000,  "Lost in Space (8-bit)",         "Micro Chiptune",        "(t*(t>>5|t>>8))>>(t>>16)",                                  NULL, gen_cosmic},
    {4,  8,  0, 8000,  "Techno Rave Beat (8-bit)",      "Demoscene 8-Bit",       "(t*5&t>>7)|(t*3&t>>10)",                                    NULL, gen_techno},
    {5,  8,  0, 8000,  "Sierra Arpeggiator (8-bit)",    "Experimental Wave",     "((t*(t>>8|t>>9)&46&t>>8))^(t&t>>13|t>>6)",                  NULL, gen_sierra},
    {6,  8,  0, 8000,  "Complex Harmony (8-bit)",       "Algorithmic Chiptune",  "t*(t^t+(t>>15|1)^(t-1280^t)>>10)",                          NULL, gen_harmony},
    {7,  8,  0, 8000,  "Alien Organ (8-bit)",           "8-Bit Synth Engine",    "((t*(t>>11&t>>8&123&t>>3))+(t>>7&t>>10))",                  NULL, gen_alien},

    // 16-bit Hi-Fi Trigonometric (8 - 11)
    {8,  16, 1, 44100, "Ambient Cyber Pad (16-bit)",    "Trigonometric Floatbeat","sin(220*t)*cos(0.5*t) + sin(440*t + sin(2*t)*4) [Stereo]", NULL, gen_ambient_pad_16},
    {9,  16, 1, 44100, "SNES FM Synth (16-bit)",       "16-bit Super Nintendo", "FM Modulation: sin(f*t + sin(2f*t)*2.5) [44.1 kHz Stereo]", NULL, gen_snes_fm_16},
    {10, 16, 0, 44100, "Alien Pulsar (16-bit)",         "Harmonic Tremolo",      "sin(330*t * (1 + 0.1*sin(4*t))) * cos(8*t)",                NULL, gen_alien_pulsar_16},
    {11, 16, 1, 44100, "Retrowave Dream (16-bit)",      "Hi-Fi Chord Synth",     "Tri-Harmonic Sine Chords (C3, E3, F3, G3) [Stereo]",        NULL, gen_retrowave_16},

    // 24-bit Studio Master & Instruments (12 - 16)
    {12, 24, 1, 48000, "Violão Acústico (24-bit)",     "Karplus-Strong Model",  "Physical String Resonance + Lowpass Decay Loop [48 kHz]",   ks_init_voices, gen_guitar_acoustic_24},
    {13, 24, 1, 48000, "Guitarra Rock Solo (24-bit)",   "Overdrive Tube Amp",    "Power Chords (E5/A5/G5) + Non-Linear tanh() Saturação",      ks_init_voices, gen_guitar_rock_24},
    {14, 24, 1, 48000, "Baixo Funk Slap (24-bit)",      "Percussive String",     "Slap Bass Karplus-Strong + Sub-Harmonic Punch [48 kHz]",    ks_init_voices, gen_funk_bass_24},
    {15, 24, 1, 48000, "Rio Zen & Gotas d'Água (24-bit)","Procedural Ambient Water","Ruído Browniano Filtrado + Pingos/Gotas Senoidais com Decay", init_water_synth, gen_water_sound_24},
    {16, 24, 1, 48000, "Cyberpunk BEAT DROP (24-bit)",  "808 Drum & Acid Bass",  "Sub-Kick Pitch Drop + Snare + HiHats + Acid Bass [128 BPM]", init_beat_drop, gen_beat_drop_24}
};
#define PRESET_COUNT (sizeof(presets) / sizeof(presets[0]))

static void print_help(void) {
    printf("%s=================================================================================%s\n", COLOR_TITLE, COLOR_RESET);
    printf("%s[ bytebeat 3.5 - 8-Bit Chiptune, 16-Bit FM & 24-Bit Studio Master Audio ]%s\n", COLOR_TITLE, COLOR_RESET);
    printf("%s=================================================================================%s\n", COLOR_TITLE, COLOR_RESET);
    printf("Usage:\n");
    printf("  bytebeat [OPTIONS]\n\n");
    printf("Options:\n");
    printf("  %s-p, --preset <1-16>%s    Select preset (15=Água/Rio Zen, 16=Beat Drop, 13=Guitarra) [Default: 16]\n", COLOR_OK, COLOR_RESET);
    printf("  %s-d, --duration <SECS>%s  Duration in seconds [Default: 10s]\n", COLOR_OK, COLOR_RESET);
    printf("  %s-r, --rate <HERTZ>%s     Sample rate (8000, 22050, 44100, 48000) [Default: por preset]\n", COLOR_OK, COLOR_RESET);
    printf("  %s-o <ARQUIVO.wav>%s       Output WAV filename [Default: musica.wav]\n", COLOR_OK, COLOR_RESET);
    printf("  %s--play%s                 Generate and auto-play in background\n", COLOR_OK, COLOR_RESET);
    printf("  %s--list%s                 List all equations and instruments\n", COLOR_OK, COLOR_RESET);
    printf("  %s--help%s                 Display this formatted help guide\n\n", COLOR_OK, COLOR_RESET);
    printf("Exemplos:\n");
    printf("  • %sbytebeat -p 16 --play%s               (Dropa o BEAT Cyberpunk com Kick 808 e Bass)\n", COLOR_TAG, COLOR_RESET);
    printf("  • %sbytebeat -p 15 -d 20 -o agua.wav --play%s (Gera 20s de som relaxante de Rio e Chuva)\n", COLOR_TAG, COLOR_RESET);
    printf("  • %sbytebeat -p 13 --play%s               (Toca a Guitarra Rock Solo em 24-bit)\n", COLOR_TAG, COLOR_RESET);
    printf("%s=================================================================================%s\n", COLOR_TITLE, COLOR_RESET);
}

static void list_presets(void) {
    printf("\n%s=================================================================================%s\n", COLOR_TITLE, COLOR_RESET);
    printf("%s[ Banco de Músicas & Instrumentos Matemáticos (8-bit / 16-bit / 24-bit) ]%s\n", COLOR_TITLE, COLOR_RESET);
    printf("%s=================================================================================%s\n\n", COLOR_TITLE, COLOR_RESET);

    printf("  \033[1;32m--- 24-BIT STUDIO MASTER INSTRUMENTS & AMBIENCE (48 kHz) ---\033[0m\n");
    for (size_t i = 11; i < PRESET_COUNT; i++) {
        printf("  %s[%2d]%s \033[1;36m%-32s\033[0m \033[1;32m[24-bit %s]\033[0m (%s)\n",
               COLOR_TAG, presets[i].id, COLOR_RESET, presets[i].name, presets[i].is_stereo ? "Stereo" : "Mono", presets[i].author);
        printf("       \033[0;90mFórmula/Síntese:\033[0m \033[1;33m%s\033[0m\n\n", presets[i].formula_desc);
    }

    printf("  \033[1;35m--- 16-BIT HI-FI SYNTHESIZERS (sin / cos / Síntese FM) ---\033[0m\n");
    for (size_t i = 7; i < 11; i++) {
        printf("  %s[%2d]%s \033[1;36m%-32s\033[0m \033[1;34m[16-bit %s]\033[0m (%s)\n",
               COLOR_TAG, presets[i].id, COLOR_RESET, presets[i].name, presets[i].is_stereo ? "Stereo" : "Mono", presets[i].author);
        printf("       \033[0;90mFórmula:\033[0m \033[1;33m%s\033[0m\n\n", presets[i].formula_desc);
    }

    printf("  \033[1;33m--- 8-BIT RETRO CHIPTUNE & BYTEBEAT CLÁSSICO (8000 Hz) ---\033[0m\n");
    for (size_t i = 0; i < 7; i++) {
        printf("  %s[%2d]%s \033[1;36m%-32s\033[0m \033[0;90m[8-bit Mono]\033[0m (%s)\n",
               COLOR_TAG, presets[i].id, COLOR_RESET, presets[i].name, presets[i].author);
        printf("       \033[0;90mFórmula:\033[0m \033[1;33m%s\033[0m\n\n", presets[i].formula_desc);
    }
}

static void draw_waveform_display(const void *samples, size_t count, int bit_depth, int is_stereo) {
    static const char *bars[] = {" ", " ", "▂", "▃", "▄", "▅", "▆", "▇", "█"};
    printf("  %sOsciloscópio ASCII da Forma de Onda (%d-Bit %s):%s\n  \033[1;32m",
           COLOR_TAG, bit_depth, is_stereo ? "Stereo" : "Mono", COLOR_RESET);

    size_t step = count / 70;
    if (step == 0) step = 1;

    for (size_t i = 0; i < 70 && (i * step) < count; i++) {
        int idx = 4;
        if (bit_depth == 8) {
            const uint8_t *b = (const uint8_t *)samples;
            idx = (int)((b[i * step] * 8) / 255);
        } else if (bit_depth == 16) {
            const int16_t *s = (const int16_t *)samples;
            int16_t val = s[i * step * (is_stereo ? 2 : 1)];
            idx = (int)(((val + 32768) * 8) / 65535);
        } else if (bit_depth == 24) {
            const uint8_t *ptr = ((const uint8_t *)samples) + (i * step * (is_stereo ? 6 : 3));
            int32_t val = (int32_t)(ptr[0] | (ptr[1] << 8) | (ptr[2] << 16));
            if (val & 0x800000) val |= 0xFF000000;
            idx = (int)(((val + 8388608) * 8) / 16777215);
        }

        if (idx < 0) idx = 0;
        if (idx > 8) idx = 8;
        printf("%s", bars[idx]);
    }
    printf("\033[0m\n\n");
}

int main(int argc, char *argv[]) {
    utilipc_init();

    int preset_idx = 15; // Padrão: Preset 16 (Cyberpunk BEAT DROP 24-bit!)
    int duration_sec = 10;
    int force_rate = 0;
    uint32_t sample_rate = 48000;
    const char *out_filename = "musica.wav";
    int auto_play = 0;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            print_help();
            utilipc_close();
            return 0;
        }
        if (strcmp(argv[i], "--list") == 0 || strcmp(argv[i], "-l") == 0) {
            list_presets();
            utilipc_close();
            return 0;
        }

        if ((strcmp(argv[i], "-p") == 0 || strcmp(argv[i], "--preset") == 0) && i + 1 < argc) {
            int p = atoi(argv[++i]);
            if (p >= 1 && p <= (int)PRESET_COUNT) preset_idx = p - 1;
        } else if ((strcmp(argv[i], "-d") == 0 || strcmp(argv[i], "--duration") == 0) && i + 1 < argc) {
            duration_sec = atoi(argv[++i]);
            if (duration_sec < 1) duration_sec = 1;
            if (duration_sec > 120) duration_sec = 120;
        } else if ((strcmp(argv[i], "-r") == 0 || strcmp(argv[i], "--rate") == 0) && i + 1 < argc) {
            sample_rate = (uint32_t)atoi(argv[++i]);
            force_rate = 1;
        } else if (strcmp(argv[i], "-o") == 0 && i + 1 < argc) {
            out_filename = argv[++i];
        } else if (strcmp(argv[i], "--play") == 0) {
            auto_play = 1;
        }
    }

    const AudioPreset *pr = &presets[preset_idx];
    if (!force_rate) sample_rate = pr->default_rate;

    int bit_depth = pr->bit_depth;
    int is_stereo = pr->is_stereo;
    int channels = is_stereo ? 2 : 1;
    size_t total_frames = (size_t)duration_sec * sample_rate;
    size_t bytes_per_sample = bit_depth / 8;
    size_t total_bytes = total_frames * channels * bytes_per_sample;

    if (pr->init) pr->init();

    printf("\n%s=================================================================================%s\n", COLOR_TITLE, COLOR_RESET);
    printf("%s[ bytebeat 3.5 - Sintetizador de Áudio %d-Bit %s ]%s\n",
           COLOR_TITLE, bit_depth, (bit_depth == 24) ? "(Studio Master / Hi-Res)" : (bit_depth == 16) ? "(Hi-Fi FM)" : "(Retro 8-bit)", COLOR_RESET);
    printf("%s=================================================================================%s\n\n", COLOR_TITLE, COLOR_RESET);
    printf("  • %sPreset / Som     :%s %s[%d] %s%s (Autor: %s)\n", COLOR_TAG, COLOR_RESET, COLOR_VAL, pr->id, pr->name, COLOR_RESET, pr->author);
    printf("  • %sSíntese / Equação:%s \033[1;33m%s\033[0m\n", COLOR_TAG, COLOR_RESET, pr->formula_desc);
    printf("  • %sFormato de Áudio :%s %s%d-bit %s @ %u Hz%s (%d s | %zu bytes)\n",
           COLOR_TAG, COLOR_RESET, COLOR_OK, bit_depth, is_stereo ? "Estéreo (2 Canais)" : "Mono", sample_rate, COLOR_RESET,
           duration_sec, total_bytes);
    printf("  • %sArquivo Gerado   :%s \033[1;36m%s\033[0m\n\n", COLOR_TAG, COLOR_RESET, out_filename);

    uint8_t *raw_audio_buf = malloc(total_bytes);
    if (!raw_audio_buf) {
        fprintf(stderr, "bytebeat: erro de alocacao de memoria para %zu bytes\n", total_bytes);
        utilipc_close();
        return 1;
    }

    // Renderização do sinal de áudio
    for (uint32_t t = 0; t < total_frames; t++) {
        double l_val = 0.0, r_val = 0.0;
        pr->render(t, sample_rate, &l_val, &r_val);

        if (bit_depth == 8) {
            raw_audio_buf[t] = (uint8_t)l_val;
        } else if (bit_depth == 16) {
            if (l_val > 1.0) l_val = 1.0; if (l_val < -1.0) l_val = -1.0;
            if (r_val > 1.0) r_val = 1.0; if (r_val < -1.0) r_val = -1.0;

            int16_t s_left = (int16_t)(l_val * 32760.0);
            int16_t s_right = (int16_t)(r_val * 32760.0);

            if (is_stereo) {
                ((int16_t *)raw_audio_buf)[t * 2 + 0] = s_left;
                ((int16_t *)raw_audio_buf)[t * 2 + 1] = s_right;
            } else {
                ((int16_t *)raw_audio_buf)[t] = s_left;
            }
        } else if (bit_depth == 24) {
            if (l_val > 1.0) l_val = 1.0; if (l_val < -1.0) l_val = -1.0;
            if (r_val > 1.0) r_val = 1.0; if (r_val < -1.0) r_val = -1.0;

            int32_t s_left = (int32_t)(l_val * 8388600.0);
            int32_t s_right = (int32_t)(r_val * 8388600.0);

            if (is_stereo) {
                size_t offset = t * 6;
                raw_audio_buf[offset + 0] = (uint8_t)(s_left & 0xFF);
                raw_audio_buf[offset + 1] = (uint8_t)((s_left >> 8) & 0xFF);
                raw_audio_buf[offset + 2] = (uint8_t)((s_left >> 16) & 0xFF);

                raw_audio_buf[offset + 3] = (uint8_t)(s_right & 0xFF);
                raw_audio_buf[offset + 4] = (uint8_t)((s_right >> 8) & 0xFF);
                raw_audio_buf[offset + 5] = (uint8_t)((s_right >> 16) & 0xFF);
            } else {
                size_t offset = t * 3;
                raw_audio_buf[offset + 0] = (uint8_t)(s_left & 0xFF);
                raw_audio_buf[offset + 1] = (uint8_t)((s_left >> 8) & 0xFF);
                raw_audio_buf[offset + 2] = (uint8_t)((s_left >> 16) & 0xFF);
            }
        }
    }

    draw_waveform_display(raw_audio_buf, total_frames, bit_depth, is_stereo);

    // Monta o cabeçalho WAVE oficial
    WavHeader hdr;
    memcpy(hdr.riff, "RIFF", 4);
    hdr.chunk_size = sizeof(WavHeader) - 8 + total_bytes;
    memcpy(hdr.wave, "WAVE", 4);
    memcpy(hdr.fmt, "fmt ", 4);
    hdr.subchunk1_size = 16;
    hdr.audio_format = 1; // PCM linear
    hdr.num_channels = channels;
    hdr.sample_rate = sample_rate;
    hdr.bits_per_sample = bit_depth;
    hdr.byte_rate = sample_rate * channels * (hdr.bits_per_sample / 8);
    hdr.block_align = channels * (hdr.bits_per_sample / 8);
    memcpy(hdr.data, "data", 4);
    hdr.data_size = total_bytes;

    FILE *fp = fopen(out_filename, "wb");
    if (!fp) {
        fprintf(stderr, "bytebeat: falha ao gravar '%s': %s\n", out_filename, strerror(errno));
        free(raw_audio_buf);
        utilipc_close();
        return 1;
    }

    fwrite(&hdr, 1, sizeof(WavHeader), fp);
    fwrite(raw_audio_buf, 1, total_bytes, fp);
    fclose(fp);
    free(raw_audio_buf);

    printf("  \033[1;32m✔ Arquivo WAV gerado com sucesso em: %s\033[0m\n", out_filename);

    if (auto_play) {
        char play_cmd[256];
        if (system("which termux-media-player >/dev/null 2>&1") == 0) {
            snprintf(play_cmd, sizeof(play_cmd), "termux-media-player play %s >/dev/null 2>&1 &", out_filename);
            printf("  \033[1;36m[Tocando via Termux Media Player...]\033[0m\n");
            (void)!system(play_cmd);
        } else if (system("which aplay >/dev/null 2>&1") == 0) {
            snprintf(play_cmd, sizeof(play_cmd), "aplay %s >/dev/null 2>&1 &", out_filename);
            printf("  \033[1;36m[Tocando via ALSA aplay...]\033[0m\n");
            (void)!system(play_cmd);
        } else if (system("which play >/dev/null 2>&1") == 0) {
            snprintf(play_cmd, sizeof(play_cmd), "play %s >/dev/null 2>&1 &", out_filename);
            printf("  \033[1;36m[Tocando via SoX play...]\033[0m\n");
            (void)!system(play_cmd);
        }
    }

    printf("%s=================================================================================%s\n\n", COLOR_TITLE, COLOR_RESET);

    char log_msg[UTILIPC_MAX_MSG];
    snprintf(log_msg, sizeof(log_msg), "bytebeat: generated %s (Preset %d - %s %d-bit)", out_filename, pr->id, pr->name, bit_depth);
    utilipc_write_status(-1, -1, -1, log_msg);

    utilipc_close();
    return 0;
}
