/*
 * sound.c - efectos AdLib y musica IMF de Wolfenstein 3D, con el OPL2 emulado.
 *
 * Lee AUDIOHED.* y AUDIOT.* originales. La logica (registros del OPL, el
 * servicio de sonido a 140 Hz y el de musica a 700 Hz) viene del codigo fuente
 * liberado por id Software (ID_SD.C, GPLv2). El emulador del chip es
 * Nuked OPL3 (opl3.c, LGPL 2.1).
 */
#include <SDL2/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <ctype.h>
#include "opl3.h"
#include "sound.h"

/* registros del OPL (ID_SD.H) */
#define alChar 0x20
#define alScale 0x40
#define alAttack 0x60
#define alSus 0x80
#define alWave 0xe0
#define alFreqL 0xa0
#define alFreqH 0xb0
#define alFeedCon 0xc0
#define alEffects 0xbd

#define RATE 44100

static opl3_chip chip;
static SDL_AudioDeviceID dev;
static int sound_on = 1, music_on = 1, audio_ready;

static uint8_t *audiot;
static uint32_t *audiohed;
static int naudio;

/* efectos */
static const uint8_t *alSound;
static int alLengthLeft, alBlock;
static int SoundNumber, SoundPriority;

/* musica */
static const uint8_t *sqHack, *sqHackStart;
static int sqHackLen, sqHackSeqLen;
static uint32_t sqHackTime, alTimeCount;
static int music_playing;

static double sfx_left, music_left;

static void opl_out(int reg, int val) { OPL3_WriteRegBuffered(&chip, (uint16_t)reg, (uint8_t)val); }

/* ------------------------------------------------------------ archivos */
static uint8_t *loadfile(const char *dir, const char *base, const char *ext, size_t *len)
{
    char path[1024], b[64], e[8];
    for (int i = 0; ext[i] && i < 7; i++) e[i] = ext[i], e[i + 1] = 0;
    for (int lc = 0; lc < 2; lc++) {
        for (unsigned i = 0; i < strlen(base); i++) b[i] = lc ? tolower((unsigned char)base[i]) : base[i];
        b[strlen(base)] = 0;
        char ee[8];
        for (unsigned i = 0; i <= strlen(e); i++) ee[i] = lc ? tolower((unsigned char)e[i]) : e[i];
        snprintf(path, sizeof path, "%s/%s.%s", dir, b, ee);
        FILE *f = fopen(path, "rb");
        if (!f) continue;
        fseek(f, 0, SEEK_END);
        long sz = ftell(f);
        fseek(f, 0, SEEK_SET);
        uint8_t *buf = malloc(sz + 8);
        if (fread(buf, 1, sz, f) != (size_t)sz) { fclose(f); free(buf); return NULL; }
        fclose(f);
        if (len) *len = sz;
        return buf;
    }
    return NULL;
}

static const uint8_t *audio_chunk(int n, int *len)
{
    if (!audiot || n < 0 || n >= naudio) return NULL;
    uint32_t a = audiohed[n], b = audiohed[n + 1];
    if (b <= a) return NULL;
    if (len) *len = b - a;
    return audiot + a;
}

/* ------------------------------------------------------------ servicios */
static void SDL_AlSetFXInst(const uint8_t *inst)
{
    int m = 0, c = 3;
    opl_out(m + alChar, inst[0]);
    opl_out(m + alScale, inst[2]);
    opl_out(m + alAttack, inst[4]);
    opl_out(m + alSus, inst[6]);
    opl_out(m + alWave, inst[8]);
    opl_out(c + alChar, inst[1]);
    opl_out(c + alScale, inst[3]);
    opl_out(c + alAttack, inst[5]);
    opl_out(c + alSus, inst[7]);
    opl_out(c + alWave, inst[9]);
    opl_out(alFeedCon, 0);
}

