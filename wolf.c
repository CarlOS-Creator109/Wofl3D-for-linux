/*
 * wolf.c - Wolfenstein 3D para Linux moderno (SDL2), lee los archivos ORIGINALES:
 *   MAPHEAD.*, GAMEMAPS.*, VSWAP.*, VGAGRAPH.*, VGAHEAD.*, VGADICT.*
 *   (.WL6 version completa o .WL1 shareware)
 *
 * Compilar:  make
 * Usar:      ./wolf /ruta/a/los/datos [-m piso] [-s dificultad 0..3] [--sens 0.12]
 *
 * La logica de juego (estados, IA, dano, velocidades, tablas) viene del codigo
 * fuente liberado por id Software bajo GPLv2; ver wolfdata.h y README.md.
 * No incluye ningun dato del juego: necesitas tus propios archivos.
 */
#include <SDL2/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>
#include <ctype.h>
#include "pal.h"
#include "wolfdata.h"
#include "sound.h"

/* ------------------------------------------------------------ constantes */
#define SCRW 320
#define SCRH 200
#define VIEWH 160
#define CENTERY (VIEWH / 2)
#define CENTERX 159
#define SHOOTDELTA 32
#define HFOV_TAN 0.7314285714
#define PROJ 218.75
#define MAPSIZE 64
#define TILEGLOBAL 0x10000L
#define TILESHIFT 16
#define MINDIST 0x5800L
#define MINACTORDIST 0x10000L
#define MINSIGHT 0x18000L
#define PLAYER_R (MINDIST / 65536.0)
#define MOVESCALE 150L
#define BACKMOVESCALE 100L
#define ANGLESCALE 20
#define BASEMOVE 35
#define RUNMOVE 70
#define RUNSPEED 6000
#define SPDPATROL 512
#define SPDDOG 1500
#define OPENTICS 300
#define AREATILE 107
#define AMBUSHTILE 106
#define ELEVATORTILE 21
#define ALTELEVATORTILE 107
#define NUMAREAS 37
#define MAXACTORS 150
#define MAXSTATS 400
#define MAXDOORS 64

#define FL_SHOOTABLE 1
#define FL_BONUS 2
#define FL_NEVERMARK 4
#define FL_VISABLE 8
#define FL_ATTACKMODE 16
#define FL_FIRSTATTACK 32
#define FL_AMBUSH 64
#define FL_NONMARK 128

enum { east, northeast, north, northwest, west, southwest, south, southeast, nodir };
static const int dirangle[9] = { 0, 45, 90, 135, 180, 225, 270, 315, 0 };
static const int opposite[9] = { west, southwest, south, southeast, east, northeast, north, northwest, nodir };
static int diagonal_dir(int a, int b)
{
    if ((a == east && b == north) || (a == north && b == east)) return northeast;
    if ((a == east && b == south) || (a == south && b == east)) return southeast;
    if ((a == west && b == north) || (a == north && b == west)) return northwest;
    if ((a == west && b == south) || (a == south && b == west)) return southwest;
    return nodir;
}

enum { nothing, playerobj, inertobj, guardobj, officerobj, ssobj, dogobj, bossobj,
       schabbobj, fakeobj, mechahitlerobj, mutantobj, needleobj, fireobj, bjobj,
       ghostobj, realhitlerobj, gretelobj, giftobj, fatobj, rocketobj };
enum { en_guard, en_officer, en_ss, en_dog, en_boss, en_schabbs, en_fake, en_hitler,
       en_mutant, en_blinky, en_clyde, en_pinky, en_inky, en_gretel, en_gift, en_fat };
enum { wp_knife, wp_pistol, wp_machinegun, wp_chaingun };
enum { st_none, st_block, bo_gibs, bo_alpo, bo_firstaid, bo_key1, bo_key2, bo_key3,
       bo_key4, bo_cross, bo_chalice, bo_bible, bo_crown, bo_clip, bo_clip2,
       bo_machinegun, bo_chaingun, bo_food, bo_fullheal, bo_25clip, bo_spear };

#define STRUCTPIC 0
#define STARTPICS 3
#define STARTTILE8 135
#define STATUSBARPIC 86
#define KNIFEPIC 91
#define NOKEYPIC 95
#define GOLDKEYPIC 96
#define SILVERKEYPIC 97
#define N_BLANKPIC 98
#define N_0PIC 99
#define FACE1APIC 109
#define FACE8APIC 130

static const int weaponscale[4] = { SPR_KNIFEREADY, SPR_PISTOLREADY, SPR_MACHINEGUNREADY, SPR_CHAINREADY };

/* ------------------------------------------------------------ tipos */
typedef struct objstruct {
    int active, obclass, state, flags;
    int32_t distance;
    int dir, tilex, tiley, areanumber;
    int32_t x, y, speed;
    int ticcount, hitpoints, temp1, temp2, temp3, angle;
    int viewx;
    double depth;
    struct objstruct *next, *prev;
} objtype;

typedef struct { int tilex, tiley, shapenum, flags, itemnumber; } statobj_t;
typedef struct { int x, y, vertical, lock, action, pos, ticcount, area1, area2; } Door;
enum { DR_CLOSED, DR_OPENING, DR_OPEN, DR_CLOSING };

/* ------------------------------------------------------------ estado global */
static const char *datadir = ".";
static char ext[4] = "WL6";
static double mouse_sens = 0.12;
static int difficulty = 1;
static int tics = 1;

static uint8_t *maphead, *gamemaps, *vswap, *vgagraph, *vgahead, *vgadict;
static size_t gamemaps_len, vgahead_len, vgagraph_len;
static int chunks, spritestart, soundstart;
static uint32_t chunk_off[4096];
#define DOORWALL (spritestart - 8)

static uint32_t pal32[256];
static uint8_t fb[SCRW * SCRH];
static double zbuf[SCRW];

static uint16_t plane0[MAPSIZE * MAPSIZE], plane1[MAPSIZE * MAPSIZE];
static int tilemap[MAPSIZE][MAPSIZE];
static objtype *actorat[MAPSIZE][MAPSIZE];
static int maparea[MAPSIZE][MAPSIZE];
static int areaconnect[NUMAREAS][NUMAREAS];
static int areabyplayer[NUMAREAS];

static Door doors[MAXDOORS];
static int ndoors;
static statobj_t statlist[MAXSTATS];
static int nstats;

static objtype objlist[MAXACTORS];
static objtype *player, *objhead, *objtail, *newobj;
static int objcount;

static int pwallstate, pwallx, pwally, pwalldir, pwallpos, pwalltile;

static char mapname[17];
static int mapon, ceilcolor;
static int madenoise;
static int rndindex;
static double px, py, pangle;
static int32_t thrustspeed;

static struct {
    int health, ammo, score, lives, weapon, chosenweapon, bestweapon;
    int keys, attackframe, attackcount, weaponframe, faceframe, facecount;
    int killcount, killtotal, treasurecount, treasuretotal, secretcount, secrettotal;
    int victoryflag, godmode, episode, timecount;
} gs;

static int playstate;
static int damageflash, bonusflash;
static int attackbutton;

static void NewState(objtype *ob, int st);
static int CheckLine(objtype *ob);
static void TakeDamage(int points, objtype *attacker);
static void PlaceItemType(int itemtype, int tilex, int tiley);
static int player_in_tile(int x, int y);
static void sync_player(void);

static int US_RndT(void) { rndindex = (rndindex + 1) & 255; return RNDTABLE[rndindex]; }

/* ------------------------------------------------------------ archivos */
static uint8_t *readfile(const char *base, size_t *len)
{
    char path[1024], b[64], e2[4];
    const char *exts[2];
    exts[0] = ext;
    for (int i = 0; i < 3; i++) e2[i] = tolower((unsigned char)ext[i]);
    e2[3] = 0;
    exts[1] = e2;
    for (int e = 0; e < 2; e++)
        for (int c = 0; c < 2; c++) {
            size_t n = strlen(base);
            for (size_t i = 0; i < n; i++) b[i] = c ? tolower((unsigned char)base[i]) : base[i];
            b[n] = 0;
            snprintf(path, sizeof path, "%s/%s.%s", datadir, b, exts[e]);
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

static int detect_ext(void)
{
    static const char *try_[] = { "WL6", "WL1", "SOD", "SDM" };
    for (int i = 0; i < 4; i++) {
        strcpy(ext, try_[i]);
        size_t n;
        uint8_t *b = readfile("MAPHEAD", &n);
        if (b) { free(b); return 1; }
    }
    return 0;
}

/* ------------------------------------------------------------ compresion */
static void carmack_expand(const uint8_t *in, uint16_t *out, int bytes)
{
    uint16_t *start = out;
    int words = bytes / 2;
    while (words > 0) {
        uint16_t ch = in[0] | (in[1] << 8);
        in += 2;
        uint8_t hi = ch >> 8;
        if (hi == 0xA7 || hi == 0xA8) {
            int count = ch & 0xff;
            if (count == 0) { *out++ = (ch & 0xff00) | *in++; words--; }
            else if (hi == 0xA7) {
                int off = *in++;
                uint16_t *cp = out - off;
                words -= count;
                while (count--) *out++ = *cp++;
            } else {
                int off = in[0] | (in[1] << 8);
                in += 2;
                uint16_t *cp = start + off;
                words -= count;
                while (count--) *out++ = *cp++;
            }
        } else { *out++ = ch; words--; }
    }
}

static void rlew_expand(const uint16_t *in, uint16_t *out, int bytes, uint16_t tag)
{
    uint16_t *end = out + bytes / 2;
    while (out < end) {
        uint16_t v = *in++;
        if (v != tag) *out++ = v;
        else {
            int c = *in++;
            uint16_t val = *in++;
            while (c-- > 0 && out < end) *out++ = val;
        }
    }
}

static const uint8_t *chunk(int i)
{
    if (i < 0 || i >= chunks || chunk_off[i] == 0) return NULL;
    return vswap + chunk_off[i];
}

/* ------------------------------------------------------------ VGAGRAPH */
static uint32_t grstart[1024];
static int ngr;
static uint16_t pictable[600][2];
static uint8_t *picbuf[600];

static void huff_expand(const uint8_t *src, int srclen, uint8_t *dst, int dstlen)
{
    const uint16_t *d = (const uint16_t *)vgadict;
    int head = 254, node = 254, o = 0;
    for (int i = 0; i < srclen && o < dstlen; i++) {
        uint8_t b = src[i];
        for (int k = 0; k < 8; k++) {
            uint16_t v = d[node * 2 + (b & 1)];
            b >>= 1;
            if (v < 256) { dst[o++] = (uint8_t)v; node = head; if (o >= dstlen) return; }
            else node = v - 256;
        }
    }
}

static uint8_t *grchunk(int n, int *outlen)
{
    if (n < 0 || n >= ngr) return NULL;
    uint32_t off = grstart[n];
    if (off == 0xFFFFFF) return NULL;
    uint32_t next = 0;
    for (int i = n + 1; i < ngr; i++) if (grstart[i] != 0xFFFFFF) { next = grstart[i]; break; }
    if (!next) next = vgagraph_len;
    int clen = (int)next - (int)off;
    if (clen <= 4) return NULL;
    const uint8_t *src = vgagraph + off;
    uint32_t explen = src[0] | (src[1] << 8) | (src[2] << 16) | ((uint32_t)src[3] << 24);
    if (explen == 0 || explen > (1u << 20)) return NULL;
    uint8_t *dst = calloc(explen, 1);
    huff_expand(src + 4, clen - 4, dst, explen);
    if (outlen) *outlen = explen;
    return dst;
}

static void load_vgagraph(void)
{
    size_t n;
    vgahead = readfile("VGAHEAD", &vgahead_len);
    vgagraph = readfile("VGAGRAPH", &vgagraph_len);
    vgadict = readfile("VGADICT", &n);
    if (!vgahead || !vgagraph || !vgadict) { vgagraph = NULL; return; }
    ngr = vgahead_len / 3;
    if (ngr > 1024) ngr = 1024;
    for (int i = 0; i < ngr; i++)
        grstart[i] = vgahead[i * 3] | (vgahead[i * 3 + 1] << 8) | (vgahead[i * 3 + 2] << 16);
    int len = 0;
    uint8_t *st = grchunk(STRUCTPIC, &len);
    if (!st) { vgagraph = NULL; return; }
    int npics = len / 4;
    if (npics > 600) npics = 600;
    for (int i = 0; i < npics; i++) {
        pictable[i][0] = st[i * 4] | (st[i * 4 + 1] << 8);
        pictable[i][1] = st[i * 4 + 2] | (st[i * 4 + 3] << 8);
    }
    free(st);
}

static const uint8_t *getpic(int n, int *w, int *h)
{
    if (!vgagraph || n < STARTPICS || n >= STARTTILE8) return NULL;
    int p = n - STARTPICS;
    *w = pictable[p][0];
    *h = pictable[p][1];
    if (*w <= 0 || *h <= 0) return NULL;
    if (picbuf[n]) return picbuf[n];
    int len = 0;
    uint8_t *raw = grchunk(n, &len);
    if (!raw) return NULL;
    int W = *w, H = *h, q = W >> 2;
    uint8_t *out = calloc(W * H, 1);
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) {
            int src = (y * q + (x >> 2)) + ((x & 3) * q * H);
            out[y * W + x] = (src < len) ? raw[src] : 0;
        }
    free(raw);
    picbuf[n] = out;
    return out;
}

static void drawpic(int px_, int py_, int n)
{
    int w, h;
    const uint8_t *p = getpic(n, &w, &h);
    if (!p) return;
    for (int y = 0; y < h; y++) {
        int sy = py_ + y;
        if (sy < 0 || sy >= SCRH) continue;
        for (int x = 0; x < w; x++) {
            int sx = px_ + x;
            if (sx >= 0 && sx < SCRW) fb[sy * SCRW + sx] = p[y * w + x];
        }
    }
}

#define StatusDrawPic(x, y, p) drawpic((x) * 8, (y) + (SCRH - 40), p)

static void LatchNumber(int x, int y, int width, long number)
{
    char str[24];
    snprintf(str, sizeof str, "%ld", number);
    int len = strlen(str);
    int i = width - len;
    while (i-- > 0) { StatusDrawPic(x, y, N_BLANKPIC); x++; }
    for (int c = 0; c < len && c < width; c++) { StatusDrawPic(x, y, str[c] - '0' + N_0PIC); x++; }
}

/* ------------------------------------------------------------ carga */
static void load_data(void)
{
    size_t n;
    if (!detect_ext()) {
        fprintf(stderr,
            "No encuentro los archivos del juego en '%s'.\n"
            "Necesitas los tuyos: MAPHEAD, GAMEMAPS, VSWAP, VGAGRAPH, VGAHEAD,\n"
            "VGADICT, AUDIOHED y AUDIOT (.WL6 completo o .WL1 shareware).\n"
            "Ponlos en una carpeta y pasala: wolf /ruta/a/los/datos\n", datadir);
        exit(1);
    }
    maphead = readfile("MAPHEAD", &n);
    gamemaps = readfile("GAMEMAPS", &gamemaps_len);
    vswap = readfile("VSWAP", &n);
    if (!maphead || !gamemaps || !vswap) { fprintf(stderr, "Faltan archivos de datos (.%s)\n", ext); exit(1); }
    chunks = vswap[0] | (vswap[1] << 8);
    spritestart = vswap[2] | (vswap[3] << 8);
    soundstart = vswap[4] | (vswap[5] << 8);
    if (chunks > 4096) chunks = 4096;
    for (int i = 0; i < chunks; i++) {
        const uint8_t *p = vswap + 6 + 4 * i;
        chunk_off[i] = p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24);
    }
    load_vgagraph();
    for (int i = 0; i < 256; i++) {
        int r = PAL6[i * 3], g = PAL6[i * 3 + 1], b = PAL6[i * 3 + 2];
        pal32[i] = 0xFF000000u | ((r * 255 / 63) << 16) | ((g * 255 / 63) << 8) | (b * 255 / 63);
    }
}

