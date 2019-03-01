//
// "$Id: tiny.c 41682 2019-02-28 14:35:10 $"
//
// tinyTerm -- A minimal serail/telnet/ssh/sftp terminal emulator
//
// tiny.c is the GUI implementation using WIN32 API.
//
// Copyright 2018-2019 by Yongchao Fan.
//
// This library is free software distributed under GNU GPL 3.0,
// see the license at:
//
//     https://github.com/yongchaofan/tinyTerm/blob/master/LICENSE
//
// Please report all bugs and problems on the following page:
//
//     https://github.com/yongchaofan/tinyTerm/issues/new
//
#include "res/resource.h"
#include "tiny.h"
#include <windows.h>
#include <windowsx.h>
#include <shlobj.h>
#include <fcntl.h>
#include <time.h>
#include <versionhelpers.h>

TERM *pt;
int iTitleHeight;
#define HOST_TIMER		64
#define ID_SCRIPT0		1000
#define ID_CONNECT0		2000

const char WELCOME[]="\n\n\n\
\ttinyTerm is an open source terminal emulator designed to be\n\n\
\tsimple, small and scriptable, release 1.0 features:\n\n\n\
\t    * Single exe, no dll, no installation\n\n\
\t    * Serial/Telnet/SSH/SFTP/Netconf client\n\n\
\t    * Command history and autocompletion\n\n\
\t    * Task automation via list of commands\n\n\
\t    * Scripting interface xmlhttp://127.0.0.1:%d\n\n\n\
\t©2018-2019 Yongchao Fan, All rights reserved\n\n\
\thttps://yongchaofan.github.io/tinyTerm\n\n\n";

const COLORREF COLORS[16] ={RGB(0,0,0), 	RGB(192,0,0), RGB(0,192,0),
							RGB(192,192,0), RGB(32,96,240), RGB(192,0,192),
							RGB(0,192,192), RGB(192,192,192),
							RGB(0,0,0), 	RGB(240,0,0), RGB(0,240,0),
							RGB(240,240,0), RGB(32,96,240), RGB(240,0,240),
							RGB(0,240,240), RGB(240,240,240) };
static HINSTANCE hInst;
static RECT termRect, wndRect;
static HWND hwndMain, hwndCmd;
static HDC hDC, hBufDC=NULL;
static HBITMAP hBufMap=NULL;
static HFONT hTermFont;
static HBRUSH dwBkBrush, dwScrollBrush, dwSliderBrush;
static HMENU hMenu[3], hTermMenu, hScriptMenu, hOptionMenu, hContextMenu;
static int menuX[4] = {32, 0, 0, 0};	//menuX[0]=32 leave space for icon

static int iFontHeight, iFontWidth;
static int iTransparency = 255;
static int iScriptDelay = 250;
static int iConnectCount, iScriptCount, httport;
static BOOL bScrollbar = FALSE, bFocus = FALSE, bEdit = FALSE;
static BOOL bFTPd = FALSE, bTFTPd = FALSE;
static BOOL bScriptRun=FALSE, bScriptPause=FALSE;

void tiny_TermSize(char *size);
void tiny_FontFace(char *face);
void tiny_FontSize(int i);
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
FILE * fopen_utf8(const char *fn, const char *mode)
{
	WCHAR wfn[MAX_PATH], wmode[4];
	utf8_to_wchar(fn, strlen(fn)+1, wfn, MAX_PATH);
	utf8_to_wchar(mode, strlen(mode)+1, wmode, 4);
	return _wfopen(wfn, wmode);
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
	OPENFILENAME ofn;

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
	LOGFONT lf;
	if ( GetObject(hTermFont, sizeof(lf), &lf)==0 )
		ZeroMemory(&lf, sizeof(lf));
	CHOOSEFONT cf;
	ZeroMemory(&cf, sizeof(cf));
	cf.lStructSize = sizeof (cf);
	cf.hwndOwner = hwndMain;
	cf.lpLogFont = &lf;
	cf.Flags = CF_SCREENFONTS|CF_FIXEDPITCHONLY|CF_INITTOLOGFONTSTRUCT;

	BOOL rc = ChooseFont(&cf);
	if ( rc ) {
		DeleteObject(hTermFont);
		hTermFont = CreateFontIndirect(cf.lpLogFont);
	}
	return rc;
}
void menu_Add(char *cmd, WCHAR *wcmd)
{
	if ( strnicmp(cmd,"com", 3)==0 ||
		 strncmp(cmd, "ssh", 3)==0 || 
		 strncmp(cmd, "sftp", 4)==0 || 
		 strncmp(cmd, "telnet",6)==0 || 
		 strncmp(cmd, "netconf",7)==0 )
		AppendMenu( hTermMenu, 0, ID_CONNECT0+iConnectCount++, wcmd);
}