/* 140 Hz: un byte de la frecuencia por tic (SDL_ALSoundService) */
static void sfx_service(void)
{
    if (!alSound) return;
    uint8_t s = *alSound++;
    if (!s) opl_out(alFreqL, 0);
    else { opl_out(alFreqL, s); opl_out(alFreqH, alBlock); }
    if (!--alLengthLeft) {
        alSound = NULL;
        alBlock = 0;
        opl_out(alFreqL, 0);
        opl_out(alFreqH, 0);
        SoundNumber = SoundPriority = 0;
    }
}

/* 700 Hz: eventos IMF (SDL_ALService) */
static void music_service(void)
{
    if (!music_playing || !sqHack) return;
    while (sqHackLen > 0 && sqHackTime <= alTimeCount) {
        int reg = sqHack[0], val = sqHack[1];
        int delay = sqHack[2] | (sqHack[3] << 8);
        sqHack += 4;
        sqHackLen -= 4;
        sqHackTime = alTimeCount + delay;
        opl_out(reg, val);
    }
    alTimeCount++;
    if (sqHackLen <= 0) {          /* repetir la cancion */
        sqHack = sqHackStart;
        sqHackLen = sqHackSeqLen;
        sqHackTime = alTimeCount = 0;
    }
}

static void audio_cb(void *ud, Uint8 *stream, int len)
{
    (void)ud;
    int16_t *out = (int16_t *)stream;
    int frames = len / 4;
    while (frames > 0) {
        int n = frames;
        if (sfx_left < n) n = (int)sfx_left;
        if (music_left < n) n = (int)music_left;
        if (n < 1) n = 1;
        if (n > frames) n = frames;
        OPL3_GenerateStream(&chip, out, n);
        out += n * 2;
        frames -= n;
        sfx_left -= n;
        music_left -= n;
        if (sfx_left < 1) { sfx_service(); sfx_left += (double)RATE / 140.0; }
        if (music_left < 1) { music_service(); music_left += (double)RATE / 700.0; }
    }
}

/* ------------------------------------------------------------ API */
/* modo sin tarjeta de sonido, para pruebas: genera el audio a mano */
int SD_Offline(const char *dir, const char *ext)
{
    size_t n = 0;
    uint8_t *hed = loadfile(dir, "AUDIOHED", ext, &n);
    if (!hed) return 0;
    naudio = n / 4 - 1;
    audiohed = malloc(n + 8);
    for (int i = 0; i <= naudio; i++) {
        const uint8_t *p = hed + 4 * i;
        audiohed[i] = p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24);
    }
    free(hed);
    audiot = loadfile(dir, "AUDIOT", ext, &n);
    if (!audiot) return 0;
    OPL3_Reset(&chip, RATE);
    opl_out(1, 0x20);
    opl_out(8, 0);
    opl_out(alEffects, 0);
    sfx_left = RATE / 140.0;
    music_left = RATE / 700.0;
    audio_ready = 1;
    return 1;
}

void SD_Pump(int16_t *buf, int frames) { audio_cb(NULL, (Uint8 *)buf, frames * 4); }

int SD_Startup(const char *dir, const char *ext)
{
    size_t n = 0;
    uint8_t *hed = loadfile(dir, "AUDIOHED", ext, &n);
    if (!hed) return 0;
    naudio = n / 4 - 1;
    audiohed = malloc(n + 8);
    for (int i = 0; i <= naudio; i++) {
        const uint8_t *p = hed + 4 * i;
        audiohed[i] = p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24);
    }
    free(hed);
    audiot = loadfile(dir, "AUDIOT", ext, &n);
    if (!audiot) { free(audiohed); audiohed = NULL; return 0; }

    if (SDL_InitSubSystem(SDL_INIT_AUDIO) < 0) return 0;
    SDL_AudioSpec want, have;
    SDL_zero(want);
    want.freq = RATE;
    want.format = AUDIO_S16SYS;
    want.channels = 2;
    want.samples = 512;
    want.callback = audio_cb;
    dev = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
    if (!dev) return 0;
    OPL3_Reset(&chip, RATE);
    opl_out(1, 0x20);              /* habilita formas de onda */
    opl_out(8, 0);
    opl_out(alEffects, 0);
    sfx_left = RATE / 140.0;
    music_left = RATE / 700.0;
    audio_ready = 1;
    SDL_PauseAudioDevice(dev, 0);
    return 1;
}

