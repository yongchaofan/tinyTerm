//
// "$Id: tiny.h 3051 2018-10-10 21:05:10 $"
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
#include <sys/stat.h>
#define TERMLINES 40
#define TERMCOLS 100
#define MAXLINES 8192
#define BUFFERSIZE 8192*64

#define CONN_IDLE			0
#define CONN_CONNECTING 	1
#define CONN_AUTHENTICATING	2
#define CONN_CONNECTED		4

#define STDIO  	1
#define TELNET 	2
#define SERIAL 	3
#define SSH		4
#define SFTP	5
#define NETCONF	6

#define MODE_A	0
#define MODE_R	1
#define MODE_W	2
#define MODE_RB	3
#define MODE_WB	4
FILE *fopen_utf8(const char *fn, int mode);
int stat_utf8(const char *fn, struct _stat *buffer);
BOOL isUTF8c(char c);	//check if a byte is a UTF8 continuation byte

/****************auto_drop.c*************/
void drop_Init( HWND hwnd );
void drop_Destroy( HWND hwnd );
int autocomplete_Init( HWND hWnd );
int autocomplete_Add( LPCTSTR cmd );
int autocomplete_Destroy( );
char *autocomplete_Prev( );
char *autocomplete_Next( );

/****************host.c****************/
int host_Init( );
void host_Open( char *port );
void host_Size( int w, int h );
void host_Send( char *buf, int len );
int host_Type();
int host_Status();
void host_Close( );
void host_Destory( );
int http_Svr( char *intf );
int tcp( const char *hostname, short port );

/****************ssh2.c****************/
void ssh2_Init( void );
void ssh2_Size( int w, int h );
void ssh2_Send( char *buf, int len );
void netconf_Send( char *msg, int len );
void ssh2_Close( );
void ssh2_Exit( void );
void scp_pwd(char *pwd);
int scp_write_one(const char *lpath, const char *rpath);
int scp_cmd(char *cmd);
int tun_cmd(char *cmd);
int sftp_put(char *src, char *dst);

/****************ftpd.c****************/
char *getFolderName(char *title);
void ftp_Svr( char *root );
void tftp_Svr( char *root );

/****************term.c****************/
void buff_Logg( char *fn );
void buff_Srch( char *sstr );
void term_Init( void );
void term_Clear( void );
void term_Size( void );
void term_Parse( char *buf, int len );
void term_Parse_XML( char *xml, int len );
void term_Print( const char *fmt, ... );
char *term_Exec( char *cmd );
char *term_Disp( char *buf );
char *term_Send( char *buf, int len );
int term_Recv( char *tl1text );
int term_TL1( char *cmd, char **tl1text);
void script_Open( char *fn );
void script_Pause( void );
void script_Stop( void );
void tiny_Drop_Script( char *cmds );

/****************tiny.c****************/
void cmd_Disp( char *buf );
void tiny_Redraw();
void tiny_Beep();
void tiny_Connecting();
void tiny_Title( char *buf );