static const int ElevatorBackTo[6] = { 1, 1, 7, 3, 5, 3 };

static const int songs[60] = {
    GETTHEM_MUS, SEARCHN_MUS, POW_MUS, SUSPENSE_MUS, GETTHEM_MUS,
    SEARCHN_MUS, POW_MUS, SUSPENSE_MUS, WARMARCH_MUS, CORNER_MUS,
    NAZI_OMI_MUS, PREGNANT_MUS, GOINGAFT_MUS, HEADACHE_MUS, NAZI_OMI_MUS,
    PREGNANT_MUS, HEADACHE_MUS, GOINGAFT_MUS, WARMARCH_MUS, DUNGEON_MUS,
    INTROCW3_MUS, NAZI_RAP_MUS, TWELFTH_MUS, ZEROHOUR_MUS, INTROCW3_MUS,
    NAZI_RAP_MUS, TWELFTH_MUS, ZEROHOUR_MUS, ULTIMATE_MUS, PACMAN_MUS,
    GETTHEM_MUS, SEARCHN_MUS, POW_MUS, SUSPENSE_MUS, GETTHEM_MUS,
    SEARCHN_MUS, POW_MUS, SUSPENSE_MUS, WARMARCH_MUS, CORNER_MUS,
    NAZI_OMI_MUS, PREGNANT_MUS, GOINGAFT_MUS, HEADACHE_MUS, NAZI_OMI_MUS,
    PREGNANT_MUS, HEADACHE_MUS, GOINGAFT_MUS, WARMARCH_MUS, DUNGEON_MUS,
    INTROCW3_MUS, NAZI_RAP_MUS, TWELFTH_MUS, ZEROHOUR_MUS, INTROCW3_MUS,
    NAZI_RAP_MUS, TWELFTH_MUS, ZEROHOUR_MUS, ULTIMATE_MUS, FUNKYOU_MUS };

static const uint8_t ceil_tab[60] = {
    0x1d,0x1d,0x1d,0x1d,0x1d,0x1d,0x1d,0x1d,0x1d,0xbf,
    0x4e,0x4e,0x4e,0x1d,0x8d,0x4e,0x1d,0x2d,0x1d,0x8d,
    0x1d,0x1d,0x1d,0x1d,0x1d,0x2d,0xdd,0x1d,0x1d,0x98,
    0x1d,0x9d,0x2d,0xdd,0xdd,0x9d,0x2d,0x4d,0x1d,0xdd,
    0x7d,0x1d,0x2d,0x2d,0xdd,0xd7,0x1d,0x1d,0x1d,0x2d,
    0x1d,0x1d,0x1d,0x1d,0xdd,0xdd,0x7d,0xdd,0xdd,0xdd };

/* ------------------------------------------------------------ objetos */
static void GetNewActor(void)
{
    for (int i = 1; i < MAXACTORS; i++)
        if (!objlist[i].active) {
            objtype *o = &objlist[i];
            memset(o, 0, sizeof *o);
            o->active = 1;
            o->prev = objtail;
            o->next = NULL;
            if (objtail) objtail->next = o;
            objtail = o;
            if (!objhead) objhead = o;
            newobj = o;
            objcount++;
            return;
        }
    newobj = &objlist[MAXACTORS - 1];
}

static void RemoveObj(objtype *ob)
{
    if (ob == player) return;
    if (ob->prev) ob->prev->next = ob->next;
    else objhead = ob->next;
    if (ob->next) ob->next->prev = ob->prev;
    else objtail = ob->prev;
    ob->active = 0;
    objcount--;
}

static void SpawnNewObj(int tilex, int tiley, int st)
{
    GetNewActor();
    newobj->state = st;
    newobj->ticcount = STATES[st].tictime ? US_RndT() % STATES[st].tictime : 0;
    newobj->tilex = tilex;
    newobj->tiley = tiley;
    newobj->x = ((int32_t)tilex << TILESHIFT) + TILEGLOBAL / 2;
    newobj->y = ((int32_t)tiley << TILESHIFT) + TILEGLOBAL / 2;
    newobj->dir = nodir;
    actorat[tiley][tilex] = newobj;
    int a = maparea[tiley][tilex] - AREATILE;
    newobj->areanumber = (a >= 0 && a < NUMAREAS) ? a : 0;
}

static void NewState(objtype *ob, int st)
{
    ob->state = st;
    ob->ticcount = STATES[st].tictime;
}

/* ------------------------------------------------------------ areas y puertas */
static void RecursiveConnect(int area)
{
    for (int i = 0; i < NUMAREAS; i++)
        if (areaconnect[area][i] && !areabyplayer[i]) { areabyplayer[i] = 1; RecursiveConnect(i); }
}

static void ConnectAreas(void)
{
    memset(areabyplayer, 0, sizeof areabyplayer);
    if (player->areanumber >= 0 && player->areanumber < NUMAREAS) {
        areabyplayer[player->areanumber] = 1;
        RecursiveConnect(player->areanumber);
    }
}

static void OpenDoor(int d)
{
    if (d < 0 || d >= ndoors) return;
    if (doors[d].action == DR_OPEN) doors[d].ticcount = 0;
    else if (doors[d].action != DR_OPENING) doors[d].action = DR_OPENING;
}

static void doors_update(void)
{
    for (int i = 0; i < ndoors; i++) {
        Door *d = &doors[i];
        int a1 = d->area1, a2 = d->area2;
        switch (d->action) {
        case DR_OPENING:
            if (!d->pos) {
                if (a1 >= 0 && a2 >= 0) { areaconnect[a1][a2]++; areaconnect[a2][a1]++; }
                ConnectAreas();
                SD_PlaySound(OPENDOORSND);
            }
            d->pos += tics << 10;
            if (d->pos >= 65535) { d->pos = 65535; d->action = DR_OPEN; d->ticcount = 0; }
            break;
        case DR_OPEN:
            d->ticcount += tics;
            if (d->ticcount >= OPENTICS) {
                if (actorat[d->y][d->x] || player_in_tile(d->x, d->y)) { d->ticcount = 0; break; }
                d->action = DR_CLOSING;
                SD_PlaySound(CLOSEDOORSND);
            }
            break;
        case DR_CLOSING:
            if (actorat[d->y][d->x] || player_in_tile(d->x, d->y)) { d->action = DR_OPENING; break; }
            d->pos -= tics << 10;
            if (d->pos <= 0) {
                d->pos = 0;
                d->action = DR_CLOSED;
                if (a1 >= 0 && a2 >= 0) { areaconnect[a1][a2]--; areaconnect[a2][a1]--; }
                ConnectAreas();
            }
            break;
        default: break;
        }
    }
}

static int doorposition(int d) { return (d >= 0 && d < ndoors) ? doors[d].pos : 0; }

/* ------------------------------------------------------------ TryWalk */
static int check_diag(int x, int y)
{
    if (x < 0 || y < 0 || x >= MAPSIZE || y >= MAPSIZE) return 0;
    if (tilemap[y][x]) return 0;
    objtype *o = actorat[y][x];
    if (o && (o->flags & FL_SHOOTABLE)) return 0;
    return 1;
}

static int check_side(int x, int y, int *doornum)
{
    if (x < 0 || y < 0 || x >= MAPSIZE || y >= MAPSIZE) return 0;
    int t = tilemap[y][x];
    if (t) {
        if (t & 0x80) *doornum = t & 0x7f;
        else return 0;
    }
    objtype *o = actorat[y][x];
    if (o && (o->flags & FL_SHOOTABLE)) return 0;
    return 1;
}

static int TryWalk(objtype *ob)
{
    int doornum = -1;
    int diag = (ob->obclass == dogobj || ob->obclass == fakeobj);
    int nx = ob->tilex, ny = ob->tiley;
    switch (ob->dir) {
    case north:
        if (diag) { if (!check_diag(nx, ny - 1)) return 0; }
        else if (!check_side(nx, ny - 1, &doornum)) return 0;
        ny--; break;
    case northeast:
        if (!check_diag(nx + 1, ny - 1) || !check_diag(nx + 1, ny) || !check_diag(nx, ny - 1)) return 0;
        nx++; ny--; break;
    case east:
        if (diag) { if (!check_diag(nx + 1, ny)) return 0; }
        else if (!check_side(nx + 1, ny, &doornum)) return 0;
        nx++; break;
    case southeast:
        if (!check_diag(nx + 1, ny + 1) || !check_diag(nx + 1, ny) || !check_diag(nx, ny + 1)) return 0;
        nx++; ny++; break;
    case south:
        if (diag) { if (!check_diag(nx, ny + 1)) return 0; }
        else if (!check_side(nx, ny + 1, &doornum)) return 0;
        ny++; break;
    case southwest:
        if (!check_diag(nx - 1, ny + 1) || !check_diag(nx - 1, ny) || !check_diag(nx, ny + 1)) return 0;
        nx--; ny++; break;
    case west:
        if (diag) { if (!check_diag(nx - 1, ny)) return 0; }
        else if (!check_side(nx - 1, ny, &doornum)) return 0;
        nx--; break;
    case northwest:
        if (!check_diag(nx - 1, ny - 1) || !check_diag(nx - 1, ny) || !check_diag(nx, ny - 1)) return 0;
        nx--; ny--; break;
    default: return 0;
    }
    ob->tilex = nx; ob->tiley = ny;
    if (doornum != -1) { OpenDoor(doornum); ob->distance = -doornum - 1; return 1; }
    int a = maparea[ny][nx] - AREATILE;
    ob->areanumber = (a >= 0 && a < NUMAREAS) ? a : 0;
    ob->distance = TILEGLOBAL;
    return 1;
}

static void SelectDodgeDir(objtype *ob)
{
    int dirtry[5], turnaround, tdir;
    if (ob->flags & FL_FIRSTATTACK) { turnaround = nodir; ob->flags &= ~FL_FIRSTATTACK; }
    else turnaround = opposite[ob->dir];
    int deltax = player->tilex - ob->tilex, deltay = player->tiley - ob->tiley;
    if (deltax > 0) { dirtry[1] = east; dirtry[3] = west; }
    else { dirtry[1] = west; dirtry[3] = east; }
    if (deltay > 0) { dirtry[2] = south; dirtry[4] = north; }
    else { dirtry[2] = north; dirtry[4] = south; }
    if (abs(deltax) > abs(deltay)) {
        tdir = dirtry[1]; dirtry[1] = dirtry[2]; dirtry[2] = tdir;
        tdir = dirtry[3]; dirtry[3] = dirtry[4]; dirtry[4] = tdir;
    }
    if (US_RndT() < 128) {
        tdir = dirtry[1]; dirtry[1] = dirtry[2]; dirtry[2] = tdir;
        tdir = dirtry[3]; dirtry[3] = dirtry[4]; dirtry[4] = tdir;
    }
    dirtry[0] = diagonal_dir(dirtry[1], dirtry[2]);
    for (int i = 0; i < 5; i++) {
        if (dirtry[i] == nodir || dirtry[i] == turnaround) continue;
        ob->dir = dirtry[i];
        if (TryWalk(ob)) return;
    }
    if (turnaround != nodir) { ob->dir = turnaround; if (TryWalk(ob)) return; }
    ob->dir = nodir;
}

static void SelectChaseDir(objtype *ob)
{
    int d[3], tdir, olddir = ob->dir, turnaround = opposite[olddir];
    int deltax = player->tilex - ob->tilex, deltay = player->tiley - ob->tiley;
    d[1] = d[2] = nodir;
    if (deltax > 0) d[1] = east; else if (deltax < 0) d[1] = west;
    if (deltay > 0) d[2] = south; else if (deltay < 0) d[2] = north;
    if (abs(deltay) > abs(deltax)) { tdir = d[1]; d[1] = d[2]; d[2] = tdir; }
    if (d[1] == turnaround) d[1] = nodir;
    if (d[2] == turnaround) d[2] = nodir;
    if (d[1] != nodir) { ob->dir = d[1]; if (TryWalk(ob)) return; }
    if (d[2] != nodir) { ob->dir = d[2]; if (TryWalk(ob)) return; }
    if (olddir != nodir) { ob->dir = olddir; if (TryWalk(ob)) return; }
    static const int order[4] = { east, north, west, south };
    if (US_RndT() > 128) {
        for (int i = 0; i < 4; i++)
            if (order[i] != turnaround) { ob->dir = order[i]; if (TryWalk(ob)) return; }
    } else {
        for (int i = 3; i >= 0; i--)
            if (order[i] != turnaround) { ob->dir = order[i]; if (TryWalk(ob)) return; }
    }
    if (turnaround != nodir) { ob->dir = turnaround; if (TryWalk(ob)) return; }
    ob->dir = nodir;
}

static void MoveObj(objtype *ob, int32_t move)
{
    int32_t dx = 0, dy = 0;
    switch (ob->dir) {
    case north: dy = -move; break;
    case northeast: dx = move; dy = -move; break;
    case east: dx = move; break;
    case southeast: dx = move; dy = move; break;
    case south: dy = move; break;
    case southwest: dx = -move; dy = move; break;
    case west: dx = -move; break;
    case northwest: dx = -move; dy = -move; break;
    default: return;
    }
    ob->x += dx; ob->y += dy;
    if (ob->areanumber >= 0 && ob->areanumber < NUMAREAS && areabyplayer[ob->areanumber]) {
        int32_t ddx = ob->x - player->x, ddy = ob->y - player->y;
        if (ddx >= -MINACTORDIST && ddx <= MINACTORDIST &&
            ddy >= -MINACTORDIST && ddy <= MINACTORDIST) {
            ob->x -= dx; ob->y -= dy;
            return;
        }
    }
    ob->distance -= move;
}

/* ------------------------------------------------------------ vision */
static int CheckLine(objtype *ob)
{
    int x1 = ob->x >> 8, y1 = ob->y >> 8;
    int xt1 = x1 >> 8, yt1 = y1 >> 8;
    int x2 = player->x >> 8, y2 = player->y >> 8;
    int xt2 = player->tilex, yt2 = player->tiley;
    int x, y, partial, delta, deltafrac, xstep, ystep, xfrac, yfrac;
    long ltemp;
    unsigned value, intercept;

    if (abs(xt2 - xt1) > 0) {
        if (xt2 > xt1) { partial = 256 - (x1 & 0xff); xstep = 1; }
        else { partial = x1 & 0xff; xstep = -1; }
        deltafrac = abs(x2 - x1);
        if (!deltafrac) deltafrac = 1;
        delta = y2 - y1;
        ltemp = ((long)delta * 256) / deltafrac;
        ystep = ltemp > 0x7fff ? 0x7fff : ltemp < -0x7fff ? -0x7fff : (int)ltemp;
        yfrac = y1 + (int)(((long)ystep * partial) >> 8);
        x = xt1 + xstep;
        int endx = xt2 + xstep;
        do {
            y = yfrac >> 8;
            yfrac += ystep;
            if (x < 0 || x >= MAPSIZE || y < 0 || y >= MAPSIZE) return 0;
            value = tilemap[y][x];
            x += xstep;
            if (!value) continue;
            if (!(value & 0x80)) return 0;
            intercept = yfrac - ystep / 2;
            if (intercept > (unsigned)doorposition(value & 0x7f)) return 0;
        } while (x != endx);
    }
    if (abs(yt2 - yt1) > 0) {
        if (yt2 > yt1) { partial = 256 - (y1 & 0xff); ystep = 1; }
        else { partial = y1 & 0xff; ystep = -1; }
        deltafrac = abs(y2 - y1);
        if (!deltafrac) deltafrac = 1;
        delta = x2 - x1;
        ltemp = ((long)delta * 256) / deltafrac;
        xstep = ltemp > 0x7fff ? 0x7fff : ltemp < -0x7fff ? -0x7fff : (int)ltemp;
        xfrac = x1 + (int)(((long)xstep * partial) >> 8);
        y = yt1 + ystep;
        int endy = yt2 + ystep;
        do {
            x = xfrac >> 8;
            xfrac += xstep;
            if (x < 0 || x >= MAPSIZE || y < 0 || y >= MAPSIZE) return 0;
            value = tilemap[y][x];
            y += ystep;
            if (!value) continue;
            if (!(value & 0x80)) return 0;
            intercept = xfrac - xstep / 2;
            if (intercept > (unsigned)doorposition(value & 0x7f)) return 0;
        } while (y != endy);
    }
    return 1;
}

