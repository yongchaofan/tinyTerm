//
// "$Id: tiny.c 31019 2018-10-10 21:05:10 $"
//
// tinyTerm -- A minimal serail/telnet/ssh/sftp terminal emulator
//
// tiny.c is the GUI implementation using WIN32 API.
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
#include <sys/stat.h>
#include <windows.h>
#include <windowsx.h>
#include <commdlg.h>
#include <commctrl.h>
#include <ole2.h>
#include <process.h>
#include <shlobj.h>
#include <shellapi.h>
#include "tiny.h"

extern char buff[], attr[];
extern int line[];
extern int cursor_y, cursor_x;
extern int size_y, size_x;
extern int screen_y;
extern int scroll_y;
extern int sel_left, sel_right;
extern BOOL bEcho, bCursor, bLogging;

#define iCmdHeight		16

#define ID_ABOUT		65	
#define ID_CONNECT		68	//^D
#define ID_ECHO			69	//^E
#define ID_FONTSIZE		70	//^F
#define ID_COPYBUF		73
#define ID_LOGGING		76
#define ID_FTPD			77
#define ID_TFTPD		78
#define ID_TRANSPARENT	84	//^T
#define ID_SCPAUSE		80	//^P
#define ID_SCSTOP		83	//^S
#define ID_SCRIPT		85

#define CONN_TIMER		127
#define IDICON_TL1		128
#define IDMENU_OPTIONS	129
#define IDMENU_SCRIPT	131
#define IDD_CONNECT     133
#define IDCONNECT       40000
#define IDCANCELC       40001
#define IDSTATIC		40002
#define IDPROTO			40003
#define IDPORT			40004
#define IDHOST			40005

const char WELCOME[]="\n\n\n\
\t\ttinyTerm is a terminal emulator designed for network engineers,\n\n\
\t\ta single executable in 120K bytes that features:\n\n\n\
\t\t    * Serial/Telnet/SSH/SFTP/Netconf connections\n\n\
\t\t    * FTP/TFTP server for file transfer\n\n\
\t\t    * Command history and autocompletion\n\n\
\t\t    * Simple scripting with list of commands\n\n\
\t\t    * Advanced scripting via xmlhttp://127.0.0.1:%d\n\n\n\
\t\tBy yongchaofan@gmail.com		10-10-2018\n\n\
\t\thttps://github.com/zoudaokou/tinyTerm\n\n\n";

static HINSTANCE hInst;						// Instance handle
static HWND hwndMain, hwndCmd;				// window handles
static HMENU hSysMenu, hScriptMenu, hOptionMenu;
static RECT termRect, wndRect;
static int iFontHeight, iFontWidth;
static int nScriptCnt, httport;
static BOOL bTransparent = FALSE;
static BOOL bScrollbar = FALSE;
static BOOL bFocus = FALSE;
static BOOL bFTPd = FALSE, bTFTPd = FALSE;
static BOOL bScriptRun=FALSE, bScriptPause=FALSE;

void buff_Copy( char *ptr, int len );
void host_Drop( HDROP hDrop );
void host_Paste( void );

BOOL isUTF8c(char c)
{
	return (c&0xc0)==0x80;
}
int wchar_to_utf8(WCHAR *wbuf, int wcnt, char *buf, int cnt)
{
	return WideCharToMultiByte(CP_UTF8, 0, wbuf, wcnt, buf, cnt, NULL, NULL);
}
int utf8_to_wchar(const char *buf, int cnt, WCHAR *wbuf, int wcnt)
{
	return MultiByteToWideChar(CP_UTF8, 0, buf, cnt, wbuf, wcnt);
}
WCHAR *MODES[] = {L"a+", L"r", L"w", L"rb", L"wb"};
FILE * fopen_utf8(const char *fn, int mode)
{
	WCHAR wfn[MAX_PATH];
	utf8_to_wchar(fn, strlen(fn)+1, wfn, MAX_PATH);
	return _wfopen(wfn, MODES[mode]);
}
int stat_utf8(const char *fn, struct _stat *buffer)
{
	WCHAR wfn[MAX_PATH];
	utf8_to_wchar(fn, strlen(fn)+1, wfn, MAX_PATH);
	return _wstat(wfn, buffer);
}

