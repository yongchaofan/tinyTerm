//
// "$Id: tiny.h 3295 2019-01-12 21:05:10 $"
//
// tinyTerm -- A minimal serail/telnet/ssh/sftp terminal emulator
//
// tiny.h has function declarations for all .c files.
//
// Copyright 2018-2019 by Yongchao Fan.
//
// This library is free software distributed under GNU LGPL 3.0,
// see the license at:
//
//     https://github.com/yongchaofan/tinyTerm/blob/master/LICENSE
//
// Please report all bugs and problems on the following page:
//
//     https://github.com/yongchaofan/tinyTerm/issues/new
//
#include <stdio.h>
#include <sys/stat.h>
#define TERMLINES	25
#define TERMCOLS	80
#define MAXLINES	8192
#define BUFFERSIZE	8192*64

#define HOST_IDLE			0
#define HOST_CONNECTING 	1
#define HOST_AUTHENTICATING	2
#define HOST_CONNECTED		4

#define STDIO  	1
#define SERIAL 	2
#define TELNET 	3
#define SSH		4
#define SFTP	5
#define NETCONF	6

BOOL isUTF8c(char c);	//check if a byte is a UTF8 continuation byte
FILE *fopen_utf8(const char *fn, const char *mode);
int stat_utf8(const char *fn, struct _stat *buffer);

/****************auto_drop.c*************/
void drop_Init( HWND hwnd, void (*handler)(char *) );
void drop_Destroy( HWND hwnd );
int autocomplete_Init( HWND hWnd );
int autocomplete_Destroy( );
int autocomplete_Add( WCHAR *cmd );
WCHAR *autocomplete_Prev( );
WCHAR *autocomplete_First( );
WCHAR *autocomplete_Next( );

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
void ssh2_Close( );
void ssh2_Exit( void );
void scp_pwd(char *pwd);
char *scp_read(char *lpath, char *rpath);
char *scp_write(char *lpath, char *rpath);
int scp_cmd(char *cmd, char **preply);
int tun_cmd(char *cmd, char **preply);
int sftp_put(char *src, char *dest);
void netconf_Send( char *msg, int len );

/****************ftpd.c****************/
BOOL ftp_Svr( char *root );
BOOL tftp_Svr( char *root );

/****************term.c****************/
void term_Init( void );
void term_Clear( void );
void term_Size( void );
void term_Parse( char *buf, int len );
void term_Parse_XML( char *xml, int len );
void term_Print( const char *fmt, ... );
void term_Scroll(int lines);
void term_Keydown(DWORD key);
void term_Prompt( char *cmd );
void term_Waitfor( char *cmd );
void term_Logg( char *fn );
void term_Srch( char *sstr );
void term_Disp( char *buf );
void term_Send( char *buf, int len );
int term_Recv( char **preply );	//get new text since last Disp/Send/Recv
int term_Cmd( char *cmd, char **preply );
int term_TL1( char *cmd, char **preply);
int term_Waitfor_Prompt();
char *term_Mark_Prompt();
BOOL term_Echo();

/****************tiny.c****************/
void cmd_Disp_utf8(char *buf);
int tiny_Cmd( char *cmd, char **preply );
void tiny_Beep();
void tiny_Redraw();
void tiny_Connecting();
void tiny_Title( char *buf );
BOOL tiny_Edit(BOOL e);			//return BOOL to indicate if status changed