static int CheckSight(objtype *ob)
{
    if (!areabyplayer[ob->areanumber]) return 0;
    int32_t deltax = player->x - ob->x, deltay = player->y - ob->y;
    if (deltax > -MINSIGHT && deltax < MINSIGHT && deltay > -MINSIGHT && deltay < MINSIGHT) return 1;
    switch (ob->dir) {
    case north: if (deltay > 0) return 0; break;
    case east: if (deltax < 0) return 0; break;
    case south: if (deltay < 0) return 0; break;
    case west: if (deltax > 0) return 0; break;
    default: break;
    }
    return CheckLine(ob);
}

static void FirstSighting(objtype *ob)
{
    switch (ob->obclass) {
    case guardobj: SD_PlaySound(HALTSND); NewState(ob, ST_GRDCHASE1); ob->speed *= 3; break;
    case officerobj: SD_PlaySound(SPIONSND); NewState(ob, ST_OFCCHASE1); ob->speed *= 5; break;
    case mutantobj: NewState(ob, ST_MUTCHASE1); ob->speed *= 3; break;
    case ssobj: SD_PlaySound(SCHUTZADSND); NewState(ob, ST_SSCHASE1); ob->speed *= 4; break;
    case dogobj: SD_PlaySound(DOGBARKSND); NewState(ob, ST_DOGCHASE1); ob->speed *= 2; break;
    case bossobj: SD_PlaySound(GUTENTAGSND); NewState(ob, ST_BOSSCHASE1); ob->speed = SPDPATROL * 3; break;
    case gretelobj: SD_PlaySound(KEINSND); NewState(ob, ST_GRETELCHASE1); ob->speed *= 3; break;
    case giftobj: SD_PlaySound(EINESND); NewState(ob, ST_GIFTCHASE1); ob->speed *= 3; break;
    case fatobj: SD_PlaySound(ERLAUBENSND); NewState(ob, ST_FATCHASE1); ob->speed *= 3; break;
    case schabbobj: SD_PlaySound(SCHABBSHASND); NewState(ob, ST_SCHABBCHASE1); ob->speed *= 3; break;
    case fakeobj: SD_PlaySound(TOT_HUNDSND); NewState(ob, ST_FAKECHASE1); ob->speed *= 3; break;
    case mechahitlerobj: SD_PlaySound(DIESND); NewState(ob, ST_MECHACHASE1); ob->speed *= 3; break;
    case realhitlerobj: SD_PlaySound(DIESND); NewState(ob, ST_HITLERCHASE1); ob->speed *= 5; break;
    case ghostobj: NewState(ob, ST_BLINKYCHASE1); ob->speed *= 2; break;
    default: break;
    }
    if (ob->distance < 0) ob->distance = 0;
    ob->flags |= FL_ATTACKMODE | FL_FIRSTATTACK;
}

static int SightPlayer(objtype *ob)
{
    if (ob->flags & FL_ATTACKMODE) return 0;
    if (ob->temp2) {
        ob->temp2 -= tics;
        if (ob->temp2 > 0) return 0;
        ob->temp2 = 0;
    } else {
        if (!areabyplayer[ob->areanumber]) return 0;
        if (ob->flags & FL_AMBUSH) {
            if (!CheckSight(ob)) return 0;
            ob->flags &= ~FL_AMBUSH;
        } else if (!madenoise && !CheckSight(ob)) return 0;
        switch (ob->obclass) {
        case guardobj: ob->temp2 = 1 + US_RndT() / 4; break;
        case officerobj: ob->temp2 = 2; break;
        case mutantobj: case ssobj: ob->temp2 = 1 + US_RndT() / 6; break;
        case dogobj: ob->temp2 = 1 + US_RndT() / 8; break;
        default: ob->temp2 = 1; break;
        }
        return 0;
    }
    FirstSighting(ob);
    return 1;
}

/* ------------------------------------------------------------ dano */
static void GivePoints(long p) { gs.score += p; }

static void KillActor(objtype *ob)
{
    int tilex = ob->tilex = ob->x >> TILESHIFT;
    int tiley = ob->tiley = ob->y >> TILESHIFT;
    switch (ob->obclass) {
    case guardobj: GivePoints(100); NewState(ob, ST_GRDDIE1); PlaceItemType(bo_clip2, tilex, tiley); break;
    case officerobj: GivePoints(400); NewState(ob, ST_OFCDIE1); PlaceItemType(bo_clip2, tilex, tiley); break;
    case mutantobj: GivePoints(700); NewState(ob, ST_MUTDIE1); PlaceItemType(bo_clip2, tilex, tiley); break;
    case ssobj:
        GivePoints(500); NewState(ob, ST_SSDIE1);
        PlaceItemType(gs.bestweapon < wp_machinegun ? bo_machinegun : bo_clip2, tilex, tiley);
        break;
    case dogobj: GivePoints(200); NewState(ob, ST_DOGDIE1); break;
    case bossobj: GivePoints(5000); NewState(ob, ST_BOSSDIE1); PlaceItemType(bo_key1, tilex, tiley); break;
    case gretelobj: GivePoints(5000); NewState(ob, ST_GRETELDIE1); PlaceItemType(bo_key1, tilex, tiley); break;
    case giftobj: GivePoints(5000); NewState(ob, ST_GIFTDIE1); break;
    case fatobj: GivePoints(5000); NewState(ob, ST_FATDIE1); break;
    case schabbobj: GivePoints(5000); NewState(ob, ST_SCHABBDIE1); break;
    case fakeobj: GivePoints(2000); NewState(ob, ST_FAKEDIE1); break;
    case mechahitlerobj: GivePoints(5000); NewState(ob, ST_MECHADIE1); break;
    case realhitlerobj: GivePoints(5000); NewState(ob, ST_HITLERDIE1); break;
    default: RemoveObj(ob); return;
    }
    gs.killcount++;
    ob->flags &= ~FL_SHOOTABLE;
    if (ob->tilex >= 0 && ob->tiley >= 0 && ob->tilex < MAPSIZE && ob->tiley < MAPSIZE)
        actorat[ob->tiley][ob->tilex] = NULL;
    ob->flags |= FL_NONMARK;
}

/* A_DeathScream (WL_ACT2.C) */
static void A_DeathScream(objtype *ob)
{
    static const int screams[8] = { DEATHSCREAM1SND, DEATHSCREAM2SND, DEATHSCREAM3SND,
        DEATHSCREAM4SND, DEATHSCREAM5SND, DEATHSCREAM7SND, DEATHSCREAM8SND, DEATHSCREAM9SND };
    switch (ob->obclass) {
    case mutantobj: SD_PlaySound(AHHHGSND); break;
    case guardobj: SD_PlaySound(screams[US_RndT() % 8]); break;
    case officerobj: SD_PlaySound(NEINSOVASSND); break;
    case ssobj: SD_PlaySound(LEBENSND); break;
    case dogobj: SD_PlaySound(DOGDEATHSND); break;
    case bossobj: SD_PlaySound(MUTTISND); break;
    case schabbobj: SD_PlaySound(MEINGOTTSND); break;
    case fakeobj: SD_PlaySound(HITLERHASND); break;
    case mechahitlerobj: SD_PlaySound(SCHEISTSND); break;
    case realhitlerobj: SD_PlaySound(EVASND); break;
    case gretelobj: SD_PlaySound(MEINSND); break;
    case giftobj: SD_PlaySound(DONNERSND); break;
    case fatobj: SD_PlaySound(ROSESND); break;
    default: break;
    }
}

static void DamageActor(objtype *ob, int damage)
{
    madenoise = 1;
    if (!(ob->flags & FL_ATTACKMODE)) damage <<= 1;
    ob->hitpoints -= damage;
    if (ob->hitpoints <= 0) { KillActor(ob); return; }
    if (!(ob->flags & FL_ATTACKMODE)) FirstSighting(ob);
    switch (ob->obclass) {
    case guardobj: NewState(ob, (ob->hitpoints & 1) ? ST_GRDPAIN : ST_GRDPAIN1); break;
    case officerobj: NewState(ob, (ob->hitpoints & 1) ? ST_OFCPAIN : ST_OFCPAIN1); break;
    case mutantobj: NewState(ob, (ob->hitpoints & 1) ? ST_MUTPAIN : ST_MUTPAIN1); break;
    case ssobj: NewState(ob, (ob->hitpoints & 1) ? ST_SSPAIN : ST_SSPAIN1); break;
    default: break;
    }
}

static void TakeDamage(int points, objtype *attacker)
{
    (void)attacker;
    if (gs.victoryflag) return;
    if (difficulty == 0) points >>= 2;
    if (!gs.godmode) gs.health -= points;
    if (gs.health <= 0) { gs.health = 0; playstate = 1; SD_PlaySound(PLAYERDEATHSND); }
    else SD_PlaySound(TAKEDAMAGESND);
    damageflash += points;
    if (damageflash > 16) damageflash = 16;
}

/* ------------------------------------------------------------ IA */
static void T_Stand(objtype *ob) { SightPlayer(ob); }

static void chase_common(objtype *ob, int shootstate)
{
    int32_t move;
    int dodge = 0;
    if (gs.victoryflag) return;
    if (CheckLine(ob)) {
        int dx = abs(ob->tilex - player->tilex), dy = abs(ob->tiley - player->tiley);
        int dist = dx > dy ? dx : dy, chance;
        if (!dist || (dist == 1 && ob->distance < 0x4000)) chance = 300;
        else chance = (tics << 4) / dist;
        if (US_RndT() < chance) {
            if (shootstate >= 0) NewState(ob, shootstate);
            return;
        }
        dodge = 1;
    }
    if (ob->dir == nodir) {
        if (dodge) SelectDodgeDir(ob); else SelectChaseDir(ob);
        if (ob->dir == nodir) return;
    }
    move = ob->speed * tics;
    while (move > 0) {
        if (ob->distance < 0) {
            int dn = -ob->distance - 1;
            OpenDoor(dn);
            if (doors[dn].action != DR_OPEN) return;
            ob->distance = TILEGLOBAL;
        }
        if (move < ob->distance) { MoveObj(ob, move); break; }
        ob->x = ((int32_t)ob->tilex << TILESHIFT) + TILEGLOBAL / 2;
        ob->y = ((int32_t)ob->tiley << TILESHIFT) + TILEGLOBAL / 2;
        move -= ob->distance;
        if (dodge) SelectDodgeDir(ob); else SelectChaseDir(ob);
        if (ob->dir == nodir) return;
    }
}

static void T_Chase(objtype *ob)
{
    int shoot = -1;
    switch (ob->obclass) {
    case guardobj: shoot = ST_GRDSHOOT1; break;
    case officerobj: shoot = ST_OFCSHOOT1; break;
    case mutantobj: shoot = ST_MUTSHOOT1; break;
    case ssobj: shoot = ST_SSSHOOT1; break;
    case bossobj: shoot = ST_BOSSSHOOT1; break;
    case gretelobj: shoot = ST_GRETELSHOOT1; break;
    case mechahitlerobj: shoot = ST_MECHASHOOT1; break;
    case realhitlerobj: shoot = ST_HITLERSHOOT1; break;
    default: break;
    }
    chase_common(ob, shoot);
}

static void T_Schabb(objtype *ob) { chase_common(ob, ST_SCHABBSHOOT1); }
static void T_Gift(objtype *ob) { chase_common(ob, ST_GIFTSHOOT1); }
static void T_Fat(objtype *ob) { chase_common(ob, ST_FATSHOOT1); }
static void T_Fake(objtype *ob) { chase_common(ob, ST_FAKESHOOT1); }
static void T_Ghosts(objtype *ob) { chase_common(ob, -1); }

static void T_DogChase(objtype *ob)
{
    int32_t move, dx, dy;
    if (ob->dir == nodir) { SelectDodgeDir(ob); if (ob->dir == nodir) return; }
    move = ob->speed * tics;
    while (move > 0) {
        dx = labs(player->x - ob->x) - move;
        if (dx <= MINACTORDIST) {
            dy = labs(player->y - ob->y) - move;
            if (dy <= MINACTORDIST) { NewState(ob, ST_DOGJUMP1); return; }
        }
        if (move < ob->distance) { MoveObj(ob, move); break; }
        ob->x = ((int32_t)ob->tilex << TILESHIFT) + TILEGLOBAL / 2;
        ob->y = ((int32_t)ob->tiley << TILESHIFT) + TILEGLOBAL / 2;
        move -= ob->distance;
        SelectDodgeDir(ob);
        if (ob->dir == nodir) return;
    }
}

static void SelectPathDir(objtype *ob)
{
    int spot = plane1[ob->tiley * MAPSIZE + ob->tilex] - 90;   /* ICONARROWS */
    if (spot >= 0 && spot < 8) ob->dir = spot;
    ob->distance = TILEGLOBAL;
    if (!TryWalk(ob)) ob->dir = nodir;
}

static void T_Path(objtype *ob)
{
    int32_t move;
    if (SightPlayer(ob)) return;
    if (ob->dir == nodir) { SelectPathDir(ob); if (ob->dir == nodir) return; }
    move = ob->speed * tics;
    while (move > 0) {
        if (ob->distance < 0) {
            int dn = -ob->distance - 1;
            OpenDoor(dn);
            if (doors[dn].action != DR_OPEN) return;
            ob->distance = TILEGLOBAL;
        }
        if (move < ob->distance) { MoveObj(ob, move); break; }
        ob->x = ((int32_t)ob->tilex << TILESHIFT) + TILEGLOBAL / 2;
        ob->y = ((int32_t)ob->tiley << TILESHIFT) + TILEGLOBAL / 2;
        move -= ob->distance;
        SelectPathDir(ob);
        if (ob->dir == nodir) return;
    }
}

static void T_Shoot(objtype *ob)
{
    int hitchance, damage;
    if (!areabyplayer[ob->areanumber]) return;
    if (!CheckLine(ob)) return;
    int dx = abs(ob->tilex - player->tilex), dy = abs(ob->tiley - player->tiley);
    int dist = dx > dy ? dx : dy;
    if (ob->obclass == ssobj || ob->obclass == bossobj) dist = dist * 2 / 3;
    if (thrustspeed >= RUNSPEED)
        hitchance = (ob->flags & FL_VISABLE) ? 160 - dist * 16 : 160 - dist * 8;
    else
        hitchance = (ob->flags & FL_VISABLE) ? 256 - dist * 16 : 256 - dist * 8;
    if (US_RndT() < hitchance) {
        if (dist < 2) damage = US_RndT() >> 2;
        else if (dist < 4) damage = US_RndT() >> 3;
        else damage = US_RndT() >> 4;
        TakeDamage(damage, ob);
    }
    switch (ob->obclass) {
    case ssobj: SD_PlaySound(SSFIRESND); break;
    case bossobj: case gretelobj: SD_PlaySound(BOSSFIRESND); break;
    case giftobj: case fatobj: SD_PlaySound(MISSILEFIRESND); break;
    default: SD_PlaySound(NAZIFIRESND); break;
    }
}

