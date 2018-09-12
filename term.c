#include <windows.h>
#include <wincrypt.h>
#include "tiny.h"

char buff[BUFFERSIZE+256], attr[BUFFERSIZE+256], c_attr;
int cursor_x, cursor_y, save_x, save_y;
int size_x, size_y;
int screen_y;
int scroll_y;
int sel_left, sel_right;
int roll_top, roll_bot;
int line[MAXLINES+2];
BOOL bLogging;
BOOL bEcho;

static FILE *fpLogFile;
static BOOL bPrompt=FALSE, bNewline=FALSE;
static char cPrompt=';', *tl1text = NULL;
static int iTimeOut=30, tl1len = 0;

BOOL bCursor, bAppCursor;
static BOOL bAlterScreen, bGraphic, bEscape, bTitle, bInsert;
unsigned char * vt100_Escape( unsigned char *sz, int cnt );
char * SHA ( char *msg );

/*******************************Line functions************************/
void term_Clear( )
{
	memset( buff, 0, BUFFERSIZE+256 );
	memset( attr, 0, BUFFERSIZE+256 );
	memset( line, 0, (MAXLINES+2)*sizeof(int) );
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
char *term_Init( )
{
	size_y = TERMLINES;
	size_x = TERMCOLS;
	bLogging = FALSE;
	bEcho = FALSE;
	term_Clear( );
	return buff;
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
void term_Roll()
{
	int i=0, step=1024;
	int len = line[step];
	memmove( buff, buff+len, cursor_x-len );
	memmove( attr, attr+len, cursor_x-len );
	memmove( line, line+step, (cursor_y-step+1)*sizeof(int));
	while ( i<=cursor_y-step+1 ) line[i++] -= len;
	while ( i<=cursor_y+1 ) line[i++] = 0;
	tl1text -= len; 
	cursor_x -= len;
	cursor_y -= step;
	screen_y -= step;
	scroll_y = 0;
	line[cursor_y+1] = cursor_x;
}
void term_nextLine()
{
	line[++cursor_y] = cursor_x;
	if ( line[cursor_y+1]<cursor_x ) line[cursor_y+1]=cursor_x;

	if ( cursor_x>=BUFFERSIZE ) term_Roll();
	if ( cursor_y==MAXLINES ) term_Roll();

	if ( scroll_y<0 ) scroll_y--;
	if ( screen_y<cursor_y-size_y+1 ) screen_y = cursor_y-size_y+1;
}
void term_NewLine( )
{
	if ( bNewline ) {
		if ( !bPrompt ) {
			tl1len = buff+cursor_x - tl1text;
			if ( buff[cursor_x-2]==cPrompt ) {
				tl1len++; bPrompt = TRUE;
			}
		}
	}
	else {
		bNewline = TRUE; 
		tl1text = buff+cursor_x;
	}
}
void term_Parse( char *buf, int len )
{
	unsigned char c, *p=(unsigned char *)buf;
	unsigned char *zz = p+len;
	
	if ( bLogging ) fwrite( buf, 1, len, fpLogFile );
	if ( bEscape ) p = vt100_Escape( p, zz-p ); 
	while ( p < (unsigned char *)buf+len ) {
		switch ( c=*p++ ) {
		case 0x00: 	break;
		case 0x07: 	if ( bTitle ) {
						buff[cursor_x]=0;
						cursor_x = line[cursor_y];
						tiny_Title(buff+cursor_x);
						bTitle = FALSE;
					}
					else
						printf("\007");
					break;
		case 0x08: 	--cursor_x; break;
		case 0x09: 	do { 
						buff[cursor_x++]=' '; 
					}
					while ( (cursor_x-line[cursor_y])%8!=0 );
					break;
		case 0x1b: 	p = vt100_Escape( p, zz-p ); break;
		case 0x0d: 	cursor_x = line[cursor_y]; break; 
		case 0x0a: 	if ( bAlterScreen ) { // scroll down if reached bottom
						if ( cursor_y==roll_bot+screen_y ) 
							vt100_Escape((unsigned char *)"D", 1);
						else {
							int x = cursor_x - line[cursor_y];
							cursor_x = line[++cursor_y] + x;
						}
					}
					else {
						cursor_x = line[cursor_y+1];
						if ( line[cursor_y+2]!=0 ) cursor_x--;
						attr[cursor_x] = c_attr; 
						buff[cursor_x++] = c;
						term_nextLine();
						term_NewLine( );
					}
					break;
		case 0xe2: 	if ( *p==0x94 ) {//utf8 box drawing
						switch ( (unsigned char )(p[1]) ) {
						case 0x80:
						case 0xac:
						case 0xb4:
						case 0xbc: c='_'; break;
						case 0x82:
						case 0x94:
						case 0x98:
						case 0x9c:
						case 0xa4: c='|'; break;
						case 0x8c:
						case 0x90: c=' '; break;
						default: c='?';
						}
						p+=2;
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
					if ( cursor_x-line[cursor_y]==size_x ) term_nextLine();
					attr[cursor_x] = c_attr; 
					buff[cursor_x++] = c;
					if ( line[cursor_y+1]<cursor_x ) line[cursor_y+1]=cursor_x;
		}
	}

	if ( !bPrompt && bNewline )
		if ( ( buff[cursor_x-2]==cPrompt && buff[cursor_x-1]==' ')
			|| buff[cursor_x-1]==cPrompt ) {
			tl1len = buff+cursor_x - tl1text;
			bPrompt=TRUE;
		}
	if ( scroll_y>-size_y ) tiny_Redraw();
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
	comm_Send( buf, len );
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
	if ( comm_Connected() ) {				//retrieve from NE
		term_Send( cmd, strlen(cmd) );
		bNewline = bPrompt = FALSE; 
		int i, oldlen = tl1len;
		for ( i=0; i<iTimeOut*4 && (!bPrompt); i++ ) {
			Sleep(250); 
			if ( tl1len>oldlen ) { i=0; oldlen=tl1len; }
		}
	}
	else {									//retrieve from buffer
		static char *pcursor=buff;		//only when retrieve from buffer
		buff[cursor_x]=0; 
		char *p = strstr( pcursor, cmd );
		if ( p==NULL ) { pcursor = buff; p = strstr( pcursor, cmd ); }
		if ( p!=NULL ) { p = strstr( p, "\r\n" );
			if ( p!=NULL ) {
				tl1text = p+2;
				p = strstr( p, "\nM " );
				p = strchr( p, cPrompt );
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
	if ( strncmp(cmd, "Title", 5)==0 ) 			tiny_Title( cmd+5 ); 
	else if ( strncmp(cmd, "Log", 3)==0 ) 		buff_Logg( cmd+4 ); 
	else if ( strncmp(cmd, "Save", 4)==0 ) 		buff_Save( cmd+5 ); 
	else if ( strncmp(cmd, "Ftpd", 4)==0 ) 		ftp_Svr( cmd+5 );
	else if ( strncmp(cmd, "Tftpd", 5)==0 ) 	tftp_Svr( cmd+6 );
	else if ( strncmp(cmd, "Prompt", 6)==0 ) 	cPrompt = cmd[7];
	else if ( strncmp(cmd, "Timeout", 7)==0 ) 	iTimeOut = atoi( cmd+8 ); 
	else if ( strncmp(cmd, "Connect", 7)==0 ) 	comm_Open( cmd+8 );
	else if ( strncmp(cmd, "Disconnect", 9)==0 )comm_Close(); 
	else if ( strncmp(cmd, "SHA",3)==0 ) 	p = SHA(cmd+3);
	return p;
}
static BOOL bScriptRun=FALSE, bScriptPause=FALSE;
DWORD WINAPI cmdScripter( void *cmds )
{
	char *p0=(char *)cmds, *p1, *p2;
	int iRepCount = -1;
	bScriptRun=TRUE; bScriptPause = FALSE; 
	do {
		p2=p1=strchr(p0, 0x0a);
		if ( p1==NULL ) p1 = p0+strlen(p0);
		*p1 = 0;

		if (p1-p0>1) {
			int i;
			cmd_Disp(p0);
			if ( *p0=='#' ) {
				if ( strncmp( p0+1, "Loop ", 5)==0 ){
					if ( iRepCount<0 ) iRepCount = atoi( p0+6 );
					if ( --iRepCount>0 ) {
						*p1=0x0a;
						p0 = p1 = p2 = (char*)cmds;
					}
				}
				else if ( strncmp( p0+1, "Wait ", 5)==0 ) {
					for ( i=atoi(p0+6); i>0&&bScriptRun; i-- ) 
						Sleep(1000);
				}
				else if ( strncmp( p0+1, "Waitfor ", 8)==0 ) {
					*(p1-1)=0;
					for ( i=iTimeOut; i>0&&bScriptRun; i-- ) {
						if ( strstr(tl1text, p0+9)!=NULL ) break;
						Sleep(1000);
					}
					*(p1-1)=0x0d;
				}
				else {
					*(p1-1)=0;
					term_Exec( p0+1 );
					*(p1-1)=0x0d;
				}
			}
			else 
				if ( comm_Connected() ) term_TL1( p0, NULL );
		}
		if ( p0!=p1 ) { p0 = p1+1; *p1 = 0x0a; }
		while ( bScriptPause && bScriptRun ) Sleep(1000);
	} 
	while ( p2!=NULL && bScriptRun );
	cmd_Disp( bScriptRun ? "script completed" : "script stopped");
	bScriptRun = bScriptPause = FALSE;
	return 0;
}
void script_Pause( void )
{
	if ( bScriptRun ) bScriptPause = !bScriptPause;
	if ( bScriptPause ) cmd_Disp( "Script Paused" );
}
void script_Stop( void )
{
	bScriptRun = FALSE; 
}
void tiny_Drop_Script( char *tl1s )
{
static char cmds[MAXLINES];
	DWORD dwPasterId;
	if ( !bScriptRun ) {
		strncpy( cmds, tl1s, MAXLINES-1 );
		cmds[MAXLINES-1] = 0;
		CreateThread( NULL, 0, (LPTHREAD_START_ROUTINE) cmdScripter, 
										(void *)cmds, 0, &dwPasterId);
	}
}
void script_Open( char *fn )
{
	FILE *fpScript = fopen( fn, "rb" );
	if ( fpScript != NULL ) {
		char buf[MAXLINES];
		int lsize = fread( buf, 1, MAXLINES-1, fpScript );
		fclose( fpScript );
		if ( lsize > 0 ) {
			buf[lsize]=0;
			tiny_Drop_Script(buf);
		}
	}
}
/***************************Dictionary functions***********************/
void dict_Load(char *fn)
{
	FILE *fp = fopen(fn, "r");
	if ( fp!=NULL ) {
		while ( !feof( fp ) ) {
			char cmd[256];
			fgets( cmd, 256, fp );
			int l = strlen(cmd)-1;
			while ( l>0 && cmd[l]<0x10 ) cmd[l--]=0;
			autocomplete_Add( cmd ) ;
		}
		fclose( fp );
	}
}
/***************************Buffer functions***************************/
void buff_Load( char *fn )
{
	char buf[8192];
	int lines=0, len;
	FILE *fp=fopen(fn, "r");
	if ( fp != NULL ) {
		while ( (len=fread(buf, 1, 8191, fp)) > 0 ) {
			term_Parse( buf, len );
			lines+=len;
		};
		fclose( fp );
		sprintf( buf, "%d bytes loaded from %s", lines, fn);
		cmd_Disp(buf);
	}
}

void buff_Save( char *fn )
{
	char buf[256];
	FILE *fp = fopen(fn, "w+");
	if ( fp != NULL ) {
		fwrite( buff, 1, cursor_x, fp );
		fclose( fp );
		sprintf( buf, "%d bytes saved to %s", cursor_x, fn);
		cmd_Disp(buf);
	}
}

void buff_Logg( char *fn )
{
	char buf[256];
	if ( bLogging ) {
		fclose( fpLogFile );
		bLogging = FALSE;
		cmd_Disp("Log file closed");
	}
	else {
		fpLogFile = fopen( fn, "wb+" );
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
				char *p = strchr(code,';');
				if ( p != NULL ) {
					n1 = atoi(code+1); n2 = atoi(p+1);
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
			case 'C': cursor_x+=n0; break; 
			case 'D': cursor_x-=n0; break;
			case 'E': //cursor to begining of next line n0 times
					  cursor_y += n0;
					  cursor_x = line[cursor_y];
					  break;
			case 'F': //cursor to begining of previous line n0 times
					  cursor_y -= n0;
					  cursor_x = line[cursor_y];
					  break;
			case '`': //character position absolute
			case 'G': cursor_x = line[cursor_y]+n0-1; break;
			case 'f': //horizontal and vertical position forced
					  if ( n1==0 ) n1=1;
					  if ( n2==0 ) n2=1;
			case 'H': if ( n1>size_y ) n1 = size_y;
					  if ( n2>size_x ) n2 = size_x;
					  cursor_y = screen_y+n1-1; 
					  cursor_x = line[cursor_y]+n2-1;
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
					memset( buff+i, ' ', j-i ); 
					memset( attr+i,   0, j-i ); 
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
					if ( !bAlterScreen ) 
						line[cursor_y+1]-=n0;
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
							for ( int i=1; i<=size_y; i++ )
								line[cursor_y+i] = 0;
							screen_y = cursor_y-size_y+1; 
							if ( screen_y<0 ) screen_y=0;
						}
					}
					break;
			case 'm': 
					if ( n0==1 && n2!=1 ) n0 = n2; 	//ESC[0;0m	ESC[01;34m
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
					if ( code[1]=='0' ) bTitle = TRUE;
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
/********************************************/
char * SHA ( char *msg ) {
const unsigned int ALGIDs[] = { CALG_SHA1, CALG_SHA_256, CALG_SHA_384, 0, CALG_SHA_512 };
	static char result[256];
	BYTE        bHash[64];
	DWORD       dwDataLen = 0;
	HCRYPTPROV  hProv = 0;
	HCRYPTHASH  hHash = 0;

	int algo = *msg - '1';
	msg = strchr(msg, ' ')+1;
	CryptAcquireContext(&hProv, NULL, NULL, PROV_RSA_AES, CRYPT_VERIFYCONTEXT);
	CryptCreateHash( hProv, ALGIDs[algo], 0, 0, &hHash);
	CryptHashData( hHash, (BYTE *)msg, strlen(msg), 0);
	CryptGetHashParam( hHash, HP_HASHVAL, NULL, &dwDataLen, 0);
	CryptGetHashParam( hHash, HP_HASHVAL, bHash, &dwDataLen, 0);

	if(hHash) CryptDestroyHash(hHash);    
	if(hProv) CryptReleaseContext(hProv, 0);

	for ( int i=0; i<dwDataLen; i++ ) sprintf(result+i*2, "%02x", bHash[i]);
	return result;
}
/************************************************/