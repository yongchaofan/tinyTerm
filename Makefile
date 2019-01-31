OBJS = obj/tiny.o obj/term.o obj/host.o obj/ssh2.o obj/ftpd.o obj/auto_drop.o obj/resource.o
LIBS = ${MINGW_PREFIX}/lib/libssh2.a ${MINGW_PREFIX}/lib/libz.a ${MINGW_PREFIX}/lib/binutils/libiberty.a 

CC = gcc
RC = windres
CFLAGS= -Os -std=c99 -D UNICODE -Wall -ffunction-sections -fdata-sections  -I${MINGW_PREFIX}/include
LDFLAGS = -s -lgdi32 -lcomdlg32 -lcomctl32 -lole32 -lwinmm -lws2_32 -lshell32 -lcrypt32 -lbcrypt -Wl,--subsystem,windows -Wl,-gc-sections

all: tinyTerm.exe

tinyTerm.exe: ${OBJS} 
	${CC} -o "$@" ${OBJS} ${LIBS} ${LDFLAGS}

obj/%.o: %.c tiny.h 
	${CC} ${CFLAGS} -c $< -o $@

obj/resource.o: res\tinyTerm.rc res\tiny.manifest res\TL1.ico 
	${RC} -I. -I.\res -i $< -o $@
	
clean:
	rm obj/*.o "tinyTerm.exe"