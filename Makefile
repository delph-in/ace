DELPHIN_CFLAGS=-isystem /Users/sweaglesw/cdev/lingo/lkb/include
DELPHIN_LIBS=-L/Users/sweaglesw/cdev/lingo/lkb/lib/macos.ppc.32 -litsdb -lpvm3

#GRAM=-DJACY
GRAM=-DERG

CC=gcc
#CFLAGS=-Wall -O6 -fomit-frame-pointer -funsigned-char ${GRAM} -mcpu=7400 -mdynamic-no-pic
#CFLAGS=-Wall -O6 -fomit-frame-pointer -funsigned-char -fno-schedule-insns -fno-schedule-insns2 ${GRAM} -mcpu=7400 -mdynamic-no-pic
#CFLAGS=-O1 -funsigned-char ${GRAM}
CFLAGS=-g -O6 -fomit-frame-pointer -funsigned-char ${GRAM} -falign-loops=32 -funroll-loops
#CFLAGS=-g -pg -O2 -funsigned-char ${GRAM}
#CFLAGS=-g -funsigned-char ${GRAM}

CFLAGS+=${DELPHIN_CFLAGS}

OBJ=lexicon.o chart.o dag.o type.o tdl.o rule.o morpho.o roots.o freeze.o unify.o qc.o agenda.o net.o ppdfa.o transducer.o glb.o semindex.o hash.o mrs.o mrsvpm.o mrsdg.o itsdb.o pack.o unpack.o maxent.o generate.o parse.o lui.o conf.o
all: ${OBJ}
	${CC} ${CFLAGS} main.c ${OBJ} -o ace ${DELPHIN_LIBS}

#dag.o: dag.c dag.h type.h
#	gcc ${CFLAGS_HACK} dag.c -c -o dag.o

clean:
	rm -f ${OBJ} ace
