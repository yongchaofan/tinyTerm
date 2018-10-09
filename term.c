//
// "$Id: term.c 20182 2018-09-30 21:05:10 $"
//
// tinyTerm -- A minimal serail/telnet/ssh/sftp terminal emulator
//
// term.c is the minimal xterm implementation, only common ESCAPE 
// control sequences are supported for apps like top, vi etc.
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
#include <windows.h>
#include "tiny.h"

char buff[BUFFERSIZE], attr[BUFFERSIZE], c_attr;
int line[MAXLINES];
int cursor_x, cursor_y, save_x, save_y;
int size_x, size_y;
int screen_y;
int scroll_y;
int sel_left, sel_right;
int roll_top, roll_bot;
BOOL bLogging, bEcho;
BOOL bCursor, bAppCursor;
BOOL bEnter=FALSE, bEnter1=FALSE;

static FILE *fpLogFile;
static BOOL bPrompt=FALSE;
static char sPrompt[16]=";\n> ", *tl1text = NULL;
static int  iPrompt=4, iTimeOut=30, tl1len = 0;
static HANDLE hTL1Event;	//for term_TL1 reader

static BOOL bAlterScreen, bGraphic, bEscape, bTitle, bInsert;
static char title[256];
static int title_idx = 0;
unsigned char * vt100_Escape( unsigned char *sz, int cnt );

