//
// "$Id: tiny.c 40008 2019-05-12 21:35:10 $"
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
#include <direct.h>
#include <shlobj.h>
#include <shlwapi.h>

#define ID_SCRIPT0		1000
#define ID_CONNECT0		2000
#define WM_DPICHANGED	0x02E0
#define SM_CXPADDEDBORDER 92

float dpi = 96;
int iTitleHeight;
int iFontSize = 16;
WCHAR wsFontFace[32] = L"Consolas";
WCHAR Title[256] = L"    Term    Script    Options                     ";

char TINYTERM[]="\n\033[32mtinyTerm > \033[37m";
const char WELCOME[]="\n\
\ttinyTerm is a simple, small and scriptable terminal emulator,\n\n\
\ta serial/telnet/ssh/sftp/netconf client with unique features:\n\n\n\
\t    * small portable exe less than 256KB\n\n\
\t    * command history and autocompletion\n\n\
\t    * text based batch command automation\n\n\
\t    * drag and drop to transfer files via scp\n\n\
\t    * scripting interface at xmlhttp://127.0.0.1:%d\n\n\n\
\tstore: https://www.microsoft.com/store/apps/9NXGN9LJTL05\n\n\
\thomepage: https://yongchaofan.github.io/tinyTerm/\n\n\n\
\tVerision 1.6.1, ©2018-2019 Yongchao Fan, All rights reserved\r\n";
const char SCP_TO_FOLDER[]="\
var xml = new ActiveXObject(\"Microsoft.XMLHTTP\");\n\
var port = \"8080/?\";\n\
if ( WScript.Arguments.length>0 ) port = WScript.Arguments(0)+\"/?\";\n\
var filename = term(\"!Selection\");\n\
var objShell = new ActiveXObject(\"Shell.Application\");\n\
var objFolder = objShell.BrowseForFolder(0,\"Destination folder\",0x11,\"\");\n\
if ( objFolder ) term(\"!scp :\"+filename+\" \"+objFolder.Self.path);\n\
function term( cmd ) {\n\
   xml.Open (\"GET\", \"http://127.0.0.1:\"+port+cmd, false);\n\
   xml.Send();\n\
   return xml.responseText;\n\
}";

const COLORREF COLORS[16] ={RGB(0,0,0), 	RGB(192,0,0), RGB(0,192,0),
							RGB(192,192,0), RGB(32,96,240), RGB(192,0,192),
							RGB(0,192,192), RGB(192,192,192),
							RGB(0,0,0), 	RGB(240,0,0), RGB(0,240,0),
							RGB(240,240,0), RGB(32,96,240), RGB(240,0,240),
							RGB(0,240,240), RGB(240,240,240) };
static HINSTANCE hInst;
static HBRUSH dwBkBrush;
static HWND hwndTerm, hwndCmd;
static RECT termRect, wndRect;
static HFONT hTermFont;
static HMENU hMainMenu, hMenu[4];
const int TTERM=0, SCRIPT=1, OPTION=2, CONTEX=3;
int menuX[4];
TERM *pt;

static int iFontHeight, iFontWidth;
static int iTransparency = 255;
static int iConnectCount=0, iScriptCount=0, httport;
static BOOL bFocus=TRUE, bLocalEdit=FALSE, bScrollbar=FALSE;
static BOOL bScriptRun=FALSE, bScriptPause=FALSE;
static BOOL bFTPd=FALSE, bTFTPd=FALSE;

void CopyText( );
void PasteText( );
BOOL LoadDict( char *fn );
BOOL SaveDict( char *fn );
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
	ofn.hwndOwner = hwndTerm;
	ofn.lpstrFile = wname;
	ofn.nMaxFile = MAX_PATH-1;
	ofn.lpstrFilter = szFilter;
	ofn.nFilterIndex = 1;
	ofn.lpstrFileTitle = NULL;
	ofn.nMaxFileTitle = 0;
	ofn.lpstrInitialDir = NULL;
	ofn.Flags = dwFlags | OFN_NOCHANGEDIR;
	if ( dwFlags&OFN_OVERWRITEPROMPT )
		ret = GetSaveFileName(&ofn);
	else
		ret = GetOpenFileName(&ofn);
	return ret ? wname : NULL;
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
		if ( SHGetPathFromIDList(pidl, wfolder) )
		{
			wchar_to_utf8(wfolder, -1, szFolder, MAX_PATH);
			return szFolder;
		}
	return NULL;
}
BOOL fontDialog()
{
	LOGFONT lf;
	CHOOSEFONT cf;
	ZeroMemory(&cf, sizeof(cf));
	cf.lStructSize = sizeof (cf);
	cf.hwndOwner = hwndTerm;
	cf.lpLogFont = &lf;
	cf.Flags = CF_SCREENFONTS|CF_FIXEDPITCHONLY|CF_INITTOLOGFONTSTRUCT;
	if ( GetObject(hTermFont, sizeof(lf), &lf)==0 ) ZeroMemory(&lf, sizeof(lf));

	if ( ChooseFont(&cf) ) 
	{
		DeleteObject(hTermFont);
		hTermFont = CreateFontIndirect(&lf);
		iFontSize = lf.lfHeight;
		if ( iFontSize<0 ) iFontSize = -iFontSize;
		wcscpy(wsFontFace, lf.lfFaceName);
		return TRUE;
	}
	return FALSE;
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
	wndRect.bottom = y + iFontHeight*pt->size_y;
	AdjustWindowRect(&wndRect, WS_TILEDWINDOW, FALSE);
	MoveWindow( hwnd, x, y, wndRect.right-wndRect.left,
							wndRect.bottom-wndRect.top, TRUE );
	SendMessage( hwndCmd, WM_SETFONT, (WPARAM)hTermFont, TRUE );
}
void tiny_Fontsize(int i)
{
	DeleteObject(hTermFont);
	iFontSize = i;
	hTermFont = CreateFont(iFontSize*(dpi/96),0,0,0,FW_MEDIUM,FALSE,FALSE,FALSE,
						DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
						DEFAULT_QUALITY, FIXED_PITCH, wsFontFace);
	tiny_Font(hwndTerm);
}

