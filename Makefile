#DELPHIN_CFLAGS=-isystem /Users/sweaglesw/cdev/lingo/lkb/include
#DELPHIN_LIBS=-L/Users/sweaglesw/cdev/lingo/lkb/lib/macos.ppc.32 -litsdb -lpvm3
POST_CFLAGS=-I../post/ -DPOST
POST_LIBS=../post/post.o -lreadline

#GRAM=-DJACY
GRAM=-DERG -DUNKNOWN=\"n_proper_le\" -DSLEX

CPU=
CC=gcc
CFLAGS=-g -O6 -fomit-frame-pointer -funsigned-char ${GRAM} -falign-loops=32 -funroll-loops ${CPU}
#CFLAGS=-g -pg -O2 -funsigned-char ${GRAM}
#CFLAGS=-g -funsigned-char ${GRAM}

CFLAGS+=${DELPHIN_CFLAGS} ${POST_CFLAGS}

OBJ=lexicon.o chart.o dag.o type.o tdl.o rule.o morpho.o roots.o freeze.o unify.o qc.o agenda.o net.o transducer.o glb.o semindex.o hash.o mrs.o mrsvpm.o mrsdg.o itsdb.o pack.o unpack.o maxent.o generate.o parse.o lui.o conf.o fsppgen.o
all: ${OBJ}
	${CC} ${CFLAGS} ${POST_LIBS} main.c ${OBJ} -o ace ${DELPHIN_LIBS} -lpthread

clean:
	rm -f ${OBJ} ace
