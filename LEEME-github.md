# Como subirlo a GitHub

IMPORTANTE: no subas los archivos del juego (`*.WL6`, `WOLF3D.EXE`). Son de
id Software / ZeniMax y redistribuirlos es ilegal. El `.gitignore` ya los
excluye, pero revisa con `git status` antes del primer commit.

    cd ~/wolf3dlinux
    git init
    git add .gitignore COPYING README.md Makefile wolf.c sound.c sound.h wolfdata.h pal.h opl3.c opl3.h
    git status            # aqui NO deben aparecer los .WL6 ni WOLF3D.EXE
    git commit -m "Motor propio de Wolfenstein 3D para Linux (SDL2)"
    git branch -M main
    git remote add origin git@github.com:CarlOS-Creator109/Wofl3D-for-linux.git
    git push -u origin main

Si el push por SSH te pide clave y no la tienes configurada, usa HTTPS:

    git remote set-url origin https://github.com/CarlOS-Creator109/Wofl3D-for-linux.git
