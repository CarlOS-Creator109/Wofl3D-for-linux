# Wolf3D para Linux moderno

Motor propio en C + SDL2 que lee los archivos ORIGINALES del juego, con sonido
AdLib y musica. No incluye ningun dato del juego: necesitas los tuyos.

## Compilar y correr

    sudo pacman -S sdl2          # Arch     (Debian/Ubuntu: sudo apt install libsdl2-dev)
    make
    ./wolf /ruta/a/los/datos

Busca `MAPHEAD`, `GAMEMAPS`, `VSWAP`, `VGAGRAPH`, `VGAHEAD`, `VGADICT`,
`AUDIOHED` y `AUDIOT` con extension `.WL6` (completo), `.WL1` (shareware),
`.SOD` o `.SDM`, en mayusculas o minusculas.

Opciones:

    -m N        empezar en el piso N (0..59 en WL6, 0..9 en WL1)
    -s N        dificultad: 0 bebe, 1 facil, 2 normal, 3 muerte (por defecto 1)
    --sens 0.2  sensibilidad del raton (grados por cuenta, por defecto 0.12)
    --nosound   arrancar sin audio

## Version portable (AppImage)

Si quieres un solo archivo que corra en cualquier distro sin compilar nada:

    ./build-appimage.sh

Genera `Wolf3D-x86_64.AppImage` (unos 6 MB) que lleva dentro SDL2 y sus
dependencias. Ponlo junto a tus archivos del juego y dale doble clic, o:

    ./Wolf3D-x86_64.AppImage /ruta/a/los/datos

Si tu sistema no tiene FUSE, corre `./Wolf3D-x86_64.AppImage --appimage-extract-and-run`.

## Controles

Raton: girar · clic izquierdo o Ctrl: disparar · clic derecho, Espacio o E: usar
W / S: avanzar y retroceder · A / D: moverse de lado · Shift: correr
1-4: cuchillo, pistola, ametralladora, gatling · Tab: soltar o capturar el raton
Esc: menu (continuar, nueva partida, guardar, cargar, sonido, musica, salir)
PgUp / PgDn: cambiar de piso

Nota sobre el binario `wolf` incluido en el repositorio: esta compilado en Arch
Linux, asi que puede no arrancar en otras distros por las versiones de las
bibliotecas. Ahi usa `make` o el AppImage.

Las partidas se guardan como `wolfsave0.sav` .. `wolfsave5.sav` en la carpeta
desde donde corres el juego.

## Que hace

- Lee los mapas (descompresion Carmack + RLEW), texturas y sprites de VSWAP,
  graficos de VGAGRAPH (Huffman + planos VGA) y el audio de AUDIOT.
- Paleta VGA original, vista 320x200 con pixeles 6:5, FOV y proyeccion del original.
- Paredes, puertas deslizables (normales, con llave, elevador), muros empujables.
- Enemigos con la maquina de estados completa del original: guardias, perros, SS,
  oficiales, mutantes, Hans, Gretel, Schabbs, Fake Hitler, Mecha Hitler, Gift, Fat
  y los fantasmas. Patrullan, te ven, te persiguen, esquivan, disparan y mueren.
- Combate con las formulas del original: acierto segun distancia y si corres,
  dano por cercania, dano doble a enemigos desprevenidos.
- Cuchillo, pistola, ametralladora y gatling con sus cuadros y cadencias.
- Vida, municion, puntaje, vidas, llaves, tesoros, secretos y conteo de enemigos.
- Barra de estado original con la cara de BJ; cambio de piso por el elevador.
- Sonido AdLib (OPL2 emulado) con los efectos originales de AUDIOT: gritos de los
  guardias, ladridos, disparos, puertas, bonus, muertes; y la musica IMF de cada
  piso, con la tabla de canciones del original.
- Menu con la fuente del juego: continuar, nueva partida (episodio y dificultad),
  guardar y cargar en 6 ranuras, y apagar efectos o musica.
- Pantalla de estadisticas al terminar cada piso, como la original: tiempo contra
  el par del nivel, bonus que va subiendo con su sonido, y porcentajes de
  muertes, secretos y tesoros (10000 puntos por cada 100%). Al terminar el piso
  del jefe sale la pantalla de fin de episodio.
- El elevador secreto lleva al piso 10 y regresa al piso que le toca a cada
  episodio, con la tabla del original.

## Que falta

- La secuencia narrada del final del juego (la de Mecha Hitler) y la tabla de
  puntajes altos.
- Los sonidos digitalizados de VSWAP: el codigo liberado deja su tabla vacia, asi
  que no hay forma fiable de saber que sonido es cual. Se usa la version AdLib,
  que es la que suena en una tarjeta AdLib real.
- Proyectiles de los jefes: funcionan, pero con mi propia version simplificada.

## Licencia

El motor es propio, pero las tablas de `wolfdata.h` y `sound.h` y la logica de
juego vienen del codigo fuente que id Software libero bajo GPLv2, asi que este
programa se distribuye bajo GPLv2. `opl3.c` y `opl3.h` son Nuked OPL3 de
Nuke.YKT, bajo LGPL 2.1.