void SD_Shutdown(void)
{
    if (dev) { SDL_PauseAudioDevice(dev, 1); SDL_CloseAudioDevice(dev); dev = 0; }
    audio_ready = 0;
}

void SD_PlaySound(int sound)
{
    if (!audio_ready || !sound_on || sound < 0) return;
    int len = 0;
    const uint8_t *c = audio_chunk(STARTADLIBSOUNDS + sound, &len);
    if (!c || len < 24) return;
    /* AdLibSound: longword length, word priority, Instrument(16), byte block, data[] */
    uint32_t slen = c[0] | (c[1] << 8) | (c[2] << 16) | ((uint32_t)c[3] << 24);
    int priority = c[4] | (c[5] << 8);
    const uint8_t *inst = c + 6;
    int block = c[22];
    const uint8_t *data = c + 23;
    if ((int)slen > len - 23) slen = len - 23;
    if (slen == 0) return;
    SDL_LockAudioDevice(dev);
    if (!alSound || priority >= SoundPriority) {
        opl_out(alFreqL, 0);
        opl_out(alFreqH, 0);
        SDL_AlSetFXInst(inst);
        alBlock = ((block & 7) << 2) | 0x20;
        alSound = data;
        alLengthLeft = slen;
        SoundNumber = sound;
        SoundPriority = priority;
    }
    SDL_UnlockAudioDevice(dev);
}

void SD_StartMusic(int song)
{
    if (!audio_ready || song < 0) return;
    int len = 0;
    const uint8_t *c = audio_chunk(STARTMUSIC + song, &len);
    if (!c || len < 4) return;
    int dlen = c[0] | (c[1] << 8);
    if (dlen <= 0 || dlen > len - 2) dlen = len - 2;
    SDL_LockAudioDevice(dev);
    /* apaga las voces que pudiera dejar la cancion anterior */
    for (int i = 0; i < 9; i++) opl_out(alFreqH + i, 0);
    sqHackStart = c + 2;
    sqHack = sqHackStart;
    sqHackLen = sqHackSeqLen = dlen;
    sqHackTime = alTimeCount = 0;
    music_playing = music_on;
    SDL_UnlockAudioDevice(dev);
}

void SD_StopMusic(void)
{
    if (!audio_ready) return;
    SDL_LockAudioDevice(dev);
    music_playing = 0;
    for (int i = 0; i < 9; i++) opl_out(alFreqH + i, 0);
    SDL_UnlockAudioDevice(dev);
}

void SD_SetSound(int on)
{
    sound_on = on;
    if (!audio_ready) return;
    SDL_LockAudioDevice(dev);
    if (!on && alSound) {
        alSound = NULL;
        alLengthLeft = 0;
        SoundNumber = SoundPriority = 0;
        opl_out(alFreqL, 0);
        opl_out(alFreqH, 0);
    }
    SDL_UnlockAudioDevice(dev);
}

void SD_SetMusic(int on)
{
    music_on = on;
    if (!audio_ready) return;
    SDL_LockAudioDevice(dev);
    music_playing = on && sqHackStart;
    if (!on) for (int i = 0; i < 9; i++) opl_out(alFreqH + i, 0);
    else { sqHack = sqHackStart; sqHackLen = sqHackSeqLen; sqHackTime = alTimeCount = 0; }
    SDL_UnlockAudioDevice(dev);
}

int SD_SoundOn(void) { return sound_on; }
int SD_MusicOn(void) { return music_on; }
