PREFIX=/usr/local

# comment out these two lines if you don't want to include [incr tsdb()] support
DELPHIN_CFLAGS=-isystem ${LOGONROOT}/lingo/lkb/include -DTSDB
DELPHIN_LIBS=-L ${LOGONROOT}/lingo/lkb/lib/linux.x86.64 -Wl,-Bstatic -litsdb -lpvm3 -Wl,-Bdynamic

POST_CFLAGS=-I post/ -DPOST
POST_LIBS=-Wl,-Bstatic -lutil -Wl,-Bdynamic	# for openpty for calling out to tnt (which uses fully buffered stdio)

REPP_LIBS=-Wl,-Bstatic -lrepp -Wl,-Bdynamic
#REPP_CFLAGS=-I ${REPP_DIR}/include/

BOOST_REGEX_LIBS=-Wl,-Bstatic -lboost_regex -lstdc++ -Wl,-Bdynamic

#CPU=-m32
CC=gcc
#CFLAGS=-g -O6 -fomit-frame-pointer -funsigned-char -falign-loops=32 -funroll-loops ${CPU} -fprofile-generate=./profile-data/
#REPP_LIBS+=-lgcov
#CFLAGS=-g -O6 -fomit-frame-pointer -funsigned-char -falign-loops=32 -funroll-loops ${CPU} -fprofile-use=./profile-data/

CFLAGS=-g -O6 -fomit-frame-pointer -funsigned-char -falign-loops=32 -funroll-loops ${CPU}
#CFLAGS=-g -O6 -funsigned-char -falign-loops=32 -funroll-loops ${CPU}  -pg
#CFLAGS=-g -O2 -funsigned-char
#CFLAGS=-g -O2 -pg -funsigned-char
#CFLAGS=-g -funsigned-char
#CFLAGS+=-fPIC

EXPORT_DYNAMIC_CFLAG=-Wl,--export-dynamic

#include MacOSX.config

CFLAGS+=${DELPHIN_CFLAGS} ${POST_CFLAGS} ${REPP_CFLAGS}

OBJ=lexicon.o chart.o dag.o type.o tdl.o rule.o morpho.o roots.o freeze.o unify.o qc.o agenda.o net.o glb.o semindex.o hash.o mrs.o mrsvpm.o mrsdg.o itsdb.o pack.o unpack.o maxent.o generate.o parse.o lui.o conf.o preprocessor.o treebank-control.o token.o lattice-mapping.o lexical-parse.o generalize.o transfer.o edge-vectors.o forest-out.o exunpack.o semilattice.o rebuild-th.o compile-qc.o idiom.o yy.o lisp.o tnt.o timer.o

PICOBJS=$(patsubst %,pic/%,${OBJ} libace.o)
APPOBJ=${OBJ} main.o post/post.o

ace: ${APPOBJ}
	${CC} ${EXPORT_DYNAMIC_CFLAG} ${CFLAGS} ${APPOBJ} -o ace ${POST_LIBS} ${REPP_LIBS} ${DELPHIN_LIBS} -lpthread -lm ${BOOST_REGEX_LIBS} -ldl

static: ${APPOBJ}
	${CC} -Wl,--dynamic-list=dylist.txt ${CFLAGS} ${APPOBJ} -static ${POST_LIBS} ${REPP_LIBS} ${DELPHIN_LIBS} -lpthread -lm -L ~/local/lib/ -lboost_regex -lstdc++ -la -ldl -o ace.static

post/post.o:
	make -C post

${PICOBJS} : pic/%.o: %.c
	${CC} ${CFLAGS} -fPIC -c $< -o $@

libace.so: pic/libace.o ${PICOBJS} post/post.o
	rm -f libace.so
	gcc -shared ${PICOBJS} post/post.o -Wl,-soname,libace.so -o libace.so -lrepp ${DELPHIN_LIBS} -ldl -lutil
# note: -lutil cannot be compiled statically into a shared library, because the bozos that built it didn't use -fPIC...

libace.a: libace.o ${OBJ}
	rm -f libace.a
	ar cru libace.a ${OBJ} post/post.o libace.o
	ranlib libace.a

install: ace libace.so
	cp ace ${PREFIX}/bin/
	cp libace.so ${PREFIX}/lib/
	cp libace.a ${PREFIX}/lib/
	mkdir -p ${PREFIX}/include/ace
	cp *.h ${PREFIX}/include/ace/

clean:
	rm -f ${OBJ} ${APPOBJ} ace ace.static
	rm -f libace.a libace.so
	rm -f ${PICOBJS}
	make -C post clean