int tiny_Cmd( char *cmd, char **preply )
{
	int rc = 0;
	if ( preply ) *preply = pt->buff+pt->cursor_x;
	cmd_Disp_utf8(cmd);
	if (strncmp(cmd,"!Transparency",13)==0)		tiny_Transparency(atoi(cmd+13));
	else if ( strncmp(cmd,"!ScriptDelay",12)==0)iScriptDelay = atoi(cmd+12);
	else if ( strncmp(cmd, "!FontSize", 9)==0 )	tiny_FontSize(atoi(cmd+9));
	else if ( strncmp(cmd, "!FontFace", 9)==0 )	tiny_FontFace(cmd+9);
	else if ( strncmp(cmd, "!TermSize", 9)==0 )	tiny_TermSize(cmd+9);
	else
		rc = term_Cmd( pt, cmd, preply);

	return rc;
}
DWORD WINAPI scper(void *pv)
{
	term_Scp( pt, (char *)pv, NULL);
	free(pv);
	return 0;
}
DWORD WINAPI tuner(void *pv)
{
	term_Tun( pt, (char *)pv, NULL);
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
		cch = wchar_to_utf8(wcmd, cch+1, cmd, 255);
		cmd[cch] = 0;
		if ( autocomplete_Add(wcmd) ) 
			if ( *cmd=='!' ) menu_Add(cmd+1, wcmd+1);
		if ( *cmd=='!' ) {
			if ( strncmp(cmd+1, "scp ", 4)==0 && host_Type( pt->host )==SSH )
				CreateThread(NULL, 0, scper, strdup(cmd+5), 0, NULL);
			  else if ( strncmp(cmd+1, "tun",  3)==0 && host_Type( pt->host )==SSH )
				CreateThread(NULL, 0, tuner, strdup(cmd+4), 0, NULL);
			  else
				tiny_Cmd(cmd, NULL);
		}
		else{
			cmd[cch]='\r'; 
			term_Send( pt, cmd, cch+1 );
		}
	}
	PostMessage(hwndCmd, EM_SETSEL, 0, -1);
}
WNDPROC wpOrigCmdProc;
LRESULT APIENTRY CmdEditProc(HWND hwnd, UINT uMsg,
								WPARAM wParam, LPARAM lParam)
{
	if ( uMsg==WM_KEYDOWN ){
		if ( GetKeyState(VK_CONTROL) & 0x8000 ) { 				//CTRL+
			char cmd = 0;
			if ( wParam==54 ) cmd = 30;											//^
			if ( wParam>64 && wParam<91 ) cmd = wParam-64;	//A-Z
			if ( wParam>218&&wParam<222 ) cmd = wParam-192;	//[\]
			if ( cmd ) term_Send( pt, &cmd, 1 );
		}
		else {
			switch ( wParam ) {
			case VK_UP: 	cmd_Disp(autocomplete_Prev()); break;
			case VK_DOWN: 	cmd_Disp(autocomplete_Next()); break;
			case VK_RETURN: cmd_Enter(); break;
			}
		}
	}
	return CallWindowProc(wpOrigCmdProc, hwnd, uMsg, wParam, lParam);
}
WCHAR Title[256] = L"   Term      Script      Options               ";
char hostname[256]="";
char *tiny_Hostname()
{
	return hostname;
}
void tiny_Title( char *buf )
{
	strncpy(hostname, buf, 200);
	hostname[200] = 0;
	utf8_to_wchar(buf, strlen(buf)+1, Title+47, 208);
	SetWindowText(hwndMain, Title);
}
char *tiny_Gets( char *prompt, BOOL bEcho)
{
	return ssh2_Gets( pt->host, prompt, bEcho );
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
	SetTimer(hwndMain, HOST_TIMER, 1000, NULL);
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
	wndRect.right = x + iFontWidth*pt->size_x;
	wndRect.bottom = y + iFontHeight*pt->size_y + 1;
	AdjustWindowRect(&wndRect, WS_TILEDWINDOW, FALSE);
	MoveWindow( hwnd, x, y, wndRect.right-wndRect.left,
							wndRect.bottom-wndRect.top, TRUE );
	SendMessage( hwndCmd, WM_SETFONT, (WPARAM)hTermFont, TRUE );
}
BOOL tiny_Edit(BOOL e)
{
	if ( bEdit==e ) return FALSE;	//nothing to change
	SendMessage(hwndMain, WM_COMMAND, ID_EDIT, 0);
	return TRUE;
}
void tiny_Transparency(int t)
{
	SendMessage(hwndMain, WM_COMMAND, ID_TRANSP, t);
}
void tiny_TermSize(char *size)
{
	char *p = strchr(size, 'x');
	if ( p!=NULL ) {
		pt->size_x = atoi(size);
		pt->size_y = atoi(p+1);
		tiny_Font(hwndMain);
	}
}
WCHAR wsFontFace[256];
int fontsize = 16;
float dpi = 96;
void tiny_FontFace(char *fontface)
{
	utf8_to_wchar(fontface, strlen(fontface)+1, wsFontFace, 255);
	wsFontFace[255] = 0;
}
void tiny_FontSize(int i)
{
	DeleteObject(hTermFont);
	fontsize = i;
	hTermFont = CreateFont(fontsize*(dpi/96),0,0,0,FW_MEDIUM, FALSE,FALSE,FALSE,
						DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
						DEFAULT_QUALITY, FIXED_PITCH, wsFontFace);
	tiny_Font(hwndMain);
}
void tiny_Paint(HDC hDC)
{
	WCHAR wbuf[1024];
	RECT text_rect = {0, 0, 0, 0};
	BitBlt(hBufDC,0,0, termRect.right, termRect.bottom, NULL,0,0, BLACKNESS);
	int y = pt->screen_y+pt->scroll_y; if ( y<0 ) y = 0;
	int sel_min = min(pt->sel_left, pt->sel_right);
	int sel_max = max(pt->sel_left, pt->sel_right);
	int dx=1, dy=0;
	for ( int l=0; l<pt->size_y; l++ ) {
		dx = 1;
		int i = pt->line[y+l];
		while ( i<pt->line[y+l+1] ) {
			BOOL utf = FALSE;
			int j = i;
			while ( pt->attr[j]==pt->attr[i] ) {
				if ( (pt->buff[j]&0xc0)==0xc0 ) utf = TRUE;
				if ( ++j==pt->line[y+l+1] ) break;
				if ( j==sel_min || j==sel_max ) break;
			}
			if ( i>=sel_min&&i<sel_max ) {
				SetTextColor( hBufDC, COLORS[0] );
				SetBkColor( hBufDC, COLORS[7] );
			}
			else {
				SetTextColor( hBufDC, COLORS[pt->attr[i]&0x0f] );
				SetBkColor( hBufDC, COLORS[(pt->attr[i]>>4)&0x0f] );
			}
			int len = j-i;
			if ( pt->buff[j-1]==0x0a ) len--;	//remove unprintable 0x0a for XP
			if ( utf ) {
				int cch = utf8_to_wchar(pt->buff+i, len, wbuf, 1024);
				TextOutW(hBufDC, dx, dy, wbuf, cch);
				DrawText(hBufDC, wbuf, cch, &text_rect, DT_CALCRECT|DT_NOPREFIX);
				dx += text_rect.right;
			}
			else {
				TextOutA(hBufDC, dx, dy, pt->buff+i, len);
				dx += iFontWidth*len;
			}
			i=j;
		}
		dy += iFontHeight;
	}
	if ( pt->scroll_y ) {
		int slider_y = termRect.bottom*(pt->cursor_y+pt->scroll_y)/pt->cursor_y;
		SelectObject(hBufDC,dwScrollBrush);
		Rectangle(hBufDC, termRect.right-11, 0, termRect.right,
												termRect.bottom);
		SelectObject(hBufDC,dwSliderBrush);
		Rectangle(hBufDC, termRect.right-11, slider_y-8,termRect.right,
														slider_y+7 );
	}
	BitBlt(hDC,0,0,termRect.right,termRect.bottom, hBufDC,0,0,SRCCOPY);
	int cch = utf8_to_wchar(pt->buff+pt->line[pt->cursor_y],
								pt->cursor_x-pt->line[pt->cursor_y], wbuf, 1024);
	DrawText(hBufDC, wbuf, cch, &text_rect, DT_CALCRECT|DT_NOPREFIX);
	if ( bEdit ) {
		MoveWindow( hwndCmd, text_rect.right, (pt->cursor_y-pt->screen_y)*iFontHeight,
						termRect.right-text_rect.right, iFontHeight, TRUE );
	}
	if ( bFocus ) {
		SetCaretPos( text_rect.right+1,
					(pt->cursor_y-pt->screen_y)*iFontHeight+iFontHeight*3/4);
		pt->bCursor ? ShowCaret(hwndMain) : HideCaret(hwndMain);
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
	static HWND hwndProto, hwndPort, hwndHost, hwndStatic, hwndTip;
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
		{//setup tooltip
			COMBOBOXINFO cbi;
			cbi.cbSize = sizeof(COMBOBOXINFO);
			hwndTip = CreateWindowEx(0, TOOLTIPS_CLASS, NULL,
										WS_POPUP |TTS_ALWAYSTIP | TTS_BALLOON,
										CW_USEDEFAULT, CW_USEDEFAULT,
										CW_USEDEFAULT, CW_USEDEFAULT,
										hwndDlg, NULL, hInst, NULL);
			if ( GetComboBoxInfo(hwndHost, &cbi) && hwndTip) {
				TOOLINFO toolInfo = { 0 };
				toolInfo.cbSize = sizeof(toolInfo);
				toolInfo.hwnd = hwndDlg;
				toolInfo.uFlags = TTF_IDISHWND | TTF_SUBCLASS;
				toolInfo.uId = (UINT_PTR)cbi.hwndItem;
				toolInfo.lpszText = L"Hostname or IPv4/IPv6 address";
				SendMessage(hwndTip, TTM_ADDTOOL, 0, (LPARAM)&toolInfo);
			}
		}
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
					SendMessage(hwndTip, TTM_ACTIVATE, FALSE, 0);
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
					SendMessage(hwndTip, TTM_ACTIVATE, TRUE, 0);
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
			case IDCANCEL:
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
BOOL menu_Command( WPARAM wParam, LPARAM lParam )
{
	switch ( LOWORD(wParam) ) {
	case ID_ABOUT:{
			char welcome[1024];
			sprintf(welcome, WELCOME, httport);
			term_Clear( pt );
			term_Disp( pt, welcome );
		}
		break;
	case ID_CONNECT:
		if ( host_Status( pt->host )==HOST_IDLE )	DialogBox(hInst,
				MAKEINTRESOURCE(IDD_CONNECT), hwndMain, (DLGPROC)ConnectProc);
		break;
	case ID_DISCONN:
		if ( host_Status( pt->host )!=HOST_IDLE ) host_Close( pt->host );
		break;
	case ID_COPY:
		if ( OpenClipboard(hwndMain) ) {
			if ( pt->sel_left!=pt->sel_right ) {
				EmptyClipboard( );
				CopyText();
			}
			CloseClipboard( );
		}
		break;
	case ID_PASTE:
		if ( OpenClipboard(hwndMain) ) {
			UINT format = 0;
			while ( (format=EnumClipboardFormats(format))!=0 ) {
				if ( format==CF_HDROP ) 
					DropFiles(GetClipboardData(CF_HDROP));
				if ( format==CF_UNICODETEXT ) 
					PasteText();
			}
			CloseClipboard();
		}
		break;
	case ID_SELALL:
		pt->sel_left=0; pt->sel_right=pt->cursor_x;
		tiny_Redraw();
		break;
	case ID_LOGGING:
		if ( !pt->bLogging ) {
			WCHAR *wfn = fileDialog(L"logfile\0*.log\0All\0*.*\0\0",
				OFN_PATHMUSTEXIST|OFN_NOREADONLYRETURN|OFN_OVERWRITEPROMPT);
			if ( wfn!=NULL ) {
				char fn[MAX_PATH];
				wchar_to_utf8(wfn, wcslen(wfn)+1, fn, MAX_PATH);
				term_Logg( pt, fn );
			}
		}
		else
			term_Logg( pt, NULL );
		check_Option( hTermMenu, ID_LOGGING, pt->bLogging );
		break;
	case ID_ECHO:	//toggle local echo
		check_Option( hTermMenu, ID_ECHO, term_Echo(pt) );
		break;
	case ID_EDIT:	//toggle local edit
		if ( (bEdit=!bEdit) ) {
			int edit_left = (pt->cursor_x-pt->line[pt->cursor_y])*iFontWidth;
			MoveWindow( hwndCmd, edit_left,	termRect.bottom-iFontHeight, 
								termRect.right-edit_left, iFontHeight, TRUE );
			SetFocus(hwndCmd);
		}
		else {
			MoveWindow( hwndCmd, 0, termRect.bottom-1, 1, 1, TRUE );
			SetFocus(hwndMain);
		}
		check_Option( hOptionMenu, ID_EDIT, bEdit);
		tiny_Redraw();
		break;
	case ID_TRANSP:
		if ( lParam>0 && lParam<256 ) 
			iTransparency = lParam;
		else
			iTransparency =  (iTransparency==255) ? 224 : 255;
		SetLayeredWindowAttributes(hwndMain,0,iTransparency,LWA_ALPHA);
		check_Option( hOptionMenu, ID_TRANSP, iTransparency!=255 );
		break;
	case ID_FONT:
		if ( fontDialog() ) tiny_Font( hwndMain );
		break;
	case ID_FONTSIZE0: 
		if ( fontsize<24 ) tiny_FontSize(fontsize+2);
		break;
	case ID_FONTSIZE1:
		if ( fontsize>8 ) tiny_FontSize(fontsize-2);
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
#define WM_DPICHANGED       0x02E0
LRESULT CALLBACK MainWndProc(HWND hwnd,UINT msg,WPARAM wParam,LPARAM lParam)
{
	PAINTSTRUCT ps;
	static WCHAR wm_chars[2]={0,0};

	switch (msg) {
	case WM_CREATE:
		hwndCmd = CreateWindow(L"EDIT", NULL,
							WS_CHILD|WS_VISIBLE|ES_AUTOHSCROLL|ES_NOHIDESEL,
							0, 0, 1, 1, hwnd, (HMENU)0, hInst, NULL);
		wpOrigCmdProc = (WNDPROC)SetWindowLongPtr(hwndCmd,
							GWLP_WNDPROC, (LONG_PTR)CmdEditProc);
		SendMessage( hwndCmd, WM_SETFONT, (WPARAM)hTermFont, TRUE );
		SendMessage( hwndCmd, EM_SETLIMITTEXT, 255, 0);
		autocomplete_Init(hwndCmd);
		SetLayeredWindowAttributes(hwnd,0,255,LWA_ALPHA);
		drop_Init( hwnd, DropScript );
		DragAcceptFiles( hwnd, TRUE );
		GetClientRect( hwnd, &termRect );
		tiny_Font( hwnd );
		break;
	case WM_SIZE:
		GetClientRect( hwnd, &termRect );
		term_Size( pt, termRect.right/iFontWidth, termRect.bottom/iFontHeight );
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
	case WM_DPICHANGED:
		dpi = LOWORD(wParam);
		tiny_FontSize(fontsize);
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
		if ( host_Status( pt->host )==HOST_IDLE ) {//press Enter to restart
			if ( (wParam&0xff)==0x0d ) host_Open(pt->host, NULL);
		}
		else {
			if ( (wParam>>8)==0 ) {
				char key = wParam&0xff;
				term_Send( pt, &key, 1);
			}
			else {
				char utf8[6], ho = wParam>>8;
				if ( (ho&0xF8)!=0xD8 ) {
					int c = wchar_to_utf8((WCHAR *)&wParam, 1, utf8, 6);
					if ( c>0 ) term_Send( pt, utf8, c );
				}
				else {
					if ( (ho&0xDC)==0xD8 )
						wm_chars[0] = wParam;	//high surrogate word
					else
						wm_chars[1] = wParam;	//low surrogate word
					if ( wm_chars[1]!=0 && wm_chars[0]!=0 ) {
						int c = wchar_to_utf8(wm_chars, 2, utf8, 6);
						if ( c>0 ) term_Send( pt, utf8, c );
						wm_chars[0] = 0;
						wm_chars[1] = 0;
					}
				}
			}
		}
		break;
	case WM_KEYDOWN:
		term_Keydown( pt, wParam );
		break;
	case WM_TIMER:
		if ( host_Status( pt->host )==HOST_CONNECTING )
			term_Disp( pt, "." );
		else
			KillTimer(hwndMain, HOST_TIMER);
		break;
	case WM_MOUSEWHEEL:
		term_Scroll( pt, GET_WHEEL_DELTA_WPARAM(wParam)/40 );
		break;
	case WM_ACTIVATE:
		SetFocus( bEdit? hwndCmd : hwndMain );
		tiny_Redraw( );
		break;
	case WM_LBUTTONDBLCLK: {
		int x = GET_X_LPARAM(lParam)/iFontWidth;
		int y = (GET_Y_LPARAM(lParam)+2)/iFontHeight+pt->screen_y+pt->scroll_y;
		pt->sel_left = pt->line[y]+x;
		pt->sel_right = pt->sel_left;
		while ( --pt->sel_left>pt->line[y] )
			if ( pt->buff[pt->sel_left]==0x0a || pt->buff[pt->sel_left]==0x20 ) {
				pt->sel_left++;
				break;
			}
		while ( ++pt->sel_right<pt->line[y+1])
			if ( pt->buff[pt->sel_right]==0x0a || pt->buff[pt->sel_right]==0x20 ) break;
		tiny_Redraw();
		break;
	}
	case WM_LBUTTONDOWN: {
		int x = GET_X_LPARAM(lParam)/iFontWidth;
		int y = (GET_Y_LPARAM(lParam)+2);

		if ( x>=pt->size_x-2 ) {
			bScrollbar = TRUE;
			pt->scroll_y = y*pt->cursor_y/termRect.bottom-pt->cursor_y;
			if ( pt->scroll_y<-pt->screen_y ) pt->scroll_y = -pt->screen_y;
			tiny_Redraw();
		}
		else {
			y = y/iFontHeight+pt->screen_y+pt->scroll_y;
			pt->sel_left = min(pt->line[y]+x, pt->line[y+1]);
			while ( isUTF8c(pt->buff[pt->sel_left]) ) pt->sel_left--;
			pt->sel_right = pt->sel_left;
		}
		SetCapture(hwnd);
		break;
	}
	case WM_MOUSEMOVE:
		if ( MK_LBUTTON&wParam ) {
			int x = GET_X_LPARAM(lParam)/iFontWidth;
			int y = (GET_Y_LPARAM(lParam)+2);
			if ( bScrollbar ) {
				pt->scroll_y = y*pt->cursor_y/termRect.bottom-pt->cursor_y;
				if ( pt->scroll_y<-pt->screen_y ) pt->scroll_y = -pt->screen_y;
				if ( pt->scroll_y>0 ) pt->scroll_y = 0;
			}
			else {
				y /= iFontHeight;
				if ( y<0 ) {
					pt->scroll_y += y*2;
					if ( pt->scroll_y<-pt->screen_y ) pt->scroll_y = -pt->screen_y;
				}
				if ( y>pt->size_y) {
					pt->scroll_y += (y-pt->size_y)*2;
					if ( pt->scroll_y>0 ) pt->scroll_y=0;
				}
				y += pt->screen_y+pt->scroll_y;
				pt->sel_right = min(pt->line[y]+x, pt->line[y+1]);
				while ( isUTF8c(pt->buff[pt->sel_right]) ) pt->sel_right++;
			}
			tiny_Redraw( );
		}
		break;
	case WM_LBUTTONUP:
		if ( pt->sel_right!=pt->sel_left ) {
			int sel_min = min(pt->sel_left, pt->sel_right);
			int sel_max = max(pt->sel_left, pt->sel_right);
			pt->sel_left = sel_min;
			pt->sel_right = sel_max;
		}
		else {
			pt->sel_left = pt->sel_right = 0;
			tiny_Redraw( );
		}
		ReleaseCapture();
		bScrollbar = FALSE;
		SetFocus( bEdit ? hwndCmd : hwndMain );
		break;
	case WM_MBUTTONUP:
		if ( pt->sel_right>pt->sel_left )
			term_Send( pt, pt->buff+pt->sel_left, pt->sel_right-pt->sel_left );
		break;
	case WM_CONTEXTMENU:
		TrackPopupMenu(hContextMenu,TPM_RIGHTBUTTON, GET_X_LPARAM(lParam),
					 					GET_Y_LPARAM(lParam), 0, hwnd, NULL);
		break;
	case WM_NCLBUTTONDOWN: {
		int x = GET_X_LPARAM(lParam)-wndRect.left;
		int y = GET_Y_LPARAM(lParam)-wndRect.top;
		if ( y>0 && y<iTitleHeight ) 
			if ( x>menuX[0] && x<menuX[3] ) return 0;
		}
		return DefWindowProc(hwnd,msg,wParam,lParam);
	case WM_NCLBUTTONUP: {
		int x = GET_X_LPARAM(lParam)-wndRect.left;
		int y = GET_Y_LPARAM(lParam)-wndRect.top;
		if ( y>0 && y<iTitleHeight ) for ( int i=0; i<3; i++ )
			if ( x>menuX[i] && x<menuX[i+1] ) {
				TrackPopupMenu( hMenu[i],TPM_LEFTBUTTON, 
									wndRect.left+menuX[i]-24,
									wndRect.top+iTitleHeight, 0, hwnd, NULL );
				return 0;
			}
		}
		return DefWindowProc(hwnd,msg,wParam,lParam);
	case WM_DROPFILES:
		DropFiles((HDROP)wParam);
		break;
	case WM_CTLCOLOREDIT:
		SetTextColor( (HDC)wParam, COLORS[3] );
		SetBkColor( (HDC)wParam, COLORS[0] );
		return (LRESULT)dwBkBrush;		//must return brush for update
	case WM_ERASEBKGND:
		return 1;
	case WM_CLOSE:
		if ( host_Status( pt->host )!=HOST_IDLE ) {
			if ( MessageBox(hwnd, L"Disconnect and quit?",
							L"tinyTerm", MB_YESNO)==IDNO ) break;
			host_Close(pt->host);
		}
		DestroyWindow(hwnd);
		break;
	case WM_DESTROY:
		DragAcceptFiles(hwnd, FALSE);
		drop_Destroy( hwnd );
		DeleteDC(hBufDC);
		DeleteObject(hBufMap);
		DeleteObject(dwBkBrush);
		DeleteObject(dwScrollBrush);
		DeleteObject(dwSliderBrush);
		PostQuitMessage(0);
		break;
	case WM_COMMAND:
	case WM_SYSCOMMAND:
		if ( menu_Command( wParam, lParam ) ) return 1;
	default: return DefWindowProc(hwnd,msg,wParam,lParam);
	}
	return 0;
}
void tiny_Menu( HWND hwnd )
{
	iTitleHeight = 	 GetSystemMetrics(SM_CYFRAME)
									+GetSystemMetrics(SM_CYCAPTION)
									+GetSystemMetrics(92);	//SM_CXPADDEDBORDER
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
								(UINT_PTR)hScriptMenu,	 L"Script");
	InsertMenu(hContextMenu, 0, MF_BYPOSITION, ID_SELALL,L"Select All\tAlt+A");
	InsertMenu(hContextMenu, 0, MF_BYPOSITION, ID_PASTE, L"Paste     \tAlt+V");
	InsertMenu(hContextMenu, 0, MF_BYPOSITION, ID_COPY,  L"Copy      \tAlt+C");

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
	wc.hIcon = 0;
	wc.hIconSm 		= LoadIcon(hInstance, MAKEINTRESOURCE(IDICON_TL1));
	wc.hCursor		= LoadCursor(NULL,IDC_ARROW);
	wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
	wc.lpszClassName = L"TWnd";
	wc.lpszMenuName = 0;
	if ( !RegisterClassEx(&wc) ) return 0;
	hInst = hInstance;
	
	INITCOMMONCONTROLSEX icce = {0};
  icce.dwSize = sizeof(INITCOMMONCONTROLSEX);
  icce.dwICC = ICC_STANDARD_CLASSES|ICC_TAB_CLASSES;
  InitCommonControlsEx(&icce);
	OleInitialize( NULL );

	WSADATA wsaData;
	WSAStartup(MAKEWORD(2, 2), &wsaData);
	libssh2_init (0);
	httport = http_Svr("127.0.0.1");

	TERM aTerm;
	HOST aHost;
	aTerm.host = &aHost;
	pt = &aTerm;
	term_Init( pt );
	host_Init( &aHost );
	aHost.term = pt;

	dwBkBrush = CreateSolidBrush( COLORS[0] );
	dwScrollBrush = CreateSolidBrush( RGB(64,64,64) );
	dwSliderBrush = CreateSolidBrush( COLORS[1] );
	HDC sysDC = GetDC(0);
	dpi = GetDeviceCaps(sysDC, LOGPIXELSX);
	ReleaseDC(0, sysDC);
	wcscpy(wsFontFace,IsWindows7OrGreater() ? L"Consolas": L"Courier New");
	
	hTermFont = CreateFont( fontsize*(dpi/96),0,0,0,FW_MEDIUM, FALSE,FALSE,FALSE,
						DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
						DEFAULT_QUALITY, FIXED_PITCH, wsFontFace);
	hwndMain = CreateWindowEx( WS_EX_LAYERED, L"TWnd", Title,
						WS_TILEDWINDOW|WS_VISIBLE, CW_USEDEFAULT, CW_USEDEFAULT,
						800, 480, NULL, NULL, hInst, NULL );
	tiny_Menu(hwndMain);
	iConnectCount = 1;

	if ( *lpCmdLine==0 )
		PostMessage(hwndMain, WM_SYSCOMMAND, ID_CONNECT, 0);
	else
		host_Open( pt->host, lpCmdLine );

	LoadDict(L"tinyTerm.hist");
	MSG msg;
	HACCEL haccel = LoadAccelerators(hInst, MAKEINTRESOURCE(IDACCEL_MAIN));
	while ( GetMessage(&msg, NULL, 0, 0) ){
		if (!TranslateAccelerator(hwndMain, haccel,  &msg)) {
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}
	}
	if ( pt->bLogging ) term_Logg( pt, NULL );

	LoadDict(L"tinyTerm.hist");
	SaveDict(L"tinyTerm.hist");
	autocomplete_Destroy( );
	
	http_Svr("127.0.0.1");
	OleUninitialize();
	libssh2_exit();
	WSACleanup();

	return 0;
}
void CopyText( )
{
	char *ptr = pt->buff+pt->sel_left;
	int len = pt->sel_right-pt->sel_left;
	HANDLE hglbCopy = GlobalAlloc(GMEM_MOVEABLE, (len+1)*2);
	if ( hglbCopy != NULL ) {
		WCHAR *wbuf = GlobalLock(hglbCopy);
		len = utf8_to_wchar( ptr, len, wbuf, len );
		wbuf[len] = 0;
		GlobalUnlock(hglbCopy);
		SetClipboardData(CF_UNICODETEXT, hglbCopy);
	}
}
void PasteText( )
{
	HANDLE hglb = GetClipboardData(CF_UNICODETEXT);
	WCHAR *ptr = (WCHAR *)GlobalLock(hglb);
	if (ptr != NULL) {
		int len =  wchar_to_utf8(ptr, -1, NULL, 0);
		char *p = (char *)malloc(len);
		if ( p!=NULL ) {
			wchar_to_utf8(ptr, -1, p, len);
			term_Send( pt, p, len);
			free(p);
		}
		GlobalUnlock(hglb);
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
			if ( autocomplete_Add(wcmd) )
				if ( *cmd=='!' ) menu_Add(cmd+1, wcmd+1);
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
	term_Learn_Prompt( pt );
	
	char *p=(char *)files, *p1;
	if ( host_Type( pt->host )==SSH ) {
		char rdir[1024];
		term_Pwd( pt, rdir, 1024 );
		strcat(rdir, "/");

		do {
			p1 = strchr(p, 0x0a);
			if ( p1!=NULL ) *p1++=0;
			scp_write( pt->host, p, rdir );
		}
		while ( (p=p1)!=NULL );
	}
	if ( host_Type( pt->host )==SFTP ) {
		char cmd[1024];
		strcpy(cmd, "put ");
		cmd[1023]=0;
		term_Disp( pt, "\n" );
		do {
			p1 = strchr(p, 0x0a);
			if ( p1!=NULL ) *p1++=0;
			strncpy(cmd+4, p, 1019);
			sftp_cmd( pt->host, cmd );
		}
		while ( (p=p1)!=NULL );
	}
	term_Send(pt, "\r", 1);
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
		if ( host_Type( pt->host )!=SSH && host_Type( pt->host )!=SFTP ) {
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
				tiny_Cmd( p0, NULL );
				Sleep(iScriptDelay);
			}
		}
		if ( p0!=p1 ) { p0 = p1+1; *p1 = 0x0a; }
		while ( bScriptPause && bScriptRun ) Sleep(500);
	}
	while ( p2!=NULL && bScriptRun );
	cmd_Disp( bScriptRun ? L"script completed" : L"script stopped");
	bScriptRun = bScriptPause = FALSE;
	free(cmds);
	return 0;
}
void DropScript( char *cmds )
{
	if ( host_Type( pt->host )==NETCONF ) {
		term_Send( pt, cmds, strlen(cmds));
		free(cmds);
	}
	else {
		if ( bScriptRun ) {
			cmd_Disp(L"a script is still running");
			free(cmds);
		}
		else {
			term_Learn_Prompt( pt );
			CreateThread( NULL, 0, scripter, (void *)cmds, 0, NULL);
		}
	}
}
void OpenScript( WCHAR *wfn )
{
	cmd_Disp(wfn);
	int len = wcslen(wfn);
	if ( wcscmp(wfn+len-3, L".js")==0 || wcscmp(wfn+len-4, L".vbs")==0 ) {
		WCHAR port[256];
		term_Learn_Prompt( pt );
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
					if ( host_Status( pt->host )==HOST_CONNECTED )
						DropScript(tl1s);
					else {
						term_Disp( pt, tl1s );
						free(tl1s);
					}
				}
			}
			else
				cmd_Disp(L"Couldn't open file");
		}
	}
}
/**********************************HTTPd*******************************/
const char *RFC1123FMT="%a, %d %b %Y %H:%M:%S GMT";
const char *exts[]={".txt",
					".htm", ".html",
					".js",
					".jpg", ".jpeg",
					".png",
					".css"
					};
