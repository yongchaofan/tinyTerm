//
// "$Id: tiny.h 2344 2018-09-15 21:05:10 $"
//
// tinyTerm -- A minimal serail/telnet/ssh/sftp terminal emulator
//
// tiny.h has function declarations for all .c files.
//
// Copyright 2015-2018 by Yongchao Fan.
//
// This library is free software distributed under GNU LGPL 3.0,
// see the license at:
//
//     https://github.com/zoudaokou/tinyTerm/blob/master/LICENSE
//
// Please report all bugs and problems on the following page:
//
//     https://github.com/zoudaokou/tinyTerm/issues/new
//
#include <stdio.h>
#define TERMLINES 40
#define TERMCOLS 100
#define MAXLINES 8190
#define BUFFERSIZE 8190*128

#define CONN_IDLE			0
#define CONN_CONNECTING 	1
#define CONN_AUTHENTICATING	2
#define CONN_CONNECTED		4

#define STDIO  	0
#define TELNET 	1
#define SERIAL 	2
#define SSH		4
#define SFTP	8

/****************auto_drop.c*************/
void drop_Init( HWND hwnd );
void drop_Destroy( HWND hwnd );
int autocomplete_Init( HWND hWnd );
int autocomplete_Add( LPCTSTR cmd );
int autocomplete_Destroy( void );
char *autocomplete_Prev( void );
char *autocomplete_Next( void );

/****************comm.c****************/
int comm_Init( void );
void comm_Destory( void );
void comm_Open( char *port );
void comm_Size( int w, int h );
void comm_Close( void );
void comm_Send( char *buf, int len );
int scp_cmd(char *cmd);
int tun_cmd(char *cmd);
int sftp_cmd(char *cmd);
int http_Svr( char *intf );

/****************ftpd.c****************/
char *getFolderName(char *title);
void ftp_Svr( char *root );
void tftp_Svr( char *root );

/****************term.c****************/
void buff_Load( char *fn );
void buff_Save( char *fn );
void buff_Logg( char *fn );
void buff_Srch( char *sstr );
void term_Clear( void );
void term_Size( void );
char *term_Init( void );
char *term_Exec( char *cmd );
char *term_Disp( char *buf );
char *term_Send( char *buf, int len );
void term_Parse( char *buf, int len );
int term_Recv( char *tl1text );
int term_TL1( char *cmd, char **tl1text);
void script_Open( char *fn );
void script_Pause( void );
void script_Stop( void );
void tiny_Drop_Script( char *cmds );

/****************tiny.c****************/
void cmd_Disp( char *buf );
void tiny_Title( char *buf );
void tiny_Redraw();
void tiny_Focus();
void tiny_Connecting();
