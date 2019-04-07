//
// "$Id: tiny.c 38710 2019-03-20 14:35:10 $"
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
#include <process.h>
#include <direct.h>
#include <fcntl.h>
#include <time.h>
#include <versionhelpers.h>

#define ID_SCRIPT0		1000
#define ID_CONNECT0		2000
#define WM_DPICHANGED	0x02E0
#define SM_CXPADDEDBORDER 92

float dpi = 96;
int iTitleHeight;
int iFontSize = 16;
WCHAR wsFontFace[32] = L"Consolas";
WCHAR Title[256] = L"    Term    Script    Options                     ";

char *cwd = NULL;			//stores current working directory
const char *homedir;
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
\tVerision 1.3, ©2018-2019 Yongchao Fan, All rights reserved\r\n";
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
static HWND hwndMain, hwndCmd;
static RECT termRect, wndRect;
static HFONT hTermFont;
static HMENU hMainMenu, hMenu[4];
const int TTERM=0, SCRIPT=1, OPTION=2;
int menuX[4];
TERM *pt;

static int iFontHeight, iFontWidth;
static int iTransparency = 255;
static int iScriptDelay = 250;
static int iConnectCount=1, iScriptCount=1, httport;
static BOOL bFocus = FALSE, bLocalEdit = FALSE;
static BOOL bFTPd = FALSE, bTFTPd = FALSE;
static BOOL bScriptRun=FALSE, bScriptPaused=FALSE;

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
	if ( cwd!=NULL ) _chdir( cwd );		//GetFilename changes working directory
										//change it back before return
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
	cf.hwndOwner = hwndMain;
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
	hTermFont = CreateFont(iFontSize*(dpi/96),0,0,0,FW_MEDIUM, FALSE,FALSE,FALSE,
						DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
						DEFAULT_QUALITY, FIXED_PITCH, wsFontFace);
	tiny_Font(hwndMain);
}