/*******************************Line functions************************/
void term_Init( )
{
	hTL1Event = CreateEvent( NULL, TRUE, FALSE, TEXT("TL1") );
	size_y = TERMLINES;
	size_x = TERMCOLS;
	bLogging = FALSE;
	bEcho = FALSE;
	term_Clear( );
}
void term_Clear( )
{
	memset( buff, 0, BUFFERSIZE );
	memset( attr, 0, BUFFERSIZE );
	memset( line, 0, MAXLINES*sizeof(int) );
	c_attr = 7; 
	cursor_y = cursor_x = 0;
	scroll_y = screen_y = 0;
	sel_left = sel_right = 0;
	bAlterScreen = FALSE;
	bAppCursor = FALSE;
	bGraphic = FALSE;
	bEscape = FALSE;
	bInsert = FALSE;
	bTitle = FALSE;
	bCursor = TRUE;
	tiny_Redraw( FALSE );
}
void term_Size()
{
	roll_top = 0;
	roll_bot = size_y-1;
	if ( !bAlterScreen ) {
		screen_y = cursor_y-size_y+1;
		if ( screen_y<0 ) screen_y = 0;
		scroll_y = 0;
	}
	tiny_Redraw();
}
void term_nextLine()
{
	line[++cursor_y] = cursor_x;
	if ( line[cursor_y+1]<cursor_x ) line[cursor_y+1]=cursor_x;

	if ( cursor_x>=BUFFERSIZE-1024 || cursor_y==MAXLINES-2 ) {
		int i, len = line[1024];
		tl1text -= len; 
		cursor_x -= len;
		cursor_y -= 1024;
		memmove( buff, buff+len, BUFFERSIZE-len );
		memset(buff+cursor_x, 0, BUFFERSIZE-cursor_x);
		memmove( attr, attr+len, BUFFERSIZE-len );
		memset(attr+cursor_x, 0, BUFFERSIZE-cursor_x);
		for ( i=0; i<cursor_y+2; i++ ) line[i] = line[i+1024]-len;
		while ( i<MAXLINES ) line[i++] = 0;
		screen_y -= 1024;
		scroll_y = 0;
	}

	if ( scroll_y<0 ) scroll_y--;
	if ( screen_y<cursor_y-size_y+1 ) screen_y = cursor_y-size_y+1;
}
void term_Parse( char *buf, int len )
{
	unsigned char *p=(unsigned char *)buf;
	unsigned char *zz = p+len;
	
	if ( bEnter1 ) { //capture prompt for scripting after Enter key pressed
		if ( len==1 ) {//and the echo is just one letter
			sPrompt[0] = buff[cursor_x-2];
			sPrompt[1] = buff[cursor_x-1];
			iPrompt = 2;
		}
		bEnter1 = FALSE;
	}
	if ( bLogging ) fwrite( buf, 1, len, fpLogFile );
	if ( bEscape ) p = vt100_Escape( p, zz-p ); 
	while ( p < zz ) {
		unsigned char c = *p++;
		if ( bTitle ) {
			if ( c==0x07 ) {
				bTitle = FALSE;
				title[title_idx]=0;
				tiny_Title(title);
			}
			else
				title[title_idx++] = c;
			continue;
		}
		switch ( c ) {
		case 0x00: 	break;
		case 0x07: 	tiny_Beep();
					break;
		case 0x08: 	if ( (buff[cursor_x--]&0xc0)==0x80 )//utf8 continuation byte
						while ( (buff[cursor_x]&0xc0)==0x80 ) cursor_x--;
					break;
		case 0x09: 	do { 
						attr[cursor_x] = c_attr;
						buff[cursor_x++]=' '; 
					}
					while ( (cursor_x-line[cursor_y])%8!=0 );
					break;
		case 0x0a: 	if ( bAlterScreen ) { // scroll down if reached bottom
						if ( cursor_y==roll_bot+screen_y ) 
							vt100_Escape((unsigned char *)"D", 1);
						else {
							int x = cursor_x - line[cursor_y];
							cursor_x = line[++cursor_y] + x;
						}
					}
					else {						//hard line feed
						cursor_x = line[cursor_y+1];
						if ( line[cursor_y+2]!=0 ) cursor_x--;
						attr[cursor_x] = c_attr; 
						buff[cursor_x++] = c;
						term_nextLine();
					}
					break;
		case 0x0d: 	if ( cursor_x-line[cursor_y]==size_x+1 && *p!=0x0a ) 
						term_nextLine();		//soft line feed
					else
						cursor_x = line[cursor_y];
					break; 
		case 0x1b: 	p = vt100_Escape( p, zz-p ); break;
		case 0xe2: 	if ( bAlterScreen ) { 
						c = ' ';			//hack utf8 box drawing
						if ( *p++==0x94 ){	//to make alterscreen easier
							switch ( *p ) {
							case 0x80:
							case 0xac:
							case 0xb4:
							case 0xbc: c='_'; break;
							case 0x82:
							case 0x94:
							case 0x98:
							case 0x9c:
							case 0xa4: c='|'; break;
							}
						}
						p++;
					}
		default:	if ( bGraphic ) switch ( c ) {
						case 'q': c='_'; break; //c=0xc4; break;
						case 'x':				//c=0xb3; break;
						case 't':				//c=0xc3; break;
						case 'u':				//c=0xb4; break;
						case 'm':				//c=0xc0; break;
						case 'j': c='|'; break; //c=0xd9; break;
						case 'l':				//c=0xda; break;
						case 'k':				//c=0xbf; break;
						default: c = ' ';
					}
					if ( bInsert ) vt100_Escape((unsigned char *)"[1@", 3);
					if ( cursor_x-line[cursor_y]>=size_x ) {
						int char_cnt=0;
						for ( int i=line[cursor_y]; i<cursor_x; i++ ) 
							if ( ( buff[i]&0xc0)!=0x80 ) char_cnt++;
						if ( char_cnt==size_x ) {
							if ( bAlterScreen ) 
								cursor_x--; //don't overflow in vi
							else
								term_nextLine();
							}
					}
					attr[cursor_x] = c_attr; 
					buff[cursor_x++] = c;
					if ( line[cursor_y+1]<cursor_x ) line[cursor_y+1]=cursor_x;
		}
	}

	if ( !bPrompt ) {
		tl1len = buff+cursor_x - tl1text;
		if ( strncmp(sPrompt, buff+cursor_x-iPrompt, iPrompt)==0 ) {
			bPrompt = TRUE;
			SetEvent( hTL1Event );
		}
	}
	if ( scroll_y>-size_y ) tiny_Redraw();
}
void term_Print( const char *fmt, ... ) 
{
	char buff[4096];
	va_list args;
	va_start(args, (char *)fmt);
	int len = vsnprintf(buff, 4096, (char *)fmt, args);
	va_end(args);
	term_Parse(buff, len);
	term_Parse("\033[37m",5);
}
char *term_Disp( char *msg )
{
	char *p = buff+cursor_x;
	term_Parse( msg, strlen(msg) );
	return p;
}
char *term_Send( char *buf, int len )
{
	if ( bEcho ) term_Parse( buf, len );
	char *p = buff+cursor_x;
	host_Send( buf, len );
	return p;
}
int term_Recv( char *tl1text ) 
{
	return  buff+cursor_x - tl1text;
}
/****************************term_TL1*********************************/
int term_TL1( char *cmd, char **pTl1Text )
{
	tl1len = 0; 
	if ( host_Status()==CONN_CONNECTED ) {	//retrieve from NE
		bPrompt = FALSE; 
		int cmdlen = strlen(cmd);
		tl1text = term_Send(cmd, cmdlen);
		if ( cmd[cmdlen-1]!='\r' ) term_Send("\r", 1);
		int i=0, oldlen = tl1len;
		ResetEvent(hTL1Event);
		while ( WaitForSingleObject( hTL1Event, 100 ) == WAIT_TIMEOUT ) { 
			if ( ++i==iTimeOut*10 ) break;
			if ( tl1len>oldlen ) { i=0; oldlen=tl1len; }
		}
	}
	else {									//retrieve from buffer
		static char *pcursor=buff;			//only when retrieve from buffer
		buff[cursor_x]=0; 
		char *p = strstr( pcursor, cmd );
		if ( p==NULL ) { pcursor = buff; p = strstr( pcursor, cmd ); }
		if ( p!=NULL ) { p = strstr( p, "\r\n" );
			if ( p!=NULL ) {
				tl1text = p+2;
				p = strstr( p, "\nM " );
				p = strstr( p, sPrompt );
				if ( p!=NULL ) {
					pcursor = ++p;
					tl1len = pcursor - tl1text;
				}
			}
		}
		if ( tl1len == 0 ) { tl1text = buff+cursor_x; }
	}

	if ( pTl1Text!=NULL ) *pTl1Text = tl1text;
	return tl1len;
}

