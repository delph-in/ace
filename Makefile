CC=gcc
CFLAGS=-g -O6 -fomit-frame-pointer -funsigned-char -fschedule-insns -DERG
#CFLAGS=-g -pg -O2 -funsigned-char -DERG
#CFLAGS=-g -funsigned-char -DERG

OBJ=lexicon.o chart.o dag.o type.o tdl.o rule.o morpho.o roots.o freeze.o unify.o
all: ${OBJ}
	gcc ${CFLAGS} main.c ${OBJ} -o parser

clean:
	rm -f ${OBJ} parser
