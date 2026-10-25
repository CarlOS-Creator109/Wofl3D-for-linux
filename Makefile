CFLAGS  = -O2 -Wall $(shell sdl2-config --cflags)
LDLIBS  = $(shell sdl2-config --libs) -lm
OBJS    = wolf.o sound.o opl3.o

wolf: $(OBJS)
	$(CC) -o $@ $(OBJS) $(LDLIBS)

wolf.o: wolf.c pal.h wolfdata.h sound.h
sound.o: sound.c sound.h opl3.h
opl3.o: opl3.c opl3.h
	$(CC) -O2 -w $(shell sdl2-config --cflags) -c opl3.c -o opl3.o

clean:
	rm -f wolf $(OBJS)