/********************Script Functions*******************************/
char *term_Exec( char *pCmd )
{
	char *p = buff+cursor_x;
	static char cmd[256];
	strncpy( cmd, pCmd, 255 );
	if ( strncmp(cmd, "Title", 5)==0 ) 		tiny_Title( cmd+5 ); 
	else if ( strncmp(cmd, "Log", 3)==0 ) 	buff_Logg( cmd+4 ); 
	else if ( strncmp(cmd, "Ftpd", 4)==0 ) 	ftp_Svr( cmd+5 );
	else if ( strncmp(cmd, "Tftpd", 5)==0 ) tftp_Svr( cmd+6 );
	else if ( strncmp(cmd, "Timeout", 7)==0 ) iTimeOut = atoi( cmd+8 ); 
	else if ( strncmp(cmd, "Disconn", 7)==0 ) host_Close(); 
	else if ( strncmp(cmd, "serial ", 7)==0 ) host_Open( cmd+7 );
	else if ( strncmp(cmd, "telnet ", 7)==0 ) host_Open( cmd+7 );
	else if ( strncmp(cmd, "ssh ",    4)==0 ) host_Open( cmd+4 );
	else if ( strncmp(cmd, "Wait ", 5)==0 ) Sleep(atoi(cmd+5)*1000);
	else if ( strncmp(cmd, "Waitfor ", 8)==0 ) {
		for ( int i=iTimeOut; i>0; i-- ) {
			if ( strstr(tl1text, cmd+8)!=NULL ) break;
			Sleep(1000);
		}
	}
	else if ( strncmp(cmd, "Prompt ", 7)==0 ) {
		char *p=cmd+7, *p1 = sPrompt;
		while ( *p ) {
			if ( *p=='%' ) {
				int a;
				sscanf(p+1, "%02x", &a);
				*p1++ = a;
				p+=3;
			}
			else 
				*p1++ = *p++;
		}
		*p1 = 0;
		iPrompt = strlen(sPrompt);
	}
	return p;
}

