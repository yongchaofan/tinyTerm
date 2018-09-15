# This Makefile will build the MinGW Win32 application.

HEADERS = tiny.h 
OBJS =	obj/tiny.o obj/term.o obj/comm.o obj/ftpd.o obj/auto_drop.o obj/resource.o
LIBS =  /mingw32/lib/libssh2.a /mingw32/lib/libz.a /mingw32/lib/binutils/libiberty.a
INCLUDE_DIRS = -I.

WARNS = -Wall

CC = gcc
RC = windres
CFLAGS= -Os -std=c99 -D _WIN32_IE=0x0500 -D WINVER=0x500 ${WARNS} 
#-ffunction-sections -fdata-sections -fno-exceptions
LDFLAGS = -s -lgdi32 -lcomdlg32 -lcomctl32 -lole32 -lws2_32 -lshell32 -lcrypt32 -lbcrypt -Wl,--subsystem,windows 
#-Wl,-gc-sections


all: tinyTerm.exe

tinyTerm.exe: ${OBJS} 
	${CC} -o "$@" ${OBJS} ${LIBS} ${LDFLAGS}

clean:
	rm obj/*.o "tinyTerm.exe"

obj/%.o: %.c ${HEADERS}
	${CC} ${CFLAGS} ${INCLUDE_DIRS} -c $< -o $@

obj/resource.o: res\tinyTerm.rc res\tinyTerm.manifest res\TL1.ico 
	${RC} -I. -I.\res -i $< -o $@

