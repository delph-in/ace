PREFIX=/usr/local



DELPHIN_CFLAGS=-isystem ~/logon/lingo/lkb/include -DTSDB
DELPHIN_LIBS=-L ~/logon/lingo/lkb/lib/linux.x86.64 -litsdb -lpvm3
POST_CFLAGS=-I../post/ -DPOST
POST_LIBS=../post/post.o -la

REPP_LIBS=-lrepp
#REPP_CFLAGS=-I ${REPP_DIR}/include/

#GRAM=-DJACY
GRAM=-DERG # -DUNKNOWN=\"n_proper_le\"

#CPU=-m32
CC=gcc
CFLAGS=-g -O6 -fomit-frame-pointer -funsigned-char ${GRAM} -falign-loops=32 -funroll-loops ${CPU}
#CFLAGS=-g -O2 -funsigned-char ${GRAM}
#CFLAGS=-g -O2 -pg -funsigned-char ${GRAM}
#CFLAGS=-g -funsigned-char ${GRAM}
#CFLAGS+=-fnested-functions
#CFLAGS+=-fPIC

CFLAGS+=${DELPHIN_CFLAGS} ${POST_CFLAGS} ${REPP_CFLAGS}

OBJ=lexicon.o chart.o dag.o type.o tdl.o rule.o morpho.o roots.o freeze.o unify.o qc.o agenda.o net.o glb.o semindex.o hash.o mrs.o mrsvpm.o mrsdg.o itsdb.o pack.o unpack.o maxent.o generate.o parse.o lui.o conf.o preprocessor.o treebank-control.o token.o lattice-mapping.o lexical-parse.o generalize.o unicode.o transfer.o edge-vectors.o forest-out.o exunpack.o semilattice.o rebuild-th.o

PICOBJS=$(patsubst %,pic/%,${OBJ} libace.o)

ace: ${OBJ} main.c
	${CC} ${CFLAGS} main.c ${OBJ} -o ace ${POST_LIBS} ${REPP_LIBS} ${DELPHIN_LIBS} -lpthread -lm -lboost_regex -lstdc++

static: ${OBJ} main.c
	${CC} ${CFLAGS} main.c ${OBJ} -static -o ace.static ${POST_LIBS} ${REPP_LIBS} ${DELPHIN_LIBS} -lpthread -lm -L ~/local/lib/ -lboost_regex -lstdc++

${PICOBJS} : pic/%.o: %.c
	${CC} ${CFLAGS} -fPIC -c $< -o $@

libace.so: pic/libace.o ${PICOBJS}
	rm -f libace.so
	gcc -shared ${PICOBJS} ../post/post.o -Wl,-soname,libace.so -o libace.so -lrepp ${DELPHIN_LIBS}

libace.a: libace.o ${OBJ}
	rm -f libace.a
	ar cru libace.a ${OBJ} ../post/post.o libace.o
	ranlib libace.a

install: ace libace.so
	cp ace ${PREFIX}/bin/
	cp libace.so ${PREFIX}/lib/
	mkdir -p ${PREFIX}/include/ace
	cp *.h ${PREFIX}/include/ace/

clean:
	rm -f ${OBJ} ace ace.static
	rm -f libace.a libace.so
	rm -f ${PICOBJS}