static void T_Bite(objtype *ob)
{
    int32_t dx = labs(player->x - ob->x);
    if (dx < 0x18000L) {
        int32_t dy = labs(player->y - ob->y);
        if (dy < 0x18000L) {
            SD_PlaySound(DOGATTACKSND);
            if (US_RndT() < 180) TakeDamage(US_RndT() >> 4, ob);
        }
    }
}

static void T_Projectile(objtype *ob)
{
    int32_t speed = ob->speed * tics;
    double a = ob->angle * M_PI / 180.0;
    ob->x += (int32_t)(speed * cos(a));
    ob->y -= (int32_t)(speed * sin(a));
    if (labs(ob->x - player->x) < 0x8000L && labs(ob->y - player->y) < 0x8000L) {
        TakeDamage(US_RndT() >> 3, ob);
        RemoveObj(ob);
        return;
    }
    ob->tilex = ob->x >> TILESHIFT;
    ob->tiley = ob->y >> TILESHIFT;
    if (ob->tilex < 0 || ob->tiley < 0 || ob->tilex >= MAPSIZE || ob->tiley >= MAPSIZE) { RemoveObj(ob); return; }
    int t = tilemap[ob->tiley][ob->tilex];
    if (t && (!(t & 0x80) || doorposition(t & 0x7f) < 0xc000)) RemoveObj(ob);
}

static void SpawnProjectile(objtype *ob, int cls, int statenum, int32_t speed)
{
    int32_t ox = ob->x, oy = ob->y;
    SpawnNewObj(ob->tilex, ob->tiley, statenum);
    newobj->obclass = cls;
    newobj->x = ox;
    newobj->y = oy;
    newobj->speed = speed;
    newobj->flags = FL_NEVERMARK;
    double dx = (double)(player->x - ox), dy = (double)(player->y - oy);
    int ang = (int)(atan2(-dy, dx) * 180.0 / M_PI);
    newobj->angle = (ang % 360 + 360) % 360;
}

static void T_SchabbThrow(objtype *ob) { SpawnProjectile(ob, needleobj, ST_NEEDLE1, 0x2000); }
static void T_GiftThrow(objtype *ob) { SpawnProjectile(ob, rocketobj, ST_ROCKET, 0x2000); }
static void T_FakeFire(objtype *ob) { SpawnProjectile(ob, fireobj, ST_FIRE1, 0x1200); }

static void A_HitlerMorph(objtype *ob)
{
    int hp = STARTHITPOINTS[difficulty][en_hitler];
    int32_t ox = ob->x, oy = ob->y;
    int odir = ob->dir;
    int32_t odist = ob->distance;
    SpawnNewObj(ob->tilex, ob->tiley, ST_HITLERCHASE1);
    newobj->obclass = realhitlerobj;
    newobj->hitpoints = hp;
    newobj->flags = FL_SHOOTABLE | FL_ATTACKMODE;
    newobj->x = ox; newobj->y = oy;
    newobj->dir = odir;
    newobj->speed = SPDPATROL * 5;
    newobj->distance = odist;
}

static void do_think(int id, objtype *ob)
{
    switch (id) {
    case TH_T_Stand: T_Stand(ob); break;
    case TH_T_Path: T_Path(ob); break;
    case TH_T_Chase: T_Chase(ob); break;
    case TH_T_DogChase: T_DogChase(ob); break;
    case TH_T_Shoot: T_Shoot(ob); break;
    case TH_T_Bite: T_Bite(ob); break;
    case TH_T_Projectile: T_Projectile(ob); break;
    case TH_T_Schabb: T_Schabb(ob); break;
    case TH_T_SchabbThrow: T_SchabbThrow(ob); break;
    case TH_T_Gift: T_Gift(ob); break;
    case TH_T_GiftThrow: T_GiftThrow(ob); break;
    case TH_T_Fat: T_Fat(ob); break;
    case TH_T_Fake: T_Fake(ob); break;
    case TH_T_FakeFire: T_FakeFire(ob); break;
    case TH_T_Ghosts: T_Ghosts(ob); break;
    case TH_A_HitlerMorph: A_HitlerMorph(ob); break;
    case TH_A_DeathScream: A_DeathScream(ob); break;
    case TH_A_MechaSound: SD_PlaySound(MECHSTEPSND); break;
    case TH_A_Slurpie: SD_PlaySound(SLURPIESND); break;
    default: break;
    }
}

static void DoActor(objtype *ob)
{
    int proj = (ob->obclass == needleobj || ob->obclass == rocketobj || ob->obclass == fireobj);
    if (!proj && ob->areanumber >= 0 && ob->areanumber < NUMAREAS
        && !areabyplayer[ob->areanumber] && !(ob->flags & FL_ATTACKMODE)) return;
    if (!(ob->flags & (FL_NONMARK | FL_NEVERMARK)))
        if (ob->tilex >= 0 && ob->tiley >= 0 && ob->tilex < MAPSIZE && ob->tiley < MAPSIZE
            && actorat[ob->tiley][ob->tilex] == ob)
            actorat[ob->tiley][ob->tilex] = NULL;

    if (!ob->ticcount) {
        do_think(STATES[ob->state].think, ob);
        if (!ob->active) return;
    } else {
        ob->ticcount -= tics;
        int guard = 0;
        while (ob->ticcount <= 0 && guard++ < 64) {
            do_think(STATES[ob->state].action, ob);
            if (!ob->active) return;
            int nxt = STATES[ob->state].next;
            if (nxt < 0) { RemoveObj(ob); return; }
            if (nxt == ob->state && !STATES[nxt].tictime) { ob->ticcount = 0; break; }
            ob->state = nxt;
            if (!STATES[ob->state].tictime) { ob->ticcount = 0; break; }
            ob->ticcount += STATES[ob->state].tictime;
        }
        do_think(STATES[ob->state].think, ob);
        if (!ob->active) return;
    }
    if (ob->flags & FL_NEVERMARK) return;
    if (ob->tilex < 0 || ob->tiley < 0 || ob->tilex >= MAPSIZE || ob->tiley >= MAPSIZE) return;
    if ((ob->flags & FL_NONMARK) && actorat[ob->tiley][ob->tilex]) return;
    actorat[ob->tiley][ob->tilex] = ob;
}

/* ------------------------------------------------------------ spawns */
static void SpawnStatic(int tilex, int tiley, int type)
{
    if (nstats >= MAXSTATS || type < 0 || type >= (int)(sizeof STATINFO / sizeof STATINFO[0])) return;
    statobj_t *s = &statlist[nstats++];
    s->tilex = tilex; s->tiley = tiley;
    s->shapenum = STATINFO[type].shapenum;
    s->itemnumber = STATINFO[type].item;
    s->flags = 0;
    if (STATINFO[type].block) tilemap[tiley][tilex] = 64;
    if (s->itemnumber) {
        s->flags = FL_BONUS;
        if ((s->itemnumber >= bo_cross && s->itemnumber <= bo_crown) || s->itemnumber == bo_fullheal)
            gs.treasuretotal++;
    }
}

static int enemy_class(int which)
{
    switch (which) {
    case en_guard: return guardobj;
    case en_officer: return officerobj;
    case en_ss: return ssobj;
    case en_dog: return dogobj;
    default: return mutantobj;
    }
}

static void SpawnStand(int which, int tilex, int tiley, int dir)
{
    int st = (which == en_guard) ? ST_GRDSTAND : (which == en_officer) ? ST_OFCSTAND :
             (which == en_ss) ? ST_SSSTAND : (which == en_dog) ? ST_DOGPATH1 : ST_MUTSTAND;
    SpawnNewObj(tilex, tiley, st);
    newobj->speed = (which == en_dog) ? SPDDOG : SPDPATROL;
    newobj->obclass = enemy_class(which);
    newobj->hitpoints = STARTHITPOINTS[difficulty][which];
    newobj->dir = dir * 2;
    newobj->flags |= FL_SHOOTABLE;
    if (maparea[tiley][tilex] == AMBUSHTILE) newobj->flags |= FL_AMBUSH;
    gs.killtotal++;
}

static void SpawnPatrol(int which, int tilex, int tiley, int dir)
{
    int st = (which == en_guard) ? ST_GRDPATH1 : (which == en_officer) ? ST_OFCPATH1 :
             (which == en_ss) ? ST_SSPATH1 : (which == en_dog) ? ST_DOGPATH1 : ST_MUTPATH1;
    SpawnNewObj(tilex, tiley, st);
    newobj->speed = (which == en_dog) ? SPDDOG : SPDPATROL;
    newobj->obclass = enemy_class(which);
    newobj->hitpoints = STARTHITPOINTS[difficulty][which];
    newobj->dir = dir * 2;
    newobj->distance = TILEGLOBAL;
    newobj->flags |= FL_SHOOTABLE;
    gs.killtotal++;
}

static void SpawnDeadGuard(int tilex, int tiley)
{
    SpawnNewObj(tilex, tiley, ST_GRDDIE4);
    newobj->obclass = inertobj;
    newobj->flags |= FL_NONMARK;
}

static void SpawnBossType(int tilex, int tiley, int st, int cls, int hpidx)
{
    SpawnNewObj(tilex, tiley, st);
    newobj->obclass = cls;
    newobj->speed = SPDPATROL;
    newobj->hitpoints = STARTHITPOINTS[difficulty][hpidx];
    newobj->dir = south;
    newobj->flags |= FL_SHOOTABLE | FL_AMBUSH;
    gs.killtotal++;
}

static void PlaceItemType(int itemtype, int tilex, int tiley)
{
    if (nstats >= MAXSTATS) return;
    int shape = -1;
    for (unsigned i = 0; i < sizeof STATINFO / sizeof STATINFO[0]; i++)
        if (STATINFO[i].item == itemtype) { shape = STATINFO[i].shapenum; break; }
    if (shape < 0) return;
    statobj_t *s = &statlist[nstats++];
    s->tilex = tilex; s->tiley = tiley;
    s->shapenum = shape;
    s->itemnumber = itemtype;
    s->flags = FL_BONUS;
}

/* ------------------------------------------------------------ nivel */
static void SpawnPlayer(int tilex, int tiley, int dir)
{
    player->obclass = playerobj;
    player->active = 1;
    player->tilex = tilex; player->tiley = tiley;
    player->x = ((int32_t)tilex << TILESHIFT) + TILEGLOBAL / 2;
    player->y = ((int32_t)tiley << TILESHIFT) + TILEGLOBAL / 2;
    px = tilex + 0.5; py = tiley + 0.5;
    pangle = (dir == north) ? 90 : (dir == east) ? 0 : (dir == south) ? 270 : 180;
    int a = maparea[tiley][tilex] - AREATILE;
    player->areanumber = (a >= 0 && a < NUMAREAS) ? a : 0;
    player->flags = FL_NEVERMARK;
}

static int load_map(int n)
{
    if (n < 0 || n >= 100) return 0;
    int32_t off;
    memcpy(&off, maphead + 2 + 4 * n, 4);
    if (off <= 0 || (size_t)off + 38 > gamemaps_len) return 0;
    uint16_t tag = maphead[0] | (maphead[1] << 8);
    const uint8_t *hdr = gamemaps + off;
    int32_t pstart[3];
    memcpy(pstart, hdr, 12);
    memcpy(mapname, hdr + 22, 16);
    mapname[16] = 0;
    for (int p = 0; p < 2; p++) {
        uint16_t *dst = p ? plane1 : plane0;
        const uint8_t *src = gamemaps + pstart[p];
        int explen = src[0] | (src[1] << 8);
        uint16_t *tmp = malloc(explen + 8);
        carmack_expand(src + 2, tmp, explen);
        rlew_expand(tmp + 1, dst, MAPSIZE * MAPSIZE * 2, tag);
        free(tmp);
    }
    mapon = n;
    gs.episode = n / 10;
    ceilcolor = (n < 60) ? ceil_tab[n] : 0x1d;
    gs.killcount = gs.killtotal = gs.treasurecount = gs.treasuretotal = 0;
    gs.secretcount = gs.secrettotal = 0;
    gs.victoryflag = 0;
    gs.keys = 0;
    playstate = 0;
    ndoors = nstats = 0;
    objcount = 0;
    objhead = objtail = NULL;
    pwallstate = pwallpos = 0;
    memset(objlist, 0, sizeof objlist);
    memset(tilemap, 0, sizeof tilemap);
    memset(actorat, 0, sizeof actorat);
    memset(areaconnect, 0, sizeof areaconnect);
    memset(doors, 0, sizeof doors);

    player = &objlist[0];
    memset(player, 0, sizeof *player);
    player->active = 1;
    player->obclass = playerobj;
    objhead = objtail = player;

    for (int y = 0; y < MAPSIZE; y++)
        for (int x = 0; x < MAPSIZE; x++) {
            int t = plane0[y * MAPSIZE + x];
            maparea[y][x] = t;
            if (t >= 90 && t <= 101) {
                if (ndoors >= MAXDOORS) continue;
                Door *d = &doors[ndoors];
                d->x = x; d->y = y;
                d->vertical = !(t & 1);
                d->lock = (t <= 91) ? 0 : (t >= 100) ? 5 : (t - 90) / 2;
                d->action = DR_CLOSED;
                int a1, a2;
                if (d->vertical) {
                    a1 = plane0[y * MAPSIZE + (x > 0 ? x - 1 : x)] - AREATILE;
                    a2 = plane0[y * MAPSIZE + (x < 63 ? x + 1 : x)] - AREATILE;
                } else {
                    a1 = plane0[(y > 0 ? y - 1 : y) * MAPSIZE + x] - AREATILE;
                    a2 = plane0[(y < 63 ? y + 1 : y) * MAPSIZE + x] - AREATILE;
                }
                d->area1 = (a1 >= 0 && a1 < NUMAREAS) ? a1 : -1;
                d->area2 = (a2 >= 0 && a2 < NUMAREAS) ? a2 : -1;
                tilemap[y][x] = 0x80 | ndoors;
                ndoors++;
            } else if (t > 0 && t < AREATILE) {
                tilemap[y][x] = t;
            }
        }

    for (int y = 0; y < MAPSIZE; y++)
        for (int x = 0; x < MAPSIZE; x++) {
            int t = plane1[y * MAPSIZE + x];
            if (!t) continue;
            if (t >= 19 && t <= 22) { SpawnPlayer(x, y, north + t - 19); continue; }
            if (t >= 23 && t <= 74) { SpawnStatic(x, y, t - 23); continue; }
            if (t == 98) { gs.secrettotal++; continue; }
            if (t == 124) { SpawnDeadGuard(x, y); continue; }
            int tt = t;
            if (tt >= 180 && tt <= 213) { if (difficulty < 3) continue; tt -= 36; }
            if (tt >= 144 && tt <= 177) { if (difficulty < 2) continue; tt -= 36; }
            if (tt >= 108 && tt <= 111) { SpawnStand(en_guard, x, y, tt - 108); continue; }
            if (tt >= 112 && tt <= 115) { SpawnPatrol(en_guard, x, y, tt - 112); continue; }
            if (tt >= 116 && tt <= 119) { SpawnStand(en_officer, x, y, tt - 116); continue; }
            if (tt >= 120 && tt <= 123) { SpawnPatrol(en_officer, x, y, tt - 120); continue; }
            if (tt >= 126 && tt <= 129) { SpawnStand(en_ss, x, y, tt - 126); continue; }
            if (tt >= 130 && tt <= 133) { SpawnPatrol(en_ss, x, y, tt - 130); continue; }
            if (tt >= 134 && tt <= 137) { SpawnStand(en_dog, x, y, tt - 134); continue; }
            if (tt >= 138 && tt <= 141) { SpawnPatrol(en_dog, x, y, tt - 138); continue; }
            tt = t;
            if (tt >= 252 && tt <= 259) { if (difficulty < 3) continue; tt -= 18; }
            if (tt >= 234 && tt <= 241) { if (difficulty < 2) continue; tt -= 18; }
            if (tt >= 216 && tt <= 219) { SpawnStand(en_mutant, x, y, tt - 216); continue; }
            if (tt >= 220 && tt <= 223) { SpawnPatrol(en_mutant, x, y, tt - 220); continue; }
            switch (t) {
            case 214: SpawnBossType(x, y, ST_BOSSSTAND, bossobj, en_boss); break;
            case 197: SpawnBossType(x, y, ST_GRETELSTAND, gretelobj, en_gretel); break;
            case 215: SpawnBossType(x, y, ST_GIFTSTAND, giftobj, en_gift); break;
            case 179: SpawnBossType(x, y, ST_FATSTAND, fatobj, en_fat); break;
            case 196: SpawnBossType(x, y, ST_SCHABBSTAND, schabbobj, en_schabbs); break;
            case 160: SpawnBossType(x, y, ST_FAKESTAND, fakeobj, en_fake); break;
            case 178: SpawnBossType(x, y, ST_MECHASTAND, mechahitlerobj, en_hitler); break;
            case 224: SpawnBossType(x, y, ST_BLINKYCHASE1, ghostobj, en_blinky); break;
            case 225: SpawnBossType(x, y, ST_CLYDECHASE1, ghostobj, en_clyde); break;
            case 226: SpawnBossType(x, y, ST_PINKYCHASE1, ghostobj, en_pinky); break;
            case 227: SpawnBossType(x, y, ST_INKYCHASE1, ghostobj, en_inky); break;
            default: break;
            }
        }
    ConnectAreas();
    return 1;
}

