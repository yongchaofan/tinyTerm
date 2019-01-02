//
// "$Id: tiny.c 32978 2019-01-01 22:15:10 $"
//
// tinyTerm -- A minimal serail/telnet/ssh/sftp terminal emulator
//
// tiny.c is the GUI implementation using WIN32 API.
//
// Copyright 2018-2019 by Yongchao Fan.
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
#include <windows.h>
#include <windowsx.h>
#include <shlobj.h>
#include "tiny.h"
#include "res/resource.h"

extern char buff[], attr[];
extern int line[];
extern int cursor_y, cursor_x;
extern int size_y, size_x;
extern int screen_y;
extern int scroll_y;
extern int sel_left, sel_right;
extern BOOL bCursor, bLogging;
int iTitleHeight;
#define iCmdHeight		18
#define CONN_TIMER		64
#define ID_SCRIPT0		1000
#define ID_CONNECT0		2000

const char WELCOME[]="\n\n\n\
\ttinyTerm is an open source terminal emulator designed to be\n\n\
\tsimple, small and scriptable, a single exe in 360KB that features:\n\n\n\
\t    * Serial/Telnet/SSH/SFTP/Netconf client\n\n\
\t    * Command history and autocompletion\n\n\
\t    * Drag and Drop to run command batches\n\n\
\t    * Scripting interface xmlhttp://127.0.0.1:%d\n\n\n\
\tVersion 1.0 ©2018-2019 Yongchao Fan, All rights reserved\n\n\
\thttps://yongchaofan.github.io/tinyTerm\n\n\n";

const COLORREF COLORS[9] ={ RGB(0,0,0), 	RGB(224,0,0), RGB(0,224,0),
							RGB(224,224,0), RGB(0,0,224), RGB(224,0,224),
							RGB(0,244,244), RGB(224,224,224), RGB(0, 64,0) };
static HINSTANCE hInst;
static RECT termRect, wndRect;
static HWND hwndMain, hwndCmd;
static HDC hDC, hBufDC=NULL;
static HBITMAP hBufMap=NULL;
static HFONT hTermFont, hEditFont;
static HBRUSH dwBkBrush, dwScrollBrush, dwSliderBrush;
static HMENU hMenu[3], hTermMenu, hScriptMenu, hOptionMenu, hContextMenu;
static int menuX[4] = {32, 0, 0, 0};	//menuX[0]=32 leave space for icon

static WCHAR wsFontFace[256]=L"Consolas";
static int iFontHeight, iFontWidth;
static int iTransparency = 255;
static int iConnectCount, iScriptCount, httport;
static BOOL bScrollbar = FALSE;
static BOOL bFocus = FALSE;
static BOOL bFTPd = FALSE, bTFTPd = FALSE;
static BOOL bScriptRun=FALSE, bScriptPause=FALSE;

void tiny_TermSize(char *size);
void tiny_FontFace(char *fontface);
void tiny_FontSize(int fontsize);
void tiny_Transparency(int t);
void CopyText( );
void PasteText( );
void LoadDict( WCHAR *wfn );
void SaveDict( WCHAR *wfn );
void OpenScript( WCHAR *wfn );
void DropScript( char *tl1s );
void DropFiles( HDROP hDrop );

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

