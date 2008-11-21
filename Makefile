#DELPHIN_CFLAGS=-isystem /Users/sweaglesw/cdev/lingo/lkb/include
#DELPHIN_LIBS=-L/Users/sweaglesw/cdev/lingo/lkb/lib/macos.ppc.32 -litsdb -lpvm3
POST_CFLAGS=-I../post/ -DPOST
POST_LIBS=../post/post.o

#GRAM=-DJACY
GRAM=-DERG -DUNKNOWN=\"n_proper_le\" #-DSLEX

#CPU=-march=k8
CC=gcc
CFLAGS=-g -O6 -fomit-frame-pointer -funsigned-char ${GRAM} -falign-loops=32 -funroll-loops ${CPU}
#CFLAGS=-g -O6 -fomit-frame-pointer -funsigned-char ${GRAM} -falign-loops=32 -funroll-loops ${CPU} -fbranch-probabilities
#CFLAGS=-g -O6 -fomit-frame-pointer -funsigned-char ${GRAM} -falign-loops=32 -funroll-loops ${CPU} -fprofile-arcs
#CFLAGS=-g -O2 -funsigned-char ${GRAM}
#CFLAGS=-g -pg -funsigned-char ${GRAM}

CFLAGS+=${DELPHIN_CFLAGS} ${POST_CFLAGS}
CFLAGS+=-m32 -L/lib32

OBJ=lexicon.o chart.o dag.o type.o tdl.o rule.o morpho.o roots.o freeze.o unify.o qc.o agenda.o net.o glb.o semindex.o hash.o mrs.o mrsvpm.o mrsdg.o itsdb.o pack.o unpack.o maxent.o generate.o parse.o lui.o conf.o preprocessor.o

all: ${OBJ}
	${CC} ${CFLAGS} ${POST_LIBS} main.c ${OBJ} -o ace ${DELPHIN_LIBS} -lpthread -lm

clean:
	rm -f ${OBJ} ace *.gcda *.gcno

libace.o: ${OBJ}
	ld -r -o libace.o conf.o dag.o freeze.o glb.o hash.o lexicon.o morpho.o mrsdg.o mrs.o mrsvpm.o roots.o rule.o tdl.o type.o unify.o qc.o maxent.o semindex.o agenda.o preprocessor.o pack.o
