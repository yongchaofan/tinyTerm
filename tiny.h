//
// "$Id: tiny.h 5703 2020-08-31 12:05:10 $"
//
// tinyTerm -- A minimal serail/telnet/ssh/sftp terminal emulator
//
// tiny.h has function declarations for all .c files.
//
// Copyright 2018-2020 by Yongchao Fan.
//
// This library is free software distributed under GNU GPL 3.0,
// see the license at:
//
//		https://github.com/yongchaofan/tinyTerm/blob/master/LICENSE
//
// Please report all bugs and problems on the following page:
//
//		https://github.com/yongchaofan/tinyTerm/issues/new
//
#include <stdio.h>
#include <sys/stat.h>
#include <libssh2.h>
#include <libssh2_sftp.h>

#define MAXLINES 16384
#define BUFFERSIZE 16384*64

struct Tunnel
{
	int socket;
	char *localip;
	char *remoteip;
	unsigned short localport;
	unsigned short remoteport;
	LIBSSH2_CHANNEL *channel;
	struct Tunnel *next;
	struct tagHOST *host;
};

typedef struct tagHOST {
	int type;
	int status;

	char cmdline[256];
	char tunline[256];
	SOCKET sock;					//for tcp/ssh/sftp reader
	short port;
	HANDLE hExitEvent, hSerial;		//for serial reader
	HANDLE hStdioRead, hStdioWrite;	//for stdio reader

	char *subsystem;
	char *username;
	char *password;
	char *passphrase;
	char *hostname;
	char homedir[MAX_PATH];
	char keys[256];
	int cursor;
	BOOL bReturn, bPassword, bGets;

	HANDLE mtx;						//ssh2 reading/writing mutex
	LIBSSH2_SESSION *session;
	LIBSSH2_CHANNEL *channel;
	HANDLE mtx_tun;					//tunnel list add/delete mutex
	struct Tunnel *tunnel_list;

	LIBSSH2_SFTP *sftp;				//sftp host
	char homepath[MAX_PATH];
	char realpath[MAX_PATH];
	int sftp_running;

	struct tagTERM *term;
} HOST;

typedef struct tagTERM {
	char *buff, *attr, c_attr, save_attr;
	int *line;
	int size_x, size_y;
	int cursor_x, cursor_y;
	int screen_y;
	int sel_left, sel_right;
	BOOL bLogging, bEcho, bCursor, bAlterScreen;
	BOOL bAppCursor, bGraphic, bEscape, bTitle, bInsert;
	BOOL bBracket, bOriginMode, bWraparound;//bracketed paste mode
	int save_x, save_y;
	int roll_top, roll_bot;
	HANDLE mtx;						//term parse mutex

	char title[64];
	int title_idx;
	FILE *fpLogFile;

	BOOL bPrompt;
	char sPrompt[32];
	int  iPrompt, iTimeOut;
	char *tl1text;
	int tl1len;

	int escape_idx;
	char escape_code[32];
	char tabstops[256];

	int xmlIndent;
	BOOL xmlPreviousIsOpen;

	HOST *host;
} TERM;
enum hostStatus {IDLE, CONNECTING, AUTHENTICATING, CONNECTED};
enum hostType {NONE, STDIO, SERIAL, TELNET, SSH, SFTP, NETCONF};
enum mouseEvents {DOUBLECLK, RIGHTCLK, LEFTDOWN, LEFTDRAG, LEFTUP, MIDDLEUP};

/****************auto_drop.c*************/
void drop_Init(HWND hwnd, void (*handler)(char *));
void drop_Destroy(HWND hwnd);
int autocomplete_Init(HWND hWnd);
int autocomplete_Destroy();
int autocomplete_Add(WCHAR *cmd);
int autocomplete_Del(WCHAR *cmd);
WCHAR *autocomplete_First();
WCHAR *autocomplete_Next();
WCHAR *autocomplete_Prev();

/****************host.c****************/
void host_Construct(HOST *ph);
void host_Open(HOST *ph, char *port);
void host_Send_Size(HOST *ph, int w, int h);
void host_Send(HOST *ph, char *buf, int len);
void host_Close(HOST *ph);
int host_Type(HOST *ph);
int host_Status(HOST *ph);
int host_tcp(HOST *ph);
void xmodem_init(HOST *ph, FILE *fp);

int url_decode(char *url);
int http_Svr(char *intf);
BOOL ftp_Svr(char *root);
BOOL tftp_Svr(char *root);

/****************ssh2.c****************/
void ssh2_Construct(HOST *ph);
void ssh2_Size(HOST *ph, int w, int h);
void ssh2_Send(HOST *ph, char *buf, int len);
char *ssh2_Gets(HOST *ph, char *prompt, BOOL bEcho);
void ssh2_Close(HOST *ph);
void ssh2_Tun(HOST *ph, char *cmd);
void scp_read(HOST *ph, char *lpath, char *rfiles);
void scp_write(HOST *ph, char *lpath, char *rpath);
void sftp_put(HOST *ph, char *src, char *dst);
void sftp_Close(HOST *ph);

/****************term.c****************/
void host_callback( void *term, char *buf, int len);
BOOL term_Construct(TERM *pt);
void term_Destruct(TERM *pt);
void term_Size(TERM *pt, int x, int y);
void term_Title(TERM *pt, char *title);
void term_Error(TERM *pt, char *error);
void term_Scroll(TERM* pt, int lines);
void term_Mouse(TERM *pt, int evt, int x, int y);
void term_Print(TERM *pt, const char *fmt, ...);
void term_Parse(TERM *pt, const char *buf, int len);
void term_Parse_XML(TERM *pt, const char *xml, int len);

BOOL term_Echo(TERM *pt);
void term_Logg(TERM *pt, char *fn);
void term_Save(TERM *pt, char *fn);
void term_Disp(TERM *pt, const char *buf);
void term_Send(TERM *pt, char *buf, int len);
void term_Paste(TERM *pt, char *buf, int len);
int  term_Copy(TERM *pt, char **buf);
int  term_Recv(TERM *pt, char **preply);
int  term_Srch(TERM *pt, char *sstr);

void term_Learn_Prompt(TERM *pt);
char *term_Mark_Prompt(TERM *pt);
int term_Waitfor_Prompt(TERM *pt);
int term_Pwd(TERM *pt, char *buf, int len);
int term_Scp(TERM *pt, char *cmd, char **preply);
int term_Tun(TERM *pt, char *cmd, char **preply);
int term_Cmd(TERM *pt, char *cmd, char **preply);

/****************tiny.c****************/
int utf8_to_wchar(const char *buf, int cnt, WCHAR *wbuf, int wcnt);
int wchar_to_utf8(WCHAR *wbuf, int wcnt, char *buf, int cnt);
int stat_utf8(const char *fn, struct _stat *buffer);
FILE *fopen_utf8(const char *fn, const char *mode);

void cmd_Disp_utf8(char *buf);
void ftpd_quit();
void tftpd_quit();
void tiny_Beep();
void wnd_Size();		//resize window when font or term size changes
void tiny_Redraw();		//redraw term window
void tiny_Title(char *buf);
BOOL tiny_Scroll(BOOL bShowScroll, int cy, int sy);//return if scrollbar is shown
char *tiny_Gets(char *prompt, BOOL bEcho);