WCHAR *fileDialog( WCHAR *szFilter, DWORD dwFlags )
{
	static WCHAR wname[MAX_PATH];
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
	if ( ret )
		return wname;
	else
		return NULL;
}
char *getFolderName(WCHAR *wtitle)
{
	static BROWSEINFO bi;
	static char szFolder[MAX_PATH];
	WCHAR szDispName[MAX_PATH];
	LPITEMIDLIST pidl;

	WCHAR wfolder[MAX_PATH];
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
BOOL fontDialog()
{
	static LOGFONT lf;
	CHOOSEFONT cf;
	ZeroMemory(&cf, sizeof(cf));
	cf.lStructSize = sizeof (cf);
	cf.hwndOwner = hwndMain;
	cf.lpLogFont = &lf;
	cf.Flags = CF_SCREENFONTS|CF_FIXEDPITCHONLY;

	BOOL rc = ChooseFont(&cf);
	if ( rc ) {
		DeleteObject(hTermFont);
		hTermFont = CreateFontIndirect(cf.lpLogFont);
	}
	return rc;
}
int tiny_Cmd( char *cmd, char **preply )
{
	int rc = 0;
	if ( preply ) *preply = buff+cursor_x;
	if ( *cmd=='!' ) {
		cmd_Disp_utf8(cmd++);
		if (strncmp(cmd,"Transparency",12)==0)	tiny_Transparency(atoi(cmd+13));
		else if ( strncmp(cmd, "FontSize", 8)==0 )	tiny_FontSize(atoi(cmd+9));
		else if ( strncmp(cmd, "FontFace", 8)==0 )	tiny_FontFace(cmd+9);
		else if ( strncmp(cmd, "TermSize", 8)==0 )	tiny_TermSize(cmd+9);
		else if ( strncmp(cmd, "Tftpd",5)==0 )		tftp_Svr( cmd+6 );
		else if ( strncmp(cmd, "Ftpd", 4)==0 ) 		ftp_Svr( cmd+5 );
		else if ( strncmp(cmd, "scp ", 4)==0 ) rc = scp_cmd(cmd+4, preply);
		else if ( strncmp(cmd, "tun",  3)==0 ) rc = tun_cmd(cmd+3, preply);
		else {
			rc = term_Cmd(cmd, preply);
			if ( rc==-1 ) host_Open(cmd);
		}
	}
	else {
		if ( host_Status()!=CONN_IDLE )
			rc = term_TL1(cmd, preply);
		else {
			term_Disp(cmd);
			term_Disp("\n");
		}
	}
	return rc;
}
DWORD WINAPI scper(void *pv)
{
	scp_cmd((char *)pv, NULL);
	free(pv);
	return 0;
}
DWORD WINAPI tuner(void *pv)
{
	tun_cmd((char *)pv, NULL);
	free(pv);
	return 0;
}
void cmd_Disp( WCHAR *wbuf )
{
	SetWindowText(hwndCmd, wbuf);
	PostMessage(hwndCmd, EM_SETSEL, 0, -1);
}
void cmd_Disp_utf8( char *buf )
{
	WCHAR wbuf[1024];
	utf8_to_wchar(buf, strlen(buf)+1, wbuf, 1024);
	cmd_Disp(wbuf);
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
		case '/': term_Srch(cmd+1); break;
		case '^': cmd[1]-=64; host_Send(cmd+1, 1); break;
		case '!': if ( strncmp(cmd+1, "scp ", 4)==0 && host_Type()==SSH )
					CreateThread(NULL, 0, scper, strdup(cmd+5), 0, NULL);
				  else if ( strncmp(cmd+1, "tun",  3)==0 && host_Type()==SSH )
					CreateThread(NULL, 0, tuner, strdup(cmd+4), 0, NULL);
				  else
					tiny_Cmd(cmd, NULL);
				  break;
		default:  cmd[cch]='\r'; host_Send(cmd, cch+1);
		}
	}
}
WNDPROC wpOrigCmdProc;
LRESULT APIENTRY CmdEditProc(HWND hwnd, UINT uMsg,
								WPARAM wParam, LPARAM lParam)
{
	if ( uMsg==WM_KEYDOWN ){
		if ( (GetKeyState(VK_CONTROL)&0x8000) == 0) {
			switch ( wParam ) {
			case VK_UP: cmd_Disp(autocomplete_Prev()); break;
			case VK_DOWN: cmd_Disp(autocomplete_Next()); break;
			case VK_RETURN: cmd_Enter(); break;
			}
		}
	}
	return CallWindowProc(wpOrigCmdProc, hwnd, uMsg, wParam, lParam);
}
void check_Option( HMENU hMenu, DWORD id, BOOL op)
{
	CheckMenuItem( hMenu, id, MF_BYCOMMAND|(op?MF_CHECKED:MF_UNCHECKED));
}
WCHAR Title[256] = L"   Term      Script      Options               ";
void tiny_Title( char *buf )
{
	utf8_to_wchar(buf, strlen(buf)+1, Title+47, 208);
	Title[255] = 0;
	SetWindowText(hwndMain, Title);

	if ( *buf ) {
		host_Size(size_x, size_y);
		ModifyMenu( hTermMenu, ID_CONNECT, MF_BYCOMMAND,
					ID_DISCONN, L"&Disconnect\tAlt+D");
	}
	else {
		term_Disp("\n\033[31mDisconnected\n\033[37m");
		ModifyMenu(hTermMenu, ID_DISCONN, MF_BYCOMMAND,
					ID_CONNECT, L"&Connect...\tAlt+C");
	}
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
void tiny_Font( HWND hwnd )
{
	HDC hdc;
    TEXTMETRIC tm;
	hdc = GetDC(hwnd);
	SelectObject(hdc, hTermFont);
	GetTextMetrics(hdc, &tm);
	ReleaseDC(hwnd, hdc);
	iFontHeight = tm.tmHeight;
	iFontWidth = tm.tmAveCharWidth;
	GetWindowRect( hwnd, &wndRect );
	int x = wndRect.left;
	int y = wndRect.top;
	wndRect.right = x + iFontWidth*size_x;
	wndRect.bottom = y + iFontHeight*size_y + iCmdHeight+1;
	AdjustWindowRect(&wndRect, WS_TILEDWINDOW, FALSE);
	MoveWindow( hwnd, x, y, wndRect.right-wndRect.left,
							wndRect.bottom-wndRect.top, TRUE );
}
void tiny_TermSize(char *size)
{
	char *p = strchr(size, 'x');
	if ( p!=NULL ) {
		size_x = atoi(size);
		size_y = atoi(p+1);
		tiny_Font(hwndMain);
	}
}
void tiny_FontFace(char *fontface)
{
	utf8_to_wchar(fontface, strlen(fontface)+1, wsFontFace, 255);
	wsFontFace[255] = 0;
}
void tiny_FontSize(int fontsize)
{
	DeleteObject(hTermFont);
	hTermFont = CreateFont(fontsize,0,0,0,FW_MEDIUM, FALSE,FALSE,FALSE,
						DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
						DEFAULT_QUALITY, FIXED_PITCH, wsFontFace);
	tiny_Font(hwndMain);
}
void tiny_Transparency(int t)
{
	iTransparency = t;
	SetLayeredWindowAttributes(hwndMain,0,iTransparency,LWA_ALPHA);
	check_Option( hOptionMenu, ID_TRANSPARENT, (iTransparency&0xff)!=255 );
}
void tiny_Paint(HDC hDC)
{
	WCHAR wbuf[1024];
	RECT text_rect = {0, 0, 0, 0};
	BitBlt(hBufDC,0,0, termRect.right, termRect.bottom, NULL,0,0, BLACKNESS);
	int y = screen_y+scroll_y; if ( y<0 ) y = 0;
	int sel_min = min(sel_left, sel_right);
	int sel_max = max(sel_left, sel_right);
	int dx=1, dy=0;
	for ( int l=0; l<size_y; l++ ) {
		dx = 1;
		int i = line[y+l];
		while ( i<line[y+l+1] ) {
			BOOL utf = FALSE;
			int j = i;
			while ( attr[j]==attr[i] ) {
				if ( (buff[j]&0xc0)==0xc0 ) utf = TRUE;
				if ( ++j==line[y+l+1] ) break;
				if ( j==sel_min || j==sel_max ) break;
			}
			if ( i>=sel_min&&i<sel_max ) {
				SetTextColor( hBufDC, COLORS[0] );
				SetBkColor( hBufDC, COLORS[7] );
			}
			else {
				SetTextColor( hBufDC, COLORS[attr[i]&7] );
				SetBkColor( hBufDC, COLORS[(attr[i]>>4)&7] );
			}
			int len = j-i;
			if ( buff[j-1]==0x0a ) len--;	//remove unprintable 0x0a for XP
			if ( utf ) {
				int cch = utf8_to_wchar(buff+i, len, wbuf, 1024);
				TextOutW(hBufDC, dx, dy, wbuf, cch);
				DrawText(hBufDC, wbuf, cch, &text_rect, DT_CALCRECT);
				dx += text_rect.right;
			}
			else {
				TextOutA(hBufDC, dx, dy, buff+i, len);
				dx += iFontWidth*len;
			}
			i=j;
		}
		dy += iFontHeight;
	}
	if ( scroll_y ) {
		int slider_y = termRect.bottom*(cursor_y+scroll_y)/cursor_y;
		SelectObject(hBufDC,dwScrollBrush);
		Rectangle(hBufDC, termRect.right-11, 0, termRect.right,
												termRect.bottom);
		SelectObject(hBufDC,dwSliderBrush);
		Rectangle(hBufDC, termRect.right-11, slider_y-8,termRect.right,
														slider_y+7 );
	}
	BitBlt(hDC,0,0,termRect.right,termRect.bottom, hBufDC,0,0,SRCCOPY);
	if ( bFocus ) {
		bCursor ? ShowCaret(hwndMain) : HideCaret(hwndMain);
		int cch = utf8_to_wchar(buff+line[cursor_y],
								cursor_x-line[cursor_y], wbuf, 1024);
		DrawText(hBufDC, wbuf, cch, &text_rect, DT_CALCRECT);
		SetCaretPos( text_rect.right+1,
					(cursor_y-screen_y+1)*iFontHeight-iFontHeight/4);
	}
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
		SetFocus(hwndHost);
		break;
	case WM_COMMAND:
		if ( HIWORD(wParam)==CBN_SELCHANGE ) {
			if ( (HWND)lParam==hwndProto ) {
				int new_proto = ComboBox_GetCurSel(hwndProto);
				if ( proto!=0 && new_proto==0 ) {
					ComboBox_ResetContent(hwndHost);
					ComboBox_ResetContent(hwndPort);
					for ( int i=1; i<32; i++ ) {
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
			int proto;
			switch ( LOWORD(wParam) ) {
			case IDCONNECT:
					proto = ComboBox_GetCurSel(hwndProto);
					conn[0]=L'!';
					if ( proto==0 ) {
						ComboBox_GetText(hwndPort, conn+1, 127);
						wcscat(conn, L":");
						ComboBox_GetText(hwndHost, conn+wcslen(conn), 127);
					}
					else {
						ComboBox_GetText(hwndProto, conn+1, 127);
						wcscat(conn, L" ");
						int len = wcslen(conn);
						ComboBox_GetText(hwndHost, conn+wcslen(conn), 127);
						int i;
						for ( i=0; i<host_cnt; i++ )
							if ( wcscmp(conn+len,HOSTS[i])==0 ) break;
						if ( i==host_cnt && host_cnt<32 ) {
							HOSTS[host_cnt++] = _wcsdup(conn+len);
							ComboBox_AddString(hwndHost,HOSTS[++i]);
						}
						len = wcslen(conn);
						wcscat(conn, L":");
						ComboBox_GetText(hwndPort, conn+wcslen(conn), 128);
						if ( wcscmp(conn+len+1, PORTS[proto])==0 ) conn[len]=0;
					}
					cmd_Disp(conn);
					cmd_Enter();
			case IDCANCELC:
					EndDialog(hwndDlg, wParam);
					return TRUE;
			}
		}
	}
	return FALSE;
}
BOOL menu_Command( WPARAM wParam )
{
	switch ( LOWORD(wParam) ) {
	case ID_ABOUT:{
			char welcome[1024];
			sprintf(welcome, WELCOME, httport);
			term_Clear();
			term_Disp(welcome);
		}
		break;
	case ID_CONNECT:
		if ( host_Status()==CONN_IDLE )	DialogBox(hInst,
				MAKEINTRESOURCE(IDD_CONNECT), hwndMain, (DLGPROC)ConnectProc);
		break;
	case ID_DISCONN:
		if ( host_Status()!=CONN_IDLE ) host_Close();
		break;
	case ID_EDITOR:
		SetFocus(GetFocus()==hwndCmd ? hwndMain : hwndCmd);
		break;
	case ID_PASTE:
		PasteText();
		break;
	case ID_COPYALL:
		sel_left=0; sel_right=cursor_x;
		CopyText(); tiny_Redraw();
		break;
	case ID_LOGGING:
		if ( !bLogging ) {
			WCHAR *wfn = fileDialog(L"logfile\0*.log\0All\0*.*\0\0",
				OFN_PATHMUSTEXIST|OFN_NOREADONLYRETURN|OFN_OVERWRITEPROMPT);
			if ( wfn!=NULL ) {
				char fn[MAX_PATH];
				wchar_to_utf8(wfn, wcslen(wfn)+1, fn, MAX_PATH);
				term_Logg( fn );
			}
		}
		else
			term_Logg( NULL );
		check_Option( hTermMenu, ID_LOGGING, bLogging );
		break;
	case ID_ECHO:
		check_Option( hTermMenu, ID_ECHO, term_Echo() );
		break;
	case ID_FONT:
		if ( fontDialog() ) tiny_Font( hwndMain );
		break;
	case ID_TRANSPARENT:
		tiny_Transparency( iTransparency==255 ? 224 : 255 );
		break;
	case ID_FTPD:
		bFTPd = ftp_Svr(bFTPd?NULL:getFolderName(L"Choose root directory"));
		check_Option( hOptionMenu, ID_FTPD, bFTPd );
		break;
	case ID_TFTPD:
		bTFTPd = tftp_Svr(bTFTPd?NULL:getFolderName(L"Choose root directory"));
		check_Option( hOptionMenu, ID_TFTPD, bTFTPd );
		break;
	case ID_TERM:
		TrackPopupMenu(hMenu[0],TPM_LEFTBUTTON, wndRect.left+menuX[0],
								wndRect.top+iTitleHeight, 0, hwndMain, NULL);
		break;
	case ID_SCRIPT:
		TrackPopupMenu(hMenu[1],TPM_LEFTBUTTON, wndRect.left+menuX[1],
								wndRect.top+iTitleHeight, 0, hwndMain, NULL);
		break;
	case ID_OPTIONS:
		TrackPopupMenu(hMenu[2],TPM_LEFTBUTTON, wndRect.left+menuX[2],
								wndRect.top+iTitleHeight, 0, hwndMain, NULL);
		break;
	case ID_SCPAUSE:
		if ( bScriptRun ) bScriptPause = !bScriptPause;
		if ( bScriptPause ) cmd_Disp( L"Script Paused" );
		break;
	case ID_SCQUIT:
		bScriptRun = FALSE;
		break;
	case ID_SCRUN: {
		WCHAR *wfn = fileDialog( L"scripts\0*.js;*.vbs;*.txt\0All\0*.*\0\0",
								OFN_PATHMUSTEXIST|OFN_FILEMUSTEXIST);
		if ( wfn!=NULL ) OpenScript( wfn );
		}
		break;
	default:
		if ( wParam>ID_SCRIPT0 && wParam<=ID_SCRIPT0+iScriptCount ) {
			WCHAR wfn[256];
			wcscpy(wfn, L"script\\");
			GetMenuString(hScriptMenu, wParam, wfn+7, 248, 0);
			OpenScript(wfn);
			break;
		}
		if ( wParam>ID_CONNECT0 && wParam<=ID_CONNECT0+iConnectCount ) {
			WCHAR cmd[256];
			cmd[0]=L'!';
			GetMenuString(hTermMenu, wParam, cmd+1, 248, 0);
			cmd_Disp(cmd);
			cmd_Enter();
			break;
		}
		return FALSE;
	}
	return TRUE;
}
LRESULT CALLBACK MainWndProc(HWND hwnd,UINT msg,WPARAM wParam,LPARAM lParam)
{
	PAINTSTRUCT ps;
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
		SetLayeredWindowAttributes(hwnd,0,255,LWA_ALPHA);
		DragAcceptFiles( hwnd, TRUE );
		GetClientRect( hwnd, &termRect );
		termRect.bottom -= iCmdHeight;
		tiny_Font( hwnd );
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
		if ( hBufDC!=NULL ) DeleteDC(hBufDC);
		if ( hBufMap!=NULL )DeleteObject(hBufMap);
		hDC = GetDC(hwnd);
		hBufDC = CreateCompatibleDC(hDC);
		hBufMap = CreateCompatibleBitmap(hDC, termRect.right, termRect.bottom);
		SelectObject(hBufDC, hBufMap);
		SelectObject(hBufDC, hTermFont);
		SetBkMode(hBufDC, OPAQUE);
		ReleaseDC(hwnd, hDC);
		tiny_Redraw( );
	case WM_MOVE:
		GetWindowRect( hwnd, &wndRect );
		break;
	case WM_PAINT:
		hDC = BeginPaint(hwnd, &ps);
		if ( hDC!=NULL ) tiny_Paint(hDC);
		EndPaint( hwnd, &ps );
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
		term_Keydown(wParam);
		break;
	case WM_TIMER:
		if ( host_Status()==CONN_CONNECTING )
			term_Disp(".");
		else
			KillTimer(hwndMain, CONN_TIMER);
		break;
	case WM_MOUSEWHEEL:
		term_Scroll( GET_WHEEL_DELTA_WPARAM(wParam)/40 );
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
		tiny_Redraw();
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
		if ( MK_LBUTTON&wParam ) {
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
			sel_left = sel_min;
			sel_right = sel_max;
			CopyText();
		}
		else {
			sel_left = sel_right = 0;
			tiny_Redraw( );
		}
		ReleaseCapture();
		bScrollbar = FALSE;
		SetFocus( hwnd );
		break;
	case WM_MBUTTONUP:
		if ( sel_right>sel_left )
			term_Send(buff+sel_left, sel_right-sel_left);
		break;
	case WM_RBUTTONUP:
		TrackPopupMenu(hContextMenu,TPM_RIGHTBUTTON,
					GET_X_LPARAM(lParam)+wndRect.left,
					GET_Y_LPARAM(lParam)+wndRect.top, 0, hwnd, NULL);
		break;
	case WM_NCLBUTTONDOWN: {
		int x = GET_X_LPARAM(lParam)-wndRect.left;
		int y = GET_Y_LPARAM(lParam)-wndRect.top;
		if ( y>0 && y<iTitleHeight ) for ( int i=0; i<3; i++ )
			if ( x>menuX[i] && x<menuX[i+1] )
				TrackPopupMenu( hMenu[i],TPM_LEFTBUTTON, wndRect.left+menuX[i],
									wndRect.top+iTitleHeight, 0, hwnd, NULL );
		}
		return DefWindowProc(hwnd,msg,wParam,lParam);
	case WM_DROPFILES:
		DropFiles((HDROP)wParam);
		break;
	case WM_CTLCOLOREDIT:
		SetTextColor( (HDC)wParam, COLORS[3] );
		SetBkColor( (HDC)wParam, COLORS[8] );
		return (LRESULT)dwBkBrush;		//must return brush for update
	case WM_ERASEBKGND:
		return 1;
	case WM_CLOSE:
		if ( host_Status()!=CONN_IDLE ) {
			if ( MessageBox(hwnd, L"Disconnect and quit?",
							L"tinyTerm", MB_YESNO)==IDNO ) break;
			host_Close();
		}
		DestroyWindow(hwnd);
		break;
	case WM_DESTROY:
		DragAcceptFiles(hwnd, FALSE);
		DeleteDC(hBufDC);
		DeleteObject(hBufMap);
		DeleteObject(dwBkBrush);
		DeleteObject(dwScrollBrush);
		DeleteObject(dwSliderBrush);
		PostQuitMessage(0);
		break;
	case WM_COMMAND:
	case WM_SYSCOMMAND:
		if ( menu_Command( wParam ) ) return 1;
	default: return DefWindowProc(hwnd,msg,wParam,lParam);
	}
	return 0;
}
void tiny_Menu( HWND hwnd )
{
	iTitleHeight = 	 GetSystemMetrics(SM_CYFRAME)
					+GetSystemMetrics(SM_CYCAPTION)
					+GetSystemMetrics(SM_CXPADDEDBORDER);
	HDC wndDC = GetWindowDC(hwnd);
	RECT menuRect;
	DrawText(wndDC, L"  Term  ", 8, &menuRect, DT_CALCRECT);
	menuX[1] = menuX[0]+menuRect.right-menuRect.left;
	DrawText(wndDC, L"  Script  ", 10, &menuRect, DT_CALCRECT);
	menuX[2] = menuX[1]+menuRect.right-menuRect.left;
	DrawText(wndDC, L"  Options  ", 11, &menuRect, DT_CALCRECT);
	menuX[3] = menuX[2]+menuRect.right-menuRect.left;
	ReleaseDC(hwnd, wndDC);

	HMENU hMainMenu = LoadMenu( hInst, MAKEINTRESOURCE(IDMENU_MAIN));
	hMenu[0] = hTermMenu  = GetSubMenu(hMainMenu, 0);
	hMenu[1] = hScriptMenu = GetSubMenu(hMainMenu, 1);
	hMenu[2] = hOptionMenu = GetSubMenu(hMainMenu, 2);
	hContextMenu = CreatePopupMenu();
	InsertMenu(hContextMenu, 0, MF_BYPOSITION|MF_POPUP,
							(UINT_PTR)hScriptMenu, 		L"Script");
	InsertMenu(hContextMenu, 0, MF_BYPOSITION, ID_COPYALL, L"Copy All");
	InsertMenu(hContextMenu, 0, MF_BYPOSITION, ID_PASTE,L"Paste");

	WIN32_FIND_DATA FindFileData;
	HANDLE hFind;
	iScriptCount = 1;
	hFind = FindFirstFile(L"script\\*.*", &FindFileData);
	if (hFind != INVALID_HANDLE_VALUE) {
		do {
			if ( FindFileData.cFileName[0]!=L'.' )
				InsertMenu( hScriptMenu, -1, MF_BYPOSITION,
							ID_SCRIPT0+iScriptCount++, FindFileData.cFileName);
		} while( FindNextFile(hFind, &FindFileData) );
	}

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

	dwBkBrush = CreateSolidBrush( COLORS[8] );
	dwScrollBrush = CreateSolidBrush( RGB(64,64,64) );
	dwSliderBrush = CreateSolidBrush( COLORS[1] );
	hEditFont = CreateFont( 18,0,0,0,FW_MEDIUM, FALSE,FALSE,FALSE,
						DEFAULT_CHARSET,OUT_TT_PRECIS,CLIP_DEFAULT_PRECIS,
						DEFAULT_QUALITY, VARIABLE_PITCH,L"Arial");
	hTermFont = CreateFont( 18,0,0,0,FW_MEDIUM, FALSE,FALSE,FALSE,
						DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
						DEFAULT_QUALITY, FIXED_PITCH, wsFontFace);
	hwndMain = CreateWindowEx( WS_EX_LAYERED, L"TWnd", Title,
						WS_TILEDWINDOW|WS_VISIBLE, CW_USEDEFAULT, CW_USEDEFAULT,
						800, 480, NULL, NULL, hInst, NULL );
	tiny_Menu(hwndMain);
	iConnectCount = 1;
	LoadDict(L"tinyTerm.hist");

	if ( *lpCmdLine==0 )
		PostMessage(hwndMain, WM_SYSCOMMAND, ID_CONNECT, 0);
	else
		host_Open( lpCmdLine );

	MSG msg;
	HACCEL haccel = LoadAccelerators(hInst, MAKEINTRESOURCE(IDACCEL_MAIN));
	while ( GetMessage(&msg, NULL, 0, 0) ){
		if (!TranslateAccelerator(hwndMain, haccel,  &msg)) {
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}
	}
	if ( bLogging ) term_Logg( NULL );
	SaveDict(L"tinyTerm.hist");

	host_Destory();
	drop_Destroy( hwndCmd );
	autocomplete_Destroy( );

	OleUninitialize();
	WSACleanup();

	return 0;
}
void CopyText( )
{
	if ( sel_left!=sel_right ) {
		char *ptr = buff+sel_left;
		int len = sel_right-sel_left;
		if ( OpenClipboard(hwndMain) ) {
			EmptyClipboard( );
			HANDLE hglbCopy = GlobalAlloc(GMEM_MOVEABLE, (len+1)*2);
			   if ( hglbCopy != NULL ) {
				WCHAR *wbuf = GlobalLock(hglbCopy);
				len = utf8_to_wchar( ptr, len, wbuf, len );
				wbuf[len] = 0;
				GlobalUnlock(hglbCopy);
				SetClipboardData(CF_UNICODETEXT, hglbCopy);
			}
			CloseClipboard( );
		}
	}
}
void PasteText( )
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

void LoadDict( WCHAR *wfn )
{
	FILE *fp = _wfopen(wfn, L"r");
	if ( fp!=NULL ) {
		char cmd[256];
		while ( fgets( cmd, 256, fp )!=NULL ) {
			int l = strlen(cmd)-1;
			while ( l>0 && cmd[l]<0x10 ) cmd[l--]=0;
			WCHAR wcmd[1024];
			utf8_to_wchar(cmd, strlen(cmd)+1, wcmd, 1024);
			autocomplete_Add(wcmd) ;
			if ( *cmd=='!' ) {
				if ( strncmp(cmd+1, "com", 3)==0 ||
					 strncmp(cmd+1, "ssh", 3)==0 || 
					 strncmp(cmd+1, "telnet",6)==0 )
					InsertMenu( hTermMenu, -1, MF_BYPOSITION,
								ID_CONNECT0+iConnectCount++, wcmd+1);
			}
		}
		fclose( fp );
	}
}
void SaveDict( WCHAR *wfn )
{
	FILE *fp = _wfopen(wfn, L"w");
	if ( fp!=NULL ) {
		WCHAR *wp = autocomplete_First();
		while ( *wp!=0 ) {
			char cmd[256];
			wchar_to_utf8(wp, wcslen(wp)+1, cmd, 255);
			cmd[255] = 0;
			fprintf(fp, "%s\n", cmd);
			wp = autocomplete_Next();
		}
		fclose( fp );
	}
}

DWORD WINAPI uploader( void *files )	//upload files through scp or sftp
{
	for ( char *q=files; *q; q++ ) if ( *q=='\\' ) *q='/';
	char rdir[1024];
	scp_pwd(rdir);
	strcat(rdir, "/");

	char *p=(char *)files, *p1;
	do {
		p1 = strchr(p, 0x0a);
		if ( p1!=NULL ) *p1++=0;
		if ( host_Type()==SSH )
			scp_write(p, rdir);
		else if ( host_Type()==SFTP ) {
			sftp_put(p, rdir);
		}
	}
	while ( (p=p1)!=NULL );
	host_Send("\r", 1);
	free((char *)files);
	return 0;
}
void DropFiles(HDROP hDrop)
{
	WCHAR wname[MAX_PATH];
	int n = DragQueryFile( hDrop, -1, NULL, 0 );
	char *files = (char*)malloc(n*1024);
	if ( files==NULL ) return;
	files[0] = 0;
	for ( int i=0; i<n; i++ ) {
		DragQueryFile( hDrop, i, wname, MAX_PATH );
		if ( host_Type()!=SSH && host_Type()!=SFTP ) {
			OpenScript(wname); 
			return;
		}
		else {
			char fn[1024];
			wchar_to_utf8(wname, wcslen(wname)+1, fn, 1023);
			fn[1023]=0;
			strcat(files, fn);
			strcat(files, "\n");
		}
	}
	DragFinish( hDrop );
	CreateThread(NULL,0,(LPTHREAD_START_ROUTINE)uploader,(void *)files,0,NULL);
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

		if ( p1>p0 ) {
			cmd_Disp_utf8(p0);
			if ( strncmp( p0, "!Wait ", 6)==0 ) Sleep(atoi(p0+6)*1000);
			else if ( strncmp( p0, "!Loop ", 6)==0 ){
				if ( iRepCount<0 ) iRepCount = atoi( p0+6 );
				if ( --iRepCount>0 ) {
					*p1=0x0a;
					p0 = p1 = p2 = (char*)cmds;
				}
			}
			else {
				if ( p1[-1]==0x0d ) p1[-1] = 0;
				tiny_Cmd( p0, NULL );
				if ( p1[-1]==0 ) p1[-1] = 0x0d;
			}
		}
		if ( p0!=p1 ) { p0 = p1+1; *p1 = 0x0a; }
		while ( bScriptPause && bScriptRun ) Sleep(1000);
	}
	while ( p2!=NULL && bScriptRun );
	cmd_Disp( bScriptRun ? L"script completed" : L"script stopped");
	bScriptRun = bScriptPause = FALSE;
	free(cmds);
	return 0;
}
void DropScript( char *cmds )
{
	if ( host_Type()==NETCONF ) {
		netconf_Send(cmds, strlen(cmds));
		free(cmds);
	}
	else {
		if ( bScriptRun ) {
			cmd_Disp(L"a script is still running");
			free(cmds);
		}
		else 
			CreateThread( NULL, 0, scripter, (void *)cmds, 0, NULL);
	}
}
void OpenScript( WCHAR *wfn )
{
	cmd_Disp(wfn);
	int len = wcslen(wfn);
	if ( wcscmp(wfn+len-3, L".js")==0 || wcscmp(wfn+len-4, L".vbs")==0 ) {
		WCHAR port[256];
		wsprintf(port, L"%d", httport);
		_wspawnlp( _P_NOWAIT, L"WScript.exe", L"WScript.exe", wfn, port, NULL );
		return;
	}

	struct _stat sb;
	if ( _wstat(wfn, &sb)==0 ) {
		char *tl1s = (char *)malloc(sb.st_size+1);
		if ( tl1s!=NULL ) {
			FILE *fpScript = _wfopen( wfn, L"rb" );
			if ( fpScript != NULL ) {
				int lsize = fread( tl1s, 1, sb.st_size, fpScript );
				fclose( fpScript );
				if ( lsize > 0 ) {
					tl1s[lsize]=0;
					if ( host_Status()==CONN_CONNECTED )
						DropScript(tl1s);
					else {
						term_Disp(tl1s);
						free(tl1s);
					}
				}
			}
			else
				cmd_Disp(L"Couldn't open file");
		}
	}
}