/* ------------------------------------------------------------ jugador */
static int player_in_tile(int x, int y)
{
    return px + PLAYER_R > x && px - PLAYER_R < x + 1 && py + PLAYER_R > y && py - PLAYER_R < y + 1;
}

static int tile_solid(int x, int y)
{
    if (x < 0 || y < 0 || x >= MAPSIZE || y >= MAPSIZE) return 1;
    int t = tilemap[y][x];
    if (t == 64) return 1;
    if (t & 0x80) return doorposition(t & 0x7f) < 65535;
    if (t) return 1;
    objtype *o = actorat[y][x];
    if (o && (o->flags & FL_SHOOTABLE)) return 1;
    return 0;
}

static int box_free(double x, double y)
{
    int x0 = (int)floor(x - PLAYER_R), x1 = (int)floor(x + PLAYER_R);
    int y0 = (int)floor(y - PLAYER_R), y1 = (int)floor(y + PLAYER_R);
    for (int j = y0; j <= y1; j++)
        for (int i = x0; i <= x1; i++)
            if (tile_solid(i, j)) return 0;
    return 1;
}

static void clipmove(double dx, double dy)
{
    if (box_free(px + dx, py + dy)) { px += dx; py += dy; }
    else if (box_free(px + dx, py)) px += dx;
    else if (box_free(px, py + dy)) py += dy;
}

static void thrust(double angle, long speed)
{
    thrustspeed += speed;
    if (speed >= MINDIST * 2) speed = MINDIST * 2 - 1;
    double a = angle * M_PI / 180.0;
    clipmove(speed / 65536.0 * cos(a), -speed / 65536.0 * sin(a));
}

static void sync_player(void)
{
    player->x = (int32_t)(px * 65536.0);
    player->y = (int32_t)(py * 65536.0);
    player->tilex = (int)px;
    player->tiley = (int)py;
    player->angle = (int)pangle;
    if (player->tilex < 0 || player->tiley < 0 || player->tilex >= MAPSIZE || player->tiley >= MAPSIZE) return;
    int a = maparea[player->tiley][player->tilex] - AREATILE;
    if (a >= 0 && a < NUMAREAS && a != player->areanumber) {
        player->areanumber = a;
        ConnectAreas();
    }
}

static void HealSelf(int h) { gs.health += h; if (gs.health > 100) gs.health = 100; }
static void GiveAmmo(int a) { if (!gs.ammo && !gs.attackframe) gs.weapon = gs.chosenweapon; gs.ammo += a; if (gs.ammo > 99) gs.ammo = 99; }
static void GiveWeapon(int w)
{
    GiveAmmo(6);
    if (gs.bestweapon < w) gs.bestweapon = gs.weapon = gs.chosenweapon = w;
}

static void GetBonus(statobj_t *s)
{
    switch (s->itemnumber) {
    case bo_firstaid: if (gs.health == 100) return; SD_PlaySound(HEALTH2SND); HealSelf(25); break;
    case bo_key1: case bo_key2: case bo_key3: case bo_key4:
        gs.keys |= 1 << (s->itemnumber - bo_key1); SD_PlaySound(GETKEYSND); break;
    case bo_cross: SD_PlaySound(BONUS1SND); GivePoints(100); gs.treasurecount++; break;
    case bo_chalice: SD_PlaySound(BONUS2SND); GivePoints(500); gs.treasurecount++; break;
    case bo_bible: SD_PlaySound(BONUS3SND); GivePoints(1000); gs.treasurecount++; break;
    case bo_crown: SD_PlaySound(BONUS4SND); GivePoints(5000); gs.treasurecount++; break;
    case bo_clip: if (gs.ammo == 99) return; SD_PlaySound(GETAMMOSND); GiveAmmo(8); break;
    case bo_clip2: if (gs.ammo == 99) return; SD_PlaySound(GETAMMOSND); GiveAmmo(4); break;
    case bo_25clip: if (gs.ammo == 99) return; GiveAmmo(25); break;
    case bo_machinegun: SD_PlaySound(GETMACHINESND); GiveWeapon(wp_machinegun); break;
    case bo_chaingun: SD_PlaySound(GETGATLINGSND); GiveWeapon(wp_chaingun); break;
    case bo_fullheal: SD_PlaySound(BONUS1UPSND); HealSelf(99); GiveAmmo(25); gs.lives++; gs.treasurecount++; break;
    case bo_food: if (gs.health == 100) return; SD_PlaySound(HEALTH1SND); HealSelf(10); break;
    case bo_alpo: if (gs.health == 100) return; HealSelf(4); break;
    case bo_gibs: if (gs.health > 10) return; HealSelf(1); break;
    default: return;
    }
    s->flags &= ~FL_BONUS;
    s->shapenum = -1;
    bonusflash = 6;
}

static void pickups(void)
{
    for (int i = 0; i < nstats; i++) {
        statobj_t *s = &statlist[i];
        if (!(s->flags & FL_BONUS)) continue;
        if (s->tilex == player->tilex && s->tiley == player->tiley) GetBonus(s);
    }
}

/* ------------------------------------------------------------ muros secretos */
static void PushWall(int checkx, int checky, int dir)
{
    if (pwallstate) return;
    int oldtile = tilemap[checky][checkx];
    if (!oldtile || (oldtile & 0x80) || oldtile == 64) return;
    int nx = checkx, ny = checky;
    if (dir == north) ny--; else if (dir == east) nx++;
    else if (dir == south) ny++; else nx--;
    if (nx < 0 || ny < 0 || nx >= MAPSIZE || ny >= MAPSIZE) return;
    if (tilemap[ny][nx] || actorat[ny][nx]) { SD_PlaySound(NOWAYSND); return; }
    SD_PlaySound(PUSHWALLSND);
    gs.secretcount++;
    pwallx = checkx; pwally = checky; pwalldir = dir;
    pwalltile = oldtile;
    pwallstate = 1; pwallpos = 0;
    tilemap[ny][nx] = oldtile;
}

static void MovePWalls(void)
{
    if (!pwallstate) return;
    int oldblock = pwallstate / 128;
    pwallstate += tics;
    pwallpos = (pwallstate & 127) * 512;
    if (pwallstate / 128 != oldblock) {
        tilemap[pwally][pwallx] = 0;
        maparea[pwally][pwallx] = player->areanumber + AREATILE;
        if (pwallstate > 256) { pwallstate = 0; pwallpos = 0; return; }
        int nx = pwallx, ny = pwally;
        if (pwalldir == north) ny--; else if (pwalldir == east) nx++;
        else if (pwalldir == south) ny++; else nx--;
        pwallx = nx; pwally = ny;
        int nx2 = nx, ny2 = ny;
        if (pwalldir == north) ny2--; else if (pwalldir == east) nx2++;
        else if (pwalldir == south) ny2++; else nx2--;
        if (nx2 < 0 || ny2 < 0 || nx2 >= MAPSIZE || ny2 >= MAPSIZE ||
            tilemap[ny2][nx2] || actorat[ny2][nx2]) { pwallstate = 0; pwallpos = 0; return; }
        tilemap[ny2][nx2] = pwalltile;
    }
}

static void Cmd_Use(void)
{
    static const int dirs[4][2] = { {1,0}, {0,-1}, {-1,0}, {0,1} };
    int q = ((int)floor((pangle + 45) / 90)) & 3;
    int cx = player->tilex + dirs[q][0], cy = player->tiley + dirs[q][1];
    if (cx < 0 || cy < 0 || cx >= MAPSIZE || cy >= MAPSIZE) return;
    int t = tilemap[cy][cx];
    if (t & 0x80) {
        int d = t & 0x7f;
        if (doors[d].lock >= 1 && doors[d].lock <= 4 && !(gs.keys & (1 << (doors[d].lock - 1)))) {
            SD_PlaySound(NOWAYSND);
            printf("Necesitas la llave %s\n", doors[d].lock == 1 ? "dorada" : "plateada");
            return;
        }
        OpenDoor(d);
        return;
    }
    if (maparea[cy][cx] == ELEVATORTILE) {
        SD_PlaySound(LEVELDONESND);
        /* si el jugador esta parado en el elevador alterno, va al piso secreto */
        playstate = (maparea[player->tiley][player->tilex] == ALTELEVATORTILE) ? 3 : 2;
        return;
    }
    if (plane1[cy * MAPSIZE + cx] == 98) {
        int pd = (q == 0) ? east : (q == 1) ? north : (q == 2) ? west : south;
        PushWall(cx, cy, pd);
    }
}

/* ------------------------------------------------------------ armas */
static const struct { signed char tics, attack, frame; } attackinfo[4][4] = {
    { {6,0,1},{6,2,2},{6,0,3},{6,-1,4} },
    { {6,0,1},{6,1,2},{6,0,3},{6,-1,4} },
    { {6,0,1},{6,1,2},{6,3,3},{6,-1,4} },
    { {6,0,1},{6,1,2},{6,4,3},{6,-1,4} } };

static void KnifeAttack(void)
{
    SD_PlaySound(ATKKNIFESND);
    objtype *closest = NULL;
    double dist = 1e30;
    for (objtype *c = objhead; c; c = c->next)
        if (c != player && (c->flags & FL_SHOOTABLE) && (c->flags & FL_VISABLE)
            && abs(c->viewx - CENTERX) < SHOOTDELTA && c->depth < dist) {
            dist = c->depth; closest = c;
        }
    if (!closest || dist > 1.5) return;
    DamageActor(closest, US_RndT() >> 4);
}

static void GunAttack(void)
{
    objtype *closest = NULL, *old;
    double viewdist = 1e30;
    madenoise = 1;
    SD_PlaySound(gs.weapon == wp_pistol ? ATKPISTOLSND :
                 gs.weapon == wp_machinegun ? ATKMACHINEGUNSND : ATKGATLINGSND);
    for (;;) {
        old = closest;
        for (objtype *c = objhead; c; c = c->next)
            if (c != player && (c->flags & FL_SHOOTABLE) && (c->flags & FL_VISABLE)
                && abs(c->viewx - CENTERX) < SHOOTDELTA && c->depth < viewdist) {
                viewdist = c->depth; closest = c;
            }
        if (closest == old) return;
        if (CheckLine(closest)) break;
        viewdist = closest->depth;
    }
    int dx = abs(closest->tilex - player->tilex), dy = abs(closest->tiley - player->tiley);
    int dist = dx > dy ? dx : dy, damage;
    if (dist < 2) damage = US_RndT() / 4;
    else if (dist < 4) damage = US_RndT() / 6;
    else { if ((US_RndT() / 12) < dist) return; damage = US_RndT() / 6; }
    DamageActor(closest, damage);
}

static void T_Attack(void)
{
    gs.attackcount -= tics;
    int guard = 0;
    while (gs.attackcount <= 0 && guard++ < 16) {
        int af = gs.attackframe;
        if (af < 0) af = 0;
        if (af > 3) af = 3;
        int atk = attackinfo[gs.weapon][af].attack;
        switch (atk) {
        case -1:
            if (!gs.ammo) gs.weapon = wp_knife;
            else if (gs.weapon != gs.chosenweapon) gs.weapon = gs.chosenweapon;
            gs.attackframe = gs.weaponframe = 0;
            gs.attackcount = 0;
            return;
        case 4:
            if (!gs.ammo) break;
            if (attackbutton) gs.attackframe -= 2;
            GunAttack();
            gs.ammo--;
            break;
        case 1:
            if (!gs.ammo) { gs.attackframe++; break; }
            GunAttack();
            gs.ammo--;
            break;
        case 2:
            KnifeAttack();
            break;
        case 3:
            if (gs.ammo && attackbutton) gs.attackframe -= 2;
            GunAttack();
            if (gs.ammo) gs.ammo--;
            break;
        default: break;
        }
        gs.attackcount += attackinfo[gs.weapon][af].tics;
        gs.attackframe++;
        if (gs.attackframe < 0) gs.attackframe = 0;
        if (gs.attackframe > 3) { gs.attackframe = 0; gs.weaponframe = 0; gs.attackcount = 0; return; }
        gs.weaponframe = attackinfo[gs.weapon][gs.attackframe].frame;
    }
}

/* ------------------------------------------------------------ render */
typedef struct { double dist; int pic, u; } Hit;

static void cast(double dx, double dy, Hit *h)
{
    int mx = (int)px, my = (int)py;
    double ddx = dx == 0 ? 1e30 : fabs(1.0 / dx);
    double ddy = dy == 0 ? 1e30 : fabs(1.0 / dy);
    int stepx, stepy;
    double sdx, sdy;
    if (dx < 0) { stepx = -1; sdx = (px - mx) * ddx; } else { stepx = 1; sdx = (mx + 1 - px) * ddx; }
    if (dy < 0) { stepy = -1; sdy = (py - my) * ddy; } else { stepy = 1; sdy = (my + 1 - py) * ddy; }
    for (;;) {
        int pmx = mx, pmy = my, side;
        double dist;
        if (sdx < sdy) { dist = sdx; sdx += ddx; mx += stepx; side = 0; }
        else { dist = sdy; sdy += ddy; my += stepy; side = 1; }
        if (mx < 0 || my < 0 || mx >= MAPSIZE || my >= MAPSIZE) { h->dist = 1e9; h->pic = 0; h->u = 0; return; }
        int t = tilemap[my][mx];
        if (t && !(t & 0x80) && t != 64) {
            double wallx = side == 0 ? py + dist * dy : px + dist * dx;
            double fr = wallx - floor(wallx);
            if (side == 0) { if (stepx < 0) fr = 1 - fr; }
            else { if (stepy > 0) fr = 1 - fr; }
            h->dist = dist;
            h->u = (int)(fr * 64);
            if (h->u > 63) h->u = 63;
            if (h->u < 0) h->u = 0;
            if (tilemap[pmy][pmx] & 0x80) h->pic = DOORWALL + (side == 0 ? 3 : 2);
            else h->pic = (t - 1) * 2 + (side == 0 ? 1 : 0);
            return;
        }
        if (t & 0x80) {
            int di = t & 0x7f;
            Door *d = &doors[di];
            if (d->pos >= 65535) continue;
            double tt, hit, fr;
            if (d->vertical) {
                if (dx == 0) continue;
                tt = (mx + 0.5 - px) / dx;
                if (tt < 0) continue;
                hit = py + tt * dy;
                if ((int)floor(hit) != my) continue;
                fr = hit - my;
            } else {
                if (dy == 0) continue;
                tt = (my + 0.5 - py) / dy;
                if (tt < 0) continue;
                hit = px + tt * dx;
                if ((int)floor(hit) != mx) continue;
                fr = hit - mx;
            }
            double u = fr - d->pos / 65536.0;
            if (u < 0) continue;
            int page = d->lock == 0 ? DOORWALL : d->lock == 5 ? DOORWALL + 4 : DOORWALL + 6;
            h->dist = tt;
            h->pic = page + (d->vertical ? 1 : 0);
            h->u = (int)(u * 64);
            if (h->u > 63) h->u = 63;
            return;
        }
    }
}

