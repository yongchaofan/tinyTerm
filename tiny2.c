//
// "$Id: tiny.c 34506 2019-01-30 14:35:10 $"
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
//     https://github.com/yongchaofan/tinyTerm/blob/master/LICENSE
//
// Please report all bugs and problems on the following page:
//
//     https://github.com/yongchaofan/tinyTerm/issues/new
//
#include <windows.h>
#include <windowsx.h>
#include <shlobj.h>
#include <dwmapi.h>
#include <uxtheme.h>
#include <Vsstyle.h>
#include <Vssym32.h>
#include "tiny.h"
#include "res/resource.h"

extern char buff[], attr[];
extern int line[];
extern int cursor_y, cursor_x;
extern int size_y, size_x;
extern int screen_y;
extern int scroll_y;
extern int sel_left, sel_right;
extern BOOL bCursor, bLogging, bAlterScreen;
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
\t�2018-2019 Yongchao Fan, All rights reserved\n\n\
\thttps://yongchaofan.github.io/tinyTerm\n\n\n";

const COLORREF COLORS[16] ={RGB(0,0,0), 	RGB(192,0,0), RGB(0,192,0),
							RGB(192,192,0), RGB(32,96,240), RGB(192,0,192),
							RGB(0,192,192), RGB(192,192,192),
							RGB(0,0,0), 	RGB(240,0,0), RGB(0,240,0),
							RGB(240,240,0), RGB(32,96,240), RGB(240,0,240),
							RGB(0,240,240), RGB(255,255,255) };
static HINSTANCE hInst;
static HFONT hTermFont;
static RECT clientRect, termRect;
static HWND hwndMain, hwndCmd, hwndTab;
static HBRUSH dwBkBrush, dwScrollBrush, dwSliderBrush;
static HMENU hMenu[3], hTermMenu, hScriptMenu, hOptionMenu, hContextMenu;
static int menuX[4] = {32, 0, 0, 0};	//menuX[0]=32 leave space for icon

static WCHAR wsFontFace[256]=L"Consolas";
static int iFontHeight, iFontWidth;
static int iTransparency = 255;
static int iScriptDelay = 250;
static int iConnectCount, iScriptCount, httport;
static BOOL bScrollbar = FALSE, bFocus = FALSE, bEdit = FALSE;
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
		InsertMenu( hTermMenu, -1, MF_BYPOSITION,
					ID_CONNECT0+iConnectCount++, wcmd);
}