void tiny_Cmd( char *cmd )
{
	if (strncmp(cmd,"~Transparency",13)==0)	{
		iTransparency = atoi(cmd+14);
		if ( iTransparency!=255  )
			CheckMenuItem( hMainMenu, ID_TRANSP, MF_BYCOMMAND|MF_CHECKED);
	}		
	else if ( strncmp(cmd,"~ScriptDelay",12)==0) {
		iScriptDelay = atoi(cmd+12);
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
			tiny_Font(hwndMain);
		}
	}
}
void menu_Add(char *cmd, WCHAR *wcmd)
{
	if ( strnicmp(cmd,"com", 3)==0 ||
		 strncmp(cmd, "ssh", 3)==0 || 
		 strncmp(cmd, "sftp", 4)==0 || 
		 strncmp(cmd, "telnet",6)==0 || 
		 strncmp(cmd, "netconf",7)==0 )
		AppendMenu( hMenu[TTERM], 0, ID_CONNECT0+iConnectCount++, wcmd);
}
void menu_Size()
{
	iTitleHeight = ( GetSystemMetrics(SM_CYFRAME)
				 + GetSystemMetrics(SM_CYCAPTION)
				 + GetSystemMetrics(SM_CXPADDEDBORDER) );
	HFONT oldFont = 0;
	RECT menuRect = { 0, 0, 0, 0};
	HDC wndDC = GetWindowDC(hwndMain);
    
{
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
	}
	if ( oldFont ) SelectObject( wndDC, oldFont );
	ReleaseDC(hwndMain, wndDC);
}
void menu_Init()
{
	hMainMenu = LoadMenu( hInst, MAKEINTRESOURCE(IDMENU_MAIN));
	hMenu[0] = hMenu[TTERM]  = GetSubMenu(hMainMenu, 0);
	hMenu[1] = hMenu[SCRIPT] = GetSubMenu(hMainMenu, 1);
	hMenu[2] = GetSubMenu(hMainMenu, 2);
	menu_Size();
	
 	if ( !LoadDict( "tinyTerm.hist" ) && homedir!=NULL )
 	{
		char hist_fn[MAX_PATH];
		strcpy(hist_fn, homedir);
		strcat( hist_fn, "\\Documents\\tinyTerm\\" );
		if ( GetFileAttributesA( hist_fn )!= FILE_ATTRIBUTE_DIRECTORY )
			CreateDirectoryA( hist_fn, NULL );
		_chdir( hist_fn );
		LoadDict( "tinyTerm.hist" );
				
		if ( GetFileAttributesA( "script\\" )!= FILE_ATTRIBUTE_DIRECTORY )
		{
			CreateDirectoryA( "script\\", NULL );
			FILE *fp = fopen("script\\scp_to_folder.js", "w+");
			if ( fp!=NULL ) 
			{
				fprintf(fp, "%s", SCP_TO_FOLDER); 
				fclose(fp);
			}
		}
	}
	cwd = _getcwd( NULL, 0 );
	WCHAR *wcmd = autocomplete_First();
	while ( *wcmd!=0 ) {
		char cmd[256];
		wchar_to_utf8(wcmd, -1, cmd, 256);
		if ( *cmd=='!' ) menu_Add(cmd+1, wcmd+1);
		if ( *cmd=='~' ) tiny_Cmd(cmd);
		wcmd = autocomplete_Next();
	}
	
	WIN32_FIND_DATA FindFileData;
	HANDLE hFind = FindFirstFile(L"script\\*.*", &FindFileData);
	if (hFind != INVALID_HANDLE_VALUE) 
	{
		do {
			if ( FindFileData.cFileName[0]!=L'.' )
				InsertMenu( hMenu[SCRIPT], -1, MF_BYPOSITION,
							ID_SCRIPT0+iScriptCount++, FindFileData.cFileName);
		} while( FindNextFile(hFind, &FindFileData) );
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
						wndRect.top+iTitleHeight, i, hwndMain, NULL );
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
	if ( cch >= 0 ) 
	{
		wcmd[cch] = 0;
		cch = wchar_to_utf8(wcmd, cch+1, cmd, 256);
		BOOL added = autocomplete_Add(wcmd);
		if ( *cmd=='~' ) 
		{
			tiny_Cmd( cmd );
		}
		else if ( *cmd=='!' )
		{
			if ( added ) menu_Add(cmd+1, wcmd+1);
			if ( strncmp(cmd+1, "scp ", 4)==0 && host_Type( pt->host )==SSH )
				CreateThread(NULL, 0, scper, strdup(cmd+5), 0, NULL);
			  else if ( strncmp(cmd+1, "tun",  3)==0 && host_Type( pt->host )==SSH )
				CreateThread(NULL, 0, tuner, strdup(cmd+4), 0, NULL);
			  else
				term_Cmd( pt, cmd, NULL );
		}
		else
		{
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
	if ( uMsg==WM_KEYDOWN )
	{
		if ( GetKeyState(VK_CONTROL) & 0x8000 ) 			//CTRL+
		{ 				
			char cmd = 0;
			if ( wParam==54 ) cmd = 30;						//^
			if ( wParam>64 && wParam<91 ) cmd = wParam-64;	//A-Z
			if ( wParam>218&&wParam<222 ) cmd = wParam-192;	//[\]
			if ( cmd ) term_Send( pt, &cmd, 1 );
		}
		else 
		{
			switch ( wParam ) 
			{
			case VK_UP: 	cmd_Disp(autocomplete_Prev()); break;
			case VK_DOWN: 	cmd_Disp(autocomplete_Next()); break;
			case VK_RETURN: cmd_Enter(); break;
			}
		}
	}
	return CallWindowProc(wpOrigCmdProc, hwnd, uMsg, wParam, lParam);
}
void tiny_Title( char *buf )
{
	utf8_to_wchar(buf, strlen(buf)+1, Title+50, 200);
	Title[250] = 0;
	SetWindowText(hwndMain, Title);
	if ( *buf ) {
		menu_Enable( ID_CONNECT, FALSE);
		menu_Enable( ID_DISCONN, TRUE);
	}
	else { 
		menu_Enable( ID_CONNECT, TRUE);
		menu_Enable( ID_DISCONN, FALSE);
	}
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
	InvalidateRect( hwndMain, &lineRect, TRUE );
}
void tiny_Redraw_Term()
{
	InvalidateRect( hwndMain, &termRect, TRUE );
}
void tiny_Scroll()
{
static BOOL bScrollbar = FALSE;
	if ( pt->scroll_y ) {
		if ( !bScrollbar ) {
			bScrollbar = TRUE;
			ShowScrollBar(hwndMain, SB_VERT, TRUE);
		}
		SetScrollRange(hwndMain, SB_VERT, 0, pt->cursor_y, TRUE);
		SetScrollPos(hwndMain, SB_VERT, pt->cursor_y+pt->scroll_y, TRUE);
	}
	else {
		if ( bScrollbar ) {
			bScrollbar = FALSE;
			ShowScrollBar(hwndMain, SB_VERT, FALSE);
		}
	}

	tiny_Redraw_Term(  );
}
void tiny_Beep()
{
	PlaySound(L"Default Beep", NULL, SND_ALIAS|SND_ASYNC);
}
BOOL tiny_Edit(BOOL e)
{
	if ( bLocalEdit==e ) return FALSE;	//nothing to change
	SendMessage(hwndMain, WM_COMMAND, ID_EDIT, 0);
	return TRUE;
}
void tiny_Paint(HDC hDC, RECT rcPaint)
{
	WCHAR wbuf[1024];
	RECT text_rect = {0, 0, 0, 0};
	SelectObject(hDC, hTermFont);
	int y = pt->screen_y+pt->scroll_y; if ( y<0 ) y = 0;
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
				int cch = utf8_to_wchar(pt->buff+i, len, wbuf, 1024);
				TextOutW(hDC, dx, dy, wbuf, cch);
				DrawText(hDC, wbuf, cch, &text_rect, DT_CALCRECT|DT_NOPREFIX);
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

	int cch = utf8_to_wchar(pt->buff+pt->line[pt->cursor_y],
								pt->cursor_x-pt->line[pt->cursor_y], wbuf, 1024);
	if ( cch > 0 ) {
		DrawText(hDC, wbuf, cch, &text_rect, DT_CALCRECT|DT_NOPREFIX);
	}
	else {
		text_rect.left = text_rect.right = 0;
	}
	if ( bLocalEdit ) {
		MoveWindow( hwndCmd, text_rect.right, (pt->cursor_y-pt->screen_y)*iFontHeight,
						termRect.right-text_rect.right, iFontHeight, TRUE );
	}
	if ( bFocus ) {
		SetCaretPos( text_rect.right+1,
					(pt->cursor_y-pt->screen_y)*iFontHeight+iFontHeight*3/4);
		pt->bCursor ? ShowCaret(hwndMain) : HideCaret(hwndMain);
	}
}
const WCHAR *PROTOCOLS[]={L"Serial ", L"telnet ", L"ssh ", L"sftp ", L"netconf "};
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
					conn[0] = L'!';
					if ( proto==0 ) {
						ComboBox_GetText(hwndPort, conn+1, 127);
						wcscat(conn, L":");
						ComboBox_GetText(hwndHost, conn+wcslen(conn), 127);
					}
					else {
						ComboBox_GetText(hwndProto, conn+1, 127);
						int len = wcslen(conn);
						ComboBox_GetText(hwndHost, conn+len, 127);
						int i;
						for ( i=0; i<host_cnt; i++ )
							if ( wcscmp(conn+len,HOSTS[i])==0 ) break;
						if ( i==host_cnt && host_cnt<32 ) {
							HOSTS[host_cnt++] = _wcsdup(conn+len);
							ComboBox_AddString(hwndHost,HOSTS[++i]);
						}
						wcscat(conn, L":");
						len = wcslen(conn);
						ComboBox_GetText(hwndPort, conn+len, 128);
						if ( wcscmp(conn+len, PORTS[proto])==0 ) conn[len-1]=0;
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
	case ID_COPYALL:
		if ( OpenClipboard(hwndMain) ) {
			EmptyClipboard( );
			pt->sel_left = 0;
			pt->sel_right = pt->cursor_x;
			CopyText( );
			tiny_Redraw_Term( );
			CloseClipboard( );
		}
		break;
	case ID_ECHO:	//toggle local echo
		menu_Check( ID_ECHO, term_Echo(pt) );
		break;
	case ID_EDIT:	//toggle local edit
		if ( (bLocalEdit=!bLocalEdit) ) {
			int edit_left = (pt->cursor_x-pt->line[pt->cursor_y])*iFontWidth;
			MoveWindow( hwndCmd, edit_left,	termRect.bottom-iFontHeight, 
								termRect.right-edit_left, iFontHeight, TRUE );
			SetFocus(hwndCmd);
		}
		else {
			MoveWindow( hwndCmd, 0, termRect.bottom-1, 1, 1, TRUE );
			SetFocus(hwndMain);
		}
		menu_Check( ID_EDIT, bLocalEdit);
		tiny_Redraw_Term( );
		break;
	case ID_TRANSP:
		if ( lParam>0 && lParam<256 ) 
			iTransparency = lParam;
		else
			iTransparency =  (iTransparency==255) ? 224 : 255;
		SetLayeredWindowAttributes(hwndMain,0,iTransparency,LWA_ALPHA);
		menu_Check( ID_TRANSP, iTransparency!=255 );
		break;
	case ID_FONT:
		if ( fontDialog() ) tiny_Font( hwndMain );
		break;
	case ID_ZOOM:
		if ( iFontSize>18 ) iFontSize = 8; 
		tiny_Fontsize(iFontSize+2);
		break;
	case ID_FTPD:
		bFTPd = ftp_Svr(bFTPd?NULL:getFolderName(L"Choose root directory"));
		menu_Check( ID_FTPD, bFTPd );
		break;
	case ID_TFTPD:
		bTFTPd = tftp_Svr(bTFTPd?NULL:getFolderName(L"Choose root directory"));
		menu_Check( ID_TFTPD, bTFTPd );
		break;
	case ID_TERM:
		menu_Popup(0);
		break;
	case ID_SCRIPT:
		menu_Popup(1);
		break;
	case ID_OPTIONS:
		menu_Popup(2);
		break;
	case ID_SCPAUSE:
		bScriptPaused = !bScriptPaused;
		menu_Check( ID_SCPAUSE, bScriptPaused );
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
			GetMenuString(hMenu[SCRIPT], wParam, wfn+wcslen(wfn), 248, 0);
			OpenScript(wfn);
			break;
		}
		if ( wParam>ID_CONNECT0 && wParam<=ID_CONNECT0+iConnectCount ) {
			WCHAR wcmd[256];
			wcmd[0]=L'!';
			GetMenuString(hMenu[TTERM], wParam, wcmd+1, 248, 0);
			cmd_Disp(wcmd);
			cmd_Enter();
			break;
		}
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
		hwndCmd = CreateWindow(L"EDIT", NULL,
							WS_CHILD|WS_VISIBLE|ES_AUTOHSCROLL|ES_NOHIDESEL,
							0, 0, 1, 1, hwnd, (HMENU)0, hInst, NULL);
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
		term_Keydown( pt, wParam );
		break;
	case WM_ACTIVATE:
		SetFocus( bLocalEdit? hwndCmd : hwndMain );
		tiny_Redraw_Term(  );
		break;
	case WM_VSCROLL: {
			int yPos;
      SCROLLINFO si; 
			si.cbSize = sizeof (si);
      si.fMask  = SIF_ALL;
			si.nPage = pt->size_y-1;
      GetScrollInfo (hwnd, SB_VERT, &si);

      yPos = si.nPos;
      switch (LOWORD (wParam))
      {
        case SB_LINEUP: 	si.nPos -= 1; break;
        case SB_LINEDOWN: si.nPos += 1; break;
        case SB_PAGEUP:   si.nPos -= si.nPage; break;
        case SB_PAGEDOWN: si.nPos += si.nPage; break;
        case SB_THUMBTRACK:si.nPos = si.nTrackPos; break;
        default:break; 
      }
      si.fMask = SIF_POS;
      SetScrollInfo (hwnd, SB_VERT, &si, TRUE);
      GetScrollInfo (hwnd, SB_VERT, &si);
      if (si.nPos != yPos) term_Scroll(pt, yPos-si.nPos);
		}
		break;
	case WM_MOUSEWHEEL:
		term_Scroll( pt, GET_WHEEL_DELTA_WPARAM(wParam)/40 );
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
		tiny_Redraw_Term(  );
		break;
	}
	case WM_LBUTTONDOWN: {
		int x = GET_X_LPARAM(lParam)/iFontWidth;
		int y = (GET_Y_LPARAM(lParam)+2)/iFontHeight;
		y += pt->screen_y + pt->scroll_y;
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
			tiny_Redraw_Term(  );
		}
		break;
	case WM_LBUTTONUP:
		if ( pt->sel_right!=pt->sel_left ) {
			int sel_min = min(pt->sel_left, pt->sel_right);
			int sel_max = max(pt->sel_left, pt->sel_right);
			pt->sel_left = sel_min;
			pt->sel_right = sel_max;
			if ( OpenClipboard(hwndMain) ) {
				if ( pt->sel_left!=pt->sel_right ) {
					EmptyClipboard( );
					CopyText();
				}
				CloseClipboard( );
			}
		}
		else {
			pt->sel_left = pt->sel_right = 0;
			tiny_Redraw_Term(  );
		}
		ReleaseCapture();
		SetFocus( bLocalEdit ? hwndCmd : hwndMain );
		break;
	case WM_RBUTTONUP:
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
	case WM_DROPFILES:
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
	if ( !IsWindows7OrGreater() ) 
		wcscpy( wsFontFace, L"Courier New" );

	TERM aTerm;
	HOST aHost;
	term_Construct( &aTerm );
	host_Construct( &aHost );
	aTerm.host = &aHost;
	aHost.term = &aTerm;

	pt = &aTerm;
	homedir = getenv("USERPROFILE");
	if ( homedir!=NULL ) {
		strncpy(aHost.homedir, homedir, MAX_PATH-64);
		aHost.homedir[MAX_PATH-64] = 0;
	}

	HDC sysDC = GetDC(0);
	dpi = GetDeviceCaps(sysDC, LOGPIXELSX);
	ReleaseDC(0, sysDC);
	
	dwBkBrush = (HBRUSH)GetStockObject(BLACK_BRUSH);
	hwndMain = CreateWindowEx( WS_EX_LAYERED, L"TWnd", Title,
						WS_TILEDWINDOW|WS_VSCROLL, 
						CW_USEDEFAULT, CW_USEDEFAULT,
						CW_USEDEFAULT, CW_USEDEFAULT,
						NULL, NULL, hInst, NULL );
	ShowScrollBar(hwndMain, SB_VERT, FALSE);


	if ( *lpCmdLine==0 )
		PostMessage(hwndMain, WM_SYSCOMMAND, ID_CONNECT, 0);
	else
		host_Open( pt->host, lpCmdLine );

	HACCEL haccel = LoadAccelerators(hInst, MAKEINTRESOURCE(IDACCEL_MAIN));
	MSG msg;
	while ( GetMessage(&msg, NULL, 0, 0) ){
		if (!TranslateAccelerator(hwndMain, haccel,  &msg)) {
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}
	}

	if ( pt->bLogging ) term_Logg( pt, NULL );
	if ( cwd!=NULL ) free( cwd );
	LoadDict( "tinyTerm.hist" );
	SaveDict( "tinyTerm.hist" );
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

BOOL LoadDict( char *fn )
{
	FILE *fp = fopen(fn, "r");
	if ( fp!=NULL ) {
		char cmd[256];
		while ( fgets( cmd, 256, fp )!=NULL ) {
			int l = strlen(cmd)-1;
			while ( l>0 && cmd[l]<0x10 ) cmd[l--]=0;
			WCHAR wcmd[256];
			utf8_to_wchar(cmd, -1, wcmd, 256);
			autocomplete_Add(wcmd);
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
//		if ( wcscmp(wsFontFace, L"Consolas")!=0 ) 
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
		while ( *wp!=0 & *wp!='~' ) {
			char cmd[256];
			int n = wchar_to_utf8(wp, -1, cmd, 256);
			if ( n>3 ) fprintf(fp, "%s\n", cmd);
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
	
	char *p=(char *)files, *p1;
	if ( host_Type( pt->host )==SSH ) {
		char rdir[1024];
		term_Pwd( pt, rdir, 1024 );
		strcat(rdir, "/");

		do {
			p1 = strchr(p, 0x0a);
			if ( p1!=NULL ) *p1++=0;
			if ( *p ) scp_write( pt->host, p, rdir );
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
	bScriptRun=TRUE; bScriptPaused = FALSE;
	menu_Enable( ID_SCRUN, FALSE);
	menu_Enable( ID_SCPAUSE, TRUE);
	menu_Enable( ID_SCQUIT, TRUE);
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
				if ( *p0=='~' ) 
					tiny_Cmd( p0 );
				else
					term_Cmd( pt, p0, NULL );
				Sleep(iScriptDelay);
			}
		}
		if ( p0!=p1 ) { p0 = p1+1; *p1 = 0x0a; }
		while ( bScriptPaused && bScriptRun ) Sleep(500);
	}
	while ( p2!=NULL && bScriptRun );
	free(cmds);
	cmd_Disp( bScriptRun ? L"script completed" : L"script stopped");
	bScriptRun = bScriptPaused = FALSE;
	menu_Enable( ID_SCRUN, TRUE);
	menu_Enable( ID_SCPAUSE, FALSE);
	menu_Enable( ID_SCQUIT, FALSE);
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