static void draw_wall(int x, const Hit *h)
{
    const uint8_t *c = chunk(h->pic);
    if (!c) return;
    double dist = h->dist < 0.05 ? 0.05 : h->dist;
    double ht = PROJ / dist;
    double top = CENTERY - ht / 2;
    int y0 = (int)top, y1 = (int)(CENTERY + ht / 2);
    if (y0 < 0) y0 = 0;
    if (y1 > VIEWH) y1 = VIEWH;
    const uint8_t *col = c + h->u * 64;
    for (int y = y0; y < y1; y++) {
        int ty = (int)((y - top) * 64.0 / ht);
        if (ty < 0) ty = 0;
        if (ty > 63) ty = 63;
        fb[y * SCRW + x] = col[ty];
    }
}

/* los numeros de sprite de wolfdata.h son indices dentro de VSWAP */
static void draw_sprite(int shapenum, double cx, double depth, double size, int usez, double topy)
{
    const uint8_t *c = chunk(spritestart + shapenum);
    if (!c) return;
    const uint16_t *s = (const uint16_t *)c;
    int left = s[0], right = s[1];
    double sx0 = cx - size / 2;
    int x0 = (int)sx0, x1 = (int)(sx0 + size);
    if (x0 < 0) x0 = 0;
    if (x1 > SCRW) x1 = SCRW;
    for (int x = x0; x < x1; x++) {
        if (usez && zbuf[x] < depth) continue;
        int tc = (int)((x - sx0) * 64.0 / size);
        if (tc < left || tc > right) continue;
        const uint16_t *p = (const uint16_t *)(c + s[2 + tc - left]);
        while (p[0] != 0) {
            int end = p[0] >> 1, start = p[2] >> 1;
            int16_t ofs = (int16_t)p[1];
            for (int ty = start; ty < end; ty++) {
                uint8_t pix = c[ofs + ty];
                int ya = (int)(topy + ty * size / 64.0), yb = (int)(topy + (ty + 1) * size / 64.0);
                if (yb == ya) yb = ya + 1;
                for (int y = ya; y < yb; y++)
                    if (y >= 0 && y < VIEWH) fb[y * SCRW + x] = pix;
            }
            p += 3;
        }
    }
}

static int CalcRotate(objtype *ob)
{
    int viewangle = player->angle + (CENTERX - ob->viewx) / 8;
    int angle = (viewangle - 180) - dirangle[ob->dir];
    angle += 360 / 16;
    while (angle >= 360) angle -= 360;
    while (angle < 0) angle += 360;
    if (STATES[ob->state].rotate == 2) return 4 * (angle / 180);
    return angle / 45;
}

typedef struct { double depth, cx, size; int shape; } Vis;
static Vis vislist[MAXSTATS + MAXACTORS];
static int nvis;

static int cmp_vis(const void *a, const void *b)
{
    double da = ((const Vis *)a)->depth, db = ((const Vis *)b)->depth;
    return da < db ? 1 : da > db ? -1 : 0;
}

static void render(void)
{
    memset(fb, ceilcolor, SCRW * CENTERY);
    memset(fb + SCRW * CENTERY, 0x19, SCRW * (VIEWH - CENTERY));
    double a = pangle * M_PI / 180.0;
    double dirx = cos(a), diry = -sin(a);
    double rx = -diry, ry = dirx;

    for (int x = 0; x < SCRW; x++) {
        double t = ((x + 0.5) - 160.0) / 160.0 * HFOV_TAN;
        Hit h;
        cast(dirx + rx * t, diry + ry * t, &h);
        zbuf[x] = h.dist;
        draw_wall(x, &h);
    }

    nvis = 0;
    for (int i = 0; i < nstats; i++) {
        statobj_t *s = &statlist[i];
        if (s->shapenum < 0) continue;
        double dx = s->tilex + 0.5 - px, dy = s->tiley + 0.5 - py;
        double depth = dx * dirx + dy * diry;
        if (depth < 0.3) continue;
        double side = dx * rx + dy * ry;
        vislist[nvis].depth = depth;
        vislist[nvis].cx = 160 + side / depth * PROJ;
        vislist[nvis].size = PROJ / depth;
        vislist[nvis].shape = s->shapenum;
        nvis++;
    }
    for (objtype *o = objhead; o; o = o->next) {
        if (o == player || !o->active) continue;
        double ox = o->x / 65536.0, oy = o->y / 65536.0;
        double dx = ox - px, dy = oy - py;
        double depth = dx * dirx + dy * diry - 0.25;
        if (depth < 0.3) { o->flags &= ~FL_VISABLE; continue; }
        double side = dx * rx + dy * ry;
        int vx = (int)(160 + side / depth * PROJ);
        o->viewx = vx;
        o->depth = depth;
        int shape = STATES[o->state].shapenum;
        if (STATES[o->state].rotate) shape += CalcRotate(o);
        if (vx > -200 && vx < SCRW + 200) {
            o->flags |= FL_VISABLE;
            if (nvis < (int)(sizeof vislist / sizeof vislist[0])) {
                vislist[nvis].depth = depth;
                vislist[nvis].cx = vx;
                vislist[nvis].size = PROJ / depth;
                vislist[nvis].shape = shape;
                nvis++;
            }
        } else o->flags &= ~FL_VISABLE;
    }
    qsort(vislist, nvis, sizeof vislist[0], cmp_vis);
    for (int i = 0; i < nvis; i++)
        draw_sprite(vislist[i].shape, vislist[i].cx, vislist[i].depth, vislist[i].size, 1,
                    CENTERY - vislist[i].size / 2);

    if (gs.weapon >= 0) {
        double size = VIEWH + 1;
        draw_sprite(weaponscale[gs.weapon] + gs.weaponframe, 160, 0, size, 0, VIEWH + 1 - size);
    }

    if (damageflash > 0 || bonusflash > 0) {
        uint8_t col = damageflash > 0 ? 0x27 : 0x7f;
        int amt = damageflash > 0 ? damageflash : bonusflash;
        if (amt > 8) amt = 8;
        for (int i = 0; i < SCRW * VIEWH; i++)
            if ((i * 7 + i / SCRW) % 9 < amt) fb[i] = col;
    }
}

static void draw_hud(void)
{
    if (!vgagraph) { memset(fb + SCRW * VIEWH, 0, SCRW * (SCRH - VIEWH)); return; }
    drawpic(0, VIEWH, STATUSBARPIC);
    LatchNumber(2, 16, 2, mapon % 10 + 1);
    LatchNumber(6, 16, 6, gs.score);
    LatchNumber(14, 16, 1, gs.lives);
    LatchNumber(21, 16, 3, gs.health);
    LatchNumber(27, 16, 2, gs.ammo);
    if (gs.health)
        StatusDrawPic(17, 4, FACE1APIC + 3 * ((100 - gs.health) / 16) + gs.faceframe);
    else
        StatusDrawPic(17, 4, FACE8APIC);
    StatusDrawPic(30, 4, (gs.keys & 1) ? GOLDKEYPIC : NOKEYPIC);
    StatusDrawPic(30, 20, (gs.keys & 2) ? SILVERKEYPIC : NOKEYPIC);
    StatusDrawPic(32, 8, KNIFEPIC + gs.weapon);
}

static void UpdateFace(void)
{
    gs.facecount += tics;
    if (gs.facecount > US_RndT() / 4 + 10) {
        gs.faceframe = US_RndT() / 85;
        if (gs.faceframe > 2) gs.faceframe = 1;
        gs.facecount = 0;
    }
}

/* ------------------------------------------------------------ partida */
static void NewGameLevel(int n)
{
    if (!load_map(n)) { fprintf(stderr, "No pude cargar el piso %d\n", n); exit(1); }
    sync_player();
    SD_StartMusic(songs[mapon % 60]);
    printf("Piso %d: %s  (enemigos %d, tesoros %d, secretos %d)\n",
           mapon % 10 + 1, mapname, gs.killtotal, gs.treasuretotal, gs.secrettotal);
    gs.timecount = 0;
}

static void NewGame(void)
{
    memset(&gs, 0, sizeof gs);
    gs.health = 100;
    gs.ammo = 8;
    gs.lives = 3;
    gs.weapon = gs.chosenweapon = gs.bestweapon = wp_pistol;
}

/* decide el piso siguiente (WL_GAME.C) y conserva lo que lleva el jugador */
static int next_level(int secret)
{
    int ep = gs.episode, floor = mapon % 10;
    if (floor == 9) return ep * 10 + ElevatorBackTo[ep % 6];   /* vuelta del secreto */
    if (secret) return ep * 10 + 9;                            /* elevador alterno */
    return mapon + 1;
}

static void go_to_level(int next)
{
    int score = gs.score, lives = gs.lives, ammo = gs.ammo;
    int w = gs.weapon, bw = gs.bestweapon, cw = gs.chosenweapon, hp = gs.health;
    NewGameLevel(next);
    gs.score = score; gs.lives = lives; gs.ammo = ammo;
    gs.weapon = w; gs.bestweapon = bw; gs.chosenweapon = cw; gs.health = hp;
}

static void PlayerDied(void)
{
    gs.lives--;
    printf("Has muerto. Vidas: %d\n", gs.lives);
    if (gs.lives < 0) { printf("Game over. Puntaje final %d\n", gs.score); NewGame(); NewGameLevel(mapon); return; }
    int score = gs.score, lives = gs.lives, m = mapon;
    NewGameLevel(m);
    gs.score = score; gs.lives = lives;
    gs.health = 100; gs.ammo = 8;
    gs.weapon = gs.chosenweapon = gs.bestweapon = wp_pistol;
}

/* ------------------------------------------------------------ fuente */
#define STARTFONT 1

static uint8_t *fontbuf;
static int fontlen;

static int font_height(void) { return fontbuf ? (fontbuf[0] | (fontbuf[1] << 8)) : 0; }
static int font_loc(int c) { return fontbuf[2 + c * 2] | (fontbuf[3 + c * 2] << 8); }
static int font_width(int c) { return (int8_t)fontbuf[2 + 512 + c]; }

static void load_font(void)
{
    if (!vgagraph) return;
    fontbuf = grchunk(STARTFONT, &fontlen);
}

static int text_width(const char *s)
{
    if (!fontbuf) return strlen(s) * 8;
    int w = 0;
    for (; *s; s++) w += font_width((unsigned char)*s);
    return w;
}

static void draw_text(int x, int y, const char *s, uint8_t color)
{
    if (!fontbuf) return;
    int h = font_height();
    for (; *s; s++) {
        int c = (unsigned char)*s;
        int w = font_width(c), off = font_loc(c);
        if (w <= 0 || off <= 0 || off + w * h > fontlen) { x += w > 0 ? w : 4; continue; }
        for (int yy = 0; yy < h; yy++)
            for (int xx = 0; xx < w; xx++)
                if (fontbuf[off + yy * w + xx]) {
                    int sx = x + xx, sy = y + yy;
                    if (sx >= 0 && sx < SCRW && sy >= 0 && sy < SCRH) fb[sy * SCRW + sx] = color;
                }
        x += w;
    }
}

static void draw_text_center(int y, const char *s, uint8_t color)
{
    draw_text((SCRW - text_width(s)) / 2, y, s, color);
}

static void fill_box(int x, int y, int w, int h, uint8_t c)
{
    for (int j = y; j < y + h; j++) {
        if (j < 0 || j >= SCRH) continue;
        for (int i = x; i < x + w; i++)
            if (i >= 0 && i < SCRW) fb[j * SCRW + i] = c;
    }
}

/* ------------------------------------------------------------ guardado */
#define SAVEMAGIC 0x314C5733   /* "3WL1" */
#define NUMSAVES 6

typedef struct {
    uint32_t magic;
    char name[32];
    int mapon, difficulty;
    double px, py, pangle;
    int ceilcolor, ndoors, nstats, objcount;
    int pwallstate, pwallx, pwally, pwalldir, pwallpos, pwalltile;
    char mapname[17];
} savehdr;

static void savepath(int slot, char *out, size_t n)
{
    snprintf(out, n, "wolfsave%d.sav", slot);
}

static int save_game(int slot)
{
    char path[256];
    savepath(slot, path, sizeof path);
    FILE *f = fopen(path, "wb");
    if (!f) return 0;
    savehdr h;
    memset(&h, 0, sizeof h);
    h.magic = SAVEMAGIC;
    snprintf(h.name, sizeof h.name, "Piso %d", mapon % 10 + 1);
    h.mapon = mapon; h.difficulty = difficulty;
    h.px = px; h.py = py; h.pangle = pangle;
    h.ceilcolor = ceilcolor; h.ndoors = ndoors; h.nstats = nstats; h.objcount = objcount;
    h.pwallstate = pwallstate; h.pwallx = pwallx; h.pwally = pwally;
    h.pwalldir = pwalldir; h.pwallpos = pwallpos; h.pwalltile = pwalltile;
    memcpy(h.mapname, mapname, 17);
    fwrite(&h, sizeof h, 1, f);
    fwrite(&gs, sizeof gs, 1, f);
    fwrite(plane0, sizeof plane0, 1, f);
    fwrite(plane1, sizeof plane1, 1, f);
    fwrite(tilemap, sizeof tilemap, 1, f);
    fwrite(maparea, sizeof maparea, 1, f);
    fwrite(areaconnect, sizeof areaconnect, 1, f);
    fwrite(areabyplayer, sizeof areabyplayer, 1, f);
    fwrite(doors, sizeof doors, 1, f);
    fwrite(statlist, sizeof statlist, 1, f);
    fwrite(objlist, sizeof objlist, 1, f);
    /* punteros -> indices */
    for (int y = 0; y < MAPSIZE; y++)
        for (int x = 0; x < MAPSIZE; x++) {
            int idx = actorat[y][x] ? (int)(actorat[y][x] - objlist) : -1;
            fwrite(&idx, sizeof idx, 1, f);
        }
    for (int i = 0; i < MAXACTORS; i++) {
        int nx = objlist[i].next ? (int)(objlist[i].next - objlist) : -1;
        int pv = objlist[i].prev ? (int)(objlist[i].prev - objlist) : -1;
        fwrite(&nx, sizeof nx, 1, f);
        fwrite(&pv, sizeof pv, 1, f);
    }
    int hd = objhead ? (int)(objhead - objlist) : -1;
    int tl = objtail ? (int)(objtail - objlist) : -1;
    fwrite(&hd, sizeof hd, 1, f);
    fwrite(&tl, sizeof tl, 1, f);
    fclose(f);
    return 1;
}