#define SCRIPTS  L"scripts\0*.txt\0All\0*.*\0\0"
#define LOGFILES L"Log file\0*.log;*.txt\0All\0*.*\0\0"
#define OPENFLAGS OFN_PATHMUSTEXIST|OFN_FILEMUSTEXIST
#define SAVEFLAGS OFN_PATHMUSTEXIST|OFN_NOREADONLYRETURN|OFN_OVERWRITEPROMPT
char * fileDialog( WCHAR *szFilter, DWORD dwFlags )
{
static char fname[MAX_PATH];
	WCHAR wname[MAX_PATH];
	BOOL ret = FALSE;
	OPENFILENAME ofn;						// common dialog box structure

	wname[0]=0;
	memset(&ofn, 0, sizeof(OPENFILENAME));
	ofn.lStructSize = sizeof(OPENFILENAME);
	ofn.hwndOwner = hwndMain;
	ofn.lpstrFile = wname;
	ofn.nMaxFile = MAX_PATH-1;
	ofn.lpstrFilter = szFilter;
	ofn.nFilterIndex = 1;
	ofn.lpstrFileTitle = NULL;
	ofn.nMaxFileTitle = 0;
	ofn.lpstrInitialDir = NULL;
	ofn.Flags = dwFlags;
	if ( dwFlags&OFN_OVERWRITEPROMPT )
		ret = GetSaveFileName(&ofn);
	else
		ret = GetOpenFileName(&ofn);
	if ( ret ) {
		wchar_to_utf8(wname,wcslen(wname)+1, fname,MAX_PATH);
		return fname;
	}
	else
		return NULL;
}
char *getFolderName(char *title)
{
	static BROWSEINFO bi;
	static char szFolder[MAX_PATH];
	WCHAR szDispName[MAX_PATH];
	LPITEMIDLIST pidl;

	WCHAR wtitle[1024], wfolder[MAX_PATH];
	utf8_to_wchar(title, strlen(title), wtitle, 1024);
	memset(&bi, 0, sizeof(BROWSEINFO));
	bi.hwndOwner = 0;
	bi.pidlRoot = NULL;
	bi.pszDisplayName = szDispName;
	bi.lpszTitle = wtitle;
	bi.ulFlags = BIF_RETURNONLYFSDIRS;
	bi.lpfn = NULL;
	bi.lParam = 0;
	pidl = SHBrowseForFolder(&bi);
	if ( pidl != NULL )
		if ( SHGetPathFromIDList(pidl, wfolder) ) {
			wchar_to_utf8(wfolder, wcslen(wfolder)+1, szFolder, MAX_PATH);
			return szFolder;
		}
	return NULL;
}
int cmd_Exec( char *pCmd, char **pReply )
{
	static char cmd[256];
	strncpy( cmd, pCmd, 255 ); cmd[255]=0;
	if ( pReply ) *pReply = buff+cursor_x;
	if ( strncmp(cmd, "Title", 5)==0 ) 		tiny_Title( cmd+5 ); 
	else if ( strncmp(cmd, "Log ", 4)==0 ) 	term_Logg( cmd+4 ); 
	else if ( strncmp(cmd, "scp ", 4)==0 )	scp_cmd(cmd+4);
	else if ( strncmp(cmd, "tun ", 4)==0 )	tun_cmd(cmd+4);
	else if ( strncmp(cmd, "Ftpd", 4)==0 ) 	ftp_Svr( cmd+5 );
	else if ( strncmp(cmd, "Tftpd",5)==0 )	tftp_Svr( cmd+6 );
	else if ( strncmp(cmd, "Timeout",7)==0 )term_Timeout(cmd+8);
	else if ( strncmp(cmd, "Prompt ",7)==0 )term_Prompt(cmd+7);
	else if ( strncmp(cmd, "Disconn",7)==0 )host_Close(); 
	else if ( strncmp(cmd, "Selection",9)==0 ) {
		if ( pReply ) *pReply = buff+sel_left; 
		return sel_right-sel_left;
	}
	else if ( host_Status()==CONN_IDLE ) {
		if (strncmp(cmd, "com",3)==0 ||
			strncmp(cmd, "ssh ",4)==0 ||
			strncmp(cmd, "sftp ",5)==0 ||
			strncmp(cmd, "netconf ",8)==0 ||
			strncmp(cmd, "telnet ",7)==0 ) host_Open(cmd);
	}
	return 0;
} 
void cmd_Enter(void)
{
	char cmd[256];
	WCHAR wcmd[256];
	int cch = SendMessage(hwndCmd, WM_GETTEXT, (WPARAM)255, (LPARAM)wcmd);
	if ( cch >= 0 ) {
		wcmd[cch] = 0; 
		autocomplete_Add(wcmd);
		cch = wchar_to_utf8(wcmd,cch+1, cmd,255); 
		cmd[cch] = 0;
		switch ( *cmd ) {
		case '!': host_Open( cmd ); break;
		case '#': cmd_Exec( cmd+1, NULL ); break;
		case '/': term_Srch( cmd+1 ); break;
		case '^': cmd[1]-=64; host_Send( cmd+1, 1 ); break;
		default:  if ( host_Status()==CONN_IDLE ) 
				  	host_Open(cmd);
				  else {
					cmd[cch]='\r';
					host_Send(cmd, cch+1);
				  }
		}
	}
}
WNDPROC wpOrigCmdProc; 
LRESULT APIENTRY CmdEditProc(HWND hwnd, UINT uMsg,
								WPARAM wParam, LPARAM lParam) 
{ 
	BOOL ret=FALSE;
	if ( uMsg==WM_KEYDOWN ){
		ret = TRUE;
		if ( (GetKeyState(VK_CONTROL)&0x8000) == 0) {
			switch ( wParam ) {
			case VK_UP: cmd_Disp(autocomplete_Prev()); break;
			case VK_DOWN: cmd_Disp(autocomplete_Next()); break;
			case VK_RETURN: cmd_Enter(); break;
			default: ret = FALSE;
			}
		}
		else {
			switch ( wParam ) {
			case VK_HOME: SetFocus( hwndMain ); break;
			case 'A': PostMessage(hwndCmd, EM_SETSEL, 0, -1); break;
			case 'D': 
			case 'E': 
			case 'F': 
			case 'P': 
			case 'S': 
			case 'T': PostMessage(hwndMain, WM_SYSCOMMAND, wParam, 0);break;
			default: ret = FALSE;
			}
		}
	}
	if ( ret ) 
		return TRUE;
	else
		return CallWindowProc(wpOrigCmdProc, hwnd, uMsg, wParam, lParam);
}
void cmd_Disp( char *buf )
{
	WCHAR wbuf[1024];
	utf8_to_wchar(buf, strlen(buf)+1, wbuf, 1024);
	SetWindowText(hwndCmd, wbuf);
	PostMessage(hwndCmd, EM_SETSEL, 0, -1);
}
void tiny_Title( char *buf )
{
	WCHAR Title[256] = L"tinyTerm ";
	WCHAR wbuf[256];
	utf8_to_wchar(buf, strlen(buf)+1, wbuf, 256);
	wcscat(Title, wbuf); 
	SetWindowText(hwndMain, Title);
	ModifyMenu(hSysMenu, ID_CONNECT, MF_BYCOMMAND, ID_CONNECT, 
					( *buf==0 ? L"Connect..." : L"Disconnect\t^D") );
	if ( *buf ) 
		host_Size(size_x, size_y);
	else
		term_Disp("\r\n\033[31mDisconnected\r\n\033[37m");
}
void tiny_Redraw()
{
	InvalidateRect( hwndMain, &termRect, TRUE );
}
void tiny_Beep()
{
	PlaySound(L"Default Beep", NULL, SND_ALIAS|SND_ASYNC);
}
void tiny_Connecting()
{
	SetTimer(hwndMain, CONN_TIMER, 1000, NULL);
}
const int FONTWEIGHT[3] = { 14, 16, 20 };
HFONT hTermFont[3], hEditFont;
HBRUSH dwBkBrush, dwScrollBrush, dwSliderBrush;
const COLORREF BKCOLOR = 	RGB(0, 64, 0);
const COLORREF SCROLLCOLOR =RGB(64,64,64);
const COLORREF SLIDERCOLOR =RGB(224,0, 0);
const COLORREF COLORS[8] ={ RGB(0,0,0), 	RGB(224,0,0), RGB(0,224,0), 
							RGB(224,224,0), RGB(0,0,224), RGB(224,0,224),
							RGB(0,244,244), RGB(224,224,224) };