/***************************Buffer functions***************************/
void buff_Logg( char *fn )
{
	char buf[256];
	if ( bLogging ) {
		fclose( fpLogFile );
		bLogging = FALSE;
		cmd_Disp("Log file closed");
	}
	else {
		fpLogFile = fopen_utf8( fn, MODE_WB );
		if ( fpLogFile != NULL ) {
			bLogging = TRUE;
			sprintf( buf, "%s opened for logging", fn);
			cmd_Disp( buf);
		}
	}
}
void buff_Srch( char *sstr )
{
	char buf[256], *p;
	int i = screen_y+scroll_y;
	int l = strlen(sstr)-1;
	while ( --i>=0 ) {
		strncpy(buf, buff+line[i], line[i+1]-line[i]+l);
		if ( (p=strstr( buf, sstr ))!=NULL ) {
			scroll_y = i-screen_y-size_y/2;
			p = strstr(buff+line[i], sstr);
			sel_left = p-buff;
			sel_right = sel_left+strlen(sstr);
			tiny_Redraw( FALSE );
			break;
		}
	}
}
unsigned char *vt100_Escape( unsigned char *sz, int cnt ) 
{
	static char code[20];
	static int idx=0;
	unsigned char *zz = sz+cnt;
	
	bEscape = TRUE; 
	while ( sz<zz && bEscape ) {
		code[idx++] = *sz++;
		switch( code[0] ) {
		case '[': if ( isalpha(code[idx-1]) 
						|| code[idx-1]=='@'
						|| code[idx-1]=='`' ) {
			bEscape = FALSE;
			int n0=1, n1=1, n2=1;
			if ( idx>1 ) {
				char *p = strchr(code, ';');
				if ( p != NULL ) {
					n0 = 0; n1 = atoi(code+1); n2 = atoi(p+1);
				}
				else
					if ( isdigit(code[1]) ) n0 = atoi(code+1);
			}
			int x;
			switch ( code[idx-1] ) {
			case 'A': x = cursor_x - line[cursor_y];
					  cursor_y -= n0;
					  cursor_x = line[cursor_y]+x;
					  break;
			case 'd'://line position absolute
					if ( n0>size_y ) n0 = size_y;
					x = cursor_x-line[cursor_y];
					cursor_y = screen_y+n0-1;
					cursor_x = line[cursor_y]+x;
				break;
			case 'e': //line position relative
			case 'B': x = cursor_x - line[cursor_y];
					  cursor_y += n0;
					  cursor_x = line[cursor_y]+x;
					  break;
			case 'a': //character position relative
			case 'C': if ( (buff[cursor_x]&0xc0)==0x80 )//utf8 continuation byte
						while ( (buff[++cursor_x]&0xc0)==0x80 );
					  else
						cursor_x+=n0; 
					  break; 
			case 'D': if ( (buff[cursor_x]&0xc0)==0x80 )//utf8 continuation byte
						while ( (buff[--cursor_x]&0xc0)==0x80 );
					  else 
						cursor_x -= n0;
					  break;
			case 'E': //cursor to begining of next line n0 times
					  cursor_y += n0;
					  cursor_x = line[cursor_y];
					  break;
			case 'F': //cursor to begining of previous line n0 times
					  cursor_y -= n0;
					  cursor_x = line[cursor_y];
					  break;
			case '`': //character position absolute
			case 'G': n1 = cursor_y-screen_y+1; 
					  n2 = n0; 
			case 'f': //horizontal and vertical position forced
					  if ( n1==0 ) n1=1;
					  if ( n2==0 ) n2=1;
			case 'H': if ( n1>size_y ) n1 = size_y;
					  if ( n2>size_x ) n2 = size_x;
					  cursor_y = screen_y+n1-1; 
					  cursor_x = line[cursor_y];
					  while ( --n2>0 ) {
						cursor_x++;
						while ( (buff[cursor_x]&0xc0)==0x80 ) cursor_x++;
					  }
					  break;
			case 'J': 	//[J kill till end, 1J begining, 2J entire screen
					if ( code[idx-2]=='[' ) {
						if ( bAlterScreen ) 
							screen_y = cursor_y;
						else {
							int i=cursor_y+1; line[i]=cursor_x;
							while ( ++i<=screen_y+size_y ) line[i]=0; 
							break;
						}
					}
					cursor_y = screen_y;
					cursor_x = line[cursor_y];
					for ( int i=0; i<size_y; i++ ) {
						memset( buff+cursor_x, ' ', size_x );
						memset( attr+cursor_x,   0, size_x );
						cursor_x += size_x;
						term_nextLine();
					}
					cursor_y = --screen_y; cursor_x = line[cursor_y];
					break;
			case 'K': {		//[K kill till end, 1K begining, 2K entire line
					int i = line[cursor_y];
					int j = line[cursor_y+1];
					if ( code[idx-2]=='[' ) 
						i = cursor_x;
					else 
						if ( n0==1 ) j = cursor_x;
					if ( j>i ) {
						memset( buff+i, ' ', j-i ); 
						memset( attr+i,   0, j-i ); 
						}
					}
					break;
			case 'L':	//insert lines
					for ( int i=roll_bot; i>cursor_y-screen_y; i-- ) {
						memcpy( buff+line[screen_y+i], 
								buff+line[screen_y+i-n0], size_x);
						memcpy( attr+line[screen_y+i], 
								attr+line[screen_y+i-n0], size_x);
					  }
					for ( int i=0; i<n0; i++ ) {
						memset(buff+line[cursor_y+i], ' ', size_x);
						memset(attr+line[cursor_y+i],   0, size_x);
					}
					break; 
			case 'M'://delete lines
					for ( int i=cursor_y-screen_y; i<=roll_bot-n0; i++ ) {
						memcpy( buff+line[screen_y+i], 
								buff+line[screen_y+i+n0], size_x);
						memcpy( attr+line[screen_y+i], 
								attr+line[screen_y+i+n0], size_x);
					}
					for ( int i=0; i<n0; i++ ) {
						memset(buff+line[screen_y+roll_bot-i], ' ', size_x);
						memset(attr+line[screen_y+roll_bot-i],   0, size_x);
					}
					break;
			case 'P': 
					for ( int i=cursor_x; i<line[cursor_y+1]-n0; i++ ) { 
						buff[i]=buff[i+n0]; 
						attr[i]=attr[i+n0];
					} 
					if ( !bAlterScreen ) {
						line[cursor_y+1]-=n0;
						if ( line[cursor_y+2]>0 ) line[cursor_y+2]-=n0;
						memset(buff+line[cursor_y+1], ' ', n0);
						memset(attr+line[cursor_y+1],   0, n0);
					}
					break;
			case '@': 
					for ( int i=line[cursor_y+1]; i>=cursor_x; i-- ) {
						buff[i+n0]=buff[i]; 
						attr[i+n0]=attr[i];
					}
					memset(buff+cursor_x, ' ', n0);
					memset(attr+cursor_x,   0, n0);
					line[cursor_y+1]+=n0;
					break;
			case 'X': //erase n0 characters
					for ( int i=0; i<n0; i++) {
						buff[cursor_x+i]=' ';
						attr[cursor_x+i]=0;
					}
					break;
			case 'S': // scroll up n0 lines
					for ( int i=roll_top; i<=roll_bot-n0; i++ ) {
						memcpy( buff+line[screen_y+i],
								buff+line[screen_y+i+n0], size_x);
						memcpy( attr+line[screen_y+i],
								attr+line[screen_y+i+n0], size_x);
					}
					memset(buff+line[screen_y+roll_bot-n0+1], ' ', n0*size_x);
					memset(attr+line[screen_y+roll_bot-n0+1],   0, n0*size_x);
					break;
			case 'T': // scroll down n0 lines
					for ( int i=roll_bot; i>=roll_top+n0; i-- ) {
						memcpy( buff+line[screen_y+i],
								buff+line[screen_y+i-n0], size_x);
						memcpy( attr+line[screen_y+i],
								attr+line[screen_y+i-n0], size_x);
					}
					memset(buff+line[screen_y+roll_top], ' ', n0*size_x);
					memset(attr+line[screen_y+roll_top],   0, n0*size_x);
					break;
				case 'I': //cursor forward n0 tab stops
					cursor_x += n0*8;
					break;
				case 'Z': //cursor backward n0 tab stops
					cursor_x -= n0*8;
					break;
			case 'h': 
					if ( code[1]=='4' )  bInsert = TRUE;
					if ( code[1]=='?' ) {
						n0 = atoi(code+2);
						if ( n0==1 ) bAppCursor = TRUE;
						if ( n0==25 ) bCursor = TRUE;
						if ( n0==1049 ) { 	//?1049h alternate screen, 
							bAlterScreen = TRUE;
							screen_y = cursor_y;
							cursor_x = line[cursor_y];
						}
					}
					break;
			case 'l':
					if ( code[1]=='4' ) bInsert = FALSE;
					if ( code[1]=='?' ) {
						n0 = atoi(code+2);
						if ( n0==1 ) bAppCursor = FALSE;
						if ( n0==25 ) bCursor = FALSE;
						if ( n0==1049 ) { 	//?1049l exit alternate screen, 
							bAlterScreen = FALSE; 
							cursor_y = screen_y; 
							cursor_x = line[cursor_y];
							for ( int i=1; i<=size_y+1; i++ )
								line[cursor_y+i] = 0;
							screen_y = cursor_y-size_y+1; 
							if ( screen_y<0 ) screen_y=0;
						}
					}
					break;
			case 'm': 
					if ( n0==0 ) n0 = n2; 	//ESC[0;0m	ESC[01;34m
					switch ( (int)(n0/10) ) {		//ESC[34m
					case 0: if ( n0%10==7 ) {c_attr = 0x70; break;}
					case 1:
					case 2: c_attr = 7; break;
					case 3: if ( n0==39 ) n0 = 37;	//39 default foreground
							c_attr = (c_attr&0xf0)+n0%10; break;
					case 4: if ( n0==49 ) n0 = 40;	//49 default background
							c_attr = (c_attr&0x0f)+((n0%10)<<4); break;
					}
					break;
			case 'r': 
					roll_top=n1-1; roll_bot=n2-1;
					break;
			case 's': //save cursor
					save_x = cursor_x-line[cursor_y];
					save_y = cursor_y-screen_y;
					break;
			case 'u': //restore cursor
					cursor_y = save_y+screen_y;
					cursor_x = line[cursor_y]+save_x;
					break;
				}
			}
			break;
		case 'D': // scroll down one line
				for ( int i=roll_top; i<roll_bot; i++ ) {
					memcpy( buff+line[screen_y+i],
							buff+line[screen_y+i+1], size_x );
					memcpy( attr+line[screen_y+i], 
							attr+line[screen_y+i+1], size_x );
				}
				memset(buff+line[screen_y+roll_bot], ' ', size_x);
				memset(attr+line[screen_y+roll_bot],   0, size_x);
				bEscape = FALSE;
				break;
		case 'F': //cursor to lower left corner
				cursor_y = screen_y+size_y-1;
				cursor_x = line[cursor_y];
				bEscape = FALSE;
				break;
		case 'M': // scroll up one line
				for ( int i=roll_bot; i>roll_top; i-- ) {
					memcpy( buff+line[screen_y+i], 
							buff+line[screen_y+i-1], size_x);
					memcpy( attr+line[screen_y+i], 
							attr+line[screen_y+i-1], size_x);
				}
				memset(buff+line[screen_y+roll_top], ' ', size_x);
				memset(attr+line[screen_y+roll_top], ' ', size_x);
				bEscape = FALSE;
				break; 
		case ']':
				if ( code[idx-1]==';' ) {
					if ( code[1]=='0' ) {
						bTitle = TRUE;
						title_idx = 0;
					}
					bEscape = FALSE;
				}
				break;
		case '(':
				if ( (code[1]=='B'||code[1]=='0') ) {
					bGraphic = (code[1]=='0');
					bEscape = FALSE; 
				}
				break;
		default: bEscape = FALSE;
		}
		if ( idx==20 ) bEscape = FALSE;
		if ( !bEscape ) { idx=0; memset(code, 0, 20); }
	}
	return sz;
}
void term_Parse_XML(char *msg, int len)
{
	int indent=0;
	BOOL previousIsOpen=TRUE;
	char *p=msg, *q;
	char spaces[256]="\r\n                                               \
                                                                              ";
	while ( *p!=0 && *p!='<' ) p++;
	if ( p>msg ) term_Parse(msg, p-msg);
	while ( *p!=0 && p<msg+len ) {
		while (*p==0x0d || *p==0x0a || *p=='\t' || *p==' ' ) p++;
		if ( *p=='<' ) { //tag
			if ( p[1]=='/' ) {
				if ( !previousIsOpen ) {
					indent -= 2;
					term_Parse(spaces, indent);
				}
				previousIsOpen = FALSE;
			}
			else {
				if ( previousIsOpen ) indent+=2;
				term_Parse(spaces, indent);
				previousIsOpen = TRUE;
			}
			q = strchr(p, '>');
			if ( q!=NULL ) {
				term_Parse("\033[32m",5);
				char *r = strchr(p, ' ');
				if ( r!=NULL && r<q ) {
					term_Parse(p, r-p);
					term_Parse("\033[35m",5);
					term_Parse(r, q-r);
				}
				else
					term_Parse(p, q-p);
				term_Parse("\033[32m>",6);
				if ( q[-1]=='/' ) previousIsOpen = FALSE;
				p = q+1;
			}
			else
				break;
		}
		else {			//data
			q = strchr(p, '<');
			if ( q!=NULL ) {
				term_Parse("\033[33m",5);
				term_Parse(p, q-p);
				p = q;
			}
			else { //not xml or incomplete xml
				term_Parse(p, strlen(p));
				break;
			}
		}
	}
	term_Parse("\033[37m",5);
}