static int save_info(int slot, char *name, size_t n)
{
    char path[256];
    savepath(slot, path, sizeof path);
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    savehdr h;
    int ok = fread(&h, sizeof h, 1, f) == 1 && h.magic == SAVEMAGIC;
    fclose(f);
    if (ok) snprintf(name, n, "%s", h.name);
    return ok;
}

static int load_game(int slot)
{
    char path[256];
    savepath(slot, path, sizeof path);
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    savehdr h;
    if (fread(&h, sizeof h, 1, f) != 1 || h.magic != SAVEMAGIC) { fclose(f); return 0; }
    mapon = h.mapon; difficulty = h.difficulty;
    px = h.px; py = h.py; pangle = h.pangle;
    ceilcolor = h.ceilcolor; ndoors = h.ndoors; nstats = h.nstats; objcount = h.objcount;
    pwallstate = h.pwallstate; pwallx = h.pwallx; pwally = h.pwally;
    pwalldir = h.pwalldir; pwallpos = h.pwallpos; pwalltile = h.pwalltile;
    memcpy(mapname, h.mapname, 17);
    if (fread(&gs, sizeof gs, 1, f) != 1) { fclose(f); return 0; }
    int rok = 1;
    rok &= fread(plane0, sizeof plane0, 1, f) == 1;
    rok &= fread(plane1, sizeof plane1, 1, f) == 1;
    rok &= fread(tilemap, sizeof tilemap, 1, f) == 1;
    rok &= fread(maparea, sizeof maparea, 1, f) == 1;
    rok &= fread(areaconnect, sizeof areaconnect, 1, f) == 1;
    rok &= fread(areabyplayer, sizeof areabyplayer, 1, f) == 1;
    rok &= fread(doors, sizeof doors, 1, f) == 1;
    rok &= fread(statlist, sizeof statlist, 1, f) == 1;
    rok &= fread(objlist, sizeof objlist, 1, f) == 1;
    for (int y = 0; y < MAPSIZE; y++)
        for (int x = 0; x < MAPSIZE; x++) {
            int idx = -1;
            rok &= fread(&idx, sizeof idx, 1, f) == 1;
            actorat[y][x] = (idx >= 0 && idx < MAXACTORS) ? &objlist[idx] : NULL;
        }
    for (int i = 0; i < MAXACTORS; i++) {
        int nx = -1, pv = -1;
        rok &= fread(&nx, sizeof nx, 1, f) == 1;
        rok &= fread(&pv, sizeof pv, 1, f) == 1;
        objlist[i].next = (nx >= 0 && nx < MAXACTORS) ? &objlist[nx] : NULL;
        objlist[i].prev = (pv >= 0 && pv < MAXACTORS) ? &objlist[pv] : NULL;
    }
    int hd = -1, tl = -1;
    rok &= fread(&hd, sizeof hd, 1, f) == 1;
    rok &= fread(&tl, sizeof tl, 1, f) == 1;
    if (!rok) { fclose(f); printf("Archivo de partida incompleto\n"); return 0; }
    objhead = (hd >= 0 && hd < MAXACTORS) ? &objlist[hd] : NULL;
    objtail = (tl >= 0 && tl < MAXACTORS) ? &objlist[tl] : NULL;
    fclose(f);
    player = &objlist[0];
    sync_player();
    SD_StartMusic(songs[mapon % 60]);
    printf("Partida cargada: piso %d (%s)\n", mapon % 10 + 1, mapname);
    return 1;
}

/* ------------------------------------------------------------ menu */
static const char *diffnames[4] = { "Puedo con esto", "No duele mucho", "Traiganlos", "Muerte encarnada" };

/* dibuja el menu y devuelve: 0 sigue abierto, 1 cerrar, 2 salir del juego */
typedef struct { const char *items[10]; int n, sel; const char *title; } Menu;

static void draw_menu(const Menu *m, const char *footer)
{
    int h = font_height() + 3;
    int boxh = m->n * h + 34;
    int boxy = (SCRH - boxh) / 2 - 10;
    fill_box(58, boxy - 2, 204, boxh + 4, 0x10);      /* marco claro */
    fill_box(60, boxy, 200, boxh, 0x1d);              /* fondo oscuro */
    draw_text_center(boxy + 6, m->title, 0x47);
    for (int i = 0; i < m->n; i++) {
        int y = boxy + 22 + i * h;
        if (i == m->sel) fill_box(66, y - 1, 188, h, 0x01);
        draw_text(78, y, m->items[i], i == m->sel ? 0x47 : 0x11);
    }
    if (footer) draw_text_center(boxy + boxh - 12, footer, 0x18);
}


/* ------------------------------------------------------------ fin de piso */
#define L_GUYPIC 43
#define L_COLONPIC 44
#define L_NUM0PIC 45
#define L_PERCENTPIC 55
#define L_GUY2PIC 84
#define L_BJWINSPIC 85
#define PAR_AMOUNT 500
#define PERCENT100AMT 10000

/* tiempos par en segundos (WL_INTER.C); 0 = jefe o piso secreto */
static const int partimes[60] = {
     90, 120, 120, 210, 180, 180, 150, 150, 0, 0,
     90, 210, 180, 120, 240, 360,  60, 180, 0, 0,
     90,  90, 150, 150, 210, 150, 120, 360, 0, 0,
    120, 120,  90,  60, 270, 210, 120, 270, 0, 0,
    150,  90, 150, 150, 240, 180, 270, 210, 0, 0,
    390, 240, 270, 360, 300, 330, 330, 510, 0, 0 };

/* dibuja un numero con los digitos grandes del juego */
static void draw_bignum(int x, int y, int value, int digits)
{
    char s[16];
    snprintf(s, sizeof s, "%0*d", digits, value);
    for (const char *p = s; *p; p++) {
        drawpic(x, y, L_NUM0PIC + (*p - '0'));
        x += 16;
    }
}

static void present_frame(SDL_Renderer *ren, SDL_Texture *tex, uint32_t *rgb)
{
    for (int i = 0; i < SCRW * SCRH; i++) rgb[i] = pal32[fb[i]];
    SDL_UpdateTexture(tex, NULL, rgb, SCRW * 4);
    SDL_RenderClear(ren);
    SDL_Rect dst = { 0, 0, 320, 240 };
    SDL_RenderCopy(ren, tex, NULL, &dst);
    SDL_RenderPresent(ren);
}

/* espera; devuelve 1 si el jugador pidio saltarse la animacion */
static int inter_wait(int ms, SDL_Renderer *ren, SDL_Texture *tex, uint32_t *rgb, int *quit)
{
    uint32_t end = SDL_GetTicks() + ms;
    for (;;) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) { *quit = 1; return 1; }
            if (e.type == SDL_KEYDOWN || e.type == SDL_MOUSEBUTTONDOWN) return 1;
        }
        if (SDL_GetTicks() >= end) return 0;
        SDL_Delay(5);
        (void)ren; (void)tex; (void)rgb;
    }
}

/* pantalla de estadisticas al terminar un piso (WL_INTER.C LevelCompleted) */
static void intermission(SDL_Renderer *ren, SDL_Texture *tex, uint32_t *rgb, int *quit)
{
    int lvl = gs.episode * 10 + mapon % 10;
    int par = partimes[lvl % 60];
    int sec = gs.timecount / 70;
    if (sec > 99 * 60) sec = 99 * 60;
    int timeleft = (par && sec < par) ? par - sec : 0;
    int kr = gs.killtotal ? gs.killcount * 100 / gs.killtotal : 0;
    int sr = gs.secrettotal ? gs.secretcount * 100 / gs.secrettotal : 0;
    int tr = gs.treasuretotal ? gs.treasurecount * 100 / gs.treasuretotal : 0;
    int bonus = 0, skip = 0;

    SD_StartMusic(ENDLEVEL_MUS);
    SDL_SetRelativeMouseMode(SDL_FALSE);

    /* dibuja el fondo fijo una vez por cuadro dentro del bucle */
    int breathe = 0;
    #define DRAWBG() do { \
        memset(fb, 0x7f, SCRW * SCRH); \
        drawpic(0, 16, breathe ? L_GUY2PIC : L_GUYPIC); \
        draw_text(14 * 8, 2 * 8, "PISO", 0x10); \
        draw_text(14 * 8, 4 * 8, "COMPLETADO", 0x10); \
        draw_bignum(26 * 8, 2 * 8, mapon % 10 + 1, 1); \
        draw_text(14 * 8, 7 * 8, "BONUS", 0x10); \
        draw_text(16 * 8, 10 * 8, "TIEMPO", 0x10); \
        draw_text(16 * 8, 12 * 8, "PAR", 0x10); \
        draw_text(9 * 8, 14 * 8, "PORCENTAJE DE MUERTES", 0x10); \
        draw_text(9 * 8, 16 * 8, "PORCENTAJE SECRETOS", 0x10); \
        draw_text(9 * 8, 18 * 8, "PORCENTAJE TESOROS", 0x10); \
        char tbuf[16]; \
        snprintf(tbuf, sizeof tbuf, "%02d:%02d", sec / 60, sec % 60); \
        draw_text(26 * 8, 10 * 8, tbuf, 0x10); \
        if (par) snprintf(tbuf, sizeof tbuf, "%02d:%02d", par / 60, par % 60); \
        else snprintf(tbuf, sizeof tbuf, "??:??"); \
        draw_text(26 * 8, 12 * 8, tbuf, 0x10); \
    } while (0)

    #define SHOWVALS(b, k, s_, t) do { \
        DRAWBG(); \
        char v[24]; \
        snprintf(v, sizeof v, "%d", (b)); draw_text(30 * 8 - text_width(v), 7 * 8, v, 0x10); \
        if ((k) >= 0) { snprintf(v, sizeof v, "%d%%", (k)); draw_text(35 * 8 - text_width(v), 14 * 8, v, 0x10); } \
        if ((s_) >= 0) { snprintf(v, sizeof v, "%d%%", (s_)); draw_text(35 * 8 - text_width(v), 16 * 8, v, 0x10); } \
        if ((t) >= 0) { snprintf(v, sizeof v, "%d%%", (t)); draw_text(35 * 8 - text_width(v), 18 * 8, v, 0x10); } \
        present_frame(ren, tex, rgb); \
    } while (0)

    SHOWVALS(0, -1, -1, -1);
    if (inter_wait(700, ren, tex, rgb, quit)) skip = 1;

    /* bonus por tiempo, contando hacia arriba */
    if (!skip && timeleft) {
        for (int i = 0; i <= timeleft; i++) {
            SHOWVALS(i * PAR_AMOUNT, -1, -1, -1);
            if (!(i % 10)) SD_PlaySound(ENDBONUS1SND);
            breathe = (i / 4) & 1;
            if (inter_wait(25, ren, tex, rgb, quit)) { skip = 1; break; }
        }
        SD_PlaySound(ENDBONUS2SND);
    }
    bonus = timeleft * PAR_AMOUNT;

    /* porcentajes, uno por uno */
    int shown[3] = { -1, -1, -1 };
    const int vals[3] = { kr, sr, tr };
    for (int r = 0; r < 3 && !skip; r++) {
        for (int i = 0; i <= vals[r]; i++) {
            shown[r] = i;
            SHOWVALS(bonus, shown[0], shown[1], shown[2]);
            if (!(i % 10)) SD_PlaySound(ENDBONUS1SND);
            breathe = (i / 4) & 1;
            if (inter_wait(12, ren, tex, rgb, quit)) { skip = 1; break; }
        }
        shown[r] = vals[r];
        if (vals[r] == 100) { bonus += PERCENT100AMT; SD_PlaySound(PERCENT100SND); }
        else SD_PlaySound(NOBONUSSND);
        if (!skip) inter_wait(300, ren, tex, rgb, quit);
    }

    if (skip) {
        bonus = timeleft * PAR_AMOUNT;
        if (kr == 100) bonus += PERCENT100AMT;
        if (sr == 100) bonus += PERCENT100AMT;
        if (tr == 100) bonus += PERCENT100AMT;
    }
    GivePoints(bonus);
    SHOWVALS(bonus, kr, sr, tr);
    draw_text_center(22 * 8, "Presiona una tecla", 0x47);
    present_frame(ren, tex, rgb);
    while (!inter_wait(50, ren, tex, rgb, quit) && !*quit) { }
    SDL_SetRelativeMouseMode(SDL_TRUE);
    #undef DRAWBG
    #undef SHOWVALS
}

/* pantalla de fin de episodio */
static void victory_screen(SDL_Renderer *ren, SDL_Texture *tex, uint32_t *rgb, int *quit)
{
    SD_StartMusic(URAHERO_MUS);
    SDL_SetRelativeMouseMode(SDL_FALSE);
    memset(fb, 0x7f, SCRW * SCRH);
    drawpic(8, 4, L_BJWINSPIC);
    draw_text_center(4 * 8, "FELICIDADES", 0x47);
    char v[48];
    snprintf(v, sizeof v, "Terminaste el episodio %d", gs.episode + 1);
    draw_text_center(22 * 8, v, 0x10);
    snprintf(v, sizeof v, "Puntaje final: %d", gs.score);
    draw_text_center(23 * 8 + 4, v, 0x10);
    present_frame(ren, tex, rgb);
    while (!inter_wait(50, ren, tex, rgb, quit) && !*quit) { }
    SDL_SetRelativeMouseMode(SDL_TRUE);
}

/* niveles del menu */
enum { MM_NONE, MM_MAIN, MM_EPISODE, MM_DIFF, MM_SAVE, MM_LOAD };
static int menu_level, menu_sel[6], menu_episode;

static void start_new_game(int episode, int diff)
{
    difficulty = diff;
    NewGame();
    NewGameLevel(episode * 10);
}

/* dibuja el menu actual sobre la pantalla */
static void menu_draw(void)
{
    Menu m;
    char buf[6][48];
    const char *footer = "Flechas, Enter, Esc";
    memset(&m, 0, sizeof m);
    switch (menu_level) {
    case MM_MAIN: {
        static char snd[32], mus[32];
        snprintf(snd, sizeof snd, "Efectos: %s", SD_SoundOn() ? "SI" : "NO");
        snprintf(mus, sizeof mus, "Musica: %s", SD_MusicOn() ? "SI" : "NO");
        m.title = "MENU";
        m.items[0] = "Continuar";
        m.items[1] = "Nueva partida";
        m.items[2] = "Guardar";
        m.items[3] = "Cargar";
        m.items[4] = snd;
        m.items[5] = mus;
        m.items[6] = "Salir";
        m.n = 7;
        break;
    }
    case MM_EPISODE:
        m.title = "EPISODIO";
        for (int i = 0; i < 6; i++) {
            snprintf(buf[i], sizeof buf[i], "Episodio %d", i + 1);
            m.items[i] = buf[i];
        }
        m.n = 6;
        break;
    case MM_DIFF:
        m.title = "DIFICULTAD";
        for (int i = 0; i < 4; i++) m.items[i] = diffnames[i];
        m.n = 4;
        break;
    case MM_SAVE:
    case MM_LOAD:
        m.title = menu_level == MM_SAVE ? "GUARDAR" : "CARGAR";
        for (int i = 0; i < NUMSAVES; i++) {
            char name[32];
            if (save_info(i, name, sizeof name))
                snprintf(buf[i], sizeof buf[i], "%d - %s", i + 1, name);
            else
                snprintf(buf[i], sizeof buf[i], "%d - vacia", i + 1);
            m.items[i] = buf[i];
        }
        m.n = NUMSAVES;
        break;
    default: return;
    }
    m.sel = menu_sel[menu_level];
    draw_menu(&m, footer);
}

