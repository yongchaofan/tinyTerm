//
// "$Id: term.c 30243 2019-05-21 15:05:10 $"
//
// tinyTerm -- A minimal serail/telnet/ssh/sftp terminal emulator
//
// term.c is the minimal xterm implementation, only common ESCAPE
// control sequences are supported for apps like top, vi etc.
//
// Copyright 2018-2019 by Yongchao Fan.
//
// This library is free software distributed under GNU GPL 3.0,
// see the license at:
//
// https://github.com/yongchaofan/tinyTerm/blob/master/LICENSE
//
// Please report all bugs and problems on the following page:
//
// https://github.com/yongchaofan/tinyTerm/issues/new
//
#include "tiny.h"
#include <windows.h>

const unsigned char *vt100_Escape( TERM *pt, const unsigned char *sz, int cnt );
const unsigned char *telnet_Options(TERM *pt, const unsigned char *p, int cnt );

BOOL term_Construct( TERM *pt )
{
	pt->size_x=80;
	pt->size_y=25;
	pt->bLogging=FALSE;
	pt->bEcho=FALSE;
	pt->title_idx = 0;
	strcpy(pt->sPrompt, "> ");
	pt->iPrompt=2;
	pt->iTimeOut=30;
	pt->tl1len=0;
	pt->tl1text=NULL;

	pt->buff = (char *)malloc(BUFFERSIZE);
	pt->attr = (char *)malloc(BUFFERSIZE);
	pt->line = (int * )malloc(MAXLINES*sizeof(int));

	if ( pt->buff!=NULL && pt->attr!=NULL && pt->line!=NULL )
	{
		term_Clear( pt );
		return TRUE;
	}
	return FALSE;
}
void term_Destruct( TERM *pt )
{
	free( pt->buff );
	free( pt->attr );
	free( pt->line );
}
void term_Clear( TERM *pt )
{
	memset( pt->buff, 0, BUFFERSIZE );
	memset( pt->attr, 0, BUFFERSIZE );
	memset( pt->line, 0, MAXLINES*sizeof(int) );
	pt->c_attr = 7;
	pt->cursor_y = pt->cursor_x = 0;
	pt->screen_y = 0;
	pt->sel_left = pt->sel_right = 0;
	pt->bAlterScreen = FALSE;
	pt->bAppCursor = FALSE;
	pt->bBracket = FALSE;
	pt->bGraphic = FALSE;
	pt->bEscape = FALSE;
	pt->bInsert = FALSE;
	pt->bTitle = FALSE;
	pt->bCursor = TRUE;
	pt->bPrompt = TRUE;
	pt->save_edit = FALSE;
	pt->escape_idx = 0;
	pt->xmlIndent = 0;
	pt->xmlPreviousIsOpen = TRUE;
	tiny_Redraw_Term(  );
}
void term_Size( TERM *pt, int x, int y )
{
	pt->size_x = x;
	pt->size_y = y;
	pt->roll_top = 0;
	pt->roll_bot = pt->size_y-1;
	if ( !pt->bAlterScreen ) {
		pt->screen_y = max(0, pt->cursor_y-pt->size_y+1);
	}
	host_Send_Size( pt->host, pt->size_x, pt->size_y );
	tiny_Redraw_Term(  );
}
void term_nextLine(TERM *pt)
{
	pt->line[++pt->cursor_y] = pt->cursor_x;
	if ( pt->line[pt->cursor_y+1]<pt->cursor_x )
		pt->line[pt->cursor_y+1]=pt->cursor_x;
	if ( pt->screen_y==pt->cursor_y-pt->size_y ) pt->screen_y++;

	if ( pt->cursor_x>=BUFFERSIZE-1024 || pt->cursor_y==MAXLINES-2 ) {
		int i, len = pt->line[1024];
		pt->tl1text -= len;
		if ( pt->tl1text<pt->buff ) {
			pt->tl1len -= pt->buff-pt->tl1text;
			pt->tl1text = pt->buff;
		}
		pt->cursor_x -= len;
		pt->cursor_y -= 1024;
		pt->screen_y -= 1024;
		if ( pt->screen_y<0 ) pt->screen_y = 0;
		memmove( pt->buff, pt->buff+len, BUFFERSIZE-len );
		memset(pt->buff+pt->cursor_x, 0, BUFFERSIZE-pt->cursor_x);
		memmove( pt->attr, pt->attr+len, BUFFERSIZE-len );
		memset(pt->attr+pt->cursor_x, 0, BUFFERSIZE-pt->cursor_x);
		for ( i=0; i<pt->cursor_y+2; i++ ) 
			pt->line[i] = pt->line[i+1024]-len;
		while ( i<MAXLINES ) pt->line[i++] = 0;
	}
}
void term_Parse( TERM *pt, const char *buf, int len )
{
	const unsigned char *p=(const unsigned char *)buf;
	const unsigned char *zz = p+len;
	int old_cursor_y = pt->cursor_y;

	if ( pt->bLogging ) fwrite( buf, 1, len, pt->fpLogFile );
	if ( pt->bEscape ) p = vt100_Escape( pt, p, zz-p );
	while ( p < zz ) {
		unsigned char c = *p++;
		if ( pt->bTitle ) {
			if ( c==0x07 ) {
				pt->bTitle = FALSE;
				pt->title[pt->title_idx]=0;
				tiny_Title(pt->title);
			}
			else
				if ( pt->title_idx<63 ) 
					pt->title[pt->title_idx++] = c;
			continue;
		}
		switch ( c ) {
		case 0x00:	break;
		case 0x07:	tiny_Beep();
					break;
		case 0x08:	if ( isUTF8c(pt->buff[pt->cursor_x--]) )
						//utf8 continuation byte
						while ( isUTF8c(pt->buff[pt->cursor_x]) ) 
							pt->cursor_x--;
					break;
		case 0x09:	do {
						pt->attr[pt->cursor_x] = pt->c_attr;
						pt->buff[pt->cursor_x++]=' ';
					}
					while ( (pt->cursor_x-pt->line[pt->cursor_y])%8!=0 );
					break;
		case 0x0a:	if ( pt->bAlterScreen ) {// scroll down if reached bottom
						if ( pt->cursor_y==pt->roll_bot+pt->screen_y )
							vt100_Escape(pt, "D", 1);
						else {
							int x = pt->cursor_x - pt->line[pt->cursor_y];
							pt->cursor_x = pt->line[++pt->cursor_y] + x;
						}
					}
					else {					//hard line feed
						pt->cursor_x = pt->line[pt->cursor_y+1];
						if ( pt->line[pt->cursor_y+2]!=0 ) pt->cursor_x--;
						pt->attr[pt->cursor_x] = pt->c_attr;
						pt->buff[pt->cursor_x++] = c;
						term_nextLine(pt);
					}
					break;
		case 0x0d:	if ( pt->cursor_x-pt->line[pt->cursor_y]
						==pt->size_x+1 && *p!=0x0a )
						term_nextLine(pt);	//soft line feed
					else
						pt->cursor_x = pt->line[pt->cursor_y];
					break;
		case 0x1b:	p = vt100_Escape( pt, p, zz-p ); break;
		case 0xff:	p = telnet_Options( pt, p-1, zz-p+1 ); break;
		case 0xe2:	if ( pt->bAlterScreen ) 
					{
						c = ' ';			//hack utf8 box drawing
						if ( *p++==0x94 )	//to make alterscreen easier
						{	
							switch ( *p ) 
							{
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
		default:	if ( pt->bGraphic ) switch ( c )
					{
						case 'q': c='_'; break;
						case 'x':
						case 't':
						case 'u':
						case 'm':
						case 'j': c='|'; break;
						case 'l':
						case 'k':
						default: c = ' ';
					}
					if ( pt->bInsert ) 
						vt100_Escape(pt, "[1@", 3);
					if ( pt->cursor_x-pt->line[pt->cursor_y]>=pt->size_x ) 
					{
						int char_cnt=0;
						for ( int i=pt->line[pt->cursor_y]; 
														i<pt->cursor_x; i++ )
							if ( !isUTF8c(pt->buff[i]) ) char_cnt++;
						if ( char_cnt==pt->size_x ) 
						{
							if ( pt->bAlterScreen )
								pt->cursor_x--; //don't overflow in vi
							else
								term_nextLine(pt);
						}
					}
					pt->attr[pt->cursor_x] = pt->c_attr;
					pt->buff[pt->cursor_x++] = c;
					if ( pt->line[pt->cursor_y+1]<pt->cursor_x ) 
						pt->line[pt->cursor_y+1]=pt->cursor_x;
		}
	}

	if ( !pt->bPrompt && pt->cursor_x>pt->iPrompt ) 
	{
		pt->tl1len = pt->buff+pt->cursor_x - pt->tl1text;
		if ( strncmp(pt->sPrompt, pt->buff+pt->cursor_x-pt->iPrompt, 
														pt->iPrompt)==0)
			pt->bPrompt = TRUE;
	}
	if ( old_cursor_y!=pt->cursor_y || pt->bAlterScreen )
		tiny_Redraw_Term();
	else
		tiny_Redraw_Line();
}
BOOL term_Echo(TERM *pt)
{
	pt->bEcho=!pt->bEcho;
	return pt->bEcho;
}
void term_Title( TERM *pt, char *title )
{
	switch ( host_Status(pt->host) )
	{
	case HOST_IDLE:
		pt->title[0] = 0;
		pt->bEcho = FALSE;
		break;
	case HOST_CONNECTING:
		break;
	case HOST_CONNECTED:
		host_Send_Size( pt->host, pt->size_x, pt->size_y);
		if ( host_Type(pt->host)==NETCONF ) pt->bEcho = TRUE;
		break;
	}
	strncpy(pt->title, title, 63);
	pt->title[63] = 0;
	tiny_Title(pt->title);
}
void term_Print( TERM *pt, const char *fmt, ... )
{
	char buff[4096];
	va_list args;
	va_start(args, (char *)fmt);
	int len = vsnprintf(buff, 4096, (char *)fmt, args);
	va_end(args);
	term_Parse(pt, buff, len);
	term_Parse(pt, "\033[37m", 5);
}
void term_Disp( TERM *pt, const char *msg )
{
	pt->tl1text = pt->buff+pt->cursor_x;
	term_Parse( pt, msg, strlen(msg) );
}
void term_Send( TERM *pt, char *buf, int len )
{
	if ( pt->bEcho ) 
		if ( host_Type(pt->host)==NETCONF ) 
			term_Parse_XML( pt, buf, len );
		else
			term_Parse(pt, buf, len);
	if ( host_Status(pt->host)!=HOST_IDLE ) 
		host_Send( pt->host, buf, len );
}
void term_Paste( TERM *pt, char *buf, int len )
{
	if ( pt->bBracket ) term_Send( pt, "\033[200~", 6);
	term_Send( pt, buf, len );
	if ( pt->bBracket ) term_Send( pt, "\033[201~", 6);
}
int term_Recv( TERM *pt, char **pTL1text )
{
	if ( pTL1text!=NULL ) *pTL1text = pt->tl1text;
	int len = pt->buff+pt->cursor_x - pt->tl1text;
	pt->tl1text = pt->buff+pt->cursor_x;
	return len;
}
void term_Learn_Prompt( TERM *pt )
{//capture prompt for scripting
	if ( pt->cursor_x>1 ) {
		pt->sPrompt[0] = pt->buff[pt->cursor_x-2];
		pt->sPrompt[1] = pt->buff[pt->cursor_x-1];
		pt->sPrompt[2] = 0;
		pt->iPrompt = 2;
	}
}
char *term_Mark_Prompt( TERM *pt )
{
	pt->bPrompt = FALSE;
	pt->tl1len = 0;
	pt->tl1text = pt->buff+pt->cursor_x;
	return pt->tl1text;
}
int term_Waitfor_Prompt( TERM *pt )
{
	int oldlen = pt->tl1len;
	for ( int i=0; i<pt->iTimeOut*10 && !pt->bPrompt; i++ ) {
		if ( pt->tl1len>oldlen ) { i=0; oldlen=pt->tl1len; }
		Sleep(100);
	}
	pt->bPrompt = TRUE;
	return pt->tl1len;
}
void term_Logg( TERM *pt, char *fn )
{
	if ( pt->bLogging ) {
		fclose( pt->fpLogFile );
		pt->bLogging = FALSE;
		term_Print( pt, "\n\033[33m logging stopped\n" );
	}
	else if ( fn!=NULL ) {
		if ( *fn==' ' ) fn++;
		pt->fpLogFile = fopen_utf8( fn, "wb" );
		if ( pt->fpLogFile != NULL ) {
			pt->bLogging = TRUE;
			term_Print( pt, "\n\033[33m%s logging started\n", fn );
		}
	}
}

int term_Srch( TERM *pt, char *sstr )
{
	int l = strlen(sstr);
	char *p = pt->buff+pt->sel_left;
	if ( pt->sel_left==pt->sel_right ) p = pt->buff+pt->cursor_x;
	while ( --p>=pt->buff+l ) {
		int i;
		for ( i=l-1; i>=0; i--) if ( sstr[i]!=p[i-l] ) break;
		if ( i==-1 ) {			//found a match
			pt->sel_left = p-l-pt->buff;
			pt->sel_right = p-pt->buff;
			for ( i=pt->screen_y; pt->line[i]>pt->sel_left; i-- );
			tiny_Scroll( pt->screen_y-i );
			return TRUE;
		}
	}
	return FALSE;
}
int term_TL1( TERM *pt, char *cmd, char **pTl1Text )
{
	if ( host_Status( pt->host )==HOST_CONNECTED ) {	//retrieve from NE
		term_Mark_Prompt( pt );
		int cmdlen = strlen(cmd);
		term_Send( pt, cmd, cmdlen );
		if ( cmd[cmdlen-1]!='\r' ) term_Send( pt, "\r", 1 );
		term_Waitfor_Prompt( pt );
	}
	else {								//retrieve from buffer
		char *pcursor=pt->buff;			//only when retrieve from buffer
		pt->tl1len = 0;
		pt->buff[pt->cursor_x]=0;
		char *p = strstr( pcursor, cmd );
		if ( p==NULL ) { pcursor = pt->buff; p = strstr( pcursor, cmd ); }
		if ( p!=NULL ) { p = strstr( p, "\r\n" );
			if ( p!=NULL ) {
				pt->tl1text = p+2;
				p = strstr( p, "\nM " );
				p = strstr( p, pt->sPrompt );
				if ( p!=NULL ) {
					pcursor = ++p;
					pt->tl1len = pcursor - pt->tl1text;
				}
			}
		}
		if ( pt->tl1len == 0 ) { pt->tl1text = pt->buff+pt->cursor_x; }
	}

	if ( pTl1Text!=NULL ) *pTl1Text = pt->tl1text;
	return pt->tl1len;
}
int term_Pwd( TERM *pt, char *pwd, int len)
{
	char *p1, *p2;
	term_TL1( pt,"pwd\r", &p2 );
	p1 = strchr(p2, 0x0a);
	if ( p1!=NULL ) {
		p2 = p1+1;
		p1 = strchr(p2, 0x0a);
		if ( p1!=NULL ) {
			if ( len>p1-p2 ) len = p1-p2;
			strncpy(pwd, p2, len);
			pwd[len]=0;
		}
	}
	else
		len = 0;
	return len;
}
int term_Scp( TERM *pt, char *cmd, char **preply )
{
	if ( host_Type( pt->host )!=SSH ) return 0; 

	for ( char *q=cmd; *q; q++ ) if ( *q=='\\' ) *q='/';
	char *p = strchr(cmd, ' ');
	if ( p==NULL ) return 0;
	*p++ = 0;

	char *lpath, *rpath, *reply = term_Mark_Prompt( pt );
	term_Learn_Prompt( pt );
	if ( *cmd==':' ) {	//scp_read
		lpath = p; rpath = cmd+1;
		char ls_1[1024]="ls -1 ";
		if ( *rpath!='/' && *rpath!='~' ) {
			term_Pwd( pt, ls_1+6, 1016 );
			strcat(ls_1, "/");
		}
		strcat(ls_1, rpath);
		
		if ( strchr(rpath, '*')==NULL 
		  && strchr(rpath, '?')==NULL ) {		//rpath is a single file
			reply = term_Mark_Prompt( pt );	
			strcat(ls_1, "\012");
			scp_read( pt->host, lpath, ls_1+6 );
		}
		else {									//rpath is a filename pattern
			char *rlist, *rfiles;
			strcat(ls_1, "\r");
			int len = term_TL1( pt, ls_1, &rlist );
			if ( len>0 ) {
				rlist[len] = 0;
				p = strchr(rlist, 0x0a);
				if ( p!=NULL ) {
					rfiles = strdup(p+1);
					reply = term_Mark_Prompt( pt );
					scp_read( pt->host, lpath, rfiles );
					free(rfiles);
				}
			}
		}
	}
	else {//scp_write
		lpath = cmd; rpath = p+1;				//*p is expected to be ':' here
		char lsld[1024]="ls -ld ";
		if ( *rpath!='/' && *rpath!='~' ) {		//rpath is relative
			term_Pwd( pt, lsld+7, 1017);
			if ( *rpath ) strcat(lsld, "/");
		}
		strcat(lsld, rpath);
		strcat(lsld, "\r");

		if ( lsld[strlen(lsld)-1]!='/' )  {
			char *rlist;			 
			if ( term_TL1( pt, lsld, &rlist )>0 ) {//check if rpath is a dir
				lsld[strlen(lsld)-1] = 0;
				p = strchr(rlist, 0x0a);
				if ( p!=NULL ) {				//append '/' if yes
					if ( p[1]=='d' ) strcat(lsld, "/");
				}
			}
		}
		reply = term_Mark_Prompt( pt );
		scp_write( pt->host, lpath, lsld+7 );
	}
	term_Send( pt, "\r", 1 );
	if ( preply!=NULL ) *preply = reply;
	return term_Waitfor_Prompt( pt );
}
int term_Tun( TERM *pt, char *cmd, char **preply)
{
	if ( host_Type( pt->host )!=SSH ) return 0; 
	
	char *reply = term_Mark_Prompt( pt );
	if ( preply!=NULL ) *preply = reply;
	ssh2_Tun( pt->host, cmd );
	return term_Waitfor_Prompt( pt );
}
int term_Cmd( TERM *pt, char *cmd, char **preply )
{
	if ( *cmd!='!' ) return term_TL1( pt, cmd, preply);
	cmd[strcspn(cmd, "\r")] = 0;	//remove trailing "\r"

	int rc = 0;
	if ( strncmp(++cmd, "Clear",5)==0 )		term_Clear( pt );
	else if ( strncmp(cmd, "Log", 3)==0 )	term_Logg( pt, cmd+3 );
	else if ( strncmp(cmd, "Find ",5)==0 )	term_Srch( pt, cmd+5 );
	else if ( strncmp(cmd, "Disp ",5)==0 )	term_Disp( pt, cmd+5 );
	else if ( strncmp(cmd, "Send ",5)==0 )
	{
		term_Mark_Prompt( pt );
		term_Send( pt, cmd+5,strlen(cmd+5) );
		term_Send( pt, "\r", 1 );
	}
	else if ( strncmp(cmd, "Hostname",8)==0) 
	{
		if ( preply!=NULL ) 
		{
			*preply = host_Status(pt->host)==HOST_IDLE ? "":pt->host->hostname;
			rc = strlen(*preply);
		}
	}
	else if ( strncmp(cmd,"Selection",9)==0) 
	{
		if ( preply!=NULL ) *preply = pt->buff+pt->sel_left;
		rc = pt->sel_right-pt->sel_left;
	}
	else if ( strncmp(cmd, "Recv" ,4)==0 )	rc = term_Recv( pt, preply );
	else if ( strncmp(cmd, "Echo", 4)==0 )	rc = term_Echo( pt ) ? 1 : 0;
	else if ( strncmp(cmd, "Timeout",7)==0 )pt->iTimeOut = atoi( cmd+8 );
	else if ( strncmp(cmd, "Prompt ",7)==0 ) 
	{
		strncpy(pt->sPrompt, cmd+7, 31);
		pt->sPrompt[31] = 0;
		url_decode(pt->sPrompt);
		pt->iPrompt = strlen(pt->sPrompt);
	}
	else if ( strncmp(cmd, "Tftpd",5)==0 )	tftp_Svr( cmd+5 );
	else if ( strncmp(cmd, "Ftpd", 4)==0 ) 	ftp_Svr( cmd+4 );
	else if ( strncmp(cmd, "tun",  3)==0 ) 	rc = term_Tun( pt, cmd+3, preply);
	else if ( strncmp(cmd, "scp ", 4)==0 ) 	rc = term_Scp( pt, cmd+4, preply);
	else if ( strncmp(cmd, "Wait ", 5)==0 ) Sleep(atoi(cmd+5)*1000);
	else if ( strncmp(cmd, "Waitfor ", 8)==0) 
	{
		for ( int i=pt->iTimeOut; i>0; i-- ) 
		{
			pt->buff[pt->cursor_x] = 0;
			if ( strstr(pt->tl1text, cmd+8)!=NULL ) {
				if ( preply!=NULL ) *preply = pt->tl1text;
				rc = pt->buff+pt->cursor_x-pt->tl1text;
				break;
			}
			Sleep(1000);
		}
	}
	else 
	{
		if ( host_Status( pt->host )==HOST_IDLE )
			term_Print(pt, "\033[33m%s\n", cmd); 
		term_Mark_Prompt( pt );
		host_Open( pt->host, cmd );
	}
	return rc;
}
const unsigned char *vt100_Escape( TERM *pt, const unsigned char *sz, int cnt )
{
	const unsigned char *zz = sz+cnt;

	pt->bEscape = TRUE;
	while ( sz<zz && pt->bEscape ) 
	{
		pt->escape_code[pt->escape_idx++] = *sz++;
		switch( pt->escape_code[0] ) 
		{
		case '[': if ( isalpha(pt->escape_code[pt->escape_idx-1])
						|| pt->escape_code[pt->escape_idx-1]=='@'
						|| pt->escape_code[pt->escape_idx-1]=='`' ) 
						{
			pt->bEscape = FALSE;
			int n0=1, n1=1, n2=1;
			if ( pt->escape_idx>1 ) 
			{
				char *p = strchr(pt->escape_code, ';');
				if ( p != NULL ) {
					n0 = 0; n1 = atoi(pt->escape_code+1); n2 = atoi(p+1);
				}
				else {
					if ( isdigit(pt->escape_code[1]) )
						n0 = atoi(pt->escape_code+1);
				}
			}
			int x;
			switch ( pt->escape_code[pt->escape_idx-1] ) 
			{
			case 'A':
					x = pt->cursor_x - pt->line[pt->cursor_y];
					pt->cursor_y -= n0;
					pt->cursor_x = pt->line[pt->cursor_y]+x;
					break;
			case 'd'://line position absolute
					if ( n0>pt->size_y ) n0 = pt->size_y;
					x = pt->cursor_x-pt->line[pt->cursor_y];
					pt->cursor_y = pt->screen_y+n0-1;
					pt->cursor_x = pt->line[pt->cursor_y]+x;
					break;
			case 'e': //line position relative
			case 'B':
					x = pt->cursor_x - pt->line[pt->cursor_y];
					pt->cursor_y += n0;
					pt->cursor_x = pt->line[pt->cursor_y]+x;
					break;
			case 'a': //character position relative
			case 'C':
					while ( n0-- > 0 ) {
					if ( isUTF8c(pt->buff[++pt->cursor_x]) )
						while ( isUTF8c(pt->buff[++pt->cursor_x]) );
					}
					break;
			case 'D':
					while ( n0-- > 0 ) {
						if ( isUTF8c(pt->buff[--pt->cursor_x]) )
							while ( isUTF8c(pt->buff[--pt->cursor_x]) );
					}
					break;
			case 'E': //cursor to begining of next line n0 times
					pt->cursor_y += n0;
					pt->cursor_x = pt->line[pt->cursor_y];
					break;
			case 'F': //cursor to begining of previous line n0 times
					pt->cursor_y -= n0;
					pt->cursor_x = pt->line[pt->cursor_y];
					break;
			case '`': //character position absolute
			case 'G':
					n1 = pt->cursor_y-pt->screen_y+1;
					n2 = n0;
			case 'f': //horizontal and vertical position forced
					if ( n1==0 ) n1=1;
					if ( n2==0 ) n2=1;
			case 'H':
					if ( n1>pt->size_y ) n1 = pt->size_y;
					if ( n2>pt->size_x ) n2 = pt->size_x;
					pt->cursor_y = pt->screen_y+n1-1;
					pt->cursor_x = pt->line[pt->cursor_y];
					while ( --n2>0 ) 
					{
						pt->cursor_x++;
						while ( isUTF8c(pt->buff[pt->cursor_x]) ) pt->cursor_x++;
					}
					break;
			case 'J': 	//[J kill till end, 1J begining, 2J entire screen
					if ( pt->escape_code[pt->escape_idx-2]=='[' ) 
					{
						if ( pt->bAlterScreen )
							pt->screen_y = pt->cursor_y;
						else 
						{
							int i=pt->cursor_y+1; 
							pt->line[i]=pt->cursor_x;
							while ( ++i<=pt->screen_y+pt->size_y ) 
								pt->line[i]=0;
							break;
						}
					}
					pt->cursor_y = pt->screen_y;
					pt->cursor_x = pt->line[pt->cursor_y];
					for ( int i=0; i<pt->size_y; i++ ) 
					{
						memset( pt->buff+pt->cursor_x, ' ', pt->size_x );
						memset( pt->attr+pt->cursor_x,   0, pt->size_x );
						pt->cursor_x += pt->size_x;
						term_nextLine( pt );
					}
					pt->cursor_y = --pt->screen_y; 
					pt->cursor_x = pt->line[pt->cursor_y];
					break;
			case 'K': {		//[K kill till end, 1K begining, 2K entire line
					int i = pt->line[pt->cursor_y];
					int j = pt->line[pt->cursor_y+1];
					if ( pt->escape_code[pt->escape_idx-2]=='[' )
						i = pt->cursor_x;
					else
						if ( n0==1 ) j = pt->cursor_x;
					if ( j>i ) {
						memset( pt->buff+i, ' ', j-i );
						memset( pt->attr+i,   0, j-i );
						}
					}
					break;
			case 'L':	//insert lines
					for ( int i=pt->roll_bot; i>pt->cursor_y-pt->screen_y;i--)
					{
						memcpy( pt->buff+pt->line[pt->screen_y+i],
								pt->buff+pt->line[pt->screen_y+i-n0],
								pt->size_x);
						memcpy( pt->attr+pt->line[pt->screen_y+i],
								pt->attr+pt->line[pt->screen_y+i-n0], 
								pt->size_x);
					}
					for ( int i=0; i<n0; i++ ) {
						memset( pt->buff+pt->line[pt->cursor_y+i], ' ',
								pt->size_x);
						memset( pt->attr+pt->line[pt->cursor_y+i],   0,
								pt->size_x);
					}
					break;
			case 'M'://delete lines
					for ( int i=pt->cursor_y-pt->screen_y; 
								i<=pt->roll_bot-n0; i++ ) 
					{
						memcpy( pt->buff+pt->line[pt->screen_y+i],
								pt->buff+pt->line[pt->screen_y+i+n0], 
								pt->size_x);
						memcpy( pt->attr+pt->line[pt->screen_y+i],
								pt->attr+pt->line[pt->screen_y+i+n0], 
								pt->size_x);
					}
					for ( int i=0; i<n0; i++ ) 
					{
						memset( pt->buff+pt->line[pt->screen_y+pt->roll_bot-i],
								' ', pt->size_x);
						memset( pt->attr+pt->line[pt->screen_y+pt->roll_bot-i],
								0, pt->size_x);
					}
					break;
			case 'P':
					for ( int i=pt->cursor_x;i<pt->line[pt->cursor_y+1]-n0;i++ )
					{
						pt->buff[i]=pt->buff[i+n0];
						pt->attr[i]=pt->attr[i+n0];
					}
					if ( !pt->bAlterScreen ) 
					{
						pt->line[pt->cursor_y+1]-=n0;
						if ( pt->line[pt->cursor_y+2]>0 )
							pt->line[pt->cursor_y+2]-=n0;
						memset(pt->buff+pt->line[pt->cursor_y+1], ' ', n0);
						memset(pt->attr+pt->line[pt->cursor_y+1],   0, n0);
					}
					break;
			case '@':	//insert n0 spaces
					for ( int i=pt->line[pt->cursor_y+1];i>=pt->cursor_x;i-- )
					{
						pt->buff[i+n0]=pt->buff[i];
						pt->attr[i+n0]=pt->attr[i];
					}
					memset(pt->buff+pt->cursor_x, ' ', n0);
					memset(pt->attr+pt->cursor_x,   0, n0);
					pt->line[pt->cursor_y+1]+=n0;
					break;
			case 'X': //erase n0 characters
					for ( int i=0; i<n0; i++) {
						pt->buff[pt->cursor_x+i]=' ';
						pt->attr[pt->cursor_x+i]=0;
					}
					break;
			case 'S': // scroll up n0 lines
					for ( int i=pt->roll_top; i<=pt->roll_bot-n0; i++ )
					{
						memcpy( pt->buff+pt->line[pt->screen_y+i],
								pt->buff+pt->line[pt->screen_y+i+n0], 
								pt->size_x);
						memcpy( pt->attr+pt->line[pt->screen_y+i],
								pt->attr+pt->line[pt->screen_y+i+n0], 
								pt->size_x);
					}
					memset( pt->buff+pt->line[pt->screen_y+pt->roll_bot-n0+1],
							' ', n0*pt->size_x);
					memset( pt->attr+pt->line[pt->screen_y+pt->roll_bot-n0+1],
							0, n0*pt->size_x);
					break;
			case 'T': // scroll down n0 lines
					for ( int i=pt->roll_bot; i>=pt->roll_top+n0; i-- ) {
						memcpy( pt->buff+pt->line[pt->screen_y+i],
								pt->buff+pt->line[pt->screen_y+i-n0], 
								pt->size_x);
						memcpy( pt->attr+pt->line[pt->screen_y+i],
								pt->attr+pt->line[pt->screen_y+i-n0], 
								pt->size_x);
					}
					memset( pt->buff+pt->line[pt->screen_y+pt->roll_top],
							' ', n0*pt->size_x);
					memset( pt->attr+pt->line[pt->screen_y+pt->roll_top],
							0, n0*pt->size_x);
					break;
				case 'I': //cursor forward n0 tab stops
					pt->cursor_x += n0*8;
					break;
				case 'Z': //cursor backward n0 tab stops
					pt->cursor_x -= n0*8;
					break;
			case 'h':
					if ( pt->escape_code[1]=='4' )  pt->bInsert = TRUE;
					if ( pt->escape_code[1]=='?' ) {
						n0 = atoi(pt->escape_code+2);
						if ( n0==1 ) pt->bAppCursor = TRUE;
						if ( n0==25 ) pt->bCursor = TRUE;
						if ( n0==2004 ) pt->bBracket = TRUE;
						if ( n0==1049 ) { 	//?1049h alternate screen,
							pt->bAlterScreen = TRUE;
							pt->save_edit = tiny_Edit(FALSE);
							pt->screen_y = pt->cursor_y;
							pt->cursor_x = pt->line[pt->cursor_y];
							for ( int i=0; i<pt->size_y; i++ ) 
							{
								memset( pt->buff+pt->cursor_x,' ',pt->size_x );
								memset( pt->attr+pt->cursor_x,  0,pt->size_x );
								pt->cursor_x += pt->size_x;
								term_nextLine( pt );
							}
							pt->cursor_y = --pt->screen_y; 
							pt->cursor_x = pt->line[pt->cursor_y];
						}
					}
					break;
			case 'l':
					if ( pt->escape_code[1]=='4' ) pt->bInsert = FALSE;
					if ( pt->escape_code[1]=='?' ) {
						n0 = atoi(pt->escape_code+2);
						if ( n0==1 ) pt->bAppCursor = FALSE;
						if ( n0==25 ) pt->bCursor = FALSE;
						if ( n0==2004 ) pt->bBracket = FALSE;
						if ( n0==1049 ) { 	//?1049l exit alternate screen,
							pt->bAlterScreen = FALSE;
							if ( pt->save_edit ) tiny_Edit(TRUE);
							pt->cursor_y = pt->screen_y;
							pt->cursor_x = pt->line[pt->cursor_y];
							for ( int i=1; i<=pt->size_y+1; i++ )
								pt->line[pt->cursor_y+i] = 0;
							pt->screen_y = max(0, pt->cursor_y-pt->size_y+1);
						}
					}
					break;
			case 'm':
					if ( pt->escape_code[pt->escape_idx-2]=='[' ) n0 = 0;
					if ( n0==0 && n2!=1 ) n0 = n2;	//ESC[0;0m	ESC[01;34m
					switch ( (int)(n0/10) ) {		//ESC[34m
					case 0:	if ( n0==1 ) { pt->c_attr|= 0x08; break; }//bright
							if ( n0==7 ) { pt->c_attr = 0x70; break; }//negative
					case 2: pt->c_attr = 7; break;					  //normal
					case 3: if ( n0==39 ) n0 = 37;	//39 default foreground
							pt->c_attr = (pt->c_attr&0xf0) + n0%10; break;
					case 4: if ( n0==49 ) n0 = 0;	//49 default background
							pt->c_attr = (pt->c_attr&0x0f) + ((n0%10)<<4); 
							break;
					case 9: pt->c_attr = (pt->c_attr&0xf0) + n0%10 + 8; 
							break;
					case 10:pt->c_attr = (pt->c_attr&0x0f) + ((n0%10+8)<<4); 
							break;
					}
					break;
			case 'r':
					pt->roll_top=n1-1; pt->roll_bot=n2-1;
					break;
			case 's': //save cursor
					pt->save_x = pt->cursor_x-pt->line[pt->cursor_y];
					pt->save_y = pt->cursor_y-pt->screen_y;
					break;
			case 'u': //restore cursor
					pt->cursor_y = pt->save_y+pt->screen_y;
					pt->cursor_x = pt->line[pt->cursor_y]+pt->save_x;
					break;
				}
			}
			break;
		case 'D': // scroll down one line
				for ( int i=pt->roll_top; i<pt->roll_bot; i++ ) {
					memcpy( pt->buff+pt->line[pt->screen_y+i],
							pt->buff+pt->line[pt->screen_y+i+1], pt->size_x );
					memcpy( pt->attr+pt->line[pt->screen_y+i],
							pt->attr+pt->line[pt->screen_y+i+1], pt->size_x );
				}
				memset( pt->buff+pt->line[pt->screen_y+pt->roll_bot], 
						' ', pt->size_x);
				memset( pt->attr+pt->line[pt->screen_y+pt->roll_bot],
						0, pt->size_x);
				pt->bEscape = FALSE;
				break;
		case 'F': //cursor to lower left corner
				pt->cursor_y = pt->screen_y+pt->size_y-1;
				pt->cursor_x = pt->line[pt->cursor_y];
				pt->bEscape = FALSE;
				break;
		case 'M': // scroll up one line
				for ( int i=pt->roll_bot; i>pt->roll_top; i-- ) {
					memcpy( pt->buff+pt->line[pt->screen_y+i],
							pt->buff+pt->line[pt->screen_y+i-1], pt->size_x);
					memcpy( pt->attr+pt->line[pt->screen_y+i],
							pt->attr+pt->line[pt->screen_y+i-1], pt->size_x);
				}
				memset( pt->buff+pt->line[pt->screen_y+pt->roll_top],
						' ', pt->size_x);
				memset( pt->attr+pt->line[pt->screen_y+pt->roll_top],
						' ', pt->size_x);
				pt->bEscape = FALSE;
				break;
		case ']':
				if ( pt->escape_code[pt->escape_idx-1]==';' ) {
					if ( pt->escape_code[1]=='0' ) {
						pt->bTitle = TRUE;
						pt->title_idx = 0;
					}
					pt->bEscape = FALSE;
				}
				break;
		case '(':
				if ( (pt->escape_code[1]=='B'||pt->escape_code[1]=='0') ) {
					pt->bGraphic = (pt->escape_code[1]=='0');
					pt->bEscape = FALSE;
				}
				break;
		default: pt->bEscape = FALSE;
		}
		if ( pt->escape_idx==20 ) pt->bEscape = FALSE;
		if ( !pt->bEscape ) 
		{
			pt->escape_idx=0; 
			memset(pt->escape_code, 0, 20);
		}
	}
	return sz;
}
void term_Parse_XML( TERM *pt, const char *msg, int len)
{
	const char *p=msg, *q;
	const char spaces[256]="\r\n                                               \
                                                                              ";
	if ( strncmp(msg, "<?xml ", 6)==0 ) {
		pt->xmlIndent = 0;
		pt->xmlPreviousIsOpen = TRUE;
	}
	while ( *p!=0 && *p!='<' ) p++;
	if ( p>msg ) term_Parse( pt, msg, p-msg );
	while ( *p!=0 && p<msg+len ) {
		while (*p==0x0d || *p==0x0a || *p=='\t' || *p==' ' ) p++;
		if ( *p=='<' ) { //tag
			if ( p[1]=='/' ) {
				if ( !pt->xmlPreviousIsOpen ) {
					pt->xmlIndent -= 2;
					term_Parse( pt, spaces, pt->xmlIndent );
				}
				pt->xmlPreviousIsOpen = FALSE;
			}
			else {
				if ( pt->xmlPreviousIsOpen ) pt->xmlIndent+=2;
				term_Parse( pt, spaces, pt->xmlIndent );
				pt->xmlPreviousIsOpen = TRUE;
			}
			term_Parse( pt, "\033[32m", 5 );
			q = strchr(p, '>');
			if ( q==NULL ) q = p+strlen(p);
			char *r = strchr(p, ' ');
			if ( r!=NULL && r<q ) {
				term_Parse( pt, p, r-p );
				term_Parse( pt, "\033[34m",5 );
				term_Parse( pt, r, q-r );
			}
			else
				term_Parse( pt, p, q-p );
			term_Parse( pt, "\033[32m>", 6 );
			p = q;
			if ( *q=='>' ) {
				p++;
				if ( q[-1]=='/' ) pt->xmlPreviousIsOpen = FALSE;
			}
		}
		else {									//data
			term_Parse( pt, "\033[33m", 5 );
			int l;
			q = strchr(p, '<');
			if ( q==NULL ) q = p+strlen(p);
			term_Parse( pt, p, q-p );
			p = q;
		}
	}
}
#define TNO_IAC		0xff
#define TNO_DONT	0xfe
#define TNO_DO		0xfd
#define TNO_WONT	0xfc
#define TNO_WILL	0xfb
#define TNO_SUB		0xfa
#define TNO_SUBEND	0xf0
#define TNO_ECHO	0x01
#define TNO_AHEAD	0x03
#define TNO_STATUS	0x05
#define TNO_WNDSIZE 0x1f
#define TNO_TERMTYPE 0x18
#define TNO_NEWENV	0x27
//UCHAR NEGOBEG[]={0xff, 0xfb, 0x03, 0xff, 0xfd, 0x03, 0xff, 0xfd, 0x01};
unsigned char TERMTYPE[]={  0xff, 0xfa, 0x18, 0x00, 
							0x76, 0x74, 0x31, 0x30, 0x30, //vt100
							0xff, 0xf0};
const unsigned char *telnet_Options( TERM *pt, const unsigned char *p, int cnt )
{
	const unsigned char *q = p+cnt;
	while ( *p==0xff && p<q ) {
		unsigned char negoreq[]={0xff,0,0,0, 0xff, 0xf0};
		switch ( p[1] ) {
		case TNO_DONT:
		case TNO_WONT:
			p+=3;
			break;
		case TNO_DO:
			negoreq[1]=TNO_WONT; negoreq[2]=p[2];
			if ( p[2]==TNO_TERMTYPE || p[2]==TNO_NEWENV
				|| p[2]==TNO_ECHO || p[2]==TNO_AHEAD ) {
				negoreq[1]=TNO_WILL; 
				if ( p[2]==TNO_ECHO ) pt->bEcho = TRUE;
			}
			term_Send( pt, (char *)negoreq, 3);
			p+=3;
			break;
		case TNO_WILL:
			negoreq[1]=TNO_DONT; negoreq[2]=p[2];
			if ( p[2]==TNO_ECHO || p[2]==TNO_AHEAD ) {
				negoreq[1]=TNO_DO;
				if ( p[2]==TNO_ECHO ) pt->bEcho = FALSE;
			} 
			term_Send( pt, (char*)negoreq, 3);
			p+=3;
			break;
		case TNO_SUB:
			negoreq[1]=TNO_SUB; negoreq[2]=p[2];
			if ( p[2]==TNO_TERMTYPE ) {
				term_Send( pt, (char *)TERMTYPE, sizeof(TERMTYPE));
			}
			if ( p[2]==TNO_NEWENV ) {
				term_Send( pt, (char*)negoreq, 6);
			}
			do { p++; } while ( *p!=0xff && p<q );
			break;
		case TNO_SUBEND:
			p+=2;
		}
	} 
	return p;
}