void tiny_Cmd( char *cmd )
{
	if (strncmp(cmd,"~Transparency",13)==0)	{
		iTransparency = atoi(cmd+14);
		if ( iTransparency!=255  )
			CheckMenuItem( hMainMenu, ID_TRANSP, MF_BYCOMMAND|MF_CHECKED);
	}		
	else if ( strncmp(cmd, "~LocalEdit", 10)==0) {
		bLocalEdit = TRUE;
		CheckMenuItem( hMainMenu, ID_EDIT, MF_BYCOMMAND|MF_CHECKED);
	}
	else if ( strncmp(cmd, "~FontSize", 9)==0 )	{
		tiny_Fontsize(atoi(cmd+9));
	}
	else if ( strncmp(cmd, "~FontFace", 9)==0 )	{
		utf8_to_wchar(cmd+10, -1, wsFontFace, 32);
		wsFontFace[31] = 0;
	}
	else if ( strncmp(cmd, "~TermSize", 9)==0 )	{
		char *p = strchr(cmd+9, 'x');
		if ( p!=NULL ) {
			pt->size_x = atoi(cmd+9);
			pt->size_y = atoi(p+1);
			tiny_Font(hwndTerm);
		}
	}
}
void menu_Add(WCHAR *wcmd)
{
	if ( wcsncmp(wcmd, L"com", 3)==0 ||
		 wcsncmp(wcmd, L"ssh", 3)==0 || 
		 wcsncmp(wcmd, L"sftp", 4)==0 || 
		 wcsncmp(wcmd, L"telnet",6)==0 || 
		 wcsncmp(wcmd, L"netconf",7)==0 )
		AppendMenu( hMenu[TTERM], 0, ID_CONNECT0+iConnectCount++, wcmd);
	if ( wcsncmp(wcmd, L"script ", 7)==0 ) 
		AppendMenu( hMenu[SCRIPT], 0, ID_SCRIPT0+iScriptCount++, wcmd+7);
}
void menu_Del(HMENU menu, int pos)
{
	int id = GetMenuItemID(menu, pos);
	if ( id>=ID_SCRIPT0 ) {
		WCHAR wcmd[264] = L"!script ";
		GetMenuString(menu, pos, wcmd+(id<ID_CONNECT0?8:1), 256, MF_BYPOSITION);
		DeleteMenu(menu, pos, MF_BYPOSITION);
		autocomplete_Del(wcmd);
	}
}
void menu_Size()
{
	iTitleHeight = GetSystemMetrics(SM_CYFRAME)
				 + GetSystemMetrics(SM_CYCAPTION)
				 + GetSystemMetrics(SM_CXPADDEDBORDER);
	HFONT oldFont = 0;
	RECT menuRect = { 0, 0, 0, 0};
	HDC wndDC = GetWindowDC(hwndTerm);

	oldFont = (HFONT)SelectObject(wndDC, GetStockObject(SYSTEM_FONT));
	menuX[0] = 32;
	DrawText(wndDC, L"  Term ", 7, &menuRect, DT_CALCRECT);
	menuX[1] = menuX[0]+(menuRect.right-menuRect.left)*(dpi/96);
	menuRect.left = menuRect.right = 0;
	DrawText(wndDC, L"Script", 6, &menuRect, DT_CALCRECT);
	menuX[2] = menuX[1]+(menuRect.right-menuRect.left)*(dpi/96);
	menuRect.left = menuRect.right = 0;
	DrawText(wndDC, L"Options", 7, &menuRect, DT_CALCRECT);
	menuX[3] = menuX[2]+(menuRect.right-menuRect.left)*(dpi/96);

	if ( oldFont ) SelectObject( wndDC, oldFont );
	ReleaseDC(hwndTerm, wndDC);
}
void menu_Init()
{
	hMainMenu = LoadMenu( hInst, MAKEINTRESOURCE(IDMENU_MAIN));
	hMenu[TTERM]  = GetSubMenu(hMainMenu, 0);
	hMenu[SCRIPT] = GetSubMenu(hMainMenu, 1);
	hMenu[OPTION] = GetSubMenu(hMainMenu, 2);
	hMenu[CONTEX] = GetSubMenu(hMainMenu, 3);
	menu_Size();

	if ( !LoadDict( "tinyTerm.hist" ) )
	{
		_chdir(getenv("USERPROFILE"));
		_mkdir("Documents\\tinyTerm");
		_chdir("Documents\\tinyTerm");
		LoadDict( "tinyTerm.hist" );

		FILE *fp = fopen("scp_to_folder.js", "r");
		if ( fp==NULL ) {
			fp = fopen("scp_to_folder.js", "w");
			if ( fp!=NULL ) {
				fprintf(fp, "%s", SCP_TO_FOLDER);
			}
		}
		if ( fp!=NULL ) fclose(fp);
		if ( autocomplete_Add(L"!script scp_to_folder.js") )
			menu_Add(L"script scp_to_folder.js");
	}
}
void menu_Check( DWORD id, BOOL op)
{
	CheckMenuItem( hMainMenu, id, MF_BYCOMMAND|(op?MF_CHECKED:MF_UNCHECKED));
}
void menu_Enable( DWORD id, BOOL op)
{
	EnableMenuItem( hMainMenu, id, MF_BYCOMMAND|(op?MF_ENABLED:MF_GRAYED));
}
void menu_Popup( int i )
{
	TrackPopupMenu(hMenu[i], TPM_LEFTBUTTON, wndRect.left+menuX[i]-24,
						wndRect.top+iTitleHeight, 0, hwndTerm, NULL );
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
void cmd_Enter(WCHAR *wcmd)
{
	char cmd[256];
	int cnt = wchar_to_utf8(wcmd, -1, cmd, 256);
	int added = autocomplete_Add(wcmd);
	if ( *cmd=='!' ) {
		if ( added ) menu_Add(wcmd+1);
		if ( strncmp(cmd+1,"scp ",4)==0 && host_Type(pt->host)==SSH )
			CreateThread(NULL, 0, scper, strdup(cmd+5), 0, NULL);
		else if ( strncmp(cmd+1,"tun",3)==0 && host_Type(pt->host)==SSH )
			CreateThread(NULL, 0, tuner, strdup(cmd+4), 0, NULL);
		else
			term_Cmd( pt, cmd, NULL );
	}
	else {
		if ( host_Status( pt->host )!=HOST_IDLE ) {
			cmd[cnt-1] = '\r';
			term_Send( pt, cmd, cnt ); 
		}
		else {
			term_Print(pt, "\033[33m%s\n", cmd); 
			host_Open(pt->host, cmd);
		}
	}
}
WNDPROC wpOrigCmdProc;
LRESULT APIENTRY CmdEditProc(HWND hwnd, UINT uMsg,
								WPARAM wParam, LPARAM lParam)
{
	WCHAR wcmd[256];
	if ( uMsg==WM_KEYDOWN )
	{
		if ( GetKeyState(VK_CONTROL) & 0x8000 ) 			//CTRL+
		{
			char cmd = 0;
			if ( wParam==54 ) cmd = 30;						//^
			if ( wParam>64 && wParam<91 ) cmd = wParam-64;	//A-Z
			if ( wParam>218&&wParam<222 ) cmd = wParam-192;	//[\]
			if ( cmd ) {
				term_Send( pt, &cmd, 1 );
				return 1;
			}
		}
		else 
		{
			switch ( wParam ) 
			{
			case VK_UP:   cmd_Disp(autocomplete_Prev()); break;
			case VK_DOWN: cmd_Disp(autocomplete_Next()); break;
			case VK_BACK: 
				if ( GetWindowText(hwndCmd, wcmd, 256)==0 ) {
					term_Send(pt, "\b", 1);
					return 1;
				}
				break;
			case VK_RETURN:
				if ( GetWindowText(hwndCmd, wcmd, 256)>=0 ) cmd_Enter(wcmd);
				break;
			}
		}
	}
	return CallWindowProc(wpOrigCmdProc, hwnd, uMsg, wParam, lParam);
}

char *tiny_Gets( char *prompt, BOOL bEcho)
{
	return ssh2_Gets( pt->host, prompt, bEcho );
}
void tiny_Redraw_Line()
{
	RECT lineRect = termRect;
	lineRect.top = (pt->cursor_y-pt->screen_y)*iFontHeight;
	lineRect.bottom = lineRect.top + iFontHeight;
	InvalidateRect( hwndTerm, &lineRect, TRUE );
}
void tiny_Redraw_Term()
{
	InvalidateRect( hwndTerm, &termRect, TRUE );
}
void tiny_Title( char *buf )
{
	utf8_to_wchar(buf, -1, Title+50, 200);
	SetWindowText(hwndTerm, Title);
	if ( host_Status(pt->host)==HOST_IDLE )
	{
		if ( bLocalEdit )
			term_Disp(pt, TINYTERM);
		else
			term_Print(pt, "\n\033[33mPress Enter to reconnect\n");
		menu_Enable( ID_CONNECT, TRUE);
		menu_Enable( ID_DISCONN, FALSE);
		menu_Check( ID_ECHO, pt->bEcho);
	}
	else {
		menu_Enable( ID_CONNECT, FALSE);
		menu_Enable( ID_DISCONN, TRUE);
		menu_Check( ID_ECHO, pt->bEcho);
	}
}
void tiny_Scroll( int lines )
{
	pt->screen_y -= lines;
	if ( pt->screen_y<0 || pt->screen_y>pt->cursor_y ) {
		pt->screen_y += lines;
		return;
	}
	if ( pt->screen_y<pt->cursor_y-pt->size_y+1 ) {
		SetScrollRange(hwndTerm, SB_VERT, 0, pt->cursor_y, TRUE);
		SetScrollPos(hwndTerm, SB_VERT, pt->screen_y, TRUE);
		if ( !bScrollbar ) {
			bScrollbar = TRUE;
			ShowScrollBar(hwndTerm, SB_VERT, TRUE);
			pt->screen_y -= lines;			//first pageup fix
		}
	}
	else {
		if ( bScrollbar ) {
			bScrollbar = FALSE;
			ShowScrollBar(hwndTerm, SB_VERT, FALSE);
		}
	}
	tiny_Redraw_Term( );
}
void tiny_Beep()
{
	PlaySound(L"Default Beep", NULL, SND_ALIAS|SND_ASYNC);
}
BOOL tiny_Edit(BOOL e)
{
	if ( bLocalEdit==e ) return FALSE;	//nothing to change
	SendMessage(hwndTerm, WM_COMMAND, ID_EDIT, 0);
	return TRUE;
}
void tiny_Paint(HDC hDC, RECT rcPaint)
{
	WCHAR wbuf[1024];
	RECT text_rect = {0, 0, 0, 0};
	SelectObject(hDC, hTermFont);
	int y = pt->screen_y;
	int sel_min = min(pt->sel_left, pt->sel_right);
	int sel_max = max(pt->sel_left, pt->sel_right);
	int dx, dy=rcPaint.top;
	for ( int l=dy/iFontHeight; l<pt->size_y; l++ ) 
	{
		dx = 0;
		int i = pt->line[y+l];
		while ( i<pt->line[y+l+1] ) {
			BOOL utf8 = FALSE;
			int j = i;
			while ( pt->attr[j]==pt->attr[i] ) {
				if ( (pt->buff[j]&0xc0)==0xc0 ) utf8 = TRUE;
				if ( ++j==pt->line[y+l+1] ) break;
				if ( j==sel_min || j==sel_max ) break;
			}
			if ( i>=sel_min&&i<sel_max ) {
				SetTextColor( hDC, COLORS[0] );
				SetBkColor( hDC, COLORS[7] );
			}
			else {
				SetTextColor( hDC, COLORS[pt->attr[i]&0x0f] );
				SetBkColor( hDC, COLORS[(pt->attr[i]>>4)&0x0f] );
			}
			int len = j-i;
			if ( pt->buff[j-1]==0x0a ) len--;	//remove unprintable 0x0a for XP
			if ( utf8 ) {
				int cnt = utf8_to_wchar(pt->buff+i, len, wbuf, 1024);
				TextOutW(hDC, dx, dy, wbuf, cnt);
				DrawText(hDC, wbuf, cnt, &text_rect, DT_CALCRECT|DT_NOPREFIX);
				dx += text_rect.right;
			}
			else {
				TextOutA(hDC, dx, dy, pt->buff+i, len);
				dx += iFontWidth*len;
			}
			i=j;
		}
		if ( dx < termRect.right )
		{
			RECT fillRect;
			fillRect.top = dy;
			fillRect.bottom = dy+iFontHeight;
			fillRect.left = dx;
			fillRect.right = termRect.right;
			FillRect(hDC, &fillRect, dwBkBrush);
		}
		dy += iFontHeight;
	}

	int cnt = utf8_to_wchar(pt->buff+pt->line[pt->cursor_y],
							pt->cursor_x-pt->line[pt->cursor_y], wbuf, 1024);
	DrawText(hDC, wbuf, cnt, &text_rect, DT_CALCRECT|DT_NOPREFIX);

	if ( bLocalEdit ) {
		MoveWindow( hwndCmd, text_rect.right,
					(pt->cursor_y-pt->screen_y)*iFontHeight,
					termRect.right-text_rect.right, iFontHeight, TRUE );
	}
	else {
		SetCaretPos( text_rect.right+1,
				(pt->cursor_y-pt->screen_y)*iFontHeight+iFontHeight*3/4);
		if ( pt->bCursor && bFocus)
			ShowCaret(hwndTerm);
		else
			HideCaret(hwndTerm);
	}
}
const WCHAR *PROTOCOLS[]={L"Serial ", L"telnet ", L"ssh ", L"sftp ", L"netconf "};
const WCHAR *PORTS[]    ={L"2024",   L"23",     L"22",  L"22",   L"830"};
const WCHAR *SETTINGS[] ={L"9600,n,8,1", L"19200,n,8,1", L"38400,n,8,1",
										L"57600,n,8,1", L"115200,n,8,1"};
void get_serial_ports(HWND hwndPort)
{
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
}
void get_hosts(HWND hwndHost)
{
	 ComboBox_AddString(hwndHost, L"192.168.1.1");
	 for ( int id=ID_CONNECT0; id<ID_CONNECT0+iConnectCount; id++ ) 
	{
		WCHAR wcmd[256], *p;
		GetMenuString(hMainMenu, id, wcmd, 256, MF_BYCOMMAND);
		p = wcschr(wcmd, L' ');
		if ( p!=NULL ) ComboBox_AddString(hwndHost,p+1);
	}
}
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
		for ( int i=0; i<5; i++ ) ComboBox_AddString(hwndProto,PROTOCOLS[i]);
		if ( proto==0 ) {
			ComboBox_SetCurSel(hwndProto, 0);
			proto = 2;
		}
		else {
			ComboBox_SetCurSel(hwndProto, proto);
			proto = 0;
		}
		PostMessage(hwndDlg, WM_COMMAND, CBN_SELCHANGE<<16, (LPARAM)hwndProto);
		//setup tooltip
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
		SetFocus(hwndHost);
		break;
	case WM_COMMAND:
		if ( HIWORD(wParam)==CBN_SELCHANGE ) {
			if ( (HWND)lParam==hwndProto ) {
				int new_proto = ComboBox_GetCurSel(hwndProto);
				if ( proto!=0 && new_proto==0 ) {
					ComboBox_ResetContent(hwndPort);
					get_serial_ports(hwndPort);
					ComboBox_ResetContent(hwndHost);
					for ( int i=0; i<5; i++ )
						ComboBox_AddString(hwndHost,SETTINGS[i]);
					ComboBox_SetCurSel(hwndHost, 0);
					Static_SetText(hwndStatic, L"Settings:");
					SendMessage(hwndTip, TTM_ACTIVATE, FALSE, 0);
				}
				if ( proto==0 && new_proto!=0 )  {
					ComboBox_ResetContent(hwndHost);
					get_hosts(hwndHost);
					ComboBox_SetCurSel(hwndHost, 0);
					ComboBox_ResetContent(hwndPort);
					for ( int i=0; i<5; i++ )
						ComboBox_AddString(hwndPort,PORTS[i]);
					Static_SetText(hwndStatic, L"Host:");
					SendMessage(hwndTip, TTM_ACTIVATE, TRUE, 0);
				}
				proto = new_proto;
				ComboBox_SetCurSel(hwndPort, proto);
			}
		}
		else {
			int proto;
			WCHAR wcmd[256], *conn = wcmd+1;
			wcmd[0] = L'!';
			switch ( LOWORD(wParam) ) {
			case IDCONNECT:
					proto = ComboBox_GetCurSel(hwndProto);
					if ( proto==0 ) {
						ComboBox_GetText(hwndPort, conn, 127);
						wcscat(conn, L":");
						ComboBox_GetText(hwndHost, conn+wcslen(conn), 127);
					}
					else {
						ComboBox_GetText(hwndProto, conn, 127);
						int len = wcslen(conn);
						ComboBox_GetText(hwndHost, conn+len, 127);
						SendMessage(hwndHost,CB_FINDSTRING,1,
												(LPARAM)(conn+len));
						if ( ComboBox_GetCurSel(hwndHost)==CB_ERR )
							ComboBox_AddString(hwndHost, conn+len);
						wcscat(conn, L":");
						len = wcslen(conn);
						ComboBox_GetText(hwndPort, conn+len, 128);
						if ( wcscmp(conn+len, PORTS[proto])==0 ) conn[len-1]=0;
					}
					cmd_Enter(wcmd);
			case IDCANCEL:
					EndDialog(hwndDlg, wParam);
					return TRUE;
			}
		}
	}
	return FALSE;
}
BOOL menu_Command( WPARAM wParam, LPARAM lParam )
{
	switch ( LOWORD(wParam) ) {
	case ID_ABOUT:{
			char welcome[1024];
			sprintf(welcome, WELCOME, httport);
			term_Disp( pt, welcome );
		}
		break;
	case ID_CONNECT:
		if ( host_Status( pt->host )==HOST_IDLE )
			DialogBox(hInst, MAKEINTRESOURCE(IDD_CONNECT), 
							hwndTerm, (DLGPROC)ConnectProc);
		break;
	case ID_DISCONN:
		if ( host_Status( pt->host )!=HOST_IDLE ) host_Close( pt->host );
		break;
	case ID_LOGG:
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
		menu_Check( ID_LOGG, pt->bLogging );
		break;
	case ID_SELALL:
		pt->sel_left = 0;
		pt->sel_right = pt->cursor_x;
		tiny_Redraw_Term();
//fall through to copy
	case ID_COPY:
		if ( OpenClipboard(hwndTerm) ) {
			EmptyClipboard( );
			CopyText( );
			CloseClipboard( );
		}
		break;
	case ID_PASTE:
		if ( OpenClipboard(hwndTerm) ) {
			PasteText();
			CloseClipboard();
		}
		break;
	case ID_MIDDLE:
		if ( pt->sel_left!=pt->sel_right ) 
			term_Send(pt, pt->buff+pt->sel_left, pt->sel_right-pt->sel_left);
		break;
	case ID_DELETE: {
		WCHAR wcmd[256];
		if ( GetWindowText(hwndCmd, wcmd, 256)>0 ) {
			autocomplete_Del(wcmd);
			cmd_Disp(autocomplete_Next());
		}
		break;
	}
	case ID_TAB: 
		if ( bLocalEdit ) {
			char cmd[256];
			WCHAR wcmd[256];
			Edit_ReplaceSel(hwndCmd, L"");
			GetWindowText(hwndCmd, wcmd, 256);
			SetWindowText(hwndCmd, L"");
			int cnt = wchar_to_utf8(wcmd, -1, cmd, 256);
			term_Send(pt, cmd, cnt-1);
		}
		term_Send(pt, LOWORD(wParam)==ID_TAB?"\t":"?", 1);
		break;
	case ID_PRIOR: tiny_Scroll( pt->size_y-1 ); break;
	case ID_NEXT:  tiny_Scroll( 1-pt->size_y ); break;
	case ID_ECHO:  menu_Check( ID_ECHO, term_Echo(pt) ); break;
	case ID_EDIT:
		bLocalEdit = !bLocalEdit;
		SetFocus(bLocalEdit ? hwndCmd : hwndTerm);
		if ( !bLocalEdit ) 
			MoveWindow( hwndCmd, 0, 0, 1, 1, TRUE );
		else {
			if ( host_Status(pt->host)==HOST_IDLE ) 
				term_Disp(pt, TINYTERM );
		}
		menu_Check( ID_EDIT, bLocalEdit);
		tiny_Redraw_Term();
		break;
	case ID_TRANSP:
		if ( lParam>0 && lParam<256 ) 
			iTransparency = lParam;
		else
			iTransparency =  (iTransparency==255) ? 224 : 255;
		SetLayeredWindowAttributes(hwndTerm, 0, iTransparency, LWA_ALPHA);
		menu_Check( ID_TRANSP, iTransparency!=255 );
		break;
	case ID_FONT:
		if ( fontDialog() ) tiny_Font( hwndTerm );
		break;
	case ID_FTPD:
		bFTPd = ftp_Svr(bFTPd?NULL:getFolderName(L"Choose root directory"));
		menu_Check( ID_FTPD, bFTPd );
		break;
	case ID_TFTPD:
		bTFTPd = tftp_Svr(bTFTPd?NULL:getFolderName(L"Choose root directory"));
		menu_Check( ID_TFTPD, bTFTPd );
		break;
	case ID_RUN: {
		WCHAR *wfn = fileDialog(L"Script\0*.js;*.vbs\0All\0*.*\0\0", 
													OFN_FILEMUSTEXIST);
		if ( wfn!=NULL )
		{
			WCHAR wport[MAX_PATH], wcwd[MAX_PATH];
			_wgetcwd(wcwd, MAX_PATH);
			PathRelativePathTo(wport, wcwd, FILE_ATTRIBUTE_DIRECTORY, wfn, 0);
			OpenScript( (wport[0]==L'.'&&wport[1]==L'\\') ? wport+2 : wfn);
		}
		break;
	}
	case ID_PAUSE: 
		bScriptPause = TRUE;
		if ( IDOK==MessageBox(hwndTerm, 
						L"script paused, OK to continue, Cancel to quit", 
						L"Script", MB_OKCANCEL|MB_ICONASTERISK) ){
			bScriptPause = FALSE;
			break;
		}
//fall through to quit
	case ID_QUIT:
		bScriptPause = bScriptRun = FALSE;
		MessageBox(hwndTerm, L"script stopped", L"Script", 
								MB_OK|MB_ICONASTERISK);
		break;
	case ID_TERM:
		menu_Popup(TTERM);
		break;
	case ID_SCRIPT:
		menu_Popup(SCRIPT);
		break;
	case ID_OPTIONS:
		menu_Popup(OPTION);
		break;
	default:
		if ( wParam>=ID_SCRIPT0 && wParam<ID_SCRIPT0+iScriptCount )
		{
			WCHAR wfn[256];
			GetMenuString(hMenu[SCRIPT], wParam, wfn, 256, 0);
			OpenScript(wfn);
		}
		else if ( wParam>=ID_CONNECT0 && wParam<ID_CONNECT0+iConnectCount )
		{
			WCHAR wcmd[256];
			GetMenuString(hMenu[TTERM], wParam, wcmd, 256, 0);
			cmd_Enter(wcmd);
		}
		else
			return FALSE;
	}
	return TRUE;
}
LRESULT CALLBACK MainWndProc(HWND hwnd,UINT msg,WPARAM wParam,LPARAM lParam)
{
	static HDC hDC;
	static WCHAR wm_chars[2]={0,0};

	switch (msg) {
	case WM_CREATE:
		hwndCmd = CreateWindow(L"EDIT", NULL, WS_CHILD|WS_VISIBLE 
							|ES_AUTOHSCROLL|ES_NOHIDESEL, 0, 0, 1, 1, 
							hwnd, (HMENU)0, hInst, NULL);
		wpOrigCmdProc = (WNDPROC)SetWindowLongPtr(hwndCmd,
							GWLP_WNDPROC, (LONG_PTR)CmdEditProc);
		SendMessage( hwndCmd, WM_SETFONT, (WPARAM)hTermFont, TRUE );
		SendMessage( hwndCmd, EM_SETLIMITTEXT, 255, 0);
		autocomplete_Init(hwndCmd);
		menu_Init();
		SetLayeredWindowAttributes(hwnd,0,iTransparency,LWA_ALPHA);
		drop_Init( hwnd, DropScript );
		DragAcceptFiles( hwnd, TRUE );
		hTermFont = CreateFont( iFontSize*(dpi/96), 0, 0, 0,
						FW_MEDIUM, FALSE, FALSE, FALSE,
						DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
						DEFAULT_QUALITY, FIXED_PITCH, wsFontFace);
		tiny_Font( hwnd );
		ShowWindow( hwnd, SW_SHOW );
		hDC = GetDC( hwnd );
		break;
	case WM_SIZE:
		GetClientRect( hwnd, &termRect );
		term_Size( pt, termRect.right/iFontWidth, termRect.bottom/iFontHeight );
		FillRect( hDC, &termRect, dwBkBrush );
		tiny_Redraw_Term(  );
	case WM_MOVE:
		GetWindowRect( hwnd, &wndRect );
		break;
	case WM_DPICHANGED:
		dpi = LOWORD(wParam);
		tiny_Fontsize(iFontSize);
		menu_Size();
		break;
	case WM_PAINT: {
			PAINTSTRUCT ps;
			if ( BeginPaint(hwnd, &ps)!=NULL ) 
				tiny_Paint(ps.hdc, ps.rcPaint);
			EndPaint( hwnd, &ps );
		}
		break;
	case WM_SETFOCUS:
		CreateCaret(hwnd, NULL, iFontWidth, iFontHeight/4);
		bFocus = TRUE;
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
		switch( wParam ) {
		case VK_DELETE:term_Send(pt, "\177",1); break;
		case VK_UP:    term_Send(pt, pt->bAppCursor?"\033OA":"\033[A",3); break;
		case VK_DOWN:  term_Send(pt, pt->bAppCursor?"\033OB":"\033[B",3); break;
		case VK_RIGHT: term_Send(pt, pt->bAppCursor?"\033OC":"\033[C",3); break;
		case VK_LEFT:  term_Send(pt, pt->bAppCursor?"\033OD":"\033[D",3); break;
		case VK_HOME:  term_Send(pt, pt->bAppCursor?"\033OH":"\033[H",3); break;
		case VK_END:   term_Send(pt, pt->bAppCursor?"\033OF":"\033[F",3); break;
		}
		if ( bScrollbar ) tiny_Scroll(pt->screen_y-(pt->cursor_y-pt->size_y+1));
		break;
	case WM_ACTIVATE:
		SetFocus( bLocalEdit? hwndCmd : hwndTerm );
		tiny_Redraw_Term(  );
		break;
	case WM_VSCROLL: 
		switch ( LOWORD (wParam) )
		{
		case SB_LINEUP:   tiny_Scroll( 1 ); break;
		case SB_LINEDOWN: tiny_Scroll(-1 ); break;
		case SB_PAGEUP:   tiny_Scroll( pt->size_y-1 ); break;
		case SB_PAGEDOWN: tiny_Scroll( 1-pt->size_y ); break;
		case SB_THUMBTRACK: {
				SCROLLINFO si; 
				si.cbSize = sizeof (si);
				si.fMask = SIF_ALL;
				GetScrollInfo (hwnd, SB_VERT, &si);
				tiny_Scroll(si.nPos-si.nTrackPos);
			}
		}
		break;
	case WM_MOUSEWHEEL:
		tiny_Scroll( GET_WHEEL_DELTA_WPARAM(wParam)/40 );
		break;
	case WM_LBUTTONDBLCLK: {
		int x = GET_X_LPARAM(lParam)/iFontWidth;
		int y = (GET_Y_LPARAM(lParam)+2)/iFontHeight+pt->screen_y;
		pt->sel_left = pt->line[y]+x;
		pt->sel_right = pt->sel_left;
		while ( --pt->sel_left>pt->line[y] )
			if ( pt->buff[pt->sel_left]==0x0a
				|| pt->buff[pt->sel_left]==0x20 ) {
				pt->sel_left++;
				break;
			}
		while ( ++pt->sel_right<pt->line[y+1]) {
			if ( pt->buff[pt->sel_right]==0x0a
				|| pt->buff[pt->sel_right]==0x20 )
				 break;
		}
		tiny_Redraw_Term(  );
		break;
	}
	case WM_LBUTTONDOWN: {
		int x = GET_X_LPARAM(lParam)/iFontWidth;
		int y = (GET_Y_LPARAM(lParam)+2)/iFontHeight;
		y += pt->screen_y;
		pt->sel_left = min(pt->line[y]+x, pt->line[y+1]);
		while ( isUTF8c(pt->buff[pt->sel_left]) ) pt->sel_left--;
		pt->sel_right = pt->sel_left;
		SetCapture(hwnd);
		break;
	}
	case WM_MOUSEMOVE:
		if ( MK_LBUTTON&wParam ) {
			int x = GET_X_LPARAM(lParam)/iFontWidth;
			int y = (GET_Y_LPARAM(lParam)+2)/iFontHeight;
			if ( y<0 ) {
				pt->screen_y += y*2;
				if ( pt->screen_y<0 ) pt->screen_y = 0;
			}
			if ( y>pt->size_y) {
				pt->screen_y += (y-pt->size_y)*2;
				if ( pt->screen_y>pt->cursor_y ) pt->screen_y=pt->cursor_y;
			}
			y += pt->screen_y;
			pt->sel_right = min(pt->line[y]+x, pt->line[y+1]);
			while ( isUTF8c(pt->buff[pt->sel_right]) ) pt->sel_right++;
			tiny_Redraw_Term( );
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
			tiny_Redraw_Term(  );
		}
		ReleaseCapture();
		SetFocus( bLocalEdit ? hwndCmd : hwndTerm );
		break;
	case WM_MBUTTONUP:
		if ( pt->sel_left!=pt->sel_right ) 
			term_Send(pt, pt->buff+pt->sel_left, pt->sel_right-pt->sel_left);
		break;
	case WM_CONTEXTMENU:
		TrackPopupMenu(hMenu[CONTEX], TPM_LEFTBUTTON, GET_X_LPARAM(lParam),
						GET_Y_LPARAM(lParam), 0, hwndTerm, NULL );
		break;
	case WM_NCLBUTTONDOWN: {
		int y = GET_Y_LPARAM(lParam)-wndRect.top;
		if ( y>0 && y<iTitleHeight ) {
			int x = GET_X_LPARAM(lParam)-wndRect.left;
			if ( x>menuX[0] && x<menuX[3] ) return 0;
		}
		return DefWindowProc(hwnd,msg,wParam,lParam);
	}
	case WM_NCLBUTTONUP: {
		int y = GET_Y_LPARAM(lParam)-wndRect.top;
		if ( y>0 && y<iTitleHeight ) {
			int x = GET_X_LPARAM(lParam)-wndRect.left;
			for ( int i=0; i<3; i++ ) {
				if ( x>menuX[i] && x<menuX[i+1] ) {
					menu_Popup( i );
					return 0;
				}
			}
		}
		return DefWindowProc(hwnd,msg,wParam,lParam);
	}
	case WM_MENURBUTTONUP: 
		menu_Del((HMENU)lParam, wParam);
		break;
	case WM_DROPFILES:
		if ( host_Type(pt->host)==SSH || host_Type(pt->host)==SFTP )
			DropFiles((HDROP)wParam);
		break;
	case WM_CTLCOLOREDIT:
		SetTextColor( (HDC)wParam, COLORS[3] );
		SetBkColor( (HDC)wParam, COLORS[0] );
		return (LRESULT)dwBkBrush;		//must return brush for update
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
		DestroyMenu(hMainMenu);
		DeleteObject(dwBkBrush);
		PostQuitMessage(0);
		break;
	case WM_COMMAND:
	case WM_SYSCOMMAND:
		if ( menu_Command( wParam, lParam ) ) return 1;
	default: return DefWindowProc(hwnd,msg,wParam,lParam);
	}
	return 0;
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
	wc.hIcon 		= 0;
	wc.hIconSm 		= LoadIcon(hInstance, MAKEINTRESOURCE(IDICON_TL1));
	wc.hCursor		= LoadCursor(NULL,IDC_ARROW);
	wc.hbrBackground = 0;
	wc.lpszClassName = L"TWnd";
	wc.lpszMenuName = 0;
	if ( !RegisterClassEx(&wc) ) return 0;
	hInst = hInstance;

	OleInitialize( NULL );
	WSADATA wsaData;
	WSAStartup(MAKEWORD(2, 2), &wsaData);
	libssh2_init (0);
	httport = http_Svr("127.0.0.1");

	TERM aTerm;
	HOST aHost;
	if ( !term_Construct( &aTerm ) ) return 0;
	host_Construct( &aHost );
	aTerm.host = &aHost;
	aHost.term = &aTerm;

	pt = &aTerm;
	const char *homedir = getenv("USERPROFILE");
	if ( homedir!=NULL )
	{
		strncpy(aHost.homedir, homedir, MAX_PATH-64);
		aHost.homedir[MAX_PATH-64] = 0;
	}

	HDC sysDC = GetDC(0);
	dpi = GetDeviceCaps(sysDC, LOGPIXELSX);
	ReleaseDC(0, sysDC);
	
	dwBkBrush = (HBRUSH)GetStockObject(BLACK_BRUSH);
	hwndTerm = CreateWindowEx( WS_EX_LAYERED,L"TWnd", Title,
						WS_TILEDWINDOW|WS_VSCROLL, 
						CW_USEDEFAULT, CW_USEDEFAULT,
						CW_USEDEFAULT, CW_USEDEFAULT,
						NULL, NULL, hInst, NULL );
	ShowScrollBar(hwndTerm, SB_VERT, FALSE);

	if ( *lpCmdLine==0 )
	{
		if ( bLocalEdit ) 
			term_Disp(pt,TINYTERM);
		else
			PostMessage(hwndTerm, WM_COMMAND, ID_CONNECT, 0);
	}
	else 
		host_Open( pt->host, lpCmdLine );

	HACCEL haccel = LoadAccelerators(hInst, MAKEINTRESOURCE(IDACCEL_MAIN));
	MSG msg;
	while ( GetMessage(&msg, NULL, 0, 0) )
	{
		if (!TranslateAccelerator(hwndTerm, haccel,  &msg))
		{
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}
	}

	if ( pt->bLogging ) term_Logg( pt, NULL );
	SaveDict( "tinyTerm.hist" );
	autocomplete_Destroy( );

	http_Svr("127.0.0.1");
	libssh2_exit();
	WSACleanup();
	OleUninitialize();

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

BOOL LoadDict( char *fn )
{
	FILE *fp = fopen(fn, "r");
	if ( fp!=NULL ) {
		char cmd[256];
		while ( fgets( cmd, 256, fp )!=NULL ) {
			cmd[255] = 0;
			cmd[strcspn(cmd, "\n")] = 0;	//remove trailing new line
			if ( *cmd=='~' ) 
				tiny_Cmd(cmd);
			else {
				WCHAR wcmd[256];
				utf8_to_wchar(cmd, -1, wcmd, 256);
				if( autocomplete_Add(wcmd) ) 
					if ( *cmd=='!' ) menu_Add(wcmd+1);
			}
		}
		fclose( fp );
		return TRUE;
	}
	return FALSE;
}
BOOL SaveDict( char *fn )
{
	FILE *fp = fopen(fn, "w");
	if ( fp!=NULL ) {
		if ( iFontSize!=16 ) 
			fprintf(fp, "~iFontSize %d\n", iFontSize);
		if ( wcscmp(wsFontFace, L"Consolas")!=0 ) 
		{
			char fontface[32];
			wchar_to_utf8(wsFontFace, -1, fontface, 32);
			fprintf(fp, "~FontFace %s\n", fontface);
		}
		if ( bLocalEdit )
			fprintf(fp, "~LocalEdit\n");
		if ( pt->size_x!=80 || pt->size_y!=25 ) 
			fprintf(fp, "~TermSize %dx%d\n", pt->size_x, pt->size_y);
		if ( iTransparency!=255 ) 
			fprintf(fp, "~Transparency %d\n", iTransparency);
		WCHAR *wp = autocomplete_First();
		while ( wp!=NULL ) {
			char cmd[256];
			int n = wchar_to_utf8(wp, -1, cmd, 256);
			if ( n>3  && *wp!='~' ) fprintf(fp, "%s\n", cmd);
			wp = autocomplete_Next();
		}
		fclose( fp );
		return TRUE;
	}
	return FALSE;
}

DWORD WINAPI uploader( void *files )	//upload files through scp or sftp
{
	for ( char *q=files; *q; q++ ) if ( *q=='\\' ) *q='/';
	term_Learn_Prompt( pt );
	
	char rdir[1024];
	if ( host_Type( pt->host )==SSH ) {
		term_Pwd( pt, rdir, 1022 );
		rdir[1023] = 0;
		strcat(rdir, "/");
	}
	else 
		term_Disp( pt, "\n" );

	char *p1=(char *)files, *p;
	while ( (p=p1)!=NULL ) {
		if ( *p==0 ) break;
		p1 = strchr(p, 0x0a);
		if ( p1!=NULL ) *p1++=0;
		if ( host_Type( pt->host )==SSH )
			scp_write( pt->host, p, rdir );
		else
			sftp_put( pt->host, p, pt->host->realpath);
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
	
	int len = 0;
	for ( int i=0; i<n; i++ ) {
		DragQueryFile( hDrop, i, wname, MAX_PATH );
		len += wchar_to_utf8(wname, -1, files+len, 1023);
		files[len-1] = '\n';
	}
	files[len] = 0;
	DragFinish( hDrop );
	CreateThread(NULL,0,(LPTHREAD_START_ROUTINE)uploader,(void *)files,0,NULL);
}
DWORD WINAPI scripter( void *cmds )
{
	if ( bScriptRun ) {
		MessageBox(hwndTerm, L"another script is running", L"Script", 
					MB_OK|MB_ICONSTOP);
		return 0;
	}

	char *p0=(char *)cmds, *p1;
	int iLoopCnt = -1, iWaitCnt = 0;
	bScriptRun=TRUE; bScriptPause = FALSE;
	menu_Enable( ID_RUN, FALSE);
	menu_Enable( ID_PAUSE, TRUE);
	menu_Enable( ID_QUIT, TRUE);
	while ( bScriptRun && p0!=NULL )
	{
		if ( iWaitCnt>0 ) {
			if ( (--iWaitCnt)%10==0 ) {
				WCHAR wait[16];
				wsprintf(wait, L"Wait %d  ", iWaitCnt/10);
				cmd_Disp(iWaitCnt==0 ? L"": wait);
			}
		}
		if ( iWaitCnt || bScriptPause ) { Sleep(100); continue; }

		p1=strchr(p0, 0x0a);
		int len = ( p1==NULL ? strlen(p0) : (p1++)-p0 ); 

		if ( strncmp( p0, "!Wait ", 6)==0 ) {
			iWaitCnt = atoi(p0+6)*10;
		}
		else if ( strncmp( p0, "!Loop ", 6)==0 ) {
			if ( iLoopCnt<0 ) iLoopCnt = atoi( p0+6 );
			if ( --iLoopCnt>0 ) p1 = (char*)cmds-1;
		}
		else if ( *p0=='!' ) {
			char cmd[256];
			strncpy(cmd, p0, len);
			cmd[len] = 0;
			term_Cmd(pt, cmd, NULL);
		}
		else {
			if ( host_Status(pt->host)==HOST_IDLE ) {
				term_Parse(pt, p0, len+1);
			}
			else if ( len>0 && *p0!='#' ) {
				term_Mark_Prompt( pt );
				term_Send( pt, p0, len );
				term_Waitfor_Prompt( pt );
			}
		}
		p0 = p1;
	}

	free(cmds);
	bScriptRun = bScriptPause = FALSE;
	menu_Enable( ID_RUN, TRUE);
	menu_Enable( ID_PAUSE, FALSE);
	menu_Enable( ID_QUIT, FALSE);
	return 0;
}
void DropScript( char *cmds )
{
	if ( host_Type( pt->host )==NETCONF ) {
		term_Send( pt, cmds, strlen(cmds));
		free(cmds);
	}
	else {
		term_Learn_Prompt( pt );
		CreateThread( NULL, 0, scripter, (void *)cmds, 0, NULL);
	}
}
void OpenScript( WCHAR *wfn )
{
	WCHAR wport[MAX_PATH];
	wsprintf(wport, L"!script %s", wfn);
	if ( autocomplete_Add(wport) ) menu_Add(wport+1);

	int len = wcslen(wfn);
	if ( wcscmp(wfn+len-3, L".js")==0 
		|| wcscmp(wfn+len-4, L".vbs")==0 ) {
		term_Learn_Prompt( pt );
		wsprintf(wport, L"%d", httport);
		ShellExecute( NULL, L"Open", wfn, wport, NULL, SW_SHOW );
	}
	else if ( wcscmp(wfn+len-5, L".html")==0)
	{
		wsprintf(wport, L"http://127.0.0.1:%d/%s", httport, wfn);
		ShellExecute( NULL, L"Open", wport, NULL, NULL, SW_SHOW );
	}
	else {	//batch commands in plain text format
		struct _stat sb;
		if ( _wstat(wfn, &sb)==0 ) {
			char *tl1s = (char *)malloc(sb.st_size+1);
			if ( tl1s!=NULL )
			{
				FILE *fpScript = _wfopen( wfn, L"rb" );
				if ( fpScript != NULL )
				{
					int lsize = fread( tl1s, 1, sb.st_size, fpScript );
					fclose( fpScript );
					if ( lsize > 0 )
					{
						tl1s[lsize]=0;
						DropScript(tl1s);
						return;
					}
				}
				free(tl1s);
			}
		}
	}
}