/* devuelve 1 si hay que salir del juego */
static int menu_key(SDL_Keycode k)
{
    int n = menu_level == MM_MAIN ? 7 : menu_level == MM_EPISODE ? 6 :
            menu_level == MM_DIFF ? 4 : NUMSAVES;
    int *sel = &menu_sel[menu_level];
    if (k == SDLK_UP || k == SDLK_w) { *sel = (*sel + n - 1) % n; SD_PlaySound(MOVEGUN1SND); return 0; }
    if (k == SDLK_DOWN || k == SDLK_s) { *sel = (*sel + 1) % n; SD_PlaySound(MOVEGUN2SND); return 0; }
    if (k == SDLK_ESCAPE) {
        SD_PlaySound(ESCPRESSEDSND);
        if (menu_level == MM_MAIN) { menu_level = MM_NONE; SDL_SetRelativeMouseMode(SDL_TRUE); }
        else if (menu_level == MM_DIFF) menu_level = MM_EPISODE;
        else menu_level = MM_MAIN;
        return 0;
    }
    if (k != SDLK_RETURN && k != SDLK_KP_ENTER && k != SDLK_SPACE) return 0;
    SD_PlaySound(SHOOTSND);
    switch (menu_level) {
    case MM_MAIN:
        switch (*sel) {
        case 0: menu_level = MM_NONE; SDL_SetRelativeMouseMode(SDL_TRUE); break;
        case 1: menu_level = MM_EPISODE; break;
        case 2: menu_level = MM_SAVE; break;
        case 3: menu_level = MM_LOAD; break;
        case 4: SD_SetSound(!SD_SoundOn()); break;
        case 5: SD_SetMusic(!SD_MusicOn()); break;
        case 6: return 1;
        }
        break;
    case MM_EPISODE:
        menu_episode = *sel;
        menu_level = MM_DIFF;
        break;
    case MM_DIFF:
        start_new_game(menu_episode, *sel);
        menu_level = MM_NONE;
        SDL_SetRelativeMouseMode(SDL_TRUE);
        break;
    case MM_SAVE:
        if (save_game(*sel)) printf("Partida guardada en la ranura %d\n", *sel + 1);
        else SD_PlaySound(NOWAYSND);
        menu_level = MM_MAIN;
        break;
    case MM_LOAD:
        if (load_game(*sel)) { menu_level = MM_NONE; SDL_SetRelativeMouseMode(SDL_TRUE); }
        else SD_PlaySound(NOWAYSND);
        break;
    }
    return 0;
}

/* ------------------------------------------------------------ main */
static void save_ppm(const char *path)
{
    FILE *f = fopen(path, "wb");
    fprintf(f, "P6\n%d %d\n255\n", SCRW, SCRH);
    for (int i = 0; i < SCRW * SCRH; i++) {
        uint32_t c = pal32[fb[i]];
        fputc((c >> 16) & 255, f); fputc((c >> 8) & 255, f); fputc(c & 255, f);
    }
    fclose(f);
}

static void game_tick(void)
{
    gs.timecount += tics;
    doors_update();
    MovePWalls();
    for (objtype *o = objhead, *nx; o; o = nx) { nx = o->next; if (o != player) DoActor(o); }
    pickups();
    UpdateFace();
    if (damageflash > 0) damageflash -= tics;
    if (bonusflash > 0) bonusflash -= tics;
}

static int menushot;

int main(int argc, char **argv)
{
    int start_map = 0, headless = 0, frames = 0, bot = 0, nosound = 0;
    const char *shot = NULL;
    double at[3];
    int have_at = 0;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-m") && i + 1 < argc) start_map = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-s") && i + 1 < argc) difficulty = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--sens") && i + 1 < argc) mouse_sens = atof(argv[++i]);
        else if (!strcmp(argv[i], "--shot") && i + 1 < argc) shot = argv[++i];
        else if (!strcmp(argv[i], "--bot")) bot = 1;
        else if (!strcmp(argv[i], "--nosound")) nosound = 1;
        else if (!strcmp(argv[i], "--menushot")) { menushot = 1; headless = 1; if (!frames) frames = 1; }
        else if (!strcmp(argv[i], "--intershot")) { menushot = 2; headless = 1; if (!frames) frames = 1; }
        else if (!strcmp(argv[i], "--frames") && i + 1 < argc) { frames = atoi(argv[++i]); headless = 1; }
        else if (!strcmp(argv[i], "--at") && i + 3 < argc) {
            at[0] = atof(argv[i + 1]); at[1] = atof(argv[i + 2]); at[2] = atof(argv[i + 3]);
            have_at = 1; i += 3;
        } else datadir = argv[i];
    }
    if (difficulty < 0) difficulty = 0;
    if (difficulty > 3) difficulty = 3;
    load_data();
    load_font();
    NewGame();
    NewGameLevel(start_map);
    if (have_at) { px = at[0]; py = at[1]; pangle = at[2]; sync_player(); }
    printf("Wolfenstein 3D (.%s) - dificultad %d\n", ext, difficulty);

    if (headless) {
        for (int f = 0; f < frames; f++) {
            tics = 2;
            madenoise = (f % 20) == 0;
            sync_player();
            if (bot && f == 3) Cmd_Use();
            if (bot) {
                attackbutton = 1;
                if (!gs.attackframe) {
                    gs.attackframe = 1;
                    gs.attackcount = attackinfo[gs.weapon][0].tics;
                    gs.weaponframe = attackinfo[gs.weapon][0].frame;
                }
                if (gs.attackframe) T_Attack();
            }
            game_tick();
            render();
            draw_hud();
            if (playstate == 1) PlayerDied();
            else if (playstate >= 2) {
                printf("Piso terminado: enemigos %d/%d, tesoros %d/%d, secretos %d/%d\n",
                       gs.killcount, gs.killtotal, gs.treasurecount, gs.treasuretotal,
                       gs.secretcount, gs.secrettotal);
                go_to_level(next_level(playstate == 3));
            }
        }
        if (bot) {
            int hp = gs.health, sc = gs.score, mp = mapon;
            double ox = px, oy = py;
            if (!save_game(0)) printf("FALLO al guardar\n");
            gs.health = 7; gs.score = 12345; px = 1.5; py = 1.5;
            if (!load_game(0)) printf("FALLO al cargar\n");
            printf("guardado/carga: vida %d(%d) puntaje %d(%d) piso %d(%d) pos %.2f,%.2f(%.2f,%.2f)\n",
                   gs.health, hp, gs.score, sc, mapon, mp, px, py, ox, oy);
        }
        if (menushot == 2) {
            gs.timecount = 70 * 95; gs.killcount = 9; gs.killtotal = 11;
            gs.secretcount = 5; gs.secrettotal = 5; gs.treasurecount = 14; gs.treasuretotal = 23;
            int lvl = gs.episode * 10 + mapon % 10;
            int par = partimes[lvl % 60], sec = gs.timecount / 70;
            int kr = gs.killcount * 100 / gs.killtotal;
            int sr = 100, tr = gs.treasurecount * 100 / gs.treasuretotal;
            int bonus = (par && sec < par ? par - sec : 0) * PAR_AMOUNT + PERCENT100AMT;
            char v[24], tb[16];
            memset(fb, 0x7f, SCRW * SCRH);
            drawpic(0, 16, L_GUYPIC);
            draw_text(14 * 8, 2 * 8, "PISO", 0x10);
            draw_text(14 * 8, 4 * 8, "COMPLETADO", 0x10);
            draw_bignum(26 * 8, 2 * 8, mapon % 10 + 1, 1);
            draw_text(14 * 8, 7 * 8, "BONUS", 0x10);
            draw_text(16 * 8, 10 * 8, "TIEMPO", 0x10);
            draw_text(16 * 8, 12 * 8, "PAR", 0x10);
            draw_text(9 * 8, 14 * 8, "PORCENTAJE DE MUERTES", 0x10);
            draw_text(9 * 8, 16 * 8, "PORCENTAJE SECRETOS", 0x10);
            draw_text(9 * 8, 18 * 8, "PORCENTAJE TESOROS", 0x10);
            snprintf(tb, sizeof tb, "%02d:%02d", sec / 60, sec % 60);
            draw_text(26 * 8, 10 * 8, tb, 0x10);
            snprintf(tb, sizeof tb, "%02d:%02d", par / 60, par % 60);
            draw_text(26 * 8, 12 * 8, tb, 0x10);
            snprintf(v, sizeof v, "%d", bonus); draw_text(30 * 8 - text_width(v), 7 * 8, v, 0x10);
            snprintf(v, sizeof v, "%d%%", kr); draw_text(35 * 8 - text_width(v), 14 * 8, v, 0x10);
            snprintf(v, sizeof v, "%d%%", sr); draw_text(35 * 8 - text_width(v), 16 * 8, v, 0x10);
            snprintf(v, sizeof v, "%d%%", tr); draw_text(35 * 8 - text_width(v), 18 * 8, v, 0x10);
        } else if (menushot) { menu_level = MM_MAIN; render(); draw_hud(); menu_draw(); }
        if (shot) save_ppm(shot);
        printf("ok: objetos %d, vida %d, municion %d, muertes %d/%d, puntaje %d\n",
               objcount, gs.health, gs.ammo, gs.killcount, gs.killtotal, gs.score);
        return 0;
    }
    if (shot) { render(); draw_hud(); save_ppm(shot); return 0; }

    if (SDL_Init(SDL_INIT_VIDEO) < 0) { fprintf(stderr, "SDL: %s\n", SDL_GetError()); return 1; }
    if (!nosound && SD_Startup(datadir, ext)) SD_StartMusic(songs[mapon % 60]);
    else printf("Sin sonido (no encontre AUDIOHED/AUDIOT o no hay tarjeta)\n");
    SDL_Window *win = SDL_CreateWindow("Wolfenstein 3D", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                       960, 720, SDL_WINDOW_RESIZABLE);
    SDL_Renderer *ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_PRESENTVSYNC);
    if (!ren) ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_SOFTWARE);
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");
    SDL_RenderSetLogicalSize(ren, 320, 240);
    SDL_Texture *tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, SCRW, SCRH);
    static uint32_t rgb[SCRW * SCRH];
    SDL_SetRelativeMouseMode(SDL_TRUE);

    uint64_t last70 = SDL_GetTicks64() * 70 / 1000;
    int running = 1;
    while (running) {
        uint64_t now70 = SDL_GetTicks64() * 70 / 1000;
        int t = (int)(now70 - last70);
        if (t < 1) { SDL_Delay(1); continue; }
        last70 = now70;
        tics = t > 10 ? 10 : t;

        SDL_Event e;
        int usepressed = 0;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) running = 0;
            else if (e.type == SDL_MOUSEMOTION && !menu_level) pangle -= e.motion.xrel * mouse_sens;
            else if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT) attackbutton = 1;
            else if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT) attackbutton = 0;
            else if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_RIGHT) usepressed = 1;
            else if (e.type == SDL_KEYDOWN && !e.key.repeat) {
                SDL_Keycode k = e.key.keysym.sym;
                if (menu_level) { if (menu_key(k)) running = 0; continue; }
                if (k == SDLK_ESCAPE) {
                    menu_level = MM_MAIN;
                    SD_PlaySound(ESCPRESSEDSND);
                    SDL_SetRelativeMouseMode(SDL_FALSE);
                    attackbutton = 0;
                    continue;
                }
                else if (k == SDLK_TAB) SDL_SetRelativeMouseMode(SDL_GetRelativeMouseMode() ? SDL_FALSE : SDL_TRUE);
                else if (k == SDLK_SPACE || k == SDLK_e) usepressed = 1;
                else if (k >= SDLK_1 && k <= SDLK_4) {
                    int w = k - SDLK_1;
                    if (w <= gs.bestweapon && (w == wp_knife || gs.ammo)) {
                        gs.chosenweapon = gs.weapon = w;
                        SD_PlaySound(SELECTWPNSND);
                    } else SD_PlaySound(NOWAYSND);
                } else if (k == SDLK_PAGEUP) NewGameLevel(mapon + 1);
                else if (k == SDLK_PAGEDOWN && mapon > 0) NewGameLevel(mapon - 1);
            }
        }
        if (menu_level) {
            render();
            draw_hud();
            menu_draw();
            for (int i = 0; i < SCRW * SCRH; i++) rgb[i] = pal32[fb[i]];
            SDL_UpdateTexture(tex, NULL, rgb, SCRW * 4);
            SDL_RenderClear(ren);
            SDL_Rect dst0 = { 0, 0, 320, 240 };
            SDL_RenderCopy(ren, tex, NULL, &dst0);
            SDL_RenderPresent(ren);
            continue;
        }
        const uint8_t *ks = SDL_GetKeyboardState(NULL);
        if (ks[SDL_SCANCODE_LCTRL] || ks[SDL_SCANCODE_RCTRL]) attackbutton = 1;
        else if (!(SDL_GetMouseState(NULL, NULL) & SDL_BUTTON(SDL_BUTTON_LEFT))) attackbutton = 0;

        int run = ks[SDL_SCANCODE_LSHIFT] || ks[SDL_SCANCODE_RSHIFT];
        int mv = (run ? RUNMOVE : BASEMOVE) * tics;
        long controly = 0, side = 0, turn = 0;
        if (ks[SDL_SCANCODE_W] || ks[SDL_SCANCODE_UP]) controly -= mv;
        if (ks[SDL_SCANCODE_S] || ks[SDL_SCANCODE_DOWN]) controly += mv;
        if (ks[SDL_SCANCODE_D]) side += mv;
        if (ks[SDL_SCANCODE_A]) side -= mv;
        if (ks[SDL_SCANCODE_RIGHT]) turn += mv;
        if (ks[SDL_SCANCODE_LEFT]) turn -= mv;

        madenoise = 0;
        thrustspeed = 0;
        pangle -= (double)turn / ANGLESCALE;
        pangle = fmod(fmod(pangle, 360.0) + 360.0, 360.0);
        if (side > 0) thrust(pangle - 90, side * MOVESCALE);
        else if (side < 0) thrust(pangle + 90, -side * MOVESCALE);
        if (controly < 0) thrust(pangle, -controly * MOVESCALE);
        else if (controly > 0) thrust(pangle + 180, controly * BACKMOVESCALE);
        sync_player();

        if (usepressed) Cmd_Use();
        if (!gs.attackframe && attackbutton && playstate == 0) {
            gs.attackframe = 1;
            gs.attackcount = attackinfo[gs.weapon][0].tics;
            gs.weaponframe = attackinfo[gs.weapon][0].frame;
            madenoise = 1;
        }
        if (gs.attackframe) T_Attack();

        game_tick();
        render();
        draw_hud();
        for (int i = 0; i < SCRW * SCRH; i++) rgb[i] = pal32[fb[i]];
        SDL_UpdateTexture(tex, NULL, rgb, SCRW * 4);
        SDL_RenderClear(ren);
        SDL_Rect dst = { 0, 0, 320, 240 };
        SDL_RenderCopy(ren, tex, NULL, &dst);
        SDL_RenderPresent(ren);

        if (playstate == 1) PlayerDied();
        else if (playstate >= 2) {
            int secret = (playstate == 3);
            int boss = (mapon % 10 == 8);
            printf("Piso terminado: enemigos %d/%d, tesoros %d/%d, secretos %d/%d\n",
                   gs.killcount, gs.killtotal, gs.treasurecount, gs.treasuretotal,
                   gs.secretcount, gs.secrettotal);
            int quit = 0;
            intermission(ren, tex, rgb, &quit);
            if (quit) { running = 0; continue; }
            if (boss) {
                victory_screen(ren, tex, rgb, &quit);
                if (quit) { running = 0; continue; }
                int score = gs.score, lives = gs.lives;
                NewGame();
                gs.score = score; gs.lives = lives;
                NewGameLevel(gs.episode * 10);
            } else go_to_level(next_level(secret));
            last70 = SDL_GetTicks64() * 70 / 1000;
        }
    }
    SD_Shutdown();
    SDL_Quit();
    return 0;
}
