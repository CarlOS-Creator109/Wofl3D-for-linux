/* Indices de sonidos y musica de AUDIOWL6.H (codigo de id Software, GPLv2) */
#ifndef WOLFSND_H
#define WOLFSND_H

#define HITWALLSND 0
#define SELECTWPNSND 1
#define SELECTITEMSND 2
#define HEARTBEATSND 3
#define MOVEGUN2SND 4
#define MOVEGUN1SND 5
#define NOWAYSND 6
#define NAZIHITPLAYERSND 7
#define SCHABBSTHROWSND 8
#define PLAYERDEATHSND 9
#define DOGDEATHSND 10
#define ATKGATLINGSND 11
#define GETKEYSND 12
#define NOITEMSND 13
#define WALK1SND 14
#define WALK2SND 15
#define TAKEDAMAGESND 16
#define GAMEOVERSND 17
#define OPENDOORSND 18
#define CLOSEDOORSND 19
#define DONOTHINGSND 20
#define HALTSND 21
#define DEATHSCREAM2SND 22
#define ATKKNIFESND 23
#define ATKPISTOLSND 24
#define DEATHSCREAM3SND 25
#define ATKMACHINEGUNSND 26
#define HITENEMYSND 27
#define SHOOTDOORSND 28
#define DEATHSCREAM1SND 29
#define GETMACHINESND 30
#define GETAMMOSND 31
#define SHOOTSND 32
#define HEALTH1SND 33
#define HEALTH2SND 34
#define BONUS1SND 35
#define BONUS2SND 36
#define BONUS3SND 37
#define GETGATLINGSND 38
#define ESCPRESSEDSND 39
#define LEVELDONESND 40
#define DOGBARKSND 41
#define ENDBONUS1SND 42
#define ENDBONUS2SND 43
#define BONUS1UPSND 44
#define BONUS4SND 45
#define PUSHWALLSND 46
#define NOBONUSSND 47
#define PERCENT100SND 48
#define BOSSACTIVESND 49
#define MUTTISND 50
#define SCHUTZADSND 51
#define AHHHGSND 52
#define DIESND 53
#define EVASND 54
#define GUTENTAGSND 55
#define LEBENSND 56
#define SCHEISTSND 57
#define NAZIFIRESND 58
#define BOSSFIRESND 59
#define SSFIRESND 60
#define SLURPIESND 61
#define TOT_HUNDSND 62
#define MEINGOTTSND 63
#define SCHABBSHASND 64
#define HITLERHASND 65
#define SPIONSND 66
#define NEINSOVASSND 67
#define DOGATTACKSND 68
#define FLAMETHROWERSND 69
#define MECHSTEPSND 70
#define GOOBSSND 71
#define YEAHSND 72
#define DEATHSCREAM4SND 73
#define DEATHSCREAM5SND 74
#define DEATHSCREAM6SND 75
#define DEATHSCREAM7SND 76
#define DEATHSCREAM8SND 77
#define DEATHSCREAM9SND 78
#define DONNERSND 79
#define EINESND 80
#define ERLAUBENSND 81
#define KEINSND 82
#define MEINSND 83
#define ROSESND 84
#define MISSILEFIRESND 85
#define MISSILEHITSND 86

#define CORNER_MUS 0
#define DUNGEON_MUS 1
#define WARMARCH_MUS 2
#define GETTHEM_MUS 3
#define HEADACHE_MUS 4
#define HITLWLTZ_MUS 5
#define INTROCW3_MUS 6
#define NAZI_NOR_MUS 7
#define NAZI_OMI_MUS 8
#define POW_MUS 9
#define SALUTE_MUS 10
#define SEARCHN_MUS 11
#define SUSPENSE_MUS 12
#define VICTORS_MUS 13
#define WONDERIN_MUS 14
#define FUNKYOU_MUS 15
#define ENDLEVEL_MUS 16
#define GOINGAFT_MUS 17
#define PREGNANT_MUS 18
#define ULTIMATE_MUS 19
#define NAZI_RAP_MUS 20
#define ZEROHOUR_MUS 21
#define TWELFTH_MUS 22
#define ROSTER_MUS 23
#define URAHERO_MUS 24
#define VICMARCH_MUS 25
#define PACMAN_MUS 26

#define STARTADLIBSOUNDS 87
#define STARTMUSIC 261

int  SD_Startup(const char *dir, const char *ext);
void SD_Shutdown(void);
void SD_PlaySound(int sound);
void SD_StartMusic(int song);
void SD_StopMusic(void);
void SD_SetSound(int on);
void SD_SetMusic(int on);
int  SD_SoundOn(void);
int  SD_MusicOn(void);
int  SD_Offline(const char *dir, const char *ext);
void SD_Pump(int16_t *buf, int frames);
#endif
