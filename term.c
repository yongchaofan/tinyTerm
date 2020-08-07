//
// "$Id: term.c 36800 2020-08-04 15:05:10 $"
//
// tinyTerm -- A minimal serail/telnet/ssh/sftp terminal emulator
//
// term.c is the minimal xterm implementation, only common ESCAPE
// control sequences are supported for apps like top, vi etc.
//
// Copyright 2018-2020 by Yongchao Fan.
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

const unsigned char *vt100_Escape(TERM *pt, const unsigned char *sz, int cnt);
const unsigned char *telnet_Options(TERM *pt, const unsigned char *p, int cnt);

BOOL term_Construct(TERM *pt)
{
	pt->size_x=80;
	pt->size_y=25;
	pt->roll_top = 0;
	pt->roll_bot = pt->size_y-1;
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

	if (pt->buff!=NULL && pt->attr!=NULL && pt->line!=NULL )
	{
		term_Clear(pt);
		return TRUE;
	}
	return FALSE;
}
void term_Destruct(TERM *pt)
{
	free(pt->buff);
	free(pt->attr);
	free(pt->line);
}
void term_Clear(TERM *pt)
{
	memset(pt->buff, 0, BUFFERSIZE);
	memset(pt->attr, 0, BUFFERSIZE);
	memset(pt->line, 0, MAXLINES*sizeof(int));
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
	pt->bOriginMode = FALSE;
	pt->bWraparound = TRUE;
	pt->bCursor = TRUE;
	pt->bPrompt = TRUE;
	pt->save_edit = FALSE;
	pt->escape_idx = 0;
	pt->xmlIndent = 0;
	pt->xmlPreviousIsOpen = TRUE;
	memset(pt->tabstops, 0, 256);
	for ( int i=0; i<256; i+=8 ) pt->tabstops[i]=1;
}
void term_Size(TERM *pt, int x, int y)
{
	pt->size_x = x;
	pt->size_y = y;
	pt->roll_top = 0;
	pt->roll_bot = pt->size_y-1;
	if ( !pt->bAlterScreen ) {
		pt->screen_y = max(0, pt->cursor_y-pt->size_y+1);
	}
	host_Send_Size(pt->host, pt->size_x, pt->size_y);
	tiny_Redraw_Term( );
}
void term_nextLine(TERM *pt)
{
	pt->line[++pt->cursor_y] = pt->cursor_x;
	if (pt->line[pt->cursor_y+1]<pt->cursor_x )
		pt->line[pt->cursor_y+1]=pt->cursor_x;
	if (pt->screen_y==pt->cursor_y-pt->size_y ) pt->screen_y++;

	if (pt->cursor_x>=BUFFERSIZE-1024 || pt->cursor_y>=MAXLINES-4 ) {
		int i, len = pt->line[4096];
		pt->tl1text -= len;
		if (pt->tl1text<pt->buff ) {
			pt->tl1len -= pt->buff-pt->tl1text;
			pt->tl1text = pt->buff;
		}
		pt->cursor_x -= len;
		pt->cursor_y -= 4096;
		pt->screen_y -= 4096;
		if (pt->screen_y<0 ) pt->screen_y = 0;
		memmove(pt->buff, pt->buff+len, BUFFERSIZE-len);
		memset(pt->buff+pt->cursor_x, 0, BUFFERSIZE-pt->cursor_x);
		memmove(pt->attr, pt->attr+len, BUFFERSIZE-len);
		memset(pt->attr+pt->cursor_x, 0, BUFFERSIZE-pt->cursor_x);
		for ( i=0; i<pt->cursor_y+2; i++ ) 
			pt->line[i] = pt->line[i+4096]-len;
		while ( i<MAXLINES ) pt->line[i++] = 0;
	}
}
void term_Parse(TERM *pt, const char *buf, int len)
{
	const unsigned char *p=(const unsigned char *)buf;
	const unsigned char *zz = p+len;

	if (pt->bLogging ) fwrite( buf, 1, len, pt->fpLogFile);
	if (pt->bEscape ) p = vt100_Escape(pt, p, zz-p);
	while ( p < zz ) {
		unsigned char c = *p++;
		if (pt->bTitle ) {
			if ( c==0x07 ) {
				pt->bTitle = FALSE;
				pt->title[pt->title_idx]=0;
				tiny_Title(pt->title);
			}
			else
				if (pt->title_idx<63 ) 
					pt->title[pt->title_idx++] = c;
			continue;
		}
		switch ( c ) {
		case 0x00:
		case 0x0e:
		case 0x0f: 	break;
		case 0x07:	tiny_Beep();break;
		case 0x08:
			if (pt->cursor_x>pt->line[pt->cursor_y] ) {
				if ( isUTF8c(pt->buff[pt->cursor_x--]) )//utf8 continuation
					while ( isUTF8c(pt->buff[pt->cursor_x]) ) 
						pt->cursor_x--;
			}
			break;
		case 0x09: {
			int l;
			do {
				pt->attr[pt->cursor_x] = pt->c_attr;
				pt->buff[pt->cursor_x++]=' ';
				l=pt->cursor_x-pt->line[pt->cursor_y];
			} while ( l<pt->size_x && pt->tabstops[l]==0);
		}
			break;
		case 0x0a:
		case 0x0b:
		case 0x0c:
			if (pt->bAlterScreen || pt->line[pt->cursor_y+2]!=0 ) {
					//IND to next line
				vt100_Escape(pt, (const unsigned char *)"D", 1);
			}
			else {	//LF and new line
				pt->cursor_x = pt->line[pt->cursor_y+1];
				pt->attr[pt->cursor_x] = pt->c_attr;
				pt->buff[pt->cursor_x++] = c;	
				term_nextLine(pt);
			}
			break;
		case 0x0d:
			if (pt->cursor_x-pt->line[pt->cursor_y]==pt->size_x+1 && *p!=0x0a)
				term_nextLine(pt);	//soft line feed
			else
				pt->cursor_x = pt->line[pt->cursor_y];
			break;
		case 0x1b:	p = vt100_Escape(pt, p, zz-p); break;
		case 0xff:	p = telnet_Options(pt, p-1, zz-p+1); break;
		case 0xe2:	
			if (pt->bAlterScreen ) {
				c = ' ';			//hack utf8 box drawing
				if ( *p++==0x94 )	//to make alterscreen easier
				{	
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
			}//fall through
		default:
			if (pt->bGraphic ) 
				switch ( c ) {
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
			if (pt->bInsert ) 
				vt100_Escape(pt, (const unsigned char *)"[1@", 3);
			if (pt->cursor_x-pt->line[pt->cursor_y]>=pt->size_x ) {
				int char_cnt=0;
				for ( int i=pt->line[pt->cursor_y]; i<pt->cursor_x; i++ )
					if ( !isUTF8c(pt->buff[i]) ) char_cnt++;
				if ( char_cnt==pt->size_x ) {
					if (pt->bWraparound )//pt->bAlterScreen
						term_nextLine(pt);
					else
						pt->cursor_x--; //don't overflow in vi
				}
			}
			pt->attr[pt->cursor_x] = pt->c_attr;
			pt->buff[pt->cursor_x++] = c;
			if (pt->line[pt->cursor_y+1]<pt->cursor_x ) 
				pt->line[pt->cursor_y+1]=pt->cursor_x;
		}
	}

	if ( !pt->bPrompt && pt->cursor_x>pt->iPrompt ) {
		char *p=pt->buff+pt->cursor_x-pt->iPrompt;
		if ( strncmp(p, pt->sPrompt, pt->iPrompt)==0 ) pt->bPrompt=TRUE;
		pt->tl1len = pt->buff+pt->cursor_x - pt->tl1text;
	}
	tiny_Redraw_Term();
}
BOOL term_Echo(TERM *pt)
{
	pt->bEcho=!pt->bEcho;
	return pt->bEcho;
}
void term_Error(TERM *pt, char *error)
{
	term_Print(pt, "\r\n\033[31m%s! \033[37m", error);
	term_Disp(pt, "Press \033[33mEnter \033[37mto reconnect\r\n");
}
void term_Title(TERM *pt, char *title)
{
	switch ( host_Status(pt->host) ) {
	case IDLE:
		pt->title[0] = 0;
		pt->bEcho = FALSE;
		break;
	case CONNECTING:
		break;
	case CONNECTED:
		host_Send_Size(pt->host, pt->size_x, pt->size_y);
		if ( host_Type(pt->host)==NETCONF ) pt->bEcho = TRUE;
		break;
	}
	strncpy(pt->title, title, 63);
	pt->title[63] = 0;
	tiny_Title(pt->title);
}
void term_Scroll(TERM *pt, int lines)
{	
	if ( pt->bAlterScreen ) return;
	pt->screen_y -= lines;
	if (pt->screen_y<0 || pt->screen_y>pt->cursor_y ) {
		pt->screen_y += lines;
		return;
	}
	if ( tiny_Scroll(pt->screen_y<pt->cursor_y-pt->size_y+1,
						pt->cursor_y, pt->screen_y) )
		pt->screen_y -= lines;			//first pageup fix
}
void term_Mouse(TERM *pt, int evt, int x, int y)
{
	switch( evt ) {
	case DOUBLECLK:
		y += pt->screen_y;
		pt->sel_left = pt->line[y]+x;
		pt->sel_right = pt->sel_left;
		while ( --pt->sel_left>pt->line[y] )
			if (pt->buff[pt->sel_left]==0x0a
				|| pt->buff[pt->sel_left]==0x20 ) {
				pt->sel_left++;
				break;
			}
		while ( ++pt->sel_right<pt->line[y+1]) {
			if (pt->buff[pt->sel_right]==0x0a
				|| pt->buff[pt->sel_right]==0x20 )
				 break;
		}
		break;
	case LEFTDOWN:
		y += pt->screen_y;
		pt->sel_left = min(pt->line[y]+x, pt->line[y+1]);
		while ( isUTF8c(pt->buff[pt->sel_left]) ) pt->sel_left--;
		pt->sel_right = pt->sel_left;
		break;
	case LEFTDRAG:
		if ( y<0 ) {
			pt->screen_y += y*2;
			if (pt->screen_y<0 ) pt->screen_y = 0;
		}
		if ( y>pt->size_y) {
			pt->screen_y += (y-pt->size_y)*2;
			if (pt->screen_y>pt->cursor_y ) pt->screen_y=pt->cursor_y;
		}
		y += pt->screen_y;
		pt->sel_right = min(pt->line[y]+x, pt->line[y+1]);
		while ( isUTF8c(pt->buff[pt->sel_right]) ) pt->sel_right++;
		break;
	case LEFTUP:
		if (pt->sel_right!=pt->sel_left ) {
			int sel_min = min(pt->sel_left, pt->sel_right);
			int sel_max = max(pt->sel_left, pt->sel_right);
			pt->sel_left = sel_min;
			pt->sel_right = sel_max;
		}
		else {
			pt->sel_left = pt->sel_right = 0;
		}
		break;
	case MIDDLEUP:
		if (pt->sel_left!=pt->sel_right ) 
			term_Send(pt, pt->buff+pt->sel_left, pt->sel_right-pt->sel_left);
		break;
	}
	tiny_Redraw_Term();
}
void term_Print(TERM *pt, const char *fmt, ...)
{
	char buff[4096];
	va_list args;
	va_start(args, (char *)fmt);
	int len = vsnprintf(buff, 4096, (char *)fmt, args);
	va_end(args);
	term_Parse(pt, buff, len);
	term_Parse(pt, "\033[37m", 5);
}
void term_Disp(TERM *pt, const char *msg )
{
	pt->tl1text = pt->buff+pt->cursor_x;
	term_Parse(pt, msg, strlen(msg));
}
void term_Send(TERM *pt, char *buf, int len)
{
	if (pt->bEcho ) {
		if ( host_Type(pt->host)==NETCONF ) 
			term_Parse_XML(pt, buf, len);
		else
			term_Parse(pt, buf, len);
	}
	if ( host_Status(pt->host)!=IDLE ) 
		host_Send(pt->host, buf, len);
}
void term_Paste(TERM *pt, char *buf, int len)
{
	if (pt->bBracket ) term_Send(pt, "\033[200~", 6);
	term_Send(pt, buf, len);
	if (pt->bBracket ) term_Send(pt, "\033[201~", 6);
}
int term_Copy(TERM *pt, char **buf)
{
	*buf = pt->buff+pt->sel_left;
	return pt->sel_right-pt->sel_left;
}
int term_Recv(TERM *pt, char **pTL1text)
{
	if ( pTL1text!=NULL ) *pTL1text = pt->tl1text;
	int len = pt->buff+pt->cursor_x - pt->tl1text;
	pt->tl1text = pt->buff+pt->cursor_x;
	return len;
}
void term_Learn_Prompt(TERM *pt)
{//capture prompt for scripting
	if (pt->cursor_x>1 ) {
		pt->sPrompt[0] = pt->buff[pt->cursor_x-2];
		pt->sPrompt[1] = pt->buff[pt->cursor_x-1];
		pt->sPrompt[2] = 0;
		pt->iPrompt = 2;
	}
}
char *term_Mark_Prompt(TERM *pt)
{
	pt->bPrompt = FALSE;
	pt->tl1len = 0;
	pt->tl1text = pt->buff+pt->cursor_x;
	return pt->tl1text;
}
int term_Waitfor_Prompt(TERM *pt)
{
	int oldlen = 0;
	for ( int i=0; i<pt->iTimeOut*10 && !pt->bPrompt; i++ ) {
		if (pt->tl1len>oldlen ) { i=0; oldlen=pt->tl1len; }
		Sleep(100);
	}
	pt->bPrompt = TRUE;
	return pt->tl1len;
}
void term_Logg(TERM *pt, char *fn)
{
	if (pt->bLogging ) {
		fclose(pt->fpLogFile);
		pt->bLogging = FALSE;
		term_Print(pt, "\n\033[33m logging stopped\n");
	}
	else if ( fn!=NULL ) {
		if ( *fn==' ' ) fn++;
		pt->fpLogFile = fopen_utf8( fn, "wb");
		if (pt->fpLogFile != NULL ) {
			pt->bLogging = TRUE;
			term_Print(pt, "\n\033[33m%s logging started\n", fn);
		}
	}
}

int term_Srch(TERM *pt, char *sstr)
{
	int l = strlen(sstr);
	char *p = pt->buff+pt->sel_left;
	if (pt->sel_left==pt->sel_right ) p = pt->buff+pt->cursor_x;
	while ( --p>=pt->buff+l ) {
		int i;
		for ( i=l-1; i>=0; i--) if ( sstr[i]!=p[i-l] ) break;
		if ( i==-1 ) {			//found a match
			pt->sel_left = p-l-pt->buff;
			pt->sel_right = p-pt->buff;
			for ( i=pt->screen_y; pt->line[i]>pt->sel_left; i--);
			term_Scroll(pt, pt->screen_y-i);
			return TRUE;
		}
	}
	return FALSE;
}
int term_TL1(TERM *pt, char *cmd, char **pTl1Text)
{
	if ( host_Status(pt->host )!=IDLE ) {	//retrieve from NE
		term_Mark_Prompt(pt);
		int cmdlen = strlen(cmd);
		term_Send(pt, cmd, cmdlen);
		if ( cmd[cmdlen-1]!='\r' ) term_Send(pt, "\r", 1);
		term_Waitfor_Prompt(pt);
	}
	else {								//retrieve from buffer
		char *pcursor=pt->buff;			//only when retrieve from buffer
		pt->tl1len = 0;
		pt->buff[pt->cursor_x]=0;
		char *p = strstr( pcursor, cmd);
		if ( p==NULL ) { pcursor = pt->buff; p = strstr( pcursor, cmd); }
		if ( p!=NULL ) { p = strstr( p, "\r\n");
			if ( p!=NULL ) {
				pt->tl1text = p+2;
				p = strstr( p, "\nM ");
				p = strstr( p, pt->sPrompt);
				if ( p!=NULL ) {
					pcursor = ++p;
					pt->tl1len = pcursor - pt->tl1text;
				}
			}
		}
		if (pt->tl1len == 0 ) { pt->tl1text = pt->buff+pt->cursor_x; }
	}

	if ( pTl1Text!=NULL ) *pTl1Text = pt->tl1text;
	return pt->tl1len;
}
int term_Pwd(TERM *pt, char *pwd, int len)
{
	char *p1, *p2;
	term_TL1(pt,"pwd\r", &p2);
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
void escape_space(char *p)
{
	do { 
		p = strchr(p, '\\');
		if ( p!=NULL ) {
			if ( p[1]==' ' ) 
				for ( char *q=p; *q; q++ ) *q=q[1];
		}
	} while ( p!=NULL );
}
int term_Scp(TERM *pt, char *cmd, char **preply)
{
	if ( host_Type(pt->host )!=SSH ) return 0; 

	for ( char *q=cmd; *q; q++ ) if ( *q=='\\'&&q[1]!=' ' ) *q='/';
	char *p = strchr(cmd, ' ');
	while ( p!=NULL ) {
		if ( p[-1]!='\\' ) { *p++=0; break; }
		p = strchr(p+1, ' ');
	}

	char *lpath, *rpath, *reply = term_Mark_Prompt(pt);
	term_Learn_Prompt(pt);
	if ( *cmd==':' ) {	//scp_read
		lpath = p; rpath = cmd+1;
		escape_space(lpath);
		char ls_1[1024]="ls -1 ";
		if ( *rpath!='/' && *rpath!='~' ) {
			term_Pwd(pt, ls_1+6, 1016);
			strcat(ls_1, "/");
		}
		strcat(ls_1, rpath);
		
		if ( strchr(rpath, '*')==NULL 
		  && strchr(rpath, '?')==NULL ) {		//rpath is a single file
			reply = term_Mark_Prompt(pt);	
			strcat(ls_1, "\012");
			scp_read(pt->host, lpath, ls_1+6);
		}
		else {									//rpath is a filename pattern
			char *rlist, *rfiles;
			strcat(ls_1, "\r");
			int len = term_TL1(pt, ls_1, &rlist);
			if ( len>0 ) {
				rlist[len] = 0;
				p = strchr(rlist, 0x0a);
				if ( p!=NULL ) {
					rfiles = strdup(p+1);
					reply = term_Mark_Prompt(pt);
					scp_read(pt->host, lpath, rfiles);
					free(rfiles);
				}
			}
		}
	}
	else {//scp_write
		lpath = cmd; rpath = p+1;				//*p is expected to be ':' here
		char lsld[1024]="ls -ld ";
		if ( *rpath!='/' && *rpath!='~' ) {		//rpath is relative
			term_Pwd(pt, lsld+7, 1017);
			if ( *rpath ) strcat(lsld, "/");
		}
		strcat(lsld, rpath);
		strcat(lsld, "\r");

		if ( lsld[strlen(lsld)-1]!='/' )  {
			char *rlist;			 
			if ( term_TL1(pt, lsld, &rlist )>0 ) {//check if rpath is a dir
				lsld[strlen(lsld)-1] = 0;
				p = strchr(rlist, 0x0a);
				if ( p!=NULL ) {				//append '/' if yes
					if ( p[1]=='d' ) strcat(lsld, "/");
				}
			}
		}
		reply = term_Mark_Prompt(pt);
		escape_space(lsld+7);
		escape_space(lpath);
		scp_write(pt->host, lpath, lsld+7);
	}
	term_Send(pt, "\r", 1);
	if ( preply!=NULL ) *preply = reply;
	return term_Waitfor_Prompt(pt);
}
int term_Tun(TERM *pt, char *cmd, char **preply)
{
	if ( host_Type(pt->host )!=SSH ) return 0; 
	
	char *reply = term_Mark_Prompt(pt);
	if ( preply!=NULL ) *preply = reply;
	ssh2_Tun(pt->host, cmd);
	return term_Waitfor_Prompt(pt);
}
int term_xmodem(TERM *pt, char *fn)
{
	if ( host_Type(pt->host)==SERIAL ) {
		FILE *fp = fopen(fn, "rb");
		if ( fp!=NULL ) {
			term_Disp(pt, "sending ");
			term_Disp(pt, fn);
			term_Disp(pt, "\n");
			xmodem_init(pt->host, fp);
			return 1;
		}
	}
	return 0;
}
int term_Cmd(TERM *pt, char *cmd, char **preply)
{
	if ( *cmd!='!' ) return term_TL1(pt, cmd, preply);

	int rc = 0;
	if ( strncmp(++cmd, "Clear",5)==0 )		term_Clear(pt);
	else if ( strncmp(cmd, "Log", 3)==0 )	term_Logg(pt, cmd+3);
	else if ( strncmp(cmd, "Find ",5)==0 )	term_Srch(pt, cmd+5);
	else if ( strncmp(cmd, "Disp ",5)==0 )	term_Disp(pt, cmd+5);
	else if ( strncmp(cmd, "Send ",5)==0 ) {
		term_Mark_Prompt(pt);
		term_Send(pt, cmd+5,strlen(cmd+5));
	}
	else if ( strncmp(cmd, "Hostname",8)==0) {
		if ( preply!=NULL ) {
			*preply = host_Status(pt->host)==IDLE ? "":pt->host->hostname;
			rc = strlen(*preply);
		}
	}
	else if ( strncmp(cmd,"Selection",9)==0) {
		if ( preply!=NULL ) *preply = pt->buff+pt->sel_left;
		rc = pt->sel_right-pt->sel_left;
	}
	else if ( strncmp(cmd, "Recv" ,4)==0 )	rc = term_Recv(pt, preply);
	else if ( strncmp(cmd, "Echo", 4)==0 )	rc = term_Echo(pt ) ? 1 : 0;
	else if ( strncmp(cmd, "Timeout",7)==0 )pt->iTimeOut = atoi( cmd+8);
	else if ( strncmp(cmd, "Prompt",6)==0 ) {
		if ( cmd[6]==' ' ) {
			strncpy(pt->sPrompt, cmd+7, 31);
			pt->sPrompt[31] = 0;
			pt->iPrompt = url_decode(pt->sPrompt);
		}
		else {
			term_Learn_Prompt(pt);
		}
		if ( preply!=NULL ) *preply = pt->sPrompt;
		rc = pt->iPrompt;
	}
	else if ( strncmp(cmd, "Tftpd",5)==0 )	tftp_Svr( cmd+5);
	else if ( strncmp(cmd, "Ftpd", 4)==0 ) 	ftp_Svr( cmd+4);
	else if ( strncmp(cmd, "tun",  3)==0 ) 	rc = term_Tun(pt, cmd+3, preply);
	else if ( strncmp(cmd, "scp ", 4)==0 ) 	rc = term_Scp(pt, cmd+4, preply);
	else if ( strncmp(cmd, "xmodem ", 7)==0 ) rc = term_xmodem(pt, cmd+7);
	else if ( strncmp(cmd, "Wait ", 5)==0 ) Sleep(atoi(cmd+5)*1000);
	else if ( strncmp(cmd, "Waitfor ", 8)==0) {
		for ( int i=pt->iTimeOut; i>0; i-- ) {
			pt->buff[pt->cursor_x] = 0;
			if ( strstr(pt->tl1text, cmd+8)!=NULL ) {
				if ( preply!=NULL ) *preply = pt->tl1text;
				rc = pt->buff+pt->cursor_x-pt->tl1text;
				break;
			}
			Sleep(1000);
		}
	}
	else {
		if ( host_Status(pt->host)==IDLE )
			term_Print(pt, "\033[33m%s\n", cmd); 
		term_Mark_Prompt(pt);
		host_Open(pt->host, cmd);
		if ( preply!=NULL ) {
			term_Waitfor_Prompt(pt);	//added for scripting
			*preply = pt->tl1text;
			rc =  pt->tl1len;
		}
	}
	return rc;
}
void buff_clear(TERM *pt, int offset, int len)
{
	memset(pt->buff+offset, ' ', len);
	memset(pt->attr+offset,   7, len);
}
void screen_clear(TERM *pt, int m0)
{
	/*mostly [2J used after [?1049h to clear screen
	  and when screen size changed during vi or raspi-config
	  flashwave TL1 use it without [?1049h for splash screen 
	  freeBSD use it without [?1049h* for top and vi*/
	int lines = pt->size_y;
	if ( m0==2 ) pt->screen_y = pt->cursor_y;
	if ( m0==1 ) {
		lines = pt->cursor_y-pt->screen_y;
		buff_clear(pt, pt->line[pt->cursor_y], 
						pt->cursor_x-pt->line[pt->cursor_y]+1);
		pt->cursor_y = pt->screen_y;
	}
	if ( m0==0 ) {
		buff_clear(pt, pt->cursor_x, 
						pt->line[pt->cursor_y+1]-pt->cursor_x);
		lines = pt->size_y+pt->screen_y-pt->cursor_y;
	}
	pt->cursor_x = pt->line[pt->cursor_y];
	int cy = pt->cursor_y;
	for ( int i=0; i<lines; i++ ) 
	{
		buff_clear(pt, pt->cursor_x, pt->size_x);
		pt->cursor_x += pt->size_x;
		term_nextLine(pt);
	}
	pt->cursor_y = cy;
	if ( m0==2 || m0==0 ) pt->screen_y--; 
	pt->cursor_x = pt->line[pt->cursor_y];
}
void check_cursor_y(TERM *pt)
{
	if (pt->cursor_y < pt->screen_y ) 
		pt->cursor_y = pt->screen_y;
	if (pt->cursor_y > pt->screen_y+pt->size_y-1 ) 
		pt->cursor_y = pt->screen_y+pt->size_y-1;
	if (pt->bOriginMode ) {
		if (pt->cursor_y< pt->screen_y+pt->roll_top )
			pt->cursor_y = pt->screen_y+pt->roll_top;
		if (pt->cursor_y> pt->screen_y+pt->roll_bot )
			pt->cursor_y = pt->screen_y+pt->roll_bot;

	}
}
const unsigned char *vt100_Escape(TERM *pt, const unsigned char *sz, int cnt)
{
	const unsigned char *zz = sz+cnt;

	pt->bEscape = TRUE;
	while ( sz<zz && pt->bEscape ) 
	{
		if ( *sz>31 ) {
			pt->escape_code[pt->escape_idx++] = *sz++;
		}
		else {	//handle control character in escape sequence
			switch ( *sz++ ) {
				case 0x08:	//BS
					if ( isUTF8c(pt->buff[pt->cursor_x--]) )
						//utf8 continuation byte
						while ( isUTF8c(pt->buff[pt->cursor_x]) ) 
							pt->cursor_x--;
					break;
				case 0x0b:{	//VT
					int x = pt->cursor_x - pt->line[pt->cursor_y];
					pt->cursor_x = pt->line[++pt->cursor_y] + x;
					break;
				}
				case 0x0d:	//CR
					pt->cursor_x = pt->line[pt->cursor_y];
			}
		}
		switch(pt->escape_code[0] ) 
		{
		case '[': if ( isalpha(pt->escape_code[pt->escape_idx-1])
						|| pt->escape_code[pt->escape_idx-1]=='@'
						|| pt->escape_code[pt->escape_idx-1]=='`' ) 
						{
			pt->bEscape = FALSE;
			int m0=0;			//ESC[J == ESC[0J	ESC[K==ESC[0K
			int n0=1;			//ESC[A == ESC[1A
			int n1=1; 			//n1;n0 used by ESC[Ps;PtH and ESC[Ps;Ptr
			if ( isdigit(pt->escape_code[1]) ) {
				m0 = n0 = atoi(pt->escape_code+1);
				if ( n0==0 ) n0 = 1;//ESC[0A == ESC[1A
			}
			char *p = strchr(pt->escape_code, ';');
			if ( p != NULL ) {
				n1 = n0 ; 
				n0 = atoi(p+1);
				if ( n0==0 ) n0=1;	//ESC[0;0f == ESC[1;1f
			}
			int x;
			switch (pt->escape_code[pt->escape_idx-1] ) 
			{
			case 'A'://cursor up n0 lines
				x = pt->cursor_x - pt->line[pt->cursor_y];
				pt->cursor_y -= n0;
				check_cursor_y(pt);
				pt->cursor_x = pt->line[pt->cursor_y]+x;
				break;
			case 'd'://line position absolute
				x = pt->cursor_x-pt->line[pt->cursor_y];
				pt->cursor_y = pt->screen_y+n0-1;
				check_cursor_y(pt);
				pt->cursor_x = pt->line[pt->cursor_y]+x;
				break;
			case 'e'://line position relative
			case 'B'://cursor down n0 lines
				x = pt->cursor_x - pt->line[pt->cursor_y];
				pt->cursor_y += n0;
				check_cursor_y(pt);
				pt->cursor_x = pt->line[pt->cursor_y]+x;
				break;
			case '`': //character position absolute
			case 'G': //cursor to n0th position from left
				pt->cursor_x = pt->line[pt->cursor_y];
				//fall through
			case 'a'://character position relative
			case 'C'://cursor right n0 characters
				while ( n0-->0 && 
					pt->cursor_x<pt->line[pt->cursor_y]+pt->size_x-1 )
				{
					if ( isUTF8c(pt->buff[++pt->cursor_x]) )
						while ( isUTF8c(pt->buff[++pt->cursor_x]));
				}
				break;
			case 'D'://cursor left n0 characters
				while ( n0-->0 && pt->cursor_x>pt->line[pt->cursor_y]) {
					if ( isUTF8c(pt->buff[--pt->cursor_x]) )
						while ( isUTF8c(pt->buff[--pt->cursor_x]));
				}
				break;
			case 'E': //cursor to begining of next line n0 times
				pt->cursor_y += n0;
				check_cursor_y(pt);
				pt->cursor_x = pt->line[pt->cursor_y];
				break;
			case 'F': //cursor to begining of previous line n0 times
				pt->cursor_y -= n0;
				check_cursor_y(pt);
				pt->cursor_x = pt->line[pt->cursor_y];
				break;
			case 'f': //horizontal and vertical position forced
				for ( int i=pt->cursor_y+1; i<pt->screen_y+n1; i++ )
					if ( i<MAXLINES && pt->line[i]<pt->cursor_x ) 
						pt->line[i]=pt->cursor_x;
			case 'H': //cursor to line n1, postion n0
				if ( !pt->bAlterScreen && n1>pt->size_y ) {
					pt->cursor_y = (pt->screen_y++) + pt->size_y;
				}
				else {
					pt->cursor_y = pt->screen_y+n1-1;
					if (pt->bOriginMode ) pt->cursor_y+=pt->roll_top;
					check_cursor_y(pt);
				}
				pt->cursor_x = pt->line[pt->cursor_y];
				while ( --n0>0 ) {
					pt->cursor_x++;
					while ( isUTF8c(pt->buff[pt->cursor_x]) ) pt->cursor_x++;
				}
				break;
			case 'J': 	//[J kill till end, 1J begining, 2J entire screen
				if ( isdigit(pt->escape_code[1]) ) {
					screen_clear(pt, m0);
				}
				else {
					pt->line[pt->cursor_y+1] = pt->cursor_x;
					for ( int i=pt->cursor_y+2; 
							  i<=pt->screen_y+pt->size_y+1; i++ )
						if ( i<MAXLINES ) pt->line[i] = 0;
				}
				break;
			case 'K': {	//[K kill till end, 1K begining, 2K entire line
				int i = pt->line[pt->cursor_y];		//setup for m0==2
				int j = pt->line[pt->cursor_y+1];
				if ( m0==0 ) i = pt->cursor_x;		//change start if m0==0
				if ( m0==1 ) j = pt->cursor_x+1;	//change stop if m0==1
				if ( j>i ) buff_clear(pt, i, j-i);
				}
				break;
			case 'L'://insert lines
				if ( n0 > pt->screen_y+pt->roll_bot-pt->cursor_y ) 
					n0 = pt->screen_y+pt->roll_bot-pt->cursor_y+1;
				else {
					for ( int i=pt->screen_y+pt->roll_bot;
								i>=pt->cursor_y+n0; i--) {
						memcpy(pt->buff+pt->line[i],
								pt->buff+pt->line[i-n0], pt->size_x);
						memcpy(pt->attr+pt->line[i],
								pt->attr+pt->line[i-n0], pt->size_x);
					}
				}
				pt->cursor_x = pt->line[pt->cursor_y];
				buff_clear(pt, pt->cursor_x, pt->size_x*n0);
				break;
			case 'M'://delete lines
				if ( n0 > pt->screen_y+pt->roll_bot-pt->cursor_y ) 
					n0 = pt->screen_y+pt->roll_bot-pt->cursor_y+1;
				else {
					for ( int i=pt->cursor_y; 
								i<=pt->screen_y+pt->roll_bot-n0; i++ ) {
						memcpy(pt->buff+pt->line[i],
								pt->buff+pt->line[i+n0], pt->size_x);
						memcpy(pt->attr+pt->line[i],
								pt->attr+pt->line[i+n0], pt->size_x);
					}
				}
				pt->cursor_x = pt->line[pt->cursor_y];
				buff_clear(pt, pt->line[pt->screen_y+pt->roll_bot-n0+1],
														pt->size_x*n0);
				break;
			case 'P'://delete n0 characters, fill with space to the right margin
				for (int i=pt->cursor_x;i<pt->line[pt->cursor_y+1]-n0;i++){
					pt->buff[i]=pt->buff[i+n0];
					pt->attr[i]=pt->attr[i+n0];
				}
				buff_clear(pt, pt->line[pt->cursor_y+1]-n0, n0);
				if ( !pt->bAlterScreen ) {
					pt->line[pt->cursor_y+1]-=n0;
					if ( pt->line[pt->cursor_y+1]<pt->line[pt->cursor_y] )
						pt->line[pt->cursor_y+1] =pt->line[pt->cursor_y];
				}
				break;
			case '@'://insert n0 spaces
				for (int i=pt->line[pt->cursor_y+1]-n0-1;i>=pt->cursor_x; i--){
					pt->buff[i+n0]=pt->buff[i];
					pt->attr[i+n0]=pt->attr[i];
				}
				if ( !pt->bAlterScreen ){
					pt->line[pt->cursor_y+1]+=n0;
					if ( pt->line[pt->cursor_y+1]>pt->line[pt->cursor_y]
															+pt->size_x )
						pt->line[pt->cursor_y+1] =pt->line[pt->cursor_y]
															+pt->size_x;
				}
				//fall through;
			case 'X': //erase n0 characters
				buff_clear(pt, pt->cursor_x, n0);
				break;
			case 'S': // scroll up n0 lines
				for ( int i=pt->roll_top; i<=pt->roll_bot-n0; i++ ) {
					memcpy(pt->buff+pt->line[pt->screen_y+i],
							pt->buff+pt->line[pt->screen_y+i+n0], 
							pt->size_x);
					memcpy(pt->attr+pt->line[pt->screen_y+i],
							pt->attr+pt->line[pt->screen_y+i+n0], 
							pt->size_x);
				}
				buff_clear(pt, pt->line[pt->screen_y+pt->roll_bot-n0+1], 
															n0*pt->size_x);
				break;
			case 'T': // scroll down n0 lines
				for ( int i=pt->roll_bot; i>=pt->roll_top+n0; i-- ) {
					memcpy(pt->buff+pt->line[pt->screen_y+i],
							pt->buff+pt->line[pt->screen_y+i-n0], 
							pt->size_x);
					memcpy(pt->attr+pt->line[pt->screen_y+i],
							pt->attr+pt->line[pt->screen_y+i-n0], 
							pt->size_x);
				}
				buff_clear(pt, pt->line[pt->screen_y+pt->roll_top], 
														n0*pt->size_x);
				break;
			case 'I': //cursor forward n0 tab stops
				break;
			case 'Z': //cursor backward n0 tab stops
				break;
			case 'c'://Send Device Attributes
				host_Send(pt->host, "\033[?1;0c", 7);	//vt100 without options
				break;
			case 'g': //clear tabstop
				if ( m0==0 ) {	//clear current tabstop
					int l = pt->cursor_x - pt->line[pt->cursor_y];
					pt->tabstops[l] = 0;
				}
				if ( m0==3 ) {	//clear all tabstops
					memset(pt->tabstops, 0, 256);
				}
				break;
			case 'h':
				if (pt->escape_code[1]=='4' )  pt->bInsert = TRUE;
				if (pt->escape_code[1]=='?' ) {
					n0 = atoi(pt->escape_code+2);
					if ( n0==1 ) pt->bAppCursor = TRUE;
					if ( n0==3 ) { 
						if (pt->size_x!=132 || pt->size_y!=25 ) {
							pt->size_x = 132;   pt->size_y = 25;
							wnd_Size();
						}
						screen_clear(pt, 2);
					}
					if ( n0==6 ) pt->bOriginMode = TRUE;
					if ( n0==7 ) pt->bWraparound = TRUE;
					if ( n0==25 ) pt->bCursor = TRUE;
					if ( n0==2004 ) pt->bBracket = TRUE;
					if ( n0==1049 ) { 	//?1049h alternate screen,
						pt->bAlterScreen = TRUE;
						pt->save_edit = tiny_Edit(FALSE);
						screen_clear(pt, 2);
					}
				}
				break;
			case 'l':
				if (pt->escape_code[1]=='4' ) pt->bInsert = FALSE;
				if (pt->escape_code[1]=='?' ) {
					n0 = atoi(pt->escape_code+2);
					if ( n0==1 ) pt->bAppCursor = FALSE;
					if ( n0==3 ) {
						if (pt->size_x!=80 || pt->size_y!=25 ) {
							pt->size_x = 80;   pt->size_y = 25;
							wnd_Size();
						}
						screen_clear(pt, 2);
					}
					if ( n0==6 ) pt->bOriginMode = FALSE;
					if ( n0==7 ) pt->bWraparound = FALSE;
					if ( n0==25 ) pt->bCursor = FALSE;
					if ( n0==2004 ) pt->bBracket = FALSE;
					if ( n0==1049 ) { 	//?1049l exit alternate screen,
						pt->bAlterScreen = FALSE;
						if (pt->save_edit ) tiny_Edit(TRUE);
						pt->cursor_y = pt->screen_y;
						pt->cursor_x = pt->line[pt->cursor_y];
						for ( int i=1; i<=pt->size_y+1; i++ )
							pt->line[pt->cursor_y+i] = 0;
						pt->screen_y = pt->cursor_y-pt->size_y+1;
						if (pt->screen_y<0 ) pt->screen_y = 0;
					}
				}
				break;
			case 'm': {
					char *p = pt->escape_code;
					while ( p!=NULL ) {
						m0 = atoi(++p);
						switch ( m0/10 ) {
						case 0:	if ( m0==0 ) pt->c_attr = 7;	//normal
								if ( m0==1 ) pt->c_attr|= 0x08; //bright
								if ( m0==7 ) pt->c_attr = 0x70; //negative
								break;
						case 2: pt->c_attr = 7;					//normal
								break;
						case 3: if ( m0==39 ) m0 = 37;	//default foreground
								pt->c_attr = (pt->c_attr&0xf8)+m0%10; 
								break;
						case 4: if ( m0==49 ) m0 = 0;	//default background
								pt->c_attr = (pt->c_attr&0x0f)+((m0%10)<<4); 
								break;
						case 9: pt->c_attr = (pt->c_attr&0xf0)+m0%10+8; 
								break;
						case 10:pt->c_attr = (pt->c_attr&0x0f)+((m0%10+8)<<4); 
								break;
						}
						p = strchr(p, ';');
					}
				}
				break;
			case 'r':
				if ( n1==1 && n0==1 ) n0 = pt->size_y;	//ESC[r
				pt->roll_top=n1-1; pt->roll_bot=n0-1;
				pt->cursor_y = pt->screen_y;
				if (pt->bOriginMode ) pt->cursor_y+=pt->roll_top;
				pt->cursor_x = pt->line[pt->cursor_y];
				break;
			case 's': //save cursor
				pt->save_x = pt->cursor_x-pt->line[pt->cursor_y];
				pt->save_y = pt->cursor_y-pt->screen_y;
				pt->save_attr = pt->c_attr;
				break;
			case 'u': //restore cursor
				pt->cursor_y = pt->save_y+pt->screen_y;
				pt->cursor_x = pt->line[pt->cursor_y]+pt->save_x;
				pt->c_attr = pt->save_attr;
				break;
				}
			}
			break;
		case '7'://save cursor
			pt->save_x = pt->cursor_x-pt->line[pt->cursor_y];
			pt->save_y = pt->cursor_y-pt->screen_y;
			pt->save_attr = pt->c_attr;
			pt->bEscape = FALSE;
			break;
		case '8': //restore cursor
			pt->cursor_y = pt->save_y+pt->screen_y;
			pt->cursor_x = pt->line[pt->cursor_y]+pt->save_x;
			pt->c_attr = pt->save_attr;
			pt->bEscape = FALSE;
			break; 
		case 'F'://cursor to lower left corner
			pt->cursor_y = pt->screen_y+pt->size_y-1;
			pt->cursor_x = pt->line[pt->cursor_y];
			pt->bEscape = FALSE;
			break;
		case 'E'://NEL, move to next line
			pt->cursor_x = pt->line[++pt->cursor_y];
			pt->bEscape = FALSE;
			break;
		case 'D'://IND, move/scroll up one line 
			if (pt->cursor_y < pt->roll_bot+pt->screen_y ) {
				int x = pt->cursor_x - pt->line[pt->cursor_y];
				pt->cursor_x = pt->line[++pt->cursor_y] + x;
			}
			else {
				int len = pt->line[pt->screen_y+pt->roll_bot+1]
						 -pt->line[pt->screen_y+pt->roll_top+1];
				int x = pt->cursor_x-pt->line[pt->cursor_y];
				memcpy( pt->buff+pt->line[pt->screen_y+pt->roll_top],
						pt->buff+pt->line[pt->screen_y+pt->roll_top+1], len);
				memcpy( pt->attr+pt->line[pt->screen_y+pt->roll_top],
						pt->attr+pt->line[pt->screen_y+pt->roll_top+1], len);
				len = pt->line[pt->screen_y+pt->roll_top+1]
					 -pt->line[pt->screen_y+pt->roll_top];
				for ( int i=pt->roll_top+1; i<=pt->roll_bot; i++ ) 
					pt->line[pt->screen_y+i] = pt->line[pt->screen_y+i+1]-len;
				buff_clear( pt, pt->line[pt->screen_y+pt->roll_bot],
							pt->line[pt->screen_y+pt->roll_bot+1]-
							pt->line[pt->screen_y+pt->roll_bot]);
				pt->cursor_x = pt->line[pt->cursor_y]+x;
			}
			pt->bEscape = FALSE;
			break;
		case 'M'://RI, move/scroll down one line
			if (pt->cursor_y > pt->roll_top+pt->screen_y ) {
				int x = pt->cursor_x - pt->line[pt->cursor_y];
				pt->cursor_x = pt->line[--pt->cursor_y] + x;
			}
			else {
				for ( int i=pt->roll_bot; i>pt->roll_top; i-- ) {
					memcpy(pt->buff+pt->line[pt->screen_y+i],
							pt->buff+pt->line[pt->screen_y+i-1],
							pt->size_x);
					memcpy(pt->attr+pt->line[pt->screen_y+i],
							pt->attr+pt->line[pt->screen_y+i-1],
							pt->size_x);
				}
				buff_clear(pt, pt->line[pt->screen_y+pt->roll_top],
							pt->size_x);
			}
			pt->bEscape = FALSE;
			break;
		case 'H':
			pt->tabstops[pt->cursor_x-pt->line[pt->cursor_y]]=1;
			pt->bEscape = FALSE;
			break;
		case ']':
			if (pt->escape_code[pt->escape_idx-1]==';' ) {
				if (pt->escape_code[1]=='0' ) {
					pt->bTitle = TRUE;
					pt->title_idx = 0;
				}
				pt->bEscape = FALSE;
			}
			break;
		case '(':
		case ')':
			if (pt->escape_code[1]=='B'||pt->escape_code[1]=='0' ) {
				pt->bGraphic = (pt->escape_code[1]=='0');
				pt->bEscape = FALSE;
			}
			break;
		case '#':	//#8 alignment test, fill screen with 'E'
			if (pt->escape_idx==2 ) {
				if (pt->escape_code[1]=='8' ) 
					memset(pt->buff+pt->line[pt->screen_y], 'E', 
											pt->size_x*pt->size_y);
				pt->bEscape = FALSE;
			}
			break;
		default: pt->bEscape = FALSE;
		}
		if (pt->escape_idx==31 ) pt->bEscape = FALSE;
		if ( !pt->bEscape ) 
		{
			pt->escape_idx=0; 
			memset(pt->escape_code, 0, 32);
		}
	}
	return sz;
}
void term_Parse_XML(TERM *pt, const char *msg, int len)
{
	const char *p=msg, *q;
	const char spaces[256]="\r\n                                               \
                                                                              ";
	if ( strncmp(msg, "<?xml ", 6)==0 ) {
		pt->xmlIndent = 0;
		pt->xmlPreviousIsOpen = TRUE;
	}
	while ( *p!=0 && *p!='<' ) p++;
	if ( p>msg ) term_Parse(pt, msg, p-msg);
	while ( *p!=0 && p<msg+len ) {
		while (*p==0x0d || *p==0x0a || *p=='\t' || *p==' ' ) p++;
		if ( *p=='<' ) {//tag
			if ( p[1]=='/' ) {
				if ( !pt->xmlPreviousIsOpen ) {
					pt->xmlIndent -= 2;
					term_Parse(pt, spaces, pt->xmlIndent);
				}
				pt->xmlPreviousIsOpen = FALSE;
			}
			else {
				if (pt->xmlPreviousIsOpen ) pt->xmlIndent+=2;
				term_Parse(pt, spaces, pt->xmlIndent);
				pt->xmlPreviousIsOpen = TRUE;
			}
			term_Parse(pt, "\033[32m", 5);
			q = strchr(p, '>');
			if ( q==NULL ) q = p+strlen(p);
			char *r = strchr(p, ' ');
			if ( r!=NULL && r<q ) {
				term_Parse(pt, p, r-p);
				term_Parse(pt, "\033[34m",5);
				term_Parse(pt, r, q-r);
			}
			else
				term_Parse(pt, p, q-p);
			term_Parse(pt, "\033[32m>", 6);
			p = q;
			if ( *q=='>' ) {
				p++;
				if ( q[-1]=='/' ) pt->xmlPreviousIsOpen = FALSE;
			}
		}
		else {//data
			term_Parse(pt, "\033[33m", 5);
			q = strchr(p, '<');
			if ( q==NULL ) q = p+strlen(p);
			term_Parse(pt, p, q-p);
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
unsigned char TERMTYPE[]={//vt100
	0xff, 0xfa, 0x18, 0x00, 0x76, 0x74, 0x31, 0x30, 0x30, 0xff, 0xf0
};
const unsigned char *telnet_Options(TERM *pt, const unsigned char *p, int cnt)
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
			term_Send(pt, (char *)negoreq, 3);
			p+=3;
			break;
		case TNO_WILL:
			negoreq[1]=TNO_DONT; negoreq[2]=p[2];
			if ( p[2]==TNO_ECHO || p[2]==TNO_AHEAD ) {
				negoreq[1]=TNO_DO;
				if ( p[2]==TNO_ECHO ) pt->bEcho = FALSE;
			} 
			term_Send(pt, (char*)negoreq, 3);
			p+=3;
			break;
		case TNO_SUB:
			negoreq[1]=TNO_SUB; negoreq[2]=p[2];
			if ( p[2]==TNO_TERMTYPE ) {
				term_Send(pt, (char *)TERMTYPE, sizeof(TERMTYPE));
			}
			if ( p[2]==TNO_NEWENV ) {
				term_Send(pt, (char*)negoreq, 6);
			}
			while ( *p!=0xff && p<q ) p++;
			break;
		case TNO_SUBEND:
			p+=2;
		}
	} 
	return p;
}