const char *mime[]={"text/plain",
					"text/html", "text/html",
					"text/javascript",
					"image/jpeg", "image/jpeg",
					"image/png",
					"text/css"
					};
const char HEADER[]="HTTP/1.1 200 Ok\
					\nServer: tinyTerm-httpd\
					\nAccess-Control-Allow-Origin: *\
					\nContent-Type: text/plain\
					\nContent-length: %d\
					\nCache-Control: no-cache\n\n";
void httpFile( int s1, char *file)
{
	char reply[4096], timebuf[128];
	int len, i, j;
	time_t now;

	now = time( NULL );
	strftime( timebuf, sizeof(timebuf), RFC1123FMT, gmtime( &now ) );

    struct stat sb;
	if ( stat( file, &sb ) ==-1 ) {
		len=sprintf(reply, "HTTP/1.1 404 not found\nDate: %s\nServer: tinyTerm_httpd\n", timebuf);
		len+=sprintf(reply+len, "Content-Type: text/html\nContent-Length: 14\n\n");
	    send(s1, reply, len, 0);
	    len=sprintf(reply, "file not found");
		send(s1, reply, len, 0);
		return;
	}

	FILE *fp = fopen( file, "rb" );	
	if ( fp!=NULL ) {
		len=sprintf(reply, "HTTP/1.1 200 Ok\nDate: %s\nServer: tinyTerm_httpd\n", timebuf);

		const char *filext=file+strlen(file)-1;
		while ( *filext!='.' ) filext--;
		for ( i=0, j=0; j<8; j++ ) if ( strcmp(filext, exts[j])==0 ) i=j;
		len+=sprintf(reply+len, "Content-Type: %s\n", mime[i]);

		long filesize = sb.st_size;
		len+=sprintf(reply+len, "Content-Length: %ld\n", filesize );
		strftime(timebuf, sizeof(timebuf), RFC1123FMT, gmtime( &sb.st_mtime ));
		len+=sprintf(reply+len, "Last-Modified: %s\n\n", timebuf );

		send(s1, reply, len, 0);
		while ( (len=fread(reply, 1, 4096, fp))>0 )
			if ( send(s1, reply, len, 0)==-1)	break;
		fclose(fp);
	}
}
void url_decode(char *url)
{
	char *p = url, *q = url;
	while ( *p ) {
		if ( *p=='%' && isdigit(p[1]) ) {
			int a;
			sscanf( ++p, "%02x", &a);
			*(++p) = a;
		}
		*q++ = *p++;
	}
	*q = 0;
}
DWORD WINAPI *httpd( void *pv )
{
	char buf[4096], *cmd, *reply;
	struct sockaddr_in cltaddr;
	int addrsize=sizeof(cltaddr), cmdlen, replen;
	SOCKET http_s0 = *(SOCKET *)pv;

	while ( TRUE ) {
		SOCKET http_s1 = accept(http_s0, (struct sockaddr*)&cltaddr, &addrsize);
		if ( http_s1 == INVALID_SOCKET ) {
			cmd_Disp_utf8("httpd terminated");
			break;
		}
		cmd_Disp_utf8( "http connected" );
		cmdlen=recv(http_s1, buf, 4095, 0);
		if ( cmdlen>0 ) {
			if ( strncmp(buf, "GET /", 5)!=0 ) {//TCP connection
				FD_SET readset;
				struct timeval tv = { 0, 100 };	//tv_sec=0, tv_usec=300
				term_Send( pt, buf, cmdlen);
				do {
					if ( host_Status( pt->host )==HOST_CONNECTED ) {
						replen = term_Recv( pt,  &reply );
						if ( replen > 0 ) send(http_s1, reply, replen, 0);
					}
					FD_ZERO(&readset);
					FD_SET(http_s1, &readset);
					if ( select(1, &readset, NULL, NULL, &tv)>0 ) {
						cmdlen = recv(http_s1, buf, 4095, 0);
						if ( cmdlen>0 ) 
							term_Send( pt, buf, cmdlen);
					}
				} while ( cmdlen>0 );
			}
			else {								//HTTP connection
				do {
					buf[cmdlen] = 0;
					cmd = buf+5;
					char *p=strchr(cmd, ' ');
					if ( p!=NULL ) *p = 0;
					url_decode(cmd);
					cmd_Disp_utf8(cmd);

					if ( *cmd=='?' ) {	//get CGI, cmd+1 points to command
						replen = term_Cmd( pt,  cmd+1, &reply );
						int len = sprintf( buf, HEADER, replen );
						send( http_s1, buf, len, 0 );
						if ( replen>0 ) send( http_s1, reply, replen, 0 );
					}
					else {				//get file, cmd points to filename
						httpFile(http_s1, cmd);
					}
					cmdlen = recv(http_s1, buf, 4095, 0);
				} while ( cmdlen>0 );
			}
		}
		cmd_Disp_utf8( "http disconnected" );
		shutdown(http_s1, SD_SEND);
		closesocket(http_s1);
	}
	return 0;
}
int http_Svr( char *intf )
{
static SOCKET http_s0=INVALID_SOCKET;

	if ( http_s0 !=INVALID_SOCKET ) {
		closesocket( http_s0 );
		http_s0 = INVALID_SOCKET;
		return 0;
	}

	http_s0 = socket(AF_INET, SOCK_STREAM, 0);
	if ( http_s0==INVALID_SOCKET ) return 0;

	struct sockaddr_in svraddr;
	int addrsize=sizeof(svraddr);
	memset(&svraddr, 0, addrsize);
	svraddr.sin_family=AF_INET;
	svraddr.sin_addr.s_addr=inet_addr( intf );
	int p;
	for ( p=8080; p<8099; p++ ) {
		svraddr.sin_port=htons(p);
		if ( bind(http_s0, (struct sockaddr*)&svraddr, addrsize)!=SOCKET_ERROR ) {
			if ( listen(http_s0, 1)!=SOCKET_ERROR){
				DWORD dwThreadId;
				CreateThread( NULL, 0, (LPTHREAD_START_ROUTINE)httpd,
											&http_s0, 0, &dwThreadId);
				return p;
			}
		}
	}
	closesocket(http_s0);
	http_s0 = INVALID_SOCKET;
	return 0;
}