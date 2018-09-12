#include <direct.h>
#include <windows.h>
#include <windowsx.h>
#include <commdlg.h>
#include <commctrl.h>
#include <ole2.h>
#include <process.h>
#include <shlobj.h>
#include <shellapi.h>
#include "tiny.h"

extern char comm_status;
extern char buff[], attr[], c_attr;
extern int line[];
extern int cursor_y, cursor_x;
extern int size_y, size_x;
extern int screen_y;
extern int scroll_y;
extern int sel_left, sel_right;
extern BOOL bAppCursor, bCursor;
extern BOOL bLogging;
extern BOOL bEcho;

#define iCmdHeight		16

#define ID_CONNECT		20
#define ID_DISCONN		68	//^D
#define ID_ECHO			69	//^E
#define ID_FONTSIZE		70	//^F
#define ID_ABOUT		65
#define ID_COPYBUF		73
#define ID_SAVEBUF		74
#define ID_LOADBUF		75
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

const char WELCOME[]="\r\n\r\n\r\n\
\ttinyTerm is a terminal emulator for network engineers,\r\n\r\n\
\ta single executable in 100K bytes that features:\r\n\r\n\r\n\
\t* Serial/Telnet/SSH connection\r\n\r\n\
\t* 8192 lines of scrollback buffer\r\n\r\n\
\t* Command line autocompletion\r\n\r\n\
\t* Drag and Drop to run command batches\r\n\r\n\
\t* FTPd/TFTPd for file transfer\r\n\r\n\
\t* xmlhttp interface at 127.0.0.1:%d\r\n\r\n\r\n\
\tBy yongchaofan@gmail.com		09-11-2018\r\n\r\n";

static HINSTANCE hInst;						// Instance handle
static HWND hwndMain, hwndCmd;				// window handles
static HMENU hConnectMenu, hBufferMenu, hScriptMenu, hOptionMenu;
static RECT termRect, wndRect;
static int iFontHeight, iFontWidth, iScrollWidth;
static int nConnectCnt, nScriptCnt, httport;
static BOOL bTransparent = FALSE;
static BOOL bScrollbar = FALSE;
static BOOL bCaret = FALSE;

BOOL host_Paste( void );
void buff_Copy( char *ptr, int len );
void dict_Load( char *fn );

