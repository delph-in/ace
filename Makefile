#DELPHIN_CFLAGS=-isystem /home/sweaglesw/logon/lingo/lkb/include -DTSDB
#DELPHIN_LIBS=-L/home/sweaglesw/logon/lingo/lkb/lib/linux.x86.64 -litsdb -lpvm3
POST_CFLAGS=-I../post/ -DPOST
POST_LIBS=../post/post.o

#GRAM=-DJACY
GRAM=-DERG # -DUNKNOWN=\"n_proper_le\"

#CPU=-m32
CC=gcc
CFLAGS=-g -O6 -fomit-frame-pointer -funsigned-char ${GRAM} -falign-loops=32 -funroll-loops ${CPU}
#CFLAGS=-g -O2 -funsigned-char ${GRAM}
#CFLAGS=-g -funsigned-char ${GRAM}
#CFLAGS+=-fnested-functions

CFLAGS+=${DELPHIN_CFLAGS} ${POST_CFLAGS}

OBJ=lexicon.o chart.o dag.o type.o tdl.o rule.o morpho.o roots.o freeze.o unify.o qc.o agenda.o net.o glb.o semindex.o hash.o mrs.o mrsvpm.o mrsdg.o itsdb.o pack.o unpack.o maxent.o generate.o parse.o lui.o conf.o preprocessor.o treebank-control.o token.o lattice-mapping.o lexical-parse.o generalize.o
all: ${OBJ}
	${CC} ${CFLAGS} ${POST_LIBS} main.c ${OBJ} -o ace ${DELPHIN_LIBS} -lpthread -lm -lrepp -lboost_regex

clean:
	rm -f ${OBJ} ace