static int iFontSize = 1;
unsigned char cAlpha = 255;
void tiny_Font( HWND hwnd )
{
	iFontHeight = FONTWEIGHT[iFontSize]; 
	iFontWidth = iFontSize + 7;
	GetWindowRect( hwnd, &wndRect );
	int x = wndRect.left;
	int y = wndRect.top;
	wndRect.right = x + iFontWidth*size_x;
	wndRect.bottom = y + iFontHeight*size_y + iCmdHeight+1;
	AdjustWindowRect(&wndRect, WS_TILEDWINDOW, FALSE);
	MoveWindow( hwnd, x, y, wndRect.right-wndRect.left, 
							wndRect.bottom-wndRect.top, TRUE );
}

const WCHAR *PROTOCOLS[]={L"Serial", L"telnet", L"ssh", L"sftp", L"netconf"};
const WCHAR *PORTS[]    ={L"2024",   L"23",     L"22",  L"22",   L"830"};
const WCHAR *SETTINGS[] ={L"9600,n,8,1", L"19200,n,8,1", L"38400,n,8,1", 
										L"57600,n,8,1", L"115200,n,8,1"};
const WCHAR *HOSTS[32]={L"192.168.1.1"};
static int host_cnt = 1;
BOOL CALLBACK ConnectProc(HWND hwndDlg, UINT message, 
							WPARAM wParam, LPARAM lParam)
{
	static HWND hwndProto, hwndPort, hwndHost, hwndStatic;
	static int proto = 2;
	switch ( message ) {
	case WM_INITDIALOG:
		hwndStatic = GetDlgItem(hwndDlg, IDSTATIC);
		hwndProto  = GetDlgItem(hwndDlg, IDPROTO);
		hwndPort   = GetDlgItem(hwndDlg, IDPORT);
		hwndHost   = GetDlgItem(hwndDlg, IDHOST);
		for ( int i=0; i<5; i++ ) 
			ComboBox_AddString(hwndProto,PROTOCOLS[i]); 
		if ( proto==0 ) {
			ComboBox_SetCurSel(hwndProto, 0);
			proto = 2;
		}
		else {
			ComboBox_SetCurSel(hwndProto, proto);
			proto = 0;
		}
		PostMessage(hwndDlg, WM_COMMAND, CBN_SELCHANGE<<16, (LPARAM)hwndProto);
		SetFocus(hwndProto);
		break;
	case WM_COMMAND: 
		if ( HIWORD(wParam)==CBN_SELCHANGE ) {
			if ( (HWND)lParam==hwndProto ) {
				int new_proto = ComboBox_GetCurSel(hwndProto);
				if ( proto!=0 && new_proto==0 ) {
					ComboBox_ResetContent(hwndHost);
					ComboBox_ResetContent(hwndPort); 
					for ( int i=1; i<16; i++ ) {
						WCHAR port[32];
						wsprintf( port, L"\\\\.\\COM%d", i );
						HANDLE hPort = CreateFile( port, GENERIC_READ, 0, NULL,
													OPEN_EXISTING, 0, NULL);
						if ( hPort != INVALID_HANDLE_VALUE ) {
							ComboBox_AddString(hwndPort,port+4); 
							CloseHandle( hPort );
						}
					}
					for ( int i=0; i<5; i++ ) 
						ComboBox_AddString(hwndHost,SETTINGS[i]); 
					ComboBox_SetCurSel(hwndHost, 0);
					Static_SetText(hwndStatic, L"Settings:");
				}
				if ( proto==0 && new_proto!=0 )  {
					ComboBox_ResetContent(hwndHost);
					ComboBox_ResetContent(hwndPort); 
					for ( int i=0; i<5; i++ )
						ComboBox_AddString(hwndPort,PORTS[i]); 
					for ( int i=0; i<host_cnt; i++ ) 
						ComboBox_AddString(hwndHost,HOSTS[i]); 
					ComboBox_SetCurSel(hwndHost, host_cnt-1);
					Static_SetText(hwndStatic, L"Host:");
				}
				proto = new_proto;
				ComboBox_SetCurSel(hwndPort, proto);
			}
		}
		else {
			WCHAR conn[256];
			char cmd[1024];
			int proto;
			switch ( LOWORD(wParam) ) {
			case IDCONNECT:
					proto = ComboBox_GetCurSel(hwndProto);
					if ( proto==0 ) {
						ComboBox_GetText(hwndPort, conn, 128);
						wcscat(conn, L":");
						ComboBox_GetText(hwndHost, conn+wcslen(conn), 128);
					}
					else {
						ComboBox_GetText(hwndProto, conn, 128);
						wcscat(conn, L" ");
						int len = wcslen(conn);
						ComboBox_GetText(hwndHost, conn+wcslen(conn), 128);
						int i;
						for ( i=0; i<host_cnt; i++ )
							if ( wcscmp(conn+len,HOSTS[i])==0 ) break;
						if ( i==host_cnt && host_cnt<32 ) {
							HOSTS[host_cnt++] = _wcsdup(conn+len);
							ComboBox_AddString(hwndHost,HOSTS[++i]);
						} 
						wcscat(conn, L":");
						ComboBox_GetText(hwndPort, conn+wcslen(conn), 128);
					}
					wchar_to_utf8(conn, wcslen(conn)+1, cmd, 1024);
					cmd_Disp(cmd);
					host_Open(cmd);
			case IDCANCELC:
					EndDialog(hwndDlg, wParam);
					return TRUE;
			}
		}
	}
	return FALSE;
}
void check_Option( HMENU hMenu, DWORD id, BOOL op)
{
	CheckMenuItem( hMenu, id, MF_BYCOMMAND|(op?MF_CHECKED:MF_UNCHECKED));
}
BOOL menu_Command( WPARAM wParam )
{
	switch ( wParam ) {
	case ID_CONNECT: 
		if ( host_Status()!=CONN_IDLE ) 
			host_Close(); 
		else 
			DialogBox(hInst, MAKEINTRESOURCE(IDD_CONNECT),
								hwndMain, (DLGPROC)ConnectProc);
		break;
	case ID_COPYBUF:
		buff_Copy( buff, line[cursor_y] );
		break;
	case ID_ABOUT  :{
		char welcome[1024];
		sprintf(welcome, WELCOME, httport);
		term_Disp(welcome);
		break;
		}
	case ID_LOGGING: 
		if ( !bLogging ) 
			term_Logg( fileDialog(LOGFILES,SAVEFLAGS) );
		else
			term_Logg( NULL ); 
		check_Option( hSysMenu, ID_LOGGING, bLogging ); 
		break;
	case ID_ECHO:
		check_Option( hOptionMenu, ID_ECHO, bEcho=!bEcho ); 
		break;
	case ID_FONTSIZE: 
		if ( ++iFontSize==3 ) iFontSize=0;
		tiny_Font( hwndMain ); 
		break;
	case ID_TRANSPARENT: 
		bTransparent=!bTransparent;
		check_Option( hOptionMenu, ID_TRANSPARENT, bTransparent );
		cAlpha = bTransparent?224:255;
		SetLayeredWindowAttributes(hwndMain,0,cAlpha,0x02); //LWA_ALPHA
		break;
	case ID_FTPD:	 
		bFTPd = ftp_Svr(bFTPd?NULL:getFolderName("Choose root directory")); 
		check_Option( hOptionMenu, ID_FTPD, bFTPd );
		break;
	case ID_TFTPD:
		bTFTPd = tftp_Svr(bTFTPd?NULL:getFolderName("Choose root directory")); 
		check_Option( hOptionMenu, ID_TFTPD, bTFTPd );
		break;
	case ID_SCPAUSE: 
		if ( bScriptRun ) bScriptPause = !bScriptPause;
		if ( bScriptPause ) cmd_Disp( "Script Paused" );
		break;
	case ID_SCSTOP:  
		bScriptRun = FALSE;  
		break;
	case ID_SCRIPT: 
		script_Open( fileDialog(SCRIPTS,OPENFLAGS) );
		break;
	default:	
		if ( wParam>ID_SCRIPT && wParam<=ID_SCRIPT+nScriptCnt ) { 
			WCHAR fn[256], port[256];
			GetMenuString(hScriptMenu, wParam, fn, 255, 0);
			wsprintf(port, L"%d", httport); 
			_wspawnlp( _P_NOWAIT, L"WScript.exe",L"WScript.exe",fn,port,NULL );
			break;
		}
		return FALSE; 
	}
	return TRUE;
}
LRESULT CALLBACK MainWndProc(HWND hwnd,UINT msg,WPARAM wParam,LPARAM lParam)
{
	PAINTSTRUCT ps;
	static HDC hDC, hBufferDC=NULL;
	static HBITMAP hBufferMap=NULL;
	static WCHAR wm_chars[2]={0,0};
	
	switch (msg) {
	case WM_CREATE:
		hwndCmd = CreateWindow(L"EDIT", NULL, 
							WS_CHILD|WS_VISIBLE|ES_AUTOHSCROLL, 
							0, 0, 1, 1, hwnd, (HMENU)0, hInst, NULL);
		wpOrigCmdProc = (WNDPROC)SetWindowLongPtr(hwndCmd, 
							GWLP_WNDPROC, (LONG_PTR)CmdEditProc); 
		SendMessage( hwndCmd, WM_SETFONT, (WPARAM)hEditFont, TRUE );
		SendMessage( hwndCmd, EM_SETLIMITTEXT, 255, 0);
		autocomplete_Init(hwndCmd);
		drop_Init( hwndCmd );
		dwBkBrush = CreateSolidBrush( BKCOLOR );
		dwScrollBrush = CreateSolidBrush( SCROLLCOLOR );
		dwSliderBrush = CreateSolidBrush( SLIDERCOLOR );

		DragAcceptFiles( hwnd, TRUE );
		SetLayeredWindowAttributes(hwnd, 0, cAlpha, LWA_ALPHA);
		GetClientRect( hwnd, &termRect );
		termRect.bottom -= iCmdHeight;
		tiny_Font( hwnd );
		PostMessage(hwnd, WM_SYSCOMMAND, ID_CONNECT, 0);
		break;
	case WM_SIZE:
		GetClientRect( hwnd, &termRect );
		termRect.bottom -= iCmdHeight;
		size_x = termRect.right / iFontWidth;
		size_y = termRect.bottom / iFontHeight;
		MoveWindow( hwndCmd, 0, termRect.bottom, termRect.right, 
												iCmdHeight, TRUE );
		term_Size();
		host_Size( size_x, size_y );
		if ( hBufferDC!=NULL ) DeleteDC(hBufferDC);
		if ( hBufferMap!=NULL ) DeleteObject(hBufferMap);
		hDC = GetDC(hwnd);
		hBufferDC = CreateCompatibleDC(hDC);
		hBufferMap = CreateCompatibleBitmap(hDC, termRect.right,
												termRect.bottom );
		SelectObject(hBufferDC, hBufferMap);
		SelectObject(hBufferDC, hTermFont[iFontSize]);
		SetBkMode(	 hBufferDC, OPAQUE);//TRANSPARENT
		ReleaseDC(hwnd, hDC);
		tiny_Redraw( );
		break;
	case WM_PAINT: 
		hDC = BeginPaint(hwnd, &ps);
		if ( hDC!=NULL ) {
			BitBlt(hBufferDC, 0,0,termRect.right, termRect.bottom, 
											NULL, 0, 0, BLACKNESS);
			int y = screen_y+scroll_y; if ( y<0 ) y = 0;
			int sel_min = min(sel_left, sel_right);
			int sel_max = max(sel_left, sel_right);
			for ( int dy=0, i=0; i<size_y; i++ ) {
				int dx = 1;
				int j = line[y+i];
				while ( j<line[y+i+1] ) {
					BOOL utf = FALSE;
					int n = j; 
					while ( attr[n]==attr[j] ) {
						if ( (buff[n]&0xc0)==0xc0 ) utf = TRUE;
						if ( ++n==line[y+i+1] ) break;
						if ( n==sel_min || n==sel_max ) break;
					}
					if ( j>=sel_min&&j<sel_max ) {
						SetTextColor( hBufferDC, COLORS[0] );
						SetBkColor( hBufferDC, COLORS[7] );

					}
					else {
						SetTextColor( hBufferDC, COLORS[attr[j]&7] );
						SetBkColor( hBufferDC, COLORS[(attr[j]>>4)&7] );
					}
					if ( utf ) {
						WCHAR buf[1024];
						int cch = utf8_to_wchar(buff+j, n-j, buf, 1024);
						TextOutW(hBufferDC, dx, dy, buf, cch);
						RECT text_rect = {0, 0, 0, 0};
						DrawText(hBufferDC, buf, cch, &text_rect, DT_CALCRECT);
						dx += text_rect.right;
					}
					else {
						TextOutA(hBufferDC, dx, dy, buff+j, n-j);
						dx += iFontWidth*(n-j);
					}
					j=n;
				}
				dy += iFontHeight;
			}
			if ( scroll_y ) {
				int slider_y = termRect.bottom*(cursor_y+scroll_y)/cursor_y;
				SelectObject(hBufferDC, dwScrollBrush);
				Rectangle(hBufferDC, termRect.right-11, 0, 
									 termRect.right, termRect.bottom);
				SelectObject(hBufferDC, dwSliderBrush);
				Rectangle(hBufferDC, termRect.right-11, slider_y-8, 
									 termRect.right, slider_y+7 );
			}
			BitBlt(hDC, 0, 0, termRect.right, termRect.bottom, 
									hBufferDC, 0, 0, SRCCOPY);
		}
		EndPaint( hwnd, &ps );
		if ( bFocus ) { 
			if ( bCursor ) {
				ShowCaret(hwnd);
				RECT cursor_rect = {0, 0, 0, 0};
				WCHAR buf[1024];
				int cch = utf8_to_wchar(buff+line[cursor_y],
										cursor_x-line[cursor_y], buf, 1024);
				DrawText(hBufferDC, buf, cch, &cursor_rect, DT_CALCRECT);
				SetCaretPos( cursor_rect.right+2, 
							(cursor_y-screen_y)*iFontHeight+12 ); 
			}
			else 
				HideCaret(hwnd);
		}
		break;
	case WM_SETFOCUS: 
		CreateCaret(hwnd, NULL, iFontWidth, iFontHeight/4); 
		bFocus = TRUE;
		tiny_Redraw();
		break; 
	case WM_KILLFOCUS: 
		DestroyCaret();
		bFocus = FALSE;
		break;
	case WM_IME_STARTCOMPOSITION: 
		//moves the composition window to cursor pos on Win10
		break;
	case WM_CHAR: 
			if ( (wParam>>8)==0 ) {
				char key = wParam&0xff;
				term_Send(&key, 1);
			}
			else {
				char utf8[6], ho = wParam>>8;
				if ( (ho&0xF8)!=0xD8 ) {	
					int c = wchar_to_utf8((WCHAR *)&wParam, 1, utf8, 6);
					if ( c>0 ) term_Send(utf8, c);
				}
				else {	
					if ( (ho&0xDC)==0xD8 ) 
						wm_chars[0] = wParam;	//high surrogate word
					else 
						wm_chars[1] = wParam;	//low surrogate word
					if ( wm_chars[1]!=0 && wm_chars[0]!=0 ) {
						int c = wchar_to_utf8(wm_chars, 2, utf8, 6);
						if ( c>0 ) term_Send(utf8, c);
						wm_chars[0] = 0;
						wm_chars[1] = 0;
					}
				}
			}

		break;
	case WM_KEYDOWN:
		switch (wParam) {
		case VK_PRIOR: PostMessage(hwnd, WM_MOUSEWHEEL, 
									MAKEWPARAM(0, (size_y-1)*40),0); break;
		case VK_NEXT:  PostMessage(hwnd, WM_MOUSEWHEEL, 
									MAKEWPARAM(0, -(size_y-1)*40),0); break;
		case VK_END: if ( GetKeyState(VK_CONTROL)&0x8000 ) SetFocus( hwndCmd );
					 break;
		default: term_Keydown(wParam);
		}
		break;
	case WM_TIMER:
		if ( host_Status()==CONN_CONNECTING ) 
			term_Disp(".");
		else
			KillTimer(hwndMain, CONN_TIMER);
		break;
	case WM_MOUSEWHEEL: 
		scroll_y -= GET_WHEEL_DELTA_WPARAM(wParam)/40;
		if ( scroll_y<-screen_y ) scroll_y = -screen_y;
		if ( scroll_y>0 ) scroll_y = 0;
		tiny_Redraw( );
		break;
	case WM_ACTIVATE: 
		SetFocus( hwnd ); 
		tiny_Redraw( );
		break; 
	case WM_LBUTTONDBLCLK: {
		int x = GET_X_LPARAM(lParam)/iFontWidth;
		int y = (GET_Y_LPARAM(lParam)+2)/iFontHeight+screen_y+scroll_y;
		sel_left = line[y]+x;
		sel_right = sel_left;
		while ( --sel_left>line[y] )
			if ( buff[sel_left]==0x0a || buff[sel_left]==0x20 ) {
				sel_left++;
				break;
			}
		while ( ++sel_right<line[y+1])
			if ( buff[sel_right]==0x0a || buff[sel_right]==0x20 ) break;
		buff_Copy( buff+sel_left, sel_right-sel_left );
		tiny_Redraw( );
		break;
	}
	case WM_LBUTTONDOWN: {	
		int x = GET_X_LPARAM(lParam)/iFontWidth;
		int y = (GET_Y_LPARAM(lParam)+2);
		
		if ( x>=size_x-2 ) {
			bScrollbar = TRUE;
			scroll_y = y*cursor_y/termRect.bottom-cursor_y;
			if ( scroll_y<-screen_y ) scroll_y = -screen_y;
			tiny_Redraw();
		}
		else {
			y = y/iFontHeight+screen_y+scroll_y;
			sel_left = min(line[y]+x, line[y+1]);
			while ( isUTF8c(buff[sel_left]) ) sel_left--;
			sel_right = sel_left;
		}
		SetCapture(hwnd);
		break;
	}
	case WM_MOUSEMOVE:
		if ( MK_LBUTTON && wParam ) {
			int x = GET_X_LPARAM(lParam)/iFontWidth;
			int y = (GET_Y_LPARAM(lParam)+2);
			if ( bScrollbar ) {
				scroll_y = y*cursor_y/termRect.bottom-cursor_y;
				if ( scroll_y<-screen_y ) scroll_y = -screen_y;
				if ( scroll_y>0 ) scroll_y = 0;
			}
			else {
				y /= iFontHeight;
				if ( y<0 ) {
					scroll_y += y*2;
					if ( scroll_y<-screen_y ) scroll_y = -screen_y;
				}
				if ( y>size_y) { 
					scroll_y += (y-size_y)*2;
					if ( scroll_y>0 ) scroll_y=0;
				}
				y += screen_y+scroll_y;
				sel_right = min(line[y]+x, line[y+1]);
				while ( isUTF8c(buff[sel_right]) ) sel_right++;
			}
			tiny_Redraw( );
		}
		break;
	case WM_LBUTTONUP:
		if ( sel_right!=sel_left ) {
			int sel_min = min(sel_left, sel_right);
			int sel_max = max(sel_left, sel_right);
			buff_Copy( buff+sel_min, sel_max-sel_min );
		}
		else {
			sel_left = sel_right = 0;
			tiny_Redraw( );
		}
		ReleaseCapture();
		bScrollbar = FALSE;
		SetFocus( hwnd );
		break;
	case WM_RBUTTONUP:
		host_Paste();
		break;
	case WM_DROPFILES: 
		host_Drop((HDROP)wParam);
		break;
	case WM_CTLCOLOREDIT: 
		SetTextColor( (HDC)wParam, COLORS[3] );
		SetBkColor( (HDC)wParam, BKCOLOR ); 
		return (int)dwBkBrush;		//brush must be returned for update
	case WM_ERASEBKGND: return 1;
	case WM_DESTROY:
		DragAcceptFiles(hwnd, FALSE); 
	case WM_CLOSE: 		
		DeleteDC(hBufferDC);
		DeleteObject(hBufferMap);
		DeleteObject(dwBkBrush);
		DeleteObject(dwScrollBrush);
		DeleteObject(dwSliderBrush);
		PostQuitMessage(0);
		break;
	case WM_SYSCOMMAND: 
		if ( menu_Command( wParam ) ) return 1;
	default: return DefWindowProc(hwnd,msg,wParam,lParam);
	}
	return 0;
}
/*<---------------------------------------------------------------------->*/
void tiny_Sysmenu( HWND hwnd )
{
	hOptionMenu = LoadMenu( hInst, MAKEINTRESOURCE(IDMENU_OPTIONS));
	hScriptMenu = LoadMenu( hInst, MAKEINTRESOURCE(IDMENU_SCRIPT) );
	WIN32_FIND_DATA FindFileData;
	HANDLE hFind;
	nScriptCnt = 1;
	hFind = FindFirstFile(L"*.js", &FindFileData);
	if (hFind != INVALID_HANDLE_VALUE) {
		do {
			InsertMenu( hScriptMenu, 0, MF_BYPOSITION, 
						ID_SCRIPT+nScriptCnt++,	FindFileData.cFileName);
		} while ( FindNextFile(hFind, &FindFileData) );
	}

	hSysMenu = GetSystemMenu(hwnd, FALSE);
	InsertMenu(hSysMenu, 0, MF_BYPOSITION|MF_SEPARATOR, 0, NULL);
	InsertMenu(hSysMenu, 0, MF_BYPOSITION, ID_ABOUT, 	L"About");
	InsertMenu(hSysMenu, 0, MF_BYPOSITION|MF_POPUP, 
							(UINT_PTR)hOptionMenu, 		L"Options");
	InsertMenu(hSysMenu, 0, MF_BYPOSITION|MF_POPUP, 
							(UINT_PTR)hScriptMenu, 		L"Script");
	InsertMenu(hSysMenu, 0, MF_BYPOSITION, ID_COPYBUF, 	L"Copy all");
	InsertMenu(hSysMenu, 0, MF_BYPOSITION, ID_LOGGING, 	L"Logging...");
	InsertMenu(hSysMenu, 0, MF_BYPOSITION, ID_CONNECT, 	L"Connect...");
}
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, 
									LPSTR lpCmdLine, INT nCmdShow)
{
	WNDCLASSEX wc;
	wc.cbSize 		= sizeof( wc ); 
	wc.style 		= CS_DBLCLKS;
	wc.cbClsExtra	= 0;
	wc.cbWndExtra	= 0;
	wc.lpfnWndProc 	= &MainWndProc;
	wc.hInstance 	= hInstance;
	wc.hIcon = LoadIcon(hInstance, MAKEINTRESOURCE(IDICON_TL1));
	wc.hIconSm 		= 0;
	wc.hCursor		= LoadCursor(NULL,IDC_ARROW);
	wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
	wc.lpszClassName = L"TWnd";
	wc.lpszMenuName = 0;
	if ( !RegisterClassEx(&wc) ) return 0;
	hInst = hInstance;

	OleInitialize( NULL );
	httport = host_Init();
	term_Init( );

	hEditFont = CreateFont( 16,0,0,0,FW_MEDIUM, FALSE,FALSE,FALSE,
						DEFAULT_CHARSET,OUT_TT_PRECIS,CLIP_DEFAULT_PRECIS,
						DEFAULT_QUALITY, VARIABLE_PITCH,L"Arial");
	for ( int i=0; i<3; i++ ) 
		hTermFont[i] = CreateFont(FONTWEIGHT[i],0,0,0,
						FW_MEDIUM, FALSE,FALSE,FALSE,
						DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
						DEFAULT_QUALITY, FIXED_PITCH, L"Consolas");
	hwndMain = CreateWindowEx( WS_EX_LAYERED, L"TWnd", L"tinyTerm",
						WS_TILEDWINDOW|WS_VISIBLE, CW_USEDEFAULT, CW_USEDEFAULT,
						1, 1, NULL, NULL, hInst, NULL );
	tiny_Sysmenu( hwndMain );
	
	char *p = lpCmdLine;
	while ( *p!=0 ) {
		while ( *p==' ' ) p++; 
		char c = p[1];
		if ( *p=='-' && ( c=='e'||c=='f'||c=='t' ) ) {
			PostMessage(hwndMain, WM_SYSCOMMAND, toupper(c), 0);
			p+=2;
		}
		else 
			break;
	}
	if ( *p!=0 ) host_Open( p );

	FILE *fp = fopen("tinyTerm.dic", "r");
	if ( fp!=NULL ) {
		while ( !feof( fp ) ) {
			char cmd[256];
			fgets( cmd, 256, fp );
			int l = strlen(cmd)-1;
			while ( l>0 && cmd[l]<0x10 ) cmd[l--]=0;
			WCHAR wcmd[1024];
			utf8_to_wchar(cmd, strlen(cmd)+1, wcmd, 1024);
			autocomplete_Add(wcmd) ;
		}
		fclose( fp );
	}

//Message Loop
	MSG msg;
	while ( GetMessage(&msg,NULL,0,0) ){
		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}
	if ( bLogging ) term_Logg( NULL );

//Clean up
	host_Destory();
	drop_Destroy( hwndCmd );
	autocomplete_Destroy( ); 

	OleUninitialize();
	WSACleanup();

	return 0;
}
/*******************************Buffer functions***********************/
void buff_Copy( char *ptr, int len )
{
	if ( len>0 ) if ( OpenClipboard(hwndMain) ) {
		EmptyClipboard( );
		HANDLE hglbCopy = GlobalAlloc(GMEM_MOVEABLE, (len+1)*2);
		   if ( hglbCopy != NULL ) {
			WCHAR *p = GlobalLock(hglbCopy); 
			len = utf8_to_wchar( ptr, len, p, len );
			p[len] = 0;
			GlobalUnlock(hglbCopy); 
			SetClipboardData(CF_UNICODETEXT, hglbCopy);
		}
		CloseClipboard( );
	}
}
void host_Paste( void )
{
	if ( OpenClipboard(hwndMain) ) {
		HANDLE hglb = GetClipboardData(CF_UNICODETEXT);
		WCHAR *ptr = (WCHAR *)GlobalLock(hglb);
		if (ptr != NULL) {
			int len =  wchar_to_utf8(ptr, -1, NULL, 0);
			char *p = (char *)malloc(len);
			if ( p!=NULL ) {
				wchar_to_utf8(ptr, -1, p, len);
				if ( host_Status()==CONN_CONNECTED ) 
					host_Send(p, len);
				else
					term_Disp(p);
				free(p);
			}
			GlobalUnlock(hglb);
		}
		CloseClipboard();
	}
}
DWORD WINAPI scp_sftp_copier(void *p)
{
	HDROP hDrop = (HDROP)p;
	char rdir[1024];
	if ( host_Type()==SSH ) scp_pwd(rdir);
	int dirlen = strlen(rdir);

	WCHAR wname[MAX_PATH];
	char put[MAX_PATH+4]="put ";
	char *fname = put+4;
	int cnt = DragQueryFile( hDrop, -1, wname,4 );
	for ( int i=0; i<cnt; i++ ) {
		DragQueryFile( hDrop, i, wname, MAX_PATH );
		wchar_to_utf8(wname, -1, fname, MAX_PATH );
		for (char *p=fname; *p; p++) if ( *p=='\\' ) *p='/';
		if ( host_Type()==SSH ) {
			char *p=strrchr(fname, '/');
			strcpy(rdir+dirlen, p);
			scp_write_one(fname, rdir);
		}
		else{
			sftp_cmd(put);
		}
	}
	DragFinish( hDrop );
	host_Send("\r", 1);
	return 0;
}
void host_Drop( HDROP hDrop )
{
	WCHAR wname[MAX_PATH];
	if ( host_Status()==CONN_CONNECTED && 
			(host_Type()==SSH||host_Type()==SFTP) ) {
		DWORD dwCopierId;
		CreateThread( NULL, 0, (LPTHREAD_START_ROUTINE) scp_sftp_copier, 
								(void *)hDrop, 0, &dwCopierId);
		return;
	}
	
	DragQueryFile( hDrop, 0, wname, MAX_PATH );
	DragFinish( hDrop );
	if ( host_Status()==CONN_CONNECTED ) {
		struct _stat sb;
		if ( _wstat(wname, &sb)==-1 ) return;
		char *tl1s = (char *)malloc(sb.st_size+1);
		if ( tl1s==NULL )return;
		FILE *fpScript = _wfopen( wname, L"rb" );
		if ( fpScript != NULL ) {
			int lsize = fread( tl1s, 1, sb.st_size, fpScript );
			fclose( fpScript );
			if ( lsize > 0 ) {
				tl1s[lsize]=0;
				script_Drop(tl1s);
			}
		}
	}
	else {
		if ( host_Status()==CONN_IDLE ) {
			char buf[8192];
			int lines=0, len;
			FILE *fp=_wfopen(wname, L"r");
			if ( fp != NULL ) {
				while ( (len=fread(buf, 1, 8191, fp)) > 0 ) {
					buf[len]=0;
					term_Disp( buf );
					lines+=len;
				};
				fclose( fp );
				sprintf( buf, "%d bytes loaded", lines);
				cmd_Disp(buf);
			}
		}
	}
}
DWORD WINAPI scripter( void *cmds )
{
	char *p0=(char *)cmds, *p1, *p2;
	int iRepCount = -1;
	bScriptRun=TRUE; bScriptPause = FALSE; 
	do {
		p2=p1=strchr(p0, 0x0a);
		if ( p1==NULL ) p1 = p0+strlen(p0);
		*p1 = 0;

		if (p1>p0) {
			cmd_Disp(p0);
			if ( *p0=='#' ) {
				if ( strncmp( p0+1, "Loop ", 5)==0 ){
					if ( iRepCount<0 ) iRepCount = atoi( p0+6 );
					if ( --iRepCount>0 ) {
						*p1=0x0a;
						p0 = p1 = p2 = (char*)cmds;
					}
				}
				else if ( strncmp(p0+1, "Wait ",5)==0 )  
					Sleep(atoi(p0+6)*1000);
				else if ( strncmp(p0+1, "Waitfor ",8)==0 )
					term_Waitfor(p0+9);
				else {
					*(p1-1)=0;
					cmd_Exec( p0+1, NULL );
					*(p1-1)=0x0d;
				}
			}
			else 
				if ( host_Status()==CONN_CONNECTED ) term_TL1( p0, NULL );
		}
		if ( p0!=p1 ) { p0 = p1+1; *p1 = 0x0a; }
		while ( bScriptPause && bScriptRun ) Sleep(1000);
	} 
	while ( p2!=NULL && bScriptRun );
	cmd_Disp( bScriptRun ? "script completed" : "script stopped");
	bScriptRun = bScriptPause = FALSE;
	free(cmds);
	return 0;
}
void script_Drop( char *tl1s )
{
	if ( !bScriptRun && !bScriptPause ) {
		if ( host_Type()!=NETCONF ) {
			DWORD dwPasterId;
			CreateThread( NULL, 0, (LPTHREAD_START_ROUTINE) scripter, 
											(void *)tl1s, 0, &dwPasterId);
		}
		else {
			netconf_Send(tl1s, strlen(tl1s));
			free(tl1s);
		}	
	}
	else {
		cmd_Disp("a script is still running");
		free(tl1s);
	}
}
void script_Open( char *fn )
{
	struct _stat sb;
	if ( stat_utf8(fn, &sb)==-1 ) {
		char *tl1s = (char *)malloc(sb.st_size+1);
		if ( tl1s!=NULL ) {
			FILE *fpScript = fopen_utf8( fn, MODE_RB );
			if ( fpScript != NULL ) {
				int lsize = fread( tl1s, 1, sb.st_size, fpScript );
				fclose( fpScript );
				if ( lsize > 0 ) {
					tl1s[lsize]=0;
					script_Drop(tl1s);
				}
			}
		}
	}
	else
		cmd_Disp("couldn't find script file");
}