#define SCRIPTS "scripts\0*.tl1;*.vbs;*.js\0All\0*.*\0\0"
#define LOGFILES "Log file\0*.log;*.txt\0All\0*.*\0\0"
#define OPENFLAGS OFN_PATHMUSTEXIST|OFN_FILEMUSTEXIST
#define SAVEFLAGS OFN_PATHMUSTEXIST|OFN_NOREADONLYRETURN|OFN_OVERWRITEPROMPT
char * fileDialog( const char *szFilter, DWORD dwFlags )
{
static char fname[2048];
	BOOL ret;
	OPENFILENAME ofn;						// common dialog box structure

	fname[0]=0;
	memset(&ofn, 0, sizeof(OPENFILENAME));
	ofn.lStructSize = sizeof(OPENFILENAME);
	ofn.hwndOwner = hwndMain;
	ofn.lpstrFile = fname;
	ofn.nMaxFile = 2047;
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
	return ret ? fname : NULL;
}
char *getFolderName(char *title)
{
	static BROWSEINFO bi;
	static char szFolder[MAX_PATH];
	char szDispName[MAX_PATH];
	LPITEMIDLIST pidl;

	memset(&bi, 0, sizeof(BROWSEINFO));
	bi.hwndOwner = 0;
	bi.pidlRoot = NULL;
	bi.pszDisplayName = szDispName;
	bi.lpszTitle = title;
	bi.ulFlags = BIF_RETURNONLYFSDIRS;
	bi.lpfn = NULL;
	bi.lParam = 0;
	pidl = SHBrowseForFolder(&bi);
	if ( pidl != NULL )
		if ( SHGetPathFromIDList(pidl, szFolder) )
			return szFolder;
	return NULL;
}
void cmd_Send(void)
{
static char cmd[256];
	int cch = SendMessage(hwndCmd, WM_GETTEXT, (WPARAM)255, (LPARAM)cmd);
	if ( cch >= 0 ) {
		cmd[cch] = 0; 
		autocomplete_Add(cmd);
		switch ( *cmd ) {
		case '!': comm_Open( cmd ); break;
		case '#': term_Exec( cmd+1 ); break;
		case '/': buff_Srch( cmd+1 ); break;
		case '^': cmd[1]-=64; comm_Send( cmd+1, 1 ); break;
		default: if ( scroll_y!=0 ) {
					scroll_y = 0; tiny_Redraw( FALSE );
				  }
				  if ( comm_status==CONN_CONNECTED ) {
					cmd[cch]='\r';
					comm_Send(cmd, cch+1);
					
				  }
				  if ( comm_status==CONN_IDLE ) 
				  	comm_Open(cmd);
		}
	}
}
WNDPROC wpOrigCmdProc; 
LRESULT APIENTRY CmdEditProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) 
{ 
	BOOL ret=FALSE;
	if ( uMsg==WM_KEYDOWN ){
		ret = TRUE;
		if ( (GetKeyState(VK_CONTROL)&0x8000) == 0) {
			switch ( wParam ) {
			case VK_UP: cmd_Disp(autocomplete_Prev()); break;
			case VK_DOWN: cmd_Disp(autocomplete_Next()); break;
			case VK_RETURN: cmd_Send(); break;
			default: ret = FALSE;
			}
		}
		else {
			switch ( wParam ) {
			case VK_HOME: bCaret = TRUE; SetFocus( hwndMain ); break;
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
	return ret ? TRUE : CallWindowProc(wpOrigCmdProc, hwnd, uMsg, wParam, lParam);
}
void cmd_Disp( char *buf )
{
	SetWindowText(hwndCmd, buf);
	PostMessage(hwndCmd, EM_SETSEL, 0, -1);
}
void tiny_Title( char *buf )
{
	char Title[256] = "tinyTerm ";
	strcat(Title, buf); 
	SetWindowText(hwndMain, Title);
	ModifyMenu(hConnectMenu, ID_DISCONN, MF_BYCOMMAND, ID_DISCONN, 
					( *buf==0 ? "Connect..." : "Disconnect\t^D") );
	if ( *buf ) 
		comm_Size(size_x, size_y);
	else
		term_Disp("\r\n\033[31mDisconnected\r\n\033[37m");
}
void tiny_Redraw()
{
	InvalidateRect( hwndMain, &termRect, TRUE );
}
void tiny_Focus() 
{
	bCaret=TRUE;
	PostMessage(hwndMain, WM_ACTIVATE, 0, 0);
}
void tiny_Connecting()
{
	term_Parse("Connecting...", 13);
	SetTimer(hwndMain, CONN_TIMER, 1000, NULL);
}
HFONT hFont[3];
const int FONTWEIGHT[3] = { 14, 16, 20 };
LONG_PTR dwBkBrush;
const COLORREF BKCOLOR = 	RGB(32,32,96);
const COLORREF COLORS[8] ={ RGB(0,0,0), RGB(224, 0, 0), RGB(0,224,0), 
							RGB(224,224,0), RGB(0,0,224), RGB(224,0,224),
							RGB(0,244,244), RGB(224, 224, 224) };
static int iFontSize = 1;
unsigned char cAlpha = 255;
void tiny_Font( HWND hwnd )
{
	iFontHeight = FONTWEIGHT[iFontSize]; 
	iFontWidth = iFontSize + 7;
	iScrollWidth = GetSystemMetrics(SM_CXVSCROLL)+1;
	GetWindowRect( hwnd, &wndRect );
	int x = wndRect.left;
	int y = wndRect.top;
	wndRect.right = x + iFontWidth*size_x + (bScrollbar?iScrollWidth:1);
	wndRect.bottom = y + iFontHeight*size_y + iCmdHeight+1;
	AdjustWindowRect(&wndRect, WS_TILEDWINDOW, FALSE);
	MoveWindow( hwnd, x, y, wndRect.right-wndRect.left, 
							wndRect.bottom-wndRect.top, TRUE );
}
void tiny_Scrollbar(BOOL b)
{
	if ( bScrollbar==b ) return;
	
	bScrollbar=b;
	ShowScrollBar(hwndMain, SB_VERT, b);
	GetWindowRect( hwndMain, &wndRect );
	SetWindowPos(hwndMain, HWND_TOP, wndRect.left, wndRect.top,
		wndRect.right-wndRect.left+(bScrollbar?1:-1)*iScrollWidth, 
		wndRect.bottom-wndRect.top, SWP_FRAMECHANGED );
	GetClientRect( hwndMain, &termRect );
	termRect.bottom -= iCmdHeight;
	tiny_Redraw( FALSE );
}
void check_Option( HMENU hMenu, DWORD id, BOOL op)
{
	CheckMenuItem( hMenu, id, MF_BYCOMMAND|(op?MF_CHECKED:MF_UNCHECKED));
}
BOOL CALLBACK ConnectProc(HWND hwndDlg, UINT message, 
							WPARAM wParam, LPARAM lParam)
{
	static HWND hwndProto, hwndPort, hwndHost, hwndStatic;
	switch ( message ) {
	case WM_INITDIALOG:
		hwndStatic = GetDlgItem(hwndDlg, IDSTATIC);
		hwndProto  = GetDlgItem(hwndDlg, IDPROTO);
		hwndPort   = GetDlgItem(hwndDlg, IDPORT);
		hwndHost   = GetDlgItem(hwndDlg, IDHOST);
		ComboBox_AddString(hwndProto,"Serial"); 
		ComboBox_AddString(hwndProto,"Telnet"); 
		ComboBox_AddString(hwndProto,"SSH"); 
		ComboBox_SetCurSel(hwndProto, 1);
		ComboBox_AddString(hwndPort,"2024"); 
		ComboBox_AddString(hwndPort,"23"); 
		ComboBox_AddString(hwndPort,"22"); 
		ComboBox_SetCurSel(hwndPort, 1);
		ComboBox_AddString(hwndHost,"192.168.1.1"); 
		ComboBox_SetCurSel(hwndHost, 0);
		break;
	case WM_COMMAND: 
		if ( HIWORD(wParam)==CBN_SELCHANGE ) {
			if ( (HWND)lParam==hwndProto ) {
				ComboBox_ResetContent(hwndHost);
				ComboBox_ResetContent(hwndPort); 
				int proto = ComboBox_GetCurSel(hwndProto);
				if ( proto==0 ) {
					for ( int i=1; i<16; i++ ) {
						char port[32];
						sprintf( port, "\\\\.\\COM%d", i );
						HANDLE hPort = CreateFile( port, GENERIC_READ, 0, NULL,
													OPEN_EXISTING, 0, NULL);
						if ( hPort != INVALID_HANDLE_VALUE ) {
							ComboBox_AddString(hwndPort,port+4); 
							CloseHandle( hPort );
						}
					}
					ComboBox_AddString(hwndHost,"9600,n,8,1"); 
					ComboBox_AddString(hwndHost,"19200,n,8,1"); 
					ComboBox_AddString(hwndHost,"38400,n,8,1"); 
					ComboBox_AddString(hwndHost,"57600,n,8,1"); 
					ComboBox_AddString(hwndHost,"115200,n,8,1"); 
					ComboBox_SetCurSel(hwndPort, 0);
					ComboBox_SetCurSel(hwndHost, 0);
					Static_SetText(hwndStatic, "Settings:");
				}
				else {
					ComboBox_AddString(hwndPort,"2024"); 
					ComboBox_AddString(hwndPort,"23"); 
					ComboBox_AddString(hwndPort,"22"); 
					ComboBox_SetCurSel(hwndPort, proto);
					ComboBox_AddString(hwndHost,"192.168.1.1"); 
					ComboBox_SetCurSel(hwndHost, 0);
					Static_SetText(hwndStatic, "Hostname:");
				}
			}
		}
		else {
			char conn[256];
			int proto;
			switch ( LOWORD(wParam) ) {
			case IDCONNECT:
					proto = ComboBox_GetCurSel(hwndProto);
					if ( proto==0 ) {
						ComboBox_GetText(hwndPort, conn, 128);
						strcat(conn, ":");
						ComboBox_GetText(hwndHost, conn+strlen(conn), 128);
					}
					else {
						if ( proto==1 )
							conn[0] = 0;
						else
							strcpy(conn, "ssh ");
						ComboBox_GetText(hwndHost, conn+strlen(conn), 128);
						strcat(conn, ":");
						ComboBox_GetText(hwndPort, conn+strlen(conn), 128);
					}
					cmd_Disp(conn);
					comm_Open(conn);
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
	switch ( wParam ) {
	case ID_DISCONN: if ( comm_status!=CONN_IDLE ) 
						comm_Close(); 
					 else 
						DialogBox(hInst, MAKEINTRESOURCE(IDD_CONNECT),
									hwndMain, (DLGPROC)ConnectProc);
					 break;
	case ID_COPYBUF: buff_Copy( buff, line[cursor_y] ); break;
	case ID_ABOUT  :{
					 term_Clear( ); 
					 char welcome[1024];
					 sprintf(welcome, WELCOME, httport);
					 term_Disp(welcome);
					}
					 break;
	case ID_LOGGING: if ( !bLogging ) 
						buff_Logg( fileDialog(LOGFILES,SAVEFLAGS) );
					  else
						buff_Logg( NULL ); 
					 check_Option( hBufferMenu, ID_LOGGING, bLogging ); 
					 break;
	case ID_ECHO:	 check_Option( hOptionMenu, ID_ECHO, bEcho=!bEcho ); 
					 break;
	case ID_FONTSIZE: if ( ++iFontSize==3 ) iFontSize=0;
					 tiny_Font( hwndMain ); 
					 break;
	case ID_TRANSPARENT: 
				bTransparent=!bTransparent;
				check_Option( hOptionMenu, ID_TRANSPARENT, bTransparent );
				cAlpha = bTransparent?224:255;
				SetLayeredWindowAttributes(hwndMain,0,cAlpha,0x02); //LWA_ALPHA
				break;
	case ID_FTPD:	 ftp_Svr(""); break;
	case ID_TFTPD:	 tftp_Svr(""); break;
	case ID_SCPAUSE: script_Pause(); break;
	case ID_SCSTOP:  script_Stop(); break;
	case ID_SCRIPT:  script_Open( fileDialog(SCRIPTS,OPENFLAGS) ); break;
	default:	if ( wParam>ID_CONNECT && wParam<=ID_CONNECT+nConnectCnt ) { 
					 if ( comm_status==CONN_IDLE ) {
						char pName[256];
						GetMenuString(hConnectMenu, wParam, pName, 256, 0);
						comm_Open( pName );
					}
					break;
				}

				if ( wParam>ID_SCRIPT && wParam<=ID_SCRIPT+nScriptCnt ) { 
					char fn[256], port[256];
					GetMenuString(hScriptMenu, wParam, fn, 255, 0);
					sprintf(port, "%d", httport); 
					_spawnlp( _P_NOWAIT, "WScript.exe", "WScript.exe", 
								fn, port, NULL );
					break;
				}
				return FALSE; 
	}
	return TRUE;
}
LRESULT CALLBACK MainWndProc(HWND hwnd,UINT msg,WPARAM wParam,LPARAM lParam)
{
	PAINTSTRUCT ps;
	SCROLLINFO si;
	si.cbSize = sizeof (si);
	int i;

	static HDC hDC, hBufferDC;
	static HBITMAP hBufferMap;

	switch (msg) {
	case WM_CREATE:
		hwndCmd = CreateWindow("EDIT",	NULL, 
							WS_CHILD|WS_VISIBLE|ES_AUTOHSCROLL, 
							0, 0, 1, 1, hwnd, (HMENU)0, hInst,NULL);
		wpOrigCmdProc = (WNDPROC)SetWindowLongPtr(hwndCmd, 
							GWLP_WNDPROC, (LONG_PTR)CmdEditProc); 
		HFONT hEditFont = CreateFont( iCmdHeight,0,0,0,FW_MEDIUM,
							FALSE,FALSE,FALSE,
							ANSI_CHARSET,OUT_TT_PRECIS,CLIP_DEFAULT_PRECIS,
							ANTIALIASED_QUALITY, VARIABLE_PITCH,TEXT("Arial"));
		SendMessage( hwndCmd, WM_SETFONT, (WPARAM)hEditFont, TRUE );
		SendMessage( hwndCmd, EM_SETLIMITTEXT, 255, 0);
		dwBkBrush = (LONG_PTR)CreateSolidBrush( BKCOLOR );
		for ( i=0; i<3; i++ ) hFont[i] = CreateFont(FONTWEIGHT[i],0,0,0,
							FW_LIGHT, FALSE,FALSE,FALSE,
							ANSI_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
							ANTIALIASED_QUALITY, FIXED_PITCH, TEXT("Consolas"));
		DragAcceptFiles( hwnd, TRUE );
		SetLayeredWindowAttributes(hwnd, 0, cAlpha, LWA_ALPHA);
		GetClientRect( hwnd, &termRect );
		termRect.bottom -= iCmdHeight;
		hDC = GetDC(hwnd);
		hBufferDC = CreateCompatibleDC(hDC);
		hBufferMap = CreateCompatibleBitmap(hDC,
							termRect.right, termRect.bottom);
		ReleaseDC(hwnd, hDC);
		tiny_Font( hwnd );
		PostMessage(hwnd, WM_SYSCOMMAND, ID_DISCONN, 0);break;
		break;
	case WM_SIZE:
		GetClientRect( hwnd, &termRect );
		termRect.bottom -= iCmdHeight;
		size_x = termRect.right / iFontWidth;
		size_y = termRect.bottom / iFontHeight;
		MoveWindow( hwndCmd, 0, termRect.bottom, termRect.right, 
												iCmdHeight, TRUE );
		term_Size();
		comm_Size( size_x, size_y );
		DeleteDC(hBufferDC);
		DeleteObject(hBufferMap);
		hDC = GetDC(hwnd);
		hBufferDC = CreateCompatibleDC(hDC);
		hBufferMap = CreateCompatibleBitmap(hDC, termRect.right,
												termRect.bottom );
		SelectObject(hBufferDC, hBufferMap);
		SelectObject(hBufferDC, hFont[iFontSize]);
		SetBkMode(	 hBufferDC, OPAQUE);//TRANSPARENT
		ReleaseDC(hwnd, hDC);
		tiny_Redraw( FALSE );
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
					int n = j; 
					while ( attr[n]==attr[j] ) {
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
					TextOut( hBufferDC, dx, dy, buff+j, n-j );
					dx += iFontWidth*(n-j);
					j=n;
				}
				dy += iFontHeight;
			}
			if ( bCaret && bCursor ) {
				RECT cursor_rect;
				cursor_rect.left = (cursor_x-line[cursor_y])*iFontWidth;
				cursor_rect.top = (cursor_y-screen_y+1)*iFontHeight-2;
				cursor_rect.right = cursor_rect.left+8;
				cursor_rect.bottom = cursor_rect.top+3;
				FillRect(hBufferDC, &cursor_rect, GetStockObject(WHITE_BRUSH)); 
			} 
			BitBlt(hDC, 0, 0, termRect.right, termRect.bottom, 
									hBufferDC, 0, 0, SRCCOPY);
		}
		EndPaint( hwnd, &ps );
		if ( bScrollbar ) {
			SetScrollRange( hwnd, SB_VERT, 0, cursor_y, TRUE );
			SetScrollPos( hwnd, SB_VERT, cursor_y+scroll_y, TRUE );
		}
		break;
	case WM_CHAR: {
			char key = wParam;
			term_Send( &key, 1 );
			if ( scroll_y!=0 ) { scroll_y=0; tiny_Redraw( FALSE ); }
		}
		break;
	case WM_KEYDOWN:
		switch (wParam) {
		case VK_UP:    term_Send(bAppCursor?"\033OA":"\033[A",3); break;
		case VK_DOWN:  term_Send(bAppCursor?"\033OB":"\033[B",3); break;
		case VK_RIGHT: term_Send(bAppCursor?"\033OC":"\033[C",3); break;
		case VK_LEFT:  term_Send(bAppCursor?"\033OD":"\033[D",3); break;
		case VK_DELETE:term_Send("\177",1); break;
		case VK_PRIOR: PostMessage(hwnd, WM_MOUSEWHEEL, 
									MAKEWPARAM(0, (size_y-1)*40),0); break;
		case VK_NEXT:  PostMessage(hwnd, WM_MOUSEWHEEL, 
									MAKEWPARAM(0, -(size_y-1)*40),0); break;
		case VK_END: if ( GetKeyState(VK_CONTROL)&0x8000 ) {
						SetFocus( hwndCmd ); 
						bCaret = FALSE;
					 }
		}
		break;
	case WM_TIMER:
		if ( comm_status!=CONN_CONNECTING ) 
			KillTimer(hwndMain, CONN_TIMER);
		else
			term_Parse(".", 1);
		break;
	case WM_VSCROLL: 
		si.fMask  = SIF_ALL;
		GetScrollInfo ( hwnd, SB_VERT, &si);
		switch (LOWORD (wParam)){
		case SB_LINEUP: si.nPos -= 1; break;
		case SB_LINEDOWN: si.nPos += 1; break;
		case SB_PAGEUP: si.nPos -= size_y; break;
		case SB_PAGEDOWN: si.nPos += size_y; break;
		case SB_THUMBTRACK: si.nPos = si.nTrackPos; break;
		}
		si.fMask = SIF_POS;
		SetScrollInfo (hwnd, SB_VERT, &si, TRUE);
		GetScrollInfo (hwnd, SB_VERT, &si);
		if (si.nPos != scroll_y+cursor_y ) {
			scroll_y = si.nPos- cursor_y;
			tiny_Redraw( FALSE );
		}
		break;
	case WM_MOUSEWHEEL: 
		scroll_y -= GET_WHEEL_DELTA_WPARAM(wParam)/40;
		if ( scroll_y<-screen_y ) scroll_y = -screen_y;
		if ( scroll_y>0 ) scroll_y = 0;
		tiny_Scrollbar(scroll_y<0);
		tiny_Redraw( FALSE );
		break;
	case WM_ACTIVATE: 
		SetFocus( bCaret ? hwnd : hwndCmd ); 
		tiny_Redraw( FALSE );
		break; 
	case WM_LBUTTONDOWN:
		{	
			int x = GET_X_LPARAM(lParam)/iFontWidth;
			int y = (GET_Y_LPARAM(lParam)+2)/iFontHeight+screen_y+scroll_y;
			sel_right = sel_left = min(line[y]+x, line[y+1]);
		}
		break;
	case WM_MOUSEMOVE:
		if ( MK_LBUTTON && wParam ) {
			int x = GET_X_LPARAM(lParam)/iFontWidth;
			int y = (GET_Y_LPARAM(lParam)+2)/iFontHeight+screen_y+scroll_y;
			sel_right = min(line[y]+x, line[y+1]);
			tiny_Redraw( FALSE );
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
			tiny_Redraw( FALSE );
		}	
		SetFocus( hwnd ); bCaret = TRUE; 
		break;
	case WM_RBUTTONUP:
			host_Paste();
			break;
	case WM_DROPFILES: {
			char fname[MAX_PATH];
			HDROP hDrop = (HDROP)wParam;
			DragQueryFile( hDrop, 0, fname, MAX_PATH );
			DragFinish( hDrop );
			if ( comm_Connected() ) 
				script_Open( fname );
			else
				buff_Load( fname );
		}
		break;
	case WM_ERASEBKGND: return 1;
	case WM_DESTROY:
	case WM_CLOSE: 		
		DeleteDC(hBufferDC);
		DeleteObject(hBufferMap);
		PostQuitMessage(0); break;
	case WM_CTLCOLOREDIT: 
		SetTextColor( (HDC)wParam, COLORS[7] );
		SetBkColor( (HDC)wParam, BKCOLOR ); 
		return dwBkBrush;		//brush must be returned for update
	case WM_SYSCOMMAND: 
		if ( menu_Command( wParam ) ) break;
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
	hFind = FindFirstFile("*.vbs", &FindFileData);
	if (hFind != INVALID_HANDLE_VALUE) {
		do {
			InsertMenu(hScriptMenu, 0, MF_BYPOSITION, ID_SCRIPT+nScriptCnt++, 
														FindFileData.cFileName);
		} while ( FindNextFile(hFind, &FindFileData) );
	}

	HMENU hSysMenu = GetSystemMenu(hwnd, FALSE);
	InsertMenu(hSysMenu, 0, MF_BYPOSITION|MF_SEPARATOR, 0, NULL);
	InsertMenu(hSysMenu, 0, MF_BYPOSITION, ID_ABOUT, "About");
	InsertMenu(hSysMenu, 0, MF_BYPOSITION|MF_POPUP, 
							(UINT_PTR)hOptionMenu, "Options");
	InsertMenu(hSysMenu, 0, MF_BYPOSITION|MF_POPUP, 
							(UINT_PTR)hScriptMenu, "Script");
	InsertMenu(hSysMenu, 0, MF_BYPOSITION, ID_COPYBUF, "Copy all");
	InsertMenu(hSysMenu, 0, MF_BYPOSITION, ID_LOGGING, "Logging...");
	InsertMenu(hSysMenu, 0, MF_BYPOSITION, ID_DISCONN, "Connect...");
}
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, 
									LPSTR lpCmdLine, INT nCmdShow)
{
	WNDCLASSEX wc;
	wc.cbSize 		= sizeof( wc ); 
	wc.style 		= 0;
	wc.cbClsExtra	= 0;
	wc.cbWndExtra	= 0;
	wc.lpfnWndProc 	= &MainWndProc;
	wc.hInstance 	= hInstance;
	wc.hIcon = LoadIcon(hInstance, MAKEINTRESOURCE(IDICON_TL1));
	wc.hIconSm 		= 0;
	wc.hCursor		= LoadCursor(NULL,IDC_ARROW);
	wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
	wc.lpszClassName = TEXT("TWnd");
	wc.lpszMenuName = 0;
	if ( !RegisterClassEx(&wc) ) return 0;
	hInst = hInstance;

	OleInitialize( NULL );
	term_Init( );

	hwndMain = CreateWindowEx( WS_EX_LAYERED, 
							TEXT("TWnd"), TEXT("tinyTerm"),
							WS_TILEDWINDOW|WS_VISIBLE, 
							CW_USEDEFAULT, CW_USEDEFAULT,
							1, 1, NULL, NULL, hInst, NULL );
	tiny_Sysmenu( hwndMain );
	
	autocomplete_Init(hwndCmd);
	drop_Init( hwndCmd );
	httport = comm_Init();

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
	if ( *p!=0 ) comm_Open( p );
	dict_Load( "tinyTerm.dic");

//Message Loop
	MSG msg;
	while ( GetMessage(&msg,NULL,0,0) ){
		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}
	if ( bLogging ) buff_Logg( NULL );

//Clean up
	comm_Destory();
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
		HANDLE hglbCopy = GlobalAlloc(GMEM_MOVEABLE, len+1);
		   if ( hglbCopy != NULL ) {
			char *p = GlobalLock(hglbCopy); 
			strncpy( p, ptr, len );
			GlobalUnlock(hglbCopy); 
			SetClipboardData(CF_TEXT, hglbCopy);
		}
		CloseClipboard( );
	}
}

BOOL host_Paste( void )
{
	BOOL ret = FALSE;
	if ( OpenClipboard(hwndMain) ) {
		HANDLE hglb = GetClipboardData(CF_TEXT);
		char *lptstr = (char *)GlobalLock(hglb);
		if (lptstr != NULL) {
			if ( comm_status==CONN_CONNECTED ) 
				term_Send(lptstr, strlen(lptstr));
			else
				term_Parse(lptstr, strlen(lptstr));
			GlobalUnlock(hglb);
			ret = TRUE;
		}
		CloseClipboard();
	}
	return ret;
}