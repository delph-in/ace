CC=gcc
CFLAGS=-Wall -O6 -fomit-frame-pointer -funsigned-char -DERG
#CFLAGS=-O6 -fomit-frame-pointer -funsigned-char -fschedule-insns -DERG -march=pentium3 -mcpu=pentium3 -g
#CFLAGS=-g -pg -O2 -funsigned-char -DERG
#CFLAGS=-g -funsigned-char -DERG

OBJ=lexicon.o chart.o dag.o type.o tdl.o rule.o morpho.o roots.o freeze.o unify.o qc.o agenda.o net.o ppdfa.o transducer.o lrfilt.o
all: ${OBJ}
	gcc ${CFLAGS} main.c ${OBJ} -o parser

clean:
	rm -f ${OBJ} parser