int tiny_Cmd( char *cmd, char **preply )
{
	int rc = 0;
	if ( preply ) *preply = buff+cursor_x;
	cmd_Disp_utf8(cmd);
	if (strncmp(cmd,"!Transparency",13)==0)		tiny_Transparency(atoi(cmd+13));
	else if ( strncmp(cmd,"!ScriptDelay",12)==0)iScriptDelay = atoi(cmd+12);
	else if ( strncmp(cmd, "!FontSize", 9)==0 )	tiny_FontSize(atoi(cmd+9));
	else if ( strncmp(cmd, "!FontFace", 9)==0 )	tiny_FontFace(cmd+9);
	else if ( strncmp(cmd, "!TermSize", 9)==0 )	tiny_TermSize(cmd+9);
	else
		rc = term_Cmd(cmd, preply);

	return rc;
}
DWORD WINAPI scper(void *pv)
{
	term_Scp((char *)pv, NULL);
	free(pv);
	return 0;
}
DWORD WINAPI tuner(void *pv)
{
	term_Tun((char *)pv, NULL);
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
			if ( strncmp(cmd+1, "scp ", 4)==0 && host_Type()==SSH )
				CreateThread(NULL, 0, scper, strdup(cmd+5), 0, NULL);
			  else if ( strncmp(cmd+1, "tun",  3)==0 && host_Type()==SSH )
				CreateThread(NULL, 0, tuner, strdup(cmd+4), 0, NULL);
			  else
				tiny_Cmd(cmd, NULL);
		}
		else{
			cmd[cch]='\r'; 
			host_Send(cmd, cch+1);
		}
	}
	PostMessage(hwndCmd, EM_SETSEL, 0, -1);
}
WNDPROC wpOrigCmdProc;
LRESULT APIENTRY CmdEditProc(HWND hwnd, UINT uMsg,
								WPARAM wParam, LPARAM lParam)
{
	if ( uMsg==WM_KEYDOWN ){
		if ( GetKeyState(VK_CONTROL) & 0x8000 ) { 			//CTRL+
			char cmd = 0;
			if ( wParam==54 ) cmd = 30;						//^
			if ( wParam>64 && wParam<91 ) cmd = wParam-64;	//A-Z
			if ( wParam>218&&wParam<222 ) cmd = wParam-192;	//[\]
			if ( cmd ) host_Send(&cmd, 1);
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
	
	if ( *buf ) {
		host_Size(size_x, size_y);
		ModifyMenu(hTermMenu,ID_CONNECT,MF_BYCOMMAND,ID_DISCONN,L"&Disconnect");
	}
	else {
		term_Disp("\n\033[31mDisconnected. Press Enter to restart\n\033[37m");
		ModifyMenu(hTermMenu,ID_DISCONN,MF_BYCOMMAND,ID_CONNECT,L"&Connect...");
	}
}

void tiny_Redraw()
{
	InvalidateRect( hwndMain, &clientRect, TRUE );
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
    TEXTMETRIC tm;
	HDC hdc = GetDC(hwnd);
	SelectObject(hdc, hTermFont);
	GetTextMetrics(hdc, &tm);
	ReleaseDC(hwnd, hdc);
	iFontHeight = tm.tmHeight;
	iFontWidth = tm.tmAveCharWidth;

	RECT wndRect;
	GetWindowRect( hwnd, &wndRect );
	int x = wndRect.left;
	int y = wndRect.top;
	wndRect.right = x + iFontWidth*size_x;
	wndRect.bottom = y + iFontHeight*size_y + 1;
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
void tiny_Menu_Position()
{
	iTitleHeight = GetSystemMetrics(SM_CYFRAME)
				 + GetSystemMetrics(SM_CYCAPTION)
				 + GetSystemMetrics(SM_CXPADDEDBORDER);
	RECT menuRect = { 0, 0, 0, 0};
	HDC wndDC = GetWindowDC(hwndMain);

    HTHEME hTheme = OpenThemeData(NULL, L"CompositedWindow::Window");
    if (hTheme)
    {
		LOGFONT lgFont;
		HFONT oldFont = 0;
		if (SUCCEEDED(GetThemeSysFont(hTheme, TMT_CAPTIONFONT, &lgFont)))
		{
			HFONT hFont = CreateFontIndirect(&lgFont);
			oldFont = (HFONT )SelectObject(wndDC, hFont);
		}
		DTTOPTS DttOpts = {sizeof(DTTOPTS)};
		DttOpts.dwFlags = DTT_CALCRECT;
		menuX[0] = 32 *(dpi/96);
		DrawThemeTextEx(hTheme, wndDC, WP_CAPTION, CS_ACTIVE, 
						L"    Term", 8,  DT_CALCRECT, &menuRect, &DttOpts);
		menuX[1] = menuX[0]+(menuRect.right-menuRect.left)*(dpi/96);
		menuRect.left = menuRect.right = 0;
		DrawThemeTextEx(hTheme, wndDC, WP_CAPTION, CS_ACTIVE, 
						L"  Script", 8,  DT_CALCRECT, &menuRect, &DttOpts);
		menuX[2] = menuX[1]+(menuRect.right-menuRect.left)*(dpi/96);
		menuRect.left = menuRect.right = 0;
		DrawThemeTextEx(hTheme, wndDC, WP_CAPTION, CS_ACTIVE, 
						L"  Options",9,  DT_CALCRECT, &menuRect, &DttOpts);
		menuX[3] = menuX[2]+(menuRect.right-menuRect.left)*(dpi/96);
		CloseThemeData(hTheme);
		if ( oldFont ) SelectObject( wndDC, oldFont );
    }

	ReleaseDC(hwndMain, wndDC);
}

void tiny_Paint(HDC hDC)
{
	WCHAR wbuf[1024];
	RECT text_rect = {0, 0, 0, 0};
//	BitBlt(hDC,termRect.left,termRect.top, termRect.right, termRect.bottom, NULL,0,0, BLACKNESS);
	HFONT hFontOld = (HFONT)SelectObject(hDC, hTermFont);
	SetBkMode(hDC, OPAQUE);
	int y = screen_y+scroll_y; if ( y<0 ) y = 0;
	int sel_min = min(sel_left, sel_right);
	int sel_max = max(sel_left, sel_right);
	int dx, dy=termRect.top;
	for ( int l=0; l<size_y; l++ ) {
		dx = termRect.left;
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
				SetTextColor( hDC, COLORS[0] );
				SetBkColor( hDC, COLORS[7] );
			}
			else {
				SetTextColor( hDC, COLORS[attr[i]&0x0f] );
				SetBkColor( hDC, COLORS[(attr[i]>>4)&0x0f] );
			}
			int len = j-i;
			if ( buff[j-1]==0x0a ) len--;	//remove unprintable 0x0a for XP
			if ( utf ) {
				int cch = utf8_to_wchar(buff+i, len, wbuf, 1024);
				TextOutW(hDC, dx, dy, wbuf, cch);
				DrawText(hDC, wbuf, cch, &text_rect, DT_CALCRECT|DT_NOPREFIX);
				dx += text_rect.right;
			}
			else {
				TextOutA(hDC, dx, dy, buff+i, len);
				dx += iFontWidth*len;
			}
			i=j;
		}
		dy += iFontHeight;
	}
	if ( scroll_y ) {
		int slider_y = termRect.bottom*(cursor_y+scroll_y)/cursor_y;
		SelectObject(hDC,dwScrollBrush);
		Rectangle(hDC, termRect.right-11, 0, termRect.right,
												termRect.bottom);
		SelectObject(hDC,dwSliderBrush);
		Rectangle(hDC, termRect.right-11, slider_y-8,termRect.right,
														slider_y+7 );
	}
	int cch = utf8_to_wchar(buff+line[cursor_y],
								cursor_x-line[cursor_y], wbuf, 1024);
	DrawText(hDC, wbuf, cch, &text_rect, DT_CALCRECT|DT_NOPREFIX);
	if ( bEdit ) {
		MoveWindow( hwndCmd, text_rect.right, 
						termRect.top+(cursor_y-screen_y)*iFontHeight,
						termRect.right-text_rect.right, iFontHeight, TRUE );
	}
	if ( bFocus ) {
		SetCaretPos( termRect.left+text_rect.right+1,
					termRect.top+(cursor_y-screen_y)*iFontHeight+iFontHeight*3/4);
		bCursor ? ShowCaret(hwndMain) : HideCaret(hwndMain);
	}
	if ( hFontOld ) SelectObject(hDC, hFontOld);
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
					Static_SetText(hwndStatic, L"Address:");
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
			term_Clear();
			term_Disp(welcome);
		}
		break;
	case ID_CONNECT:
		if ( host_Status()==HOST_IDLE )	DialogBox(hInst,
				MAKEINTRESOURCE(IDD_CONNECT), hwndMain, (DLGPROC)ConnectProc);
		break;
	case ID_DISCONN:
		if ( host_Status()!=HOST_IDLE ) host_Close();
		break;
	case ID_COPY:
		if ( OpenClipboard(hwndMain) ) {
			if ( sel_left!=sel_right ) {
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
		sel_left=0; sel_right=cursor_x;
		tiny_Redraw();
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
	case ID_ECHO:	//toggle local echo
		check_Option( hTermMenu, ID_ECHO, term_Echo() );
		break;
	case ID_EDIT:	//toggle local edit
		if ( (bEdit=!bEdit) ) {
			int edit_left = (cursor_x-line[cursor_y])*iFontWidth;
			MoveWindow( hwndCmd, edit_left,	termRect.bottom-iFontHeight, 
								termRect.right-edit_left, iFontHeight, TRUE );
			SetFocus(hwndCmd);
		}
		else {
			MoveWindow( hwndCmd, 0, termRect.bottom-1, 1, 1, TRUE );
			SetFocus(hwndMain);
		}
		check_Option( hTermMenu, ID_EDIT, bEdit);
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
	case ID_FTPD:
		bFTPd = ftp_Svr(bFTPd?NULL:getFolderName(L"Choose root directory"));
		check_Option( hOptionMenu, ID_FTPD, bFTPd );
		break;
	case ID_TFTPD:
		bTFTPd = tftp_Svr(bTFTPd?NULL:getFolderName(L"Choose root directory"));
		check_Option( hOptionMenu, ID_TFTPD, bTFTPd );
		break;
	case ID_TERM:
		TrackPopupMenu(hMenu[0],TPM_LEFTBUTTON, clientRect.left+menuX[0],
								clientRect.top, 0, hwndMain, NULL);
		break;
	case ID_SCRIPT:
		TrackPopupMenu(hMenu[1],TPM_LEFTBUTTON, clientRect.left+menuX[1],
								clientRect.top, 0, hwndMain, NULL);
		break;
	case ID_OPTIONS:
		TrackPopupMenu(hMenu[2],TPM_LEFTBUTTON, clientRect.left+menuX[2],
								clientRect.top, 0, hwndMain, NULL);
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
// Hit test the frame for resizing and moving.
#define LEFTEXTENDWIDTH 	1
#define RIGHTEXTENDWIDTH	1
#define BOTTOMEXTENDWIDTH	1
#define TOPEXTENDWIDTH		30
LRESULT HitTestNCA(HWND hWnd, WPARAM wParam, LPARAM lParam)
{
    // Get the point coordinates for the hit test.
    POINT ptMouse = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};

    // Get the window rectangle.
    RECT rcWindow;
    GetWindowRect(hWnd, &rcWindow);

    // Get the frame rectangle, adjusted for the style without a caption.
    RECT rcFrame = { 0 };
    AdjustWindowRectEx(&rcFrame, WS_OVERLAPPEDWINDOW & ~WS_CAPTION, FALSE, 0);

    // Determine if the hit test is for resizing. Default middle (1,1).
    USHORT uRow = 1;
    USHORT uCol = 1;
    BOOL fOnResizeBorder = FALSE;

    // Determine if the point is at the top or bottom of the window.
    if (ptMouse.y >= rcWindow.top && ptMouse.y < rcWindow.top + TOPEXTENDWIDTH)
    {
        fOnResizeBorder = (ptMouse.y < (rcWindow.top - rcFrame.top));
        uRow = 0;
    }
    else if (ptMouse.y < rcWindow.bottom && ptMouse.y >= rcWindow.bottom - BOTTOMEXTENDWIDTH)
    {
        uRow = 2;
    }

    // Determine if the point is at the left or right of the window.
    if (ptMouse.x >= rcWindow.left && ptMouse.x < rcWindow.left + LEFTEXTENDWIDTH)
    {
        uCol = 0; // left side
    }
    else if (ptMouse.x < rcWindow.right && ptMouse.x >= rcWindow.right - RIGHTEXTENDWIDTH)
    {
        uCol = 2; // right side
    }
	if ( uRow==0 && uCol==1 ) {
		if ( ptMouse.x<rcWindow.left+40 ) uRow = 1;		//sysmenu
		if ( ptMouse.x>rcWindow.right-128 ) uRow = 1;	//minimize/maxmize/close
	}
    // Hit test (HTTOPLEFT, ... HTBOTTOMRIGHT)
    LRESULT hitTests[3][3] = 
    {
        { HTTOPLEFT,    fOnResizeBorder ? HTTOP : HTCAPTION,    HTTOPRIGHT },
        { HTLEFT,       HTNOWHERE,     HTRIGHT },
        { HTBOTTOMLEFT, HTBOTTOM, HTBOTTOMRIGHT },
    };

    return hitTests[uRow][uCol];
}
void CreateTabControl(HWND hwnd)
{
    TCITEM tie = {0};  // tab item structure

    /* create tab control */
    hwndTab = CreateWindowEx(
        0,                      // extended style
        WC_TABCONTROL,          // tab control constant
        L"",                    // text/caption
        WS_CHILD|WS_VISIBLE,
        1,                      // X position - device units from left
        1,                      // Y position - device units from top
        1,    					// Width - in device units
        1,                      // Height - in device units
        hwnd,            		// parent window
        NULL,                   // no menu
        hInst,                 	// instance
        NULL                    // no extra junk
    );
	
    // set up tab item structure for Tab1
    tie.mask = TCIF_TEXT;  // we're only displaying text in the tabs
    WCHAR pszTab1 [] = L"Tab1";  // tab1's text  (2-step process necessary to avoid compiler warnings)
    tie.pszText = pszTab1;  // the tab's text/caption
		TabCtrl_InsertItem(hwndTab, 0, &tie) ;

    WCHAR pszTab2 [] = L"Tab2";  // tab2's text  (2-step process necessary to avoid compiler warnings)
    tie.pszText = pszTab2;  // the tab's text/caption
    TabCtrl_InsertItem(hwndTab, 1, &tie);
}
// Paint the title on the custom frame.
void title_Paint(HWND hWnd, HDC hdc)
{
    RECT rcClient;
    GetClientRect(hWnd, &rcClient);

    HTHEME hTheme = OpenThemeData(NULL, L"CompositedWindow::Window");
    if (hTheme)
    {
        HDC hdcPaint = CreateCompatibleDC(hdc);
        if (hdcPaint)
        {
            int cx = rcClient.right-rcClient.left;
            int cy = rcClient.bottom-rcClient.top;

            // Define the BITMAPINFO structure used to draw text.
            // Note that biHeight is negative. This is done because
            // DrawThemeTextEx() needs the bitmap to be in top-to-bottom
            // order.
            BITMAPINFO dib = { 0 };
            dib.bmiHeader.biSize            = sizeof(BITMAPINFOHEADER);
            dib.bmiHeader.biWidth           = cx;
            dib.bmiHeader.biHeight          = -cy;
            dib.bmiHeader.biPlanes          = 1;
            dib.bmiHeader.biBitCount        = 32;//BIT_COUNT;
            dib.bmiHeader.biCompression     = BI_RGB;

            HBITMAP hbm = CreateDIBSection(hdc, &dib, DIB_RGB_COLORS, NULL, NULL, 0);
//			HBITMAP hbm = CreateCompatibleBitmap(hdc, cx, cy);
            if (hbm)
            {
				HFONT hFontOld;
                HBITMAP hbmOld = (HBITMAP)SelectObject(hdcPaint, hbm);
				tiny_Paint(hdcPaint);
                // Setup the theme drawing options.
 //               DTTOPTS DttOpts = {sizeof(DTTOPTS)};
 //               DttOpts.dwFlags = DTT_COMPOSITED;
                // Select a font.
                LOGFONT lgFont;
                const int TMT_CAPTIONFONT=801;
                if (SUCCEEDED(GetThemeSysFont(hTheme, TMT_CAPTIONFONT, &lgFont)))
                {
                    HFONT hFont = CreateFontIndirect(&lgFont);
                    hFontOld = (HFONT) SelectObject(hdcPaint, hFont);
                }
                // Draw the title.
                RECT rcPaint = rcClient;
                rcPaint.top += 8;
                rcPaint.right -= 2;
                rcPaint.left += 32;
                rcPaint.bottom = 30;
                DrawThemeText(hTheme, 
                                hdcPaint, 
                                0,0,	//WP_CAPTION, CS_ACTIVE, 
                                L"  &Term", 
                                -1, 
                                DT_LEFT | DT_WORD_ELLIPSIS,0,
                                &rcPaint);
                rcPaint.left += 48;
                DrawThemeText(hTheme, 
                                hdcPaint, 
                                0,0,	//WP_CAPTION, CS_ACTIVE, 
                                L"  &Script", 
                                -1, 
                                DT_LEFT | DT_WORD_ELLIPSIS,0,
                                &rcPaint);
                rcPaint.left += 48;
               	DrawThemeText(hTheme, 
                                hdcPaint, 
                                0,0,	//WP_CAPTION, CS_ACTIVE, 
                                L"  &Options", 
                                -1, 
                                DT_LEFT | DT_WORD_ELLIPSIS,0,
                                &rcPaint);
            	if (hFontOld) SelectObject(hdcPaint, hFontOld);

                // Blit text to the frame.
				BitBlt(hdc, 0, 0, cx, cy, hdcPaint, 0, 0, SRCCOPY);

        		SelectObject(hdcPaint, hbmOld);
                DeleteObject(hbm);
            }
            DeleteDC(hdcPaint);
        }
        CloseThemeData(hTheme);
    }
}
BOOL CustomCaptionProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam,
						  LRESULT *plRet)
{
	BOOL fCallDWP = !DwmDefWindowProc(hwnd, msg, wParam, lParam, plRet);
	switch ( msg ) {
	case WM_ACTIVATE: {
			// Extend the frame into the client area.
			MARGINS margins;
			margins.cxLeftWidth = LEFTEXTENDWIDTH;
			margins.cxRightWidth = RIGHTEXTENDWIDTH;
			margins.cyBottomHeight = BOTTOMEXTENDWIDTH;
			margins.cyTopHeight = TOPEXTENDWIDTH;
			DwmExtendFrameIntoClientArea(hwnd, &margins);
		}
		return TRUE;
	case WM_NCCALCSIZE:
		if ( wParam==TRUE ) {
			// Calculate new NCCALCSIZE_PARAMS based on custom NCA inset.
			NCCALCSIZE_PARAMS *pncsp = (NCCALCSIZE_PARAMS*)lParam;
			pncsp->rgrc[0].left   = pncsp->rgrc[0].left   + 0;
			pncsp->rgrc[0].top    = pncsp->rgrc[0].top    + 0;
			pncsp->rgrc[0].right  = pncsp->rgrc[0].right  - 0;
			pncsp->rgrc[0].bottom = pncsp->rgrc[0].bottom - 0;
			return FALSE;
		}
		return TRUE;
	case WM_NCHITTEST: 
		if ( *plRet==0 ) {
			*plRet = HitTestNCA(hwnd, wParam, lParam);
			if ( *plRet!=HTNOWHERE ) return FALSE; 
		}
		break;
	}
	return fCallDWP;
}
LRESULT CALLBACK MainWndProc(HWND hwnd,UINT msg,WPARAM wParam,LPARAM lParam)
{
	PAINTSTRUCT ps;
	LRESULT lRet = 0;
	if ( !CustomCaptionProc(hwnd, msg, wParam, lParam, &lRet ) )
		return lRet;

	switch (msg) {
	case WM_CREATE:
//		CreateTabControl(hwnd);
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
		tiny_Font( hwnd );
		break;
	case WM_SIZE:
		GetClientRect( hwnd, &clientRect );
//		MoveWindow( hwndTab, clientRect.left+1, clientRect.top+30, clientRect.right-2, 24, TRUE);
		termRect.left  	= clientRect.left	+LEFTEXTENDWIDTH;
		termRect.right 	= clientRect.right	-RIGHTEXTENDWIDTH;
		termRect.top 	= clientRect.top	+TOPEXTENDWIDTH;
		termRect.bottom	= clientRect.bottom	-BOTTOMEXTENDWIDTH;
		size_x = (termRect.right-termRect.left) / iFontWidth;
		size_y = (termRect.bottom-termRect.top) / iFontHeight;
		term_Size();
		host_Size( size_x, size_y );
		tiny_Redraw( );
		break;
	case WM_PAINT:
		if ( BeginPaint(hwnd, &ps)!=NULL ) {
			title_Paint(hwnd, ps.hdc);
		}
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
	case WM_ACTIVATE: 
		SetFocus( bEdit? hwndCmd : hwndMain );
		tiny_Redraw( );
		break;
	case WM_IME_STARTCOMPOSITION:
		//moves the composition window to cursor pos on Win10
		break;
	case WM_CHAR:
		if ( host_Status()==HOST_IDLE ) {
			if ( (wParam&0xff)==0x0d ) //press Enter to restart
				host_Open(NULL);
		}
		else {
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
					static WCHAR wm_chars[2]={0,0};
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
		}
		break;
	case WM_KEYDOWN:
		term_Keydown(wParam);
		break;
	case WM_TIMER:
		if ( host_Status()==HOST_CONNECTING )
			term_Disp(".");
		else
			KillTimer(hwndMain, HOST_TIMER);
		break;
	case WM_MOUSEWHEEL:
		term_Scroll( GET_WHEEL_DELTA_WPARAM(wParam)/40 );
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
		}
		else {
			sel_left = sel_right = 0;
			tiny_Redraw( );
		}
		ReleaseCapture();
		bScrollbar = FALSE;
		SetFocus( bEdit ? hwndCmd : hwndMain );
		break;
	case WM_MBUTTONUP:
		if ( sel_right>sel_left )
			term_Send(buff+sel_left, sel_right-sel_left);
		break;
	case WM_CONTEXTMENU:
		TrackPopupMenu(hContextMenu,TPM_RIGHTBUTTON, GET_X_LPARAM(lParam),
					 					GET_Y_LPARAM(lParam), 0, hwnd, NULL);
		break;
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
		if ( host_Status()!=HOST_IDLE ) {
			if ( MessageBox(hwnd, L"Disconnect and quit?",
							L"tinyTerm", MB_YESNO)==IDNO ) break;
			host_Close();
		}
		DestroyWindow(hwnd);
		break;
	case WM_DESTROY:
		DragAcceptFiles(hwnd, FALSE);
		drop_Destroy( hwnd );
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
HMENU tiny_Menu()
{
	HMENU hMainMenu = LoadMenu( hInst, MAKEINTRESOURCE(IDMENU_MAIN));
	hTermMenu  = GetSubMenu(hMainMenu, 0);
	hScriptMenu = GetSubMenu(hMainMenu, 1);
	hOptionMenu = GetSubMenu(hMainMenu, 2);
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
	return hMainMenu;
}
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance,
									LPSTR lpCmdLine, INT nCmdShow)
{
	dwBkBrush = CreateSolidBrush( COLORS[0] );
	dwScrollBrush = CreateSolidBrush( RGB(64,64,64) );
	dwSliderBrush = CreateSolidBrush( COLORS[1] );
	hTermFont = CreateFont( 18,0,0,0,FW_MEDIUM, FALSE,FALSE,FALSE,
						DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
						DEFAULT_QUALITY, FIXED_PITCH, wsFontFace);
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
	wc.hbrBackground = dwBkBrush;
	wc.lpszClassName = L"TWnd";
	wc.lpszMenuName = 0;
	if ( !RegisterClassEx(&wc) ) return 0;
	hInst = hInstance;

	OleInitialize( NULL );
	httport = host_Init();
	term_Init( );

	hwndMain = CreateWindowEx( WS_EX_LAYERED, L"TWnd", L"",
						WS_TILEDWINDOW|WS_VISIBLE|WS_CLIPCHILDREN,
						CW_USEDEFAULT, CW_USEDEFAULT,
						800, 480, NULL, NULL, hInst, NULL );
	iConnectCount = 1;

	if ( *lpCmdLine==0 )
		PostMessage(hwndMain, WM_SYSCOMMAND, ID_CONNECT, 0);
	else
		host_Open( lpCmdLine );

	LoadDict(L"tinyTerm.hist");
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
	autocomplete_Destroy( );
	host_Destory();
	OleUninitialize();
	WSACleanup();

	return 0;
}
void CopyText( )
{
	char *ptr = buff+sel_left;
	int len = sel_right-sel_left;
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
			if ( host_Status()==HOST_CONNECTED )
				host_Send(p, len);
			else
				term_Disp(p);
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
	term_Learn_Prompt();
	
	char *p=(char *)files, *p1;
	if ( host_Type()==SSH ) {
		char rdir[1024];
		term_Pwd(rdir, 1024);
		strcat(rdir, "/");

		do {
			p1 = strchr(p, 0x0a);
			if ( p1!=NULL ) *p1++=0;
			scp_write(p, rdir);
		}
		while ( (p=p1)!=NULL );
	}
	if ( host_Type()==SFTP ) {
		char cmd[1024];
		strcpy(cmd, "put ");
		cmd[1023]=0;
		term_Disp("\n");
		do {
			p1 = strchr(p, 0x0a);
			if ( p1!=NULL ) *p1++=0;
			strncpy(cmd+4, p, 1019);
			sftp_cmd(cmd);
		}
		while ( (p=p1)!=NULL );
	}
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
	if ( host_Type()==NETCONF ) {
		netconf_Send(cmds, strlen(cmds));
		free(cmds);
	}
	else {
		if ( bScriptRun ) {
			cmd_Disp(L"a script is still running");
			free(cmds);
		}
		else {
			term_Learn_Prompt();
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
		term_Learn_Prompt();
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
					if ( host_Status()==HOST_CONNECTED )
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