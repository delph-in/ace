DELPHIN_CFLAGS=-isystem /Users/sweaglesw/cdev/lingo/lkb/include
DELPHIN_LIBS=-L/Users/sweaglesw/cdev/lingo/lkb/lib/macos.ppc.32 -litsdb -lpvm3

CC=gcc
#CFLAGS=-Wall -O6 -fomit-frame-pointer -funsigned-char -DERG -mcpu=7400 -mdynamic-no-pic
#CFLAGS=-Wall -O6 -fomit-frame-pointer -funsigned-char -fno-schedule-insns -fno-schedule-insns2 -DERG -mcpu=7400 -mdynamic-no-pic
#CFLAGS=-O1 -funsigned-char -DERG
CFLAGS=-g -O6 -fomit-frame-pointer -funsigned-char -DERG -mcpu=7400 -mdynamic-no-pic -falign-loops=32 -funroll-loops
#CFLAGS=-g -pg -O2 -funsigned-char -DERG
#CFLAGS=-g -funsigned-char -DERG

CFLAGS+=${DELPHIN_CFLAGS}

OBJ=lexicon.o chart.o dag.o type.o tdl.o rule.o morpho.o roots.o freeze.o unify.o qc.o agenda.o net.o ppdfa.o transducer.o lrfilt.o glb.o semindex.o hash.o mrs.o mrsdg.o itsdb.o pack.o unpack.o maxent.o generate.o
all: ${OBJ}
	${CC} ${CFLAGS} main.c ${OBJ} -o parser ${DELPHIN_LIBS}

#dag.o: dag.c dag.h type.h
#	gcc ${CFLAGS_HACK} dag.c -c -o dag.o

clean:
	rm -f ${OBJ} parser
