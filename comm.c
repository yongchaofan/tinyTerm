//
// "$Id: comm.c 48134 2018-09-15 21:05:10 $"
//
// tinyTerm -- A minimal serail/telnet/ssh/sftp terminal emulator
//
// comm.c is the host communication implementation 
// serial communication is based on WIN32 API.
// telnet communication is based on posix socket API
// ssh/scp/sftp communication is based on libssh2
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
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <libssh2.h>
#include <libssh2_sftp.h>
#include <time.h>
#include <fcntl.h>
#include <dirent.h>
#include <fnmatch.h>
#include "tiny.h"

int comm_type = 0;
int comm_status=CONN_IDLE;

static HANDLE hExitEvent, hWrite;	//for serial reader
static HANDLE mtx;					//libssh2 reading/writing mutex
static SOCKET sock;					//for telnet and ssh2 connections
static LIBSSH2_SESSION *sshSession;
static LIBSSH2_CHANNEL *sshChannel;
static LIBSSH2_SFTP *sftpSession;

static DWORD dwReaderId = 0;		//reader thread id
DWORD WINAPI serial( void *pv );
DWORD WINAPI telnet( void *pv );
DWORD WINAPI ssh( void *pv );
DWORD WINAPI sftp( void *pv );
DWORD WINAPI stdio( void *pv);
void stdio_Close( void );

extern BOOL bEcho;
unsigned char * telnet_Options( unsigned char *buf );

void print( const char *fmt, ... ) 
{
	char buff[4096];
	va_list args;
	va_start(args, (char *)fmt);
	int len = vsnprintf(buff, 4096, (char *)fmt, args);
	va_end(args);
	term_Parse(buff, len);
	term_Parse("\033[37m",5);
}

int comm_Init( void )
{
	WSADATA wsaData;
	WSAStartup(MAKEWORD(2, 2), &wsaData);
	libssh2_init (0);
	mtx = CreateMutex( NULL, FALSE, NULL ); 
	return http_Svr("127.0.0.1");
}
int ssh_wait_socket() 
{
	fd_set fds, *rfd=NULL, *wfd=NULL;
	FD_ZERO(&fds); FD_SET(sock, &fds);
	int dir = libssh2_session_block_directions(sshSession);
	if ( dir==0 ) return 1;
	if ( dir & LIBSSH2_SESSION_BLOCK_INBOUND ) rfd = &fds;
	if ( dir & LIBSSH2_SESSION_BLOCK_OUTBOUND ) wfd = &fds;
	return select(sock+1, rfd, wfd, NULL, NULL );
}
void comm_Open( char *port )
{
static char Port[256];

	LPTHREAD_START_ROUTINE reader = serial;
	if ( strncmp(port, "telnet", 6)==0 ) { port+=7; reader = telnet; }
	if ( strncmp(port, "ssh", 3)==0 )    { port+=4; reader = ssh; }
	if ( strncmp(port, "sftp", 4)==0 )   { port+=5; reader = sftp; }
	if ( *port=='!' ) { port++; reader = stdio; }
	if ( dwReaderId==0 ) {
		strncpy(Port, port, 255);
		comm_status=CONN_CONNECTING;
		tiny_Connecting();
		CreateThread( NULL,0,reader, Port,0,&dwReaderId );
	}
}
static char *keys;
static int cursor, bReturn=TRUE, bPassword;
void comm_Send( char *buf, int len )
{
	if ( bReturn && comm_status==CONN_CONNECTED ) {
		DWORD dwWrite;
		switch ( comm_type ) {
		case STDIO: 
		case SERIAL:WriteFile(hWrite, buf, len, &dwWrite, NULL); break;
		case TELNET: send( sock, buf, len, 0); break;
		case SSH: if ( sshChannel!=NULL ) {
				int total=0, cch=0;
				while ( total<len ) {
					if ( WaitForSingleObject(mtx, INFINITE)==WAIT_OBJECT_0 ) {
						cch=libssh2_channel_write( sshChannel, buf, len); 
						ReleaseMutex(mtx);
					}
					else break;
					if ( cch>=0 )
						total += cch;
					else
						if ( cch==LIBSSH2_ERROR_EAGAIN ) {
							ssh_wait_socket();
							continue;
						}
						else break;
				}
			}
		}
	}
	else if ( !bReturn ) {
		switch( *buf ) {
		case '\015': keys[cursor]=0; bReturn=TRUE; break;
		case '\010':
		case '\177': if ( --cursor<0 ) cursor=0; 
					  else
						if ( !bPassword ) term_Parse("\010 \010", 3);
					  break;
		default: keys[cursor++]=*buf;
				  if ( cursor>255 ) cursor=255;
				  if ( !bPassword ) term_Parse(buf, 1);
	    }
	}
}
int comm_Gets(char *buf, BOOL bPass)
{
	bReturn=FALSE; bPassword=bPass;
	keys=buf; keys[0]=0;
	cursor=0; 
	tiny_Focus();
	int old_cursor=0;
	for ( int i=0; i<600&&bReturn==FALSE; i++ ) { 
		if ( cursor>old_cursor ) { old_cursor=cursor; i=0; }
		Sleep(100);
	}
	bReturn = TRUE;
	return cursor;
}
void comm_Size( int w, int h )
{
	if ( comm_type==SSH ) 
		libssh2_channel_request_pty_size( sshChannel, w, h);
}
void comm_Close( void )
{
	bReturn = TRUE;
	switch ( comm_type ) {
	case STDIO:  stdio_Close(); break;
	case SERIAL: SetEvent( hExitEvent ); break;
	case SSH:    libssh2_channel_send_eof( sshChannel );
	case TELNET: closesocket( sock ); break;
	}
}
void comm_Destory( void )
{
	if ( comm_type!=0 ) comm_Close( );
	libssh2_exit();
	http_Svr("127.0.0.1");
}

/***************************Serial*****************************/
DWORD WINAPI serial(void *pv)
{
	BOOL bConnected = TRUE;
	char port[256] = "\\\\.\\";
	strcat( port, (char *)pv );

	char *p = strchr( port, ':' );
	if ( p!=NULL ) *p++ = 0;
	if ( p==NULL || *p==0 ) p = "9600,n,8,1";

	HANDLE hCommPort = CreateFile( port, GENERIC_READ|GENERIC_WRITE, 0, NULL,
														OPEN_EXISTING, 0, NULL);
	bConnected = (hCommPort!=INVALID_HANDLE_VALUE);
	if ( !bConnected ) 
		term_Disp("Couldn't open COM port\r\n");
	else {									//timeout and buffer settings
		COMMTIMEOUTS timeouts ={10,			//ReadIntervalTimeout = 10;
								 0,			//ReadTotalTimeoutMultiplier = 0;
								 1,			//ReadTotalTimeoutConstant = 1;
								 0,			//WriteTotalTimeoutMultiplier = 0;
								 0 };		//WriteTotalTimeoutConstant = 0;
		   	
		bConnected = (SetCommTimeouts(hCommPort,&timeouts)!=0); 
		if ( !bConnected ) term_Disp("couldn't set comm timeout\r\n");
		SetupComm( hCommPort, 4096, 1024 );	//comm buffer sizes
	}
	if ( bConnected ) {
		DCB dcb;							// comm port settings
		memset(&dcb, 0, sizeof(dcb));
		dcb.DCBlength = sizeof(dcb);
		BuildCommDCBA(p, &dcb);
		bConnected = (SetCommState(hCommPort, &dcb)!=0);
		if ( !bConnected ) 
			term_Disp("Invalid comm port settings\r\n" );
	}
	if ( bConnected ) {
		hWrite = hCommPort;
		comm_status=CONN_CONNECTED;
		comm_type = SERIAL;
		tiny_Title( (char *)pv );
		hExitEvent = CreateEvent( NULL, TRUE, FALSE, "COM exit" );
		while ( WaitForSingleObject( hExitEvent, 0 ) == WAIT_TIMEOUT ) { 
			char buf[256];
			DWORD dwCCH;
			if ( ReadFile(hCommPort, buf, 255, &dwCCH, NULL) ) {
				if ( dwCCH > 0 ) {
					buf[dwCCH] = 0;
					term_Parse( buf, dwCCH );
				}
			}
			else
				if ( !ClearCommError(hCommPort, NULL, NULL ) ) break;
		}
		CloseHandle(hExitEvent);
		tiny_Title("");
		comm_type = 0;
	}
	CloseHandle(hCommPort);
	comm_status=CONN_IDLE;
	dwReaderId = 0;
	return 1;
}

/***************Telnet*******************************/
int tcp(const char *host, short port)
{
    struct addrinfo *ainfo;    
    if ( getaddrinfo(host, NULL, NULL, &ainfo)!=0 ) return -1;
    sock = socket(ainfo->ai_family, SOCK_STREAM, 0);
    ((struct sockaddr_in *)(ainfo->ai_addr))->sin_port = htons(port);
    int rc = connect(sock, ainfo->ai_addr, ainfo->ai_addrlen);
    freeaddrinfo(ainfo);
	if ( rc==SOCKET_ERROR ) term_Disp( "connection failure!\r\n" );
	return rc;
}
DWORD WINAPI telnet( void *pv )
{
	short nPort = 23;
	char *port = (char *)pv;
	char *p=strchr(port, ':');
	if ( p!=NULL ){ *p++=0; nPort=atoi(p); }
	if ( tcp(port, nPort)==-1 ) goto socket_close;

	int cch;
	char buf[1536];
	comm_status=CONN_CONNECTED;
	comm_type=TELNET;
	tiny_Title(port);
	while ( (cch=recv(sock, buf, 1500, 0)) > 0 ) {
		for ( char *p=buf; p<buf+cch; p++ ) {
			while ( *p==-1 && p<buf+cch ) {
				char *p0 = (char *)telnet_Options((unsigned char *)p);
				memcpy(p, p0, buf+cch-p0);
				cch -= p0-p;	//cch could become 0 after this
			}
		}
		buf[cch] = 0;
		if ( cch>0 ) term_Parse( buf, cch );
	}
	tiny_Title("");
	comm_type = 0;

socket_close:
	closesocket(sock);
	comm_status=CONN_IDLE;
	dwReaderId = 0;

	return 1;
}
#define TNO_IAC		0xff
#define TNO_DONT	0xfe
#define TNO_DO		0xfd
#define TNO_WONT	0xfc
#define TNO_WILL	0xfb
#define TNO_SUB		0xfa
#define TNO_ECHO	0x01
#define TNO_AHEAD	0x03
#define TNO_WNDSIZE 0x1f
#define TNO_TERMTYPE 0x18
#define TNO_NEWENV	0x27
UCHAR NEGOBEG[]={0xff, 0xfb, 0x03, 0xff, 0xfd, 0x03, 0xff, 0xfd, 0x01};
UCHAR TERMTYPE[]={0xff, 0xfa, 0x18, 0x00, 0x76, 0x74, 0x31, 0x30, 0x30, 0xff, 0xf0};
unsigned char * telnet_Options( unsigned char *buf )
{
	UCHAR negoreq[]={0xff,0,0,0, 0xff, 0xf0};
	unsigned char *p = buf;
		switch ( *p++ ) {
		case TNO_DO:
			if ( *p==TNO_TERMTYPE || *p==TNO_NEWENV || *p==TNO_ECHO ) {
				negoreq[1]=TNO_WILL; negoreq[2]=*p;
				term_Send((char*)negoreq, 3);
				if ( *p==TNO_ECHO ) bEcho = TRUE;
			}
			else if ( *p!=TNO_AHEAD ) {
				negoreq[1]=TNO_WONT; negoreq[2]=*p;
				term_Send((char*)negoreq, 3);
			}
			break;
		case TNO_SUB:
			if ( *p==TNO_TERMTYPE ) {
				term_Send((char*)TERMTYPE, sizeof(TERMTYPE));
			}
			if ( *p==TNO_NEWENV ) {
				negoreq[1]=TNO_SUB; negoreq[2]=*p;
				term_Send((char*)negoreq, 6);
			}
			p += 3;
			break;
		case TNO_WILL: 
			if ( *p==TNO_ECHO ) bEcho = FALSE;
			negoreq[1]=TNO_DO; negoreq[2]=*p;
			term_Send((char*)negoreq, 3);
			break;
		case TNO_WONT: 
			negoreq[1]=TNO_DONT; negoreq[2]=*p;
			term_Send((char*)negoreq, 3);
		   break;
		case TNO_DONT:
			break;
		}
	return p+1; 
}

/***************************STDIO*******************************/
static HANDLE Std_IN_Rd = NULL;
static HANDLE Std_IN_Wr = NULL;
static HANDLE Std_OUT_Rd = NULL;
static HANDLE Std_OUT_Wr = NULL;
static PROCESS_INFORMATION piStd; 
DWORD WINAPI stdio( void *pv)
{
	memset( &piStd, 0, sizeof(PROCESS_INFORMATION) );	
	//Set up PROCESS_INFORMATION 

	SECURITY_ATTRIBUTES saAttr; 
	saAttr.nLength = sizeof(SECURITY_ATTRIBUTES); 
	saAttr.bInheritHandle = TRUE;			//Set the bInheritHandle flag
	saAttr.lpSecurityDescriptor = NULL; 	//so pipe handles are inherited

	CreatePipe(&Std_OUT_Rd, &Std_OUT_Wr, &saAttr, 0);//pipe for child's STDOUT 
	SetHandleInformation(Std_OUT_Rd, HANDLE_FLAG_INHERIT, 0);
	// Ensure the read handle to the pipe for STDOUT is not inherited
	CreatePipe(&Std_IN_Rd, &Std_IN_Wr, &saAttr, 0);	//pipe for child's STDIN 
	SetHandleInformation(Std_IN_Wr, HANDLE_FLAG_INHERIT, 0);	
	// Ensure the write handle to the pipe for STDIN is not inherited

	STARTUPINFO siStartInfo;
	memset( &siStartInfo, 0, sizeof(STARTUPINFO) );	// Set STARTUPINFO
	siStartInfo.cb = sizeof(STARTUPINFO); 
	siStartInfo.hStdError = Std_OUT_Wr;
	siStartInfo.hStdOutput = Std_OUT_Wr;
	siStartInfo.hStdInput = Std_IN_Rd;
	siStartInfo.dwFlags |= STARTF_USESTDHANDLES;

	if ( !CreateProcess( NULL,			// Create the child process.
						(char *)pv,		// command line 
						NULL,			// process security attributes 
						NULL,			// primary thread security attributes 
						TRUE,			// handles are inherited 
						CREATE_NO_WINDOW,// creation flags 
						NULL,			// use parent's environment 
						NULL,			// use parent's current directory 
						&siStartInfo,	// STARTUPINFO pointer 
						&piStd) ) {		// receives PROCESS_INFORMATION 
		term_Disp("Couldn't create STDIO process\r\n");
		goto stdio_close;
	}
	CloseHandle( Std_IN_Rd );
	CloseHandle( Std_OUT_Wr );
	
	hWrite = Std_IN_Wr;
	comm_status=CONN_CONNECTED;
	comm_type = STDIO;
	tiny_Title( (char *)pv );
	DWORD dwCCH;
	char buf[1536];
	while ( ReadFile( Std_OUT_Rd, buf, 1500, &dwCCH, NULL) > 0 ) {
		buf[dwCCH] = 0;
		if ( dwCCH > 0 ) term_Parse( buf, dwCCH ); 
	}
	CloseHandle( Std_OUT_Rd );
	CloseHandle( Std_IN_Wr );
	tiny_Title("");
	comm_type = 0;

stdio_close:
	comm_status=CONN_IDLE;
	dwReaderId = 0;
	return 1;
}
void stdio_Close() 
{
	if ( WaitForSingleObject(piStd.hProcess, 100)==WAIT_TIMEOUT ) {
		term_Disp("Terminating stdio process...\r\n");
		TerminateProcess(piStd.hProcess,0);
	}
	CloseHandle(piStd.hThread);
	CloseHandle(piStd.hProcess);
}
/**********************************HTTPd*******************************/
const char HEADER[]="HTTP/1.1 200 Ok\
					\nServer: tinyTerm-httpd\
					\nAccess-Control-Allow-Origin: *\
					\nContent-Type: text/plain\
					\nContent-length: %d\
					\nConnection: Keep-Alive\
					\nCache-Control: no-cache\n\n";		//max-age=1
DWORD WINAPI *httpd( void *pv )
{
	char buf[1024], *cmd, *reply;
	struct sockaddr_in cltaddr;
	int addrsize=sizeof(cltaddr), cmdlen, replen;
	SOCKET http_s1, http_s0 = *(SOCKET *)pv;

	while ( (http_s1=accept(http_s0, (struct sockaddr*)&cltaddr, &addrsize))!= INVALID_SOCKET ) {
		cmd_Disp( "xmlhttp connected" );
		cmdlen=recv(http_s1,buf,1023,0);
		while ( cmdlen>5 ) {
			buf[cmdlen] = 0;
			if ( strncmp(buf, "GET /", 5)!=0 ) {		//TCP connection
				FD_SET readset;
				struct timeval tv = { 0, 300 };			//tv_sec=0, tv_usec=300

				cmd_Disp(buf);
				replen = term_TL1( buf, &reply );
				send( http_s1, reply, replen, 0 );
				reply += replen;

				if ( comm_status==CONN_CONNECTED ) do {
					replen = term_Recv( reply ); 
					if ( replen > 0 ) {
						send(http_s1, reply, replen, 0);
						reply += replen;
					}
					FD_ZERO(&readset);
					FD_SET(http_s1, &readset);
				} while ( select(1, &readset, NULL, NULL, &tv) == 0 );
			}
			else {											//HTTP connection
				if ( (cmd=strtok(buf, " ")) == NULL ) continue; 
				if ( (cmd=strtok(NULL, " ")) == NULL ) continue; 
				cmd++;
				char *p;
				for ( p=cmd+4; *p!=0; p++ ) {
					if ( *p=='%' ) {
					int a;
						sscanf( p+1, "%02x", &a);
						*p = a;
						strcpy(p+1, p+3);
					}
				}
				replen = 0;
				cmd_Disp(cmd+4);
				if ( strncmp(cmd,"Cmd=",4)==0 ) {
					replen = term_TL1( cmd+4, &reply );
				} else
				if ( strncmp(cmd,"Exec=",5)==0 ) {
					reply = term_Exec( cmd+5 );
				} else
				if ( strncmp(cmd,"Send=",5)==0 ) {
					reply = term_Send( cmd+5, strlen(cmd+5) );
				} else
				if ( strncmp(cmd,"Disp=",5)==0 ) {
					reply = term_Disp( cmd+5 );
				} else
				if ( strncmp(cmd,"Recv=",5)==0 ) { 
					replen = term_Recv( reply );
					if ( replen<0 ) replen=0;
				}
				int len = sprintf( buf, HEADER, replen );
				send( http_s1, buf, len, 0 );
				send( http_s1, reply, replen, 0 );
				reply += replen;
			}
			cmdlen = recv( http_s1, buf, 1023, 0 );
		}
		cmd_Disp( "xmlhttp disconnected" );
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
	for ( p=2024; p<2040; p++ ) {
		svraddr.sin_port=htons(p);
		if ( bind(http_s0, (struct sockaddr*)&svraddr, addrsize)!=SOCKET_ERROR ) {
			if ( listen(http_s0, 1)!=SOCKET_ERROR){
				DWORD dwThreadId;
				CreateThread( NULL, 0, (LPTHREAD_START_ROUTINE)httpd, &http_s0, 0, &dwThreadId);
				return p;
			}
		}
	}
	closesocket(http_s0);
	http_s0 = INVALID_SOCKET;
	return 0;
}
/***************************SSH*******************************/
const char *keytypes[]={"unknown", "ssh-rsa", "ssh-dss", "ssh-ecdsa"};
static char homedir[MAX_PATH];
static char *subsystem;
static char *username;
static char *password;
static char *passphrase;
static char *hostname;
static char *port;
int ssh_parameters(char *p)
{
	subsystem =  username = password = passphrase = hostname = NULL;
	port = "22";
	while ( (p!=NULL) && (*p!=0) ) {
		while ( *p==' ' ) p++;
		if ( *p=='-' ) {
			switch ( p[1] ) {
			case 'l': p+=3; username = p; break;
			case 'p': if ( p[2]=='w' ) { p+=4; password = p; } 
						if ( p[2]=='p' ) { p+=4; passphrase = p; }
						break;
			case 'P': p+=3; port = p; break;
			case 's': p+=3; subsystem = p; break;
			}
			p = strchr( p, ' ' ); 
			if ( p!=NULL ) *p++ = 0;
		}
		else { hostname = p; break; }
	}
	if ( username==NULL && hostname!=NULL ) {
		p = strchr( hostname, '@' );
		if ( p!=NULL ) {
			username = hostname; 
			hostname = p+1;
			*p=0; 
		}
	}
	if ( hostname==NULL ) {
		term_Disp( "hostname or ip required!\r\n");
		return -1;
	}
	p = strchr(hostname, ':');
	if ( p!=NULL ) {
		*p = 0; 
		port = p+1;
	}
	return 0;
}
int ssh_knownhost()
{
	const char *dir = getenv("USERPROFILE");
	if ( dir!=NULL ) {
		strncpy(homedir, dir, MAX_PATH-64);
		homedir[MAX_PATH-64] = 0;
	}
	else 
		homedir[0]=0;
	strcat(homedir, "/");
    
    int type, check, buff_len;
	size_t len;
	char knownhostfile[MAX_PATH];
	strcpy(knownhostfile, homedir);
	strcat(knownhostfile, ".ssh/known_hosts");

	char buf[256];
	const char *key = libssh2_session_hostkey(sshSession, &len, &type);
	if ( key==NULL ) return -4;
	buff_len=sprintf(buf, "host key fingerprint(%s):\r\n", keytypes[type]);
	if ( type>0 ) type++;

	const char *fingerprint;
	fingerprint = libssh2_hostkey_hash(sshSession, LIBSSH2_HOSTKEY_HASH_SHA1);
	for(int i = 0; i < 20; i++) {
		sprintf(buf+buff_len+i*3, "%02X:", (unsigned char)fingerprint[i]);
	}
	term_Disp( buf ); term_Disp("\r\n");

    LIBSSH2_KNOWNHOSTS *nh = libssh2_knownhost_init(sshSession);
	if ( nh==NULL ) return -4;
	struct libssh2_knownhost *knownhost;
	libssh2_knownhost_readfile(nh, knownhostfile,
                               LIBSSH2_KNOWNHOST_FILE_OPENSSH);
    check = libssh2_knownhost_check(nh, hostname, key, len,
								LIBSSH2_KNOWNHOST_TYPE_PLAIN|
								LIBSSH2_KNOWNHOST_KEYENC_RAW, &knownhost);
    if ( check==LIBSSH2_KNOWNHOST_CHECK_MATCH ) {
		term_Disp("\033[32mmatch found in .ssh/known_hosts\r\n\033[37m");
		goto Verified;
	}
    if ( check==LIBSSH2_KNOWNHOST_CHECK_MISMATCH ) {
		if ( type==((knownhost->typemask&LIBSSH2_KNOWNHOST_KEY_MASK)
								  >>LIBSSH2_KNOWNHOST_KEY_SHIFT) ) {
			term_Disp("\033[31m!!!changed! proceed with care!!!\r\n\033[37m");
		}
		else 
			check=LIBSSH2_KNOWNHOST_CHECK_NOTFOUND;
	}
 
	if ( check==LIBSSH2_KNOWNHOST_CHECK_NOTFOUND ) {
		libssh2_knownhost_addc(nh, hostname, "", key, len, "**tinyTerm**", 12,
								LIBSSH2_KNOWNHOST_TYPE_PLAIN|
								LIBSSH2_KNOWNHOST_KEYENC_RAW|
								(type<<LIBSSH2_KNOWNHOST_KEY_SHIFT), &knownhost);
		FILE *fp = fopen(knownhostfile, "a+");
		if ( fp==NULL ) {
			int len = strlen(knownhostfile);
			knownhostfile[len-12]=0;
			mkdir(knownhostfile);
			knownhostfile[len-12]='/';
			fp = fopen(knownhostfile, "a+");
		}
		if ( fp ) {
			char buf[2048];
			libssh2_knownhost_writeline(nh, knownhost, buf, 2048, &len, 
								LIBSSH2_KNOWNHOST_FILE_OPENSSH);
			fprintf(fp, "%s", buf );
			fclose(fp);
			term_Disp("\033[32madded to .ssh/known_hosts\r\n\033[37m");
		} 
		else 
			term_Disp("\033[33mcouldn't add to .ssh/known_hosts\r\n\033[37m");
	}
Verified:
    libssh2_knownhost_free(nh);
    return 0;
}
int ssh_authentication(char *username, char *password, char *passphrase)
{
	char user[256], pass[256];
	if ( username==NULL ) {
		term_Parse("\r\nusername:", 11);
		comm_Gets(user, FALSE);
		username = user;
	}
	if ( libssh2_userauth_list(sshSession, username, strlen(username))==NULL )
		return 0;
	if ( password==NULL) {	// try public key
		char pubkeyfile[MAX_PATH], privkeyfile[MAX_PATH];
		strcpy(pubkeyfile, homedir);
		strcat(pubkeyfile, ".ssh/id_rsa.pub");
		strcpy(privkeyfile, homedir);
		strcat(privkeyfile, ".ssh/id_rsa");
		if ( !libssh2_userauth_publickey_fromfile(sshSession,
				username, pubkeyfile, privkeyfile, passphrase) ) return 0;
	}
	for ( int rep=0; rep<3; rep++ ) {			
		if ( password==NULL ) {
			term_Parse("\r\npassword:", 11);
			comm_Gets(pass, TRUE);
			password = pass;
		}
		if ( !libssh2_userauth_password(sshSession, username, password) ) {
			term_Parse("\r\n", 2);
			return 0;
		}
		password=NULL;
	} 
	term_Disp("too many tries!\r\n");
	return -5;
}
DWORD WINAPI ssh( void *pv )
{
	int rc;
	
	if ( ssh_parameters( (char *)pv)<0 ) goto TCP_Close;
	if ( tcp(hostname, atoi(port))==-1 ) goto TCP_Close;

	sshSession = libssh2_session_init();
	while ((rc = libssh2_session_handshake(sshSession, sock)) ==
													LIBSSH2_ERROR_EAGAIN);
	if ( rc!=0 ) {
		term_Disp("sshSession failed!\r\n");
		goto Session_Close;
	}

	if ( ssh_knownhost()<0 ) goto Session_Close;
	comm_status=CONN_AUTHENTICATING;
	if ( ssh_authentication(username, password, passphrase)<0 ) 
		goto Session_Close;
	
	if (!(sshChannel = libssh2_channel_open_session(sshSession))) {
		term_Disp("sshChannel failed!\r\n");
		goto Session_Close;
	}
	if ( subsystem == NULL ) {
		if (libssh2_channel_request_pty(sshChannel, "xterm")) {
			term_Disp("pty failed!\r\n");
			goto Channel_Close;
		}
		if (libssh2_channel_shell(sshChannel)) {
			term_Disp("shell failed!\r\n");
			goto Channel_Close; 
		}
	}
	else {
		if (libssh2_channel_subsystem(sshChannel, subsystem)) {
			term_Disp("subsystem failed!\r\n");
			goto Channel_Close;
		}
	}
	libssh2_session_set_blocking(sshSession, 0); 
//	libssh2_keepalive_config(sshSession, FALSE, 60);

	comm_status=CONN_CONNECTED;
	comm_type=SSH;
	tiny_Title(hostname);
	while ( libssh2_channel_eof(sshChannel) == 0 ) {
		char buf[4096];
		if ( WaitForSingleObject(mtx, INFINITE)==WAIT_OBJECT_0 ) {
			int cch=libssh2_channel_read(sshChannel, buf, 4096);
			ReleaseMutex(mtx);
			if ( cch >= 0 ) {
				term_Parse( buf, cch );
			}
			else {
				if ( cch==LIBSSH2_ERROR_EAGAIN ) 
					if ( ssh_wait_socket()>0 ) 
						continue;
				break;
			}
		}
	}
	tiny_Title("");
	comm_type = 0;

Channel_Close:
	libssh2_channel_close(sshChannel);
	libssh2_channel_free(sshChannel);

Session_Close:
	libssh2_session_disconnect(sshSession, "Normal Shutdown");
	libssh2_session_free(sshSession);

TCP_Close:
	closesocket(sock);
	comm_status=CONN_IDLE;
	dwReaderId = 0;

	return 1;
}
int scp_read_one(const char *rpath, const char *lpath)
{
	LIBSSH2_CHANNEL *scp_channel;
	libssh2_struct_stat fileinfo;
	int err_no=0;
	do {
        WaitForSingleObject(mtx, INFINITE);
		scp_channel = libssh2_scp_recv2(sshSession, rpath, &fileinfo);
		if ( !scp_channel ) err_no = libssh2_session_last_errno(sshSession);
		ReleaseMutex(mtx);
        if (!scp_channel) {
            if ( err_no==LIBSSH2_ERROR_EAGAIN) 
                if ( ssh_wait_socket()>0 ) continue;
			print("\r\n\033[31mSCP: couldn't open \033[32m%s",rpath);
            return -1;
        }
    } while (!scp_channel);

	FILE *fp = fopen(lpath, "wb");
	if ( fp==NULL ) {
		print("\r\n\033[31mSCP: couldn't write to \033[32m%s", lpath);
		return -2;
	}
	print("\r\n\033[32mSCP: %s\t ", lpath);

	time_t start = time(NULL);
    libssh2_struct_stat_size got = 0;
    libssh2_struct_stat_size total = fileinfo.st_size;
    int rc, nmeg=0;
    while  ( got<total ) {
        char mem[1024*32];
        int amount=sizeof(mem);
        if ( (total-got) < amount) {
            amount = (int)(total-got);
        }
        WaitForSingleObject(mtx, INFINITE);
		rc = libssh2_channel_read(scp_channel, mem, amount);
        ReleaseMutex(mtx);
        if ( rc==0 ) continue;
		if ( rc>0) {
            fwrite(mem, 1,rc,fp);
            got += rc;
			if ( ++nmeg%32==0 ) print(".");
        }
		else {
			if ( rc==LIBSSH2_ERROR_EAGAIN ) 
				if ( ssh_wait_socket()>0 ) continue; 
            print("\033[31minterrupted at %ld bytes", total);
            break;
		}
    }
	fclose(fp);

    int duration = (int)(time(NULL)-start);
	print(" %lld bytes in %d seconds", got, duration);
	WaitForSingleObject(mtx, INFINITE);
    libssh2_channel_free(scp_channel);
	ReleaseMutex(mtx);
	return 0;
}
int scp_write_one(const char *lpath, const char *rpath)
{
	LIBSSH2_CHANNEL *scp_channel;
	struct stat fileinfo;
    FILE *fp =fopen(lpath, "rb");
	if ( !fp ) {
		print("\r\n\033[31mSCP: couldn't read from \033[32m%s", lpath);
		return -1;
	}
    stat(lpath, &fileinfo);	//fl_stat causes wrong file size been sent on win32
	
    int err_no = 0;
	do {
        WaitForSingleObject(mtx, INFINITE);
		scp_channel = libssh2_scp_send(sshSession, rpath, 
					fileinfo.st_mode&0777, (unsigned long)fileinfo.st_size);
		if ( !scp_channel ) err_no = libssh2_session_last_errno(sshSession);
		ReleaseMutex(mtx);
        if ( !scp_channel ) {
			if ( err_no==LIBSSH2_ERROR_EAGAIN ) 
				if ( ssh_wait_socket()>0 ) continue;
			print("\r\n\033[31mSCP: couldn't create \033[32m%s",rpath);
			fclose(fp);
			return -2;
		}
    } while ( !scp_channel );
 
    print("\r\n\033[32mSCP: %s\t", rpath);
	time_t start = time(NULL);
	size_t nread = 0, total = 0;
	int rc, nmeg = 0;
    while ( nread==0 ) {
		char mem[1024*32];
        if ( (nread=fread(mem, 1, sizeof(mem), fp))<=0 ) break;// end of file 
        total += nread;
		if ( ++nmeg%32==0 ) print(".");

        char *ptr = mem;
        while ( nread>0 ) {
            WaitForSingleObject(mtx, INFINITE);
			rc = libssh2_channel_write(scp_channel, ptr, nread);
			ReleaseMutex(mtx);
			if ( rc>0 ) {
                nread -= rc;
                ptr += rc;
			}
			else {
				if ( rc==LIBSSH2_ERROR_EAGAIN ) 
               		if ( ssh_wait_socket()>=0 ) continue;
                print("\033[31minterrupted at %ld bytes", total);
                break;
            }
        }
    }/* only continue if nread was drained */ 
	fclose(fp);
    int duration = (int)(time(NULL)-start);
	print("%ld bytes in %d seconds", total, duration);

	do {
		WaitForSingleObject(mtx, INFINITE);
		rc = libssh2_channel_send_eof(scp_channel);
		ReleaseMutex(mtx);
	} while ( rc==LIBSSH2_ERROR_EAGAIN );
	do {
		WaitForSingleObject(mtx, INFINITE);
		rc = libssh2_channel_wait_eof(scp_channel);
		ReleaseMutex(mtx);
	} while ( rc==LIBSSH2_ERROR_EAGAIN );
    do {
		WaitForSingleObject(mtx, INFINITE);
		rc = libssh2_channel_wait_closed(scp_channel);
		ReleaseMutex(mtx);
	} while ( rc == LIBSSH2_ERROR_EAGAIN);
	WaitForSingleObject(mtx, INFINITE);
    libssh2_channel_free(scp_channel);
	ReleaseMutex(mtx);
	return 0;
}
void scp_pwd(char *pwd)
{
	char *p1, *p2;
	term_TL1("pwd\r", &p2);
	p1 = strchr(p2, 0x0a);
	if ( p1!=NULL ) {
		p2 = p1+1;
		p1 = strchr(p2, 0x0a);
		if ( p1!=NULL ) {
			int len = p1-p2;
			strncpy(pwd, p2, len);
			pwd[len]=0;
		}
	}
}
DWORD WINAPI scp_read( void *pv )
{
	char *cmd = (char *)pv;
	char *p = strchr(cmd, ' '); 
	if ( p==NULL ) return 0;
	*p = 0;
	char *lpath = p+1;
	char *rpath = cmd+1;

	char rnames[4096]="ls -1 ", *rdir=rnames+6, *rlist;
	if ( *rpath!='/' && *rpath!='~' ) {
		scp_pwd(rdir);
		strcat(rnames, "/");
	}
	strcat(rnames, rpath);
	
	if ( strchr(rpath,'*')==NULL && strchr(rpath, '?')==NULL ) {
		char lfile[1024];
		strcpy(lfile, lpath);
		struct stat statbuf;
		if ( stat(lpath, &statbuf)!=-1 ) {
			if ( S_ISDIR(statbuf.st_mode) ) {
				strcat(lfile, "/");
				const char *p = strrchr(rpath, '/');
				if ( p!=NULL ) p++; else p=rpath;
				strcat(lfile, p);
			}
		}	
		scp_read_one(rdir, lfile);
	}
	else {
		strcat(rnames, "\r");
		int len = term_TL1(rnames, &rlist );
		if ( len>0 ) {
			char rfile[1024], lfile[1024];
			char *p1, *p2, *p = strrchr(rdir, '/');
			if ( p!=NULL ) *p=0;
			p = strchr(rlist, '\012');
			if ( p==NULL ) return 0;
			while ( (p1=strchr(++p, '\012'))!=NULL ) {
				if ( p1-rlist>=len ) break; 
				strncpy(rfile, p, p1-p);
				rfile[p1-p] = 0;
				p2 = strrchr(rfile, '/');
				if ( p2==NULL ) p2=rfile; else p2++;
				strcpy(lfile, lpath);
				strcat(lfile, "/");
				strcat(lfile, p2);
				scp_read_one(rfile, lfile);
				p = p1;
			}
		}
	}
	comm_Send("\015", 1);
	return 0;
}
DWORD WINAPI scp_write( void *pv )
{
	char *cmd = (char *)pv;
	char *p = strchr(cmd, ' '); 
	if ( p==NULL ) return 0;
	*p = 0;
	char *lpath = cmd;
	char *rpath = p+2;

	DIR *dir;
	struct dirent *dp;
	struct stat statbuf;

	char rnames[1024]="ls -ld ";
	if ( *rpath!='/' && *rpath!='~' ) {
		scp_pwd(rnames+7);
		if ( *rpath ) {
			strcat(rnames, "/");
			strcat(rnames, rpath);
		}
	}
	else 
		strcpy(rnames+7, rpath);

	if ( stat(lpath, &statbuf)!=-1 ) {
		char *rlist, rfile[1024];
		strcpy(rfile, rnames+7);
		strcat(rnames, "\r");
		if ( term_TL1(rnames, &rlist )>0 ) {
			const char *p = strchr(rlist, 0x0a);
			if ( p!=NULL ) if ( p[1]=='d' ) {
				p = strrchr(lpath, '/');
				if ( p!=NULL ) p++; else p=lpath;
				strcat(rfile, "/");
				strcat(rfile, p);
			}
		}
		scp_write_one(lpath, rfile);
	}
	else {
		const char *lname=lpath;
		char ldir[1024]=".";
		char *p = (char *)strrchr(lpath, '/');
		if ( p!=NULL ) {
			*p++ = 0; 
			lname = p;
			strcpy(ldir, lpath);
		}

		if ( (dir=opendir(ldir) ) == NULL ){
			print("\r\n\033[31mSCP: couldn't open \033[32m%s\r\n", ldir);
			return 0;
		}
		while ( (dp=readdir(dir)) != NULL ) {
			if ( fnmatch(lname, dp->d_name, 0)==0 ) {
				char lfile[1024], rfile[1024];
				strcpy(lfile, ldir);
				strcat(lfile, "/");
				strcat(lfile, dp->d_name);
				strcpy(rfile, rnames+7);
				strcat(rfile, "/");
				strcat(rfile, dp->d_name);
				scp_write_one(lfile, rfile);
			}
		}
	}
	comm_Send("\015", 1);
	return 0;
}
int scp_cmd(char *cmd)
{
	static char scpCmd[4096];
	strncpy(scpCmd, cmd, 4095);
	scpCmd[4095] = 0;
	for ( char *p=scpCmd; *p; p++ ) if ( *p=='\\' ) *p='/';

	DWORD dwCopierId;
	CreateThread( NULL,0,(*cmd==':'?scp_read:scp_write),scpCmd,0,&dwCopierId );
	return 0;
}
int tun_worker( int tun_sock, LIBSSH2_CHANNEL *tun_channel ) 
{
	char buff[16384];
	int rc, i;
	int len, wr;
    fd_set fds;
	struct timeval tv;
	while ( TRUE ) {
		WaitForSingleObject(mtx, INFINITE);
		rc = libssh2_channel_eof(tun_channel);
		ReleaseMutex(mtx);
		if ( rc!=0 ) break;

        FD_ZERO(&fds);
		FD_SET(sock, &fds);
        FD_SET(tun_sock, &fds);
        tv.tv_sec = 0;
        tv.tv_usec = 100000;
        rc = select(tun_sock + 1, &fds, NULL, NULL, &tv);
        if ( rc==-1 ) {
            print("\r\n\033[31mselect error\r\n");
            break;
        }
		if ( rc==0 ) continue;
        if ( FD_ISSET(tun_sock, &fds) ) {
            len = recv(tun_sock, buff, sizeof(buff), 0);
            if ( len<=0 ) break;
			for ( wr=0, i=0; wr<len; wr+=i ) {
                WaitForSingleObject(mtx, INFINITE);
                i = libssh2_channel_write(tun_channel, buff+wr, len-wr);
                ReleaseMutex(mtx);
                if ( i==LIBSSH2_ERROR_EAGAIN ) continue;
				if ( i<=0 ) goto shutdown;
            }
        }
		if ( FD_ISSET(sock, &fds) ) while ( TRUE )
		{
            WaitForSingleObject(mtx, INFINITE);
            len = libssh2_channel_read(tun_channel, buff, sizeof(buff));
 	        ReleaseMutex(mtx);
 	        if ( len==LIBSSH2_ERROR_EAGAIN ) break;
            if ( len<=0 ) goto shutdown;
            for ( wr=0, i=0; wr<len; wr+=i ) {
                i = send(tun_sock, buff + wr, len - wr, 0);
                if ( i<=0 ) break;
            }
        }
    }
shutdown:
    print("\r\n\033[32mTunnel %d disconnected!\r\n", tun_sock); 
	WaitForSingleObject(mtx, INFINITE);
    libssh2_channel_free(tun_channel);
	ReleaseMutex(mtx);
	closesocket(tun_sock);
	return 0;
}
DWORD WINAPI tun_local(void *pv)
{
	char *cmd = (char *)pv, *lpath, *rpath;
	char *p = strchr(cmd, ' ');
	if ( p!=NULL ) {
		lpath = cmd;
		rpath = p+1;
		*p = 0;
	}
	else 
		return 0;
		
    char sockopt = 1;
    char shost[256], dhost[256], *client_host;
    unsigned short sport, dport, client_port;

	strncpy(shost, lpath, 255); shost[255]=0;
	strncpy(dhost, rpath, 255); dhost[255]=0;
	if ( (p=strchr(shost, ':'))==NULL ) return -1;
	*p = 0; sport = atoi(++p);
	if ( (p=strchr(dhost, ':'))==NULL ) return -1;
	*p = 0; dport = atoi(++p);

	struct sockaddr_in sin;
 	int sinlen=sizeof(sin);
    struct addrinfo *ainfo;    
    if ( getaddrinfo(shost, NULL, NULL, &ainfo)!=0 ) {
		print("\033[31minvalid address: %s\r\n", shost);
		return -1;
	}
    int listensock = socket(ainfo->ai_family, SOCK_STREAM, 0);
    setsockopt(listensock,SOL_SOCKET,SO_REUSEADDR,&sockopt,sizeof(sockopt));
    ((struct sockaddr_in *)(ainfo->ai_addr))->sin_port = htons(sport);
    int rc = bind(listensock, ainfo->ai_addr, ainfo->ai_addrlen);
    freeaddrinfo(ainfo);
	if ( rc==-1 ) {
        print("\033[31mport %d invalid or in use\r\n", sport);
        closesocket(listensock);
        return -1;
    }
    if ( listen(listensock, 2)==-1 ) {
        print("\033[31mlisten error\r\n");
        closesocket(listensock);
        return -1;
    }
	print("\r\n\033[32mTunnel listening on %s:%d\r\n", shost, sport);

	int tun_sock;
	LIBSSH2_CHANNEL *tun_channel;
	if ((tun_sock=accept(listensock,(struct sockaddr*)&sin,&sinlen))!=-1) {
		client_host = inet_ntoa(sin.sin_addr);
		client_port = ntohs(sin.sin_port);
		do {
			int rc = 0;
			WaitForSingleObject(mtx, INFINITE);
			tun_channel = libssh2_channel_direct_tcpip_ex(sshSession, 
									dhost, dport, client_host, client_port);
			if (!tun_channel) rc = libssh2_session_last_errno(sshSession);
			ReleaseMutex(mtx);
			if ( !tun_channel ) {
				if ( rc==LIBSSH2_ERROR_EAGAIN )	
					if ( ssh_wait_socket()>0 ) continue;
				print("\033[31mCouldn't establish tunnel, is it supported?\r\n");
				closesocket(tun_sock);
				goto shutdown;
			}
		} while ( !tun_channel );
		print("\r\n\033[32mTunnel %d from local %s:%d to remote %s:%d\r\n",
       			tun_sock,client_host, client_port, dhost, dport);
       	tun_worker(tun_sock, tun_channel);
	}
shutdown:
    closesocket(listensock);
	return 0;
}
DWORD WINAPI tun_remote( void *pv )
{
	char *cmd = (char *)pv, *lpath, *rpath;
	char *p = strchr(cmd, ' ');
	if ( p!=NULL ) {
		lpath = p+1;
		rpath = cmd;
		*p = 0;
	}
	else 
		return 0;

	int r_listenport;
	LIBSSH2_LISTENER *listener = NULL;
    LIBSSH2_CHANNEL *tun_channel = NULL;

	char shost[256], dhost[256];
	unsigned short sport, dport;
	strncpy(shost, rpath, 255);
	strncpy(dhost, lpath, 255);
	if ( (p=strchr(shost, ':'))==NULL ) return -1;
	*p = 0; sport = atoi(++p);
	if ( (p=strchr(dhost, ':'))==NULL ) return -1;
	*p = 0; dport = atoi(++p);

	do {
		int err_no = 0;
		WaitForSingleObject(mtx, INFINITE);
		listener = libssh2_channel_forward_listen_ex(sshSession, shost,
										sport, &r_listenport, 1);
		if ( !listener ) err_no = libssh2_session_last_errno(sshSession);
		ReleaseMutex(mtx);
		if (!listener) {
			if ( err_no==LIBSSH2_ERROR_EAGAIN ) 
				if ( ssh_wait_socket()>0 ) continue;
			print("\033[31mCouldn't listen for tunnel, is it supported?\r\n");
			return -1;
		}
	} while ( !listener );
    print("\r\n\033[32mTunnel server is listening on R%s:%d\r\n",
														shost, r_listenport);

	do {
		int err_no = 0;
		WaitForSingleObject(mtx, INFINITE);
	    tun_channel = libssh2_channel_forward_accept(listener);
		if ( !tun_channel ) err_no = libssh2_session_last_errno(sshSession);
		ReleaseMutex(mtx);
		if (!tun_channel) {
			if ( err_no==LIBSSH2_ERROR_EAGAIN )
				if ( ssh_wait_socket()>0 ) continue;
			print("\033[31mCouldn't accept tunnel connection!\r\n");
			return -1;
		}
	} while ( !tun_channel );

    struct addrinfo *ainfo;    
    if ( getaddrinfo(shost, NULL, NULL, &ainfo)==0 ) {
		int tun_sock = socket(ainfo->ai_family, SOCK_STREAM, 0);
		((struct sockaddr_in *)(ainfo->ai_addr))->sin_port = htons(dport);
		int rc = connect(tun_sock,  ainfo->ai_addr, ainfo->ai_addrlen);
		freeaddrinfo(ainfo);
		
		if ( rc==0 ) {
			print("\r\n\033[31mTunnel %d from remote %s:%d to local %s:%d\r\n",
						tun_sock, shost, r_listenport, dhost, dport);
			tun_worker(tun_sock, tun_channel);
		}
		else {
			print("\r\n\033[31mremote tunneling connect error\r\n");
			closesocket(tun_sock);
		}
	}
	else {
		print("\r\n\033[31mremote tunneling address error\r\n");
	}	
	WaitForSingleObject(mtx, INFINITE);
	libssh2_channel_free(tun_channel);
	libssh2_channel_forward_cancel(listener);
	ReleaseMutex(mtx);
	return 0;
}
int tun_cmd(char *cmd)
{
	DWORD dwTunnelId; 
	CreateThread( NULL, 0, (*cmd=='R'?tun_remote:tun_local), 
					cmd, 0, &dwTunnelId );
	Sleep(1000);
	return 0;
}

/*******************sftpHost*******************************/
static char homepath[MAX_PATH], realpath[MAX_PATH];
int sftp_lcd(char *cmd)
{
	if ( cmd==NULL || *cmd==0 ) {
		char buf[4096];
		if ( getcwd(buf, 4096)!=NULL ) {
			print("\t\033[32m%s \033[37mis current local directory\r\n", cmd);
		}
		else {
			print("\t\033[31mCouldn't get current local directory\r\n");
		}
	}
	else {
		while ( *cmd==' ' ) cmd++;
		if ( chdir(cmd)==0 ) {
			print("\t\033[32m%s\033[37m is now local directory!\r\n", cmd);
		}
		else {
			print("\t\033[31mCouldn't change local directory to\033[32m%s\r\n", cmd);
		}
	}
	return 0;
}
int sftp_cd(char *path) 
{
	char newpath[1024];
	if ( path!=NULL ) {
		LIBSSH2_SFTP_HANDLE *sftp_handle;
		sftp_handle = libssh2_sftp_opendir(sftpSession, path);
		if (!sftp_handle) {
			print("\t\033[31mUnable to change working directory to\033[32m%s\r\n", path);
			return 0;
		}
	    libssh2_sftp_closedir(sftp_handle);		
		int rc = libssh2_sftp_realpath(sftpSession, path, newpath, 1024);
		if ( rc>0 ) strcpy( realpath, newpath );
	}
	print("\t\033[32m%s \033[37mis current working directory\r\n", realpath);
	return 0;
}
int sftp_ls(char *path, int ll)
{
    char *pattern = NULL;
	char mem[512], longentry[512];
    LIBSSH2_SFTP_ATTRIBUTES attrs; 
	LIBSSH2_SFTP_HANDLE *sftp_handle = libssh2_sftp_opendir(sftpSession, path);
    if (!sftp_handle) {
        if ( strchr(path, '*')==NULL && strchr(path, '?')==NULL ) {
			print("\t\033[31mUnable to open dir\033[32m%s\r\n", path);
        	return 0;
		}
		pattern = strrchr(path, '/');
		if ( pattern!=path ) {
			*pattern++ = 0;
			sftp_handle = libssh2_sftp_opendir(sftpSession, path);
		}
		else {
			pattern++;
			sftp_handle = libssh2_sftp_opendir(sftpSession, "/");
		}
		if ( !sftp_handle ) {
			print("\t\033[31munable to open dir\033[32m%s\r\n", path);
			return 0;
		}
    }

    while ( libssh2_sftp_readdir_ex(sftp_handle, mem, sizeof(mem),
                        	longentry, sizeof(longentry), &attrs)>0 ) {
		if ( pattern==NULL || fnmatch(pattern, mem, 0)==0 ) 
			print("\t%s\r\n", ll ? longentry : mem);
    }
    libssh2_sftp_closedir(sftp_handle);
	return 0;
}
int sftp_rm(char *path)
{
	if ( strchr(path, '*')==NULL && strchr(path, '?')==NULL ) {
		if ( libssh2_sftp_unlink(sftpSession, path) ) 
			print("\t\033[31mcouldn't delete file\033[32m%s\r\n", path);
		return 0;
	}
    char mem[512], rfile[1024];
    LIBSSH2_SFTP_ATTRIBUTES attrs; 
	LIBSSH2_SFTP_HANDLE *sftp_handle;
	char *pattern = strrchr(path, '/');
	if ( pattern!=path ) *pattern++ = 0;
	sftp_handle = libssh2_sftp_opendir(sftpSession, path);
	if ( !sftp_handle ) {
		print("\t\033[31munable to open dir\033[32m%s\r\n", path);
		return 0;
	}

    while ( libssh2_sftp_readdir(sftp_handle, mem, sizeof(mem), &attrs)>0 ) {
		if ( fnmatch(pattern, mem, 0)==0 ) {
			strcpy(rfile, path);
			strcat(rfile, "/");
			strcat(rfile, mem);
			if ( libssh2_sftp_unlink(sftpSession, rfile) ) 
				print("\t\033[31mcouldn't delete file\033[32m%s\r\n", rfile);
		}
    }
    libssh2_sftp_closedir(sftp_handle);
	return 0;
}
int sftp_md(char *path)
{
 	int rc = libssh2_sftp_mkdir(sftpSession, path,
                            LIBSSH2_SFTP_S_IRWXU|
                            LIBSSH2_SFTP_S_IRGRP|LIBSSH2_SFTP_S_IXGRP|
                            LIBSSH2_SFTP_S_IROTH|LIBSSH2_SFTP_S_IXOTH);
    if ( rc ) {
        print("\t\033[31mcouldn't create directory\033[32m%s\r\n", path);	
	}
	return 0;
}
int sftp_rd(char *path)
{
 	int rc = libssh2_sftp_rmdir(sftpSession, path);
    if ( rc ) {
        print("\t\033[31mcouldn't remove directory\033[32m%s\r\n", path);	
	}
	return 0;
}
int sftp_ren(char *src, char *dst)
{
	int rc = libssh2_sftp_rename(sftpSession, src, dst);
	if ( rc ) 
		print("\t\033[31mcouldn't rename file\033[32m%s\r\n", src);
	return 0;	
}
int sftp_get_one(char *src, char *dst)
{
	LIBSSH2_SFTP_HANDLE *sftp_handle=libssh2_sftp_open(sftpSession, 
											src, LIBSSH2_FXF_READ, 0);

	if (!sftp_handle) {
        print("\t\033[31mUnable to read file\033[32m%s\r\n", src);
		return 0;
    }
    FILE *fp = fopen(dst, "wb");
	if ( fp==NULL ) {
		print("\t\033[31munable to create local file\033[32m%s\r\n", dst);
    	libssh2_sftp_close(sftp_handle);
		return 0;
	}
	print("\t\033[32m%s ", dst);
    char mem[1024*64];
	unsigned int rc, block=0;
	long total=0;
	time_t start = time(NULL);
    while ( (rc=libssh2_sftp_read(sftp_handle, mem, sizeof(mem)))>0 ) {
        if ( fwrite(mem, 1, rc, fp)<rc ) break;
		total += rc;
		block +=rc;
		if ( block>1024*1024 ) { block=0; print("."); }
    } 
    int duration = (int)(time(NULL)-start);
	print("%ld bytes %d seconds\r\n", total, duration);
	fclose(fp);
    libssh2_sftp_close(sftp_handle);
	return 0;	
}
int sftp_put_one(char *src, char *dst)
{
	LIBSSH2_SFTP_HANDLE *sftp_handle = libssh2_sftp_open(sftpSession, dst,
                      LIBSSH2_FXF_WRITE|LIBSSH2_FXF_CREAT|LIBSSH2_FXF_TRUNC,
                      LIBSSH2_SFTP_S_IRUSR|LIBSSH2_SFTP_S_IWUSR|
                      LIBSSH2_SFTP_S_IRGRP|LIBSSH2_SFTP_S_IROTH);
    if (!sftp_handle) {
		print("\t\033[31mcouldn't open remote file\033[32m%s\r\n", dst);
		return 0;
    }
	FILE *fp = fopen(src, "rb");
	if ( fp==NULL ) {
		print("\t\033[31mcouldn't open local file\033[32m%s\r\n", src);
		return 0;
	}
	print("\t\033[32m%s ", dst);
    char mem[1024*64];
	int nread, block=0;
	long total=0;
    time_t start = time(NULL);
	while ( (nread=fread(mem, 1, sizeof(mem), fp))>0 ) {
        int nwrite=0;
        while ( nread>nwrite ){
			int rc=libssh2_sftp_write(sftp_handle, mem+nwrite, nread-nwrite);
			if ( rc<0 ) break;
            nwrite += rc;
			total += rc;
        }
        block += nwrite;
		if ( block>1024*1024 ) { block=0; print("."); }
	}
    int duration = (int)(time(NULL)-start);
	fclose(fp);
	print("%ld bytes %d seconds\r\n", total, duration);
    libssh2_sftp_close(sftp_handle);	
	return 0;
}
int sftp_get(char *src, char *dst)
{
    char mem[512];
    LIBSSH2_SFTP_ATTRIBUTES attrs; 
	LIBSSH2_SFTP_HANDLE *sftp_handle;
	if ( strchr(src,'*')==NULL && strchr(src, '?')==NULL ) {
		char lfile[1024];
		strcpy(lfile, *dst?dst:".");
		struct stat statbuf;
		if ( stat(lfile, &statbuf)!=-1 ) {
			if ( S_ISDIR(statbuf.st_mode) ) {
				strcat(lfile, "/");
				char *p = strrchr(src, '/');
				if ( p!=NULL ) p++; else p=src;
				strcat(lfile, p);
			}
		}	
		sftp_get_one(src, lfile);
	}
	else {
		char *pattern = strrchr(src, '/');
		*pattern++ = 0;
		sftp_handle = libssh2_sftp_opendir(sftpSession, src);
		if ( !sftp_handle ) {
			print("\t\033[31mcould't open remote diretory\033[32m%s\r\n", src);
			return 0;
		}
		
		char rfile[1024], lfile[1024];
		strcpy(rfile, src); strcat(rfile, "/");
		int rlen = strlen(rfile);
		strcpy(lfile, dst); if ( *lfile ) strcat(lfile, "/");
		int llen = strlen(lfile);
		while ( libssh2_sftp_readdir(sftp_handle, mem, 
								sizeof(mem), &attrs)>0 ) {
			if ( fnmatch(pattern, mem, 0)==0 ) {
				strcpy(rfile+rlen, mem);
				strcpy(lfile+llen, mem);
				sftp_get_one(rfile, lfile);
			}
		}
	}
	return 0;
}
int sftp_put(char *src, char *dst)
{
	DIR *dir;
	struct dirent *dp;
	struct stat statbuf;

	if ( stat(src, &statbuf)!=-1 ) {
		char rfile[1024];
		strcpy(rfile, *dst?dst:".");
		LIBSSH2_SFTP_ATTRIBUTES attrs;
		if ( libssh2_sftp_stat(sftpSession, rfile, &attrs)==0 ) {
			if ( LIBSSH2_SFTP_S_ISDIR(attrs.permissions) ) {
				char *p = strrchr(src, '/');
				if ( p!=NULL ) p++; else p=src;
				strcat(rfile, "/");
				strcat(rfile, p);
			}
		}		
		sftp_put_one(src, rfile);
	}
	else {
		char *pattern=src;
		char lfile[1024]=".", rfile[1024];
		char *p = strrchr(src, '/');
		if ( p!=NULL ) {
			*p++ = 0; 
			pattern = p;
			strcpy(lfile, src);
		}

		if ( (dir=opendir(lfile) ) == NULL ){
			print("\t\033[31mcouldn't open \033[32m%s\r\n",lfile);
			return 0;
		}
		strcat(lfile, "/");
		int llen = strlen(lfile);
		strcpy(rfile, dst); 
		if ( *rfile!='/' || strlen(rfile)>1 ) strcat(rfile, "/");
		int rlen = strlen(rfile);
		while ( (dp=readdir(dir)) != NULL ) {
			if ( fnmatch(pattern, dp->d_name, 0)==0 ) {
				strcpy(lfile+llen, dp->d_name);
				strcpy(rfile+rlen, dp->d_name);
				sftp_put_one(lfile, rfile);
			}
		}
	}
	return 0;
}
int sftp_cmd(char *p){
	char *p1, *p2, src[1024], dst[1024];
	
	for ( p1=p; *p1; p1++ ) if ( *p1=='\\' ) *p1='/';
	
	p1 = strchr(p, ' ');		//p1 is first parameter of the command
	if ( p1==NULL ) 
		p1 = p+strlen(p);
	else 
		while ( *p1==' ' ) *p1++=0;

	p2 = strchr(p1, ' '); 		//p2 is second parameter of the command
	if ( p2==NULL ) 
		p2 = p1+strlen(p1);
	else
		while ( *p2==' ' ) *p2++=0;

	strcpy(src, p1);			//src is remote source file
	if ( *p1!='/') {
		strcpy(src, realpath);	
		if ( *p1!=0 ) { 
			if ( *src!='/' || strlen(src)>1 ) strcat(src, "/"); 
			strcat(src, p1); 
		}
	}

	strcpy(dst, p2);			//dst is remote destination file	
	if ( *p2!='/' ) {
		strcpy( dst, realpath );	
		if ( *p2!=0 ) {
			if ( *dst!='/' || strlen(dst)>1 ) strcat( dst, "/" );
			strcat( dst, p2 );
		}
	}
	if ( strncmp(p, "lpwd",4)==0 ) sftp_lcd(NULL);
	else if ( strncmp(p, "lcd",3)==0 ) sftp_lcd(p1);
	else if ( strncmp(p, "pwd",3)==0 ) sftp_cd(NULL);
	else if ( strncmp(p, "cd", 2)==0 ) sftp_cd(*p1==0?homepath:src);
	else if ( strncmp(p, "ls", 2)==0 ) sftp_ls(src, FALSE);
	else if ( strncmp(p, "dir",3)==0 ) sftp_ls(src, TRUE);		
	else if ( strncmp(p, "mkdir",5)==0 ) sftp_md(src);
	else if ( strncmp(p, "rmdir",5)==0 ) sftp_rd(src);
	else if ( strncmp(p, "rm", 2)==0
			||strncmp(p, "del",3)==0)  sftp_rm(src);		
	else if ( strncmp(p, "ren",3)==0)  sftp_ren(src, dst);
	else if ( strncmp(p, "get",3)==0 ) sftp_get(src, p2);
	else if ( strncmp(p, "put",3)==0 ) sftp_put(p1, dst);
	else if ( strncmp(p, "bye",3)==0 ) return -1;
	else print("\t\033[31m%s is not supported command, try %s\r\n\t%s\r\n",
				p, "\033[37mlcd, lpwd, cd, pwd,", 
				"ls, dir, get, put, ren, rm, del, mkdir, rmdir, bye");
	term_Parse("sftp> ", 6);
	return 0;
}
DWORD WINAPI sftp( void *pv )
{
	int rc;
	
	if ( ssh_parameters( (char *)pv)<0 ) goto TCP_Close;
	if ( tcp(hostname, atoi(port))==-1 ) goto TCP_Close;

	sshSession = libssh2_session_init();
	while ((rc = libssh2_session_handshake(sshSession, sock)) ==
													LIBSSH2_ERROR_EAGAIN);
	if ( rc!=0 ) {
		term_Disp("sshSession failed!\r\n");
		goto sftp_Close;
	}

	if ( ssh_knownhost()<0 ) goto sftp_Close;
	comm_status=CONN_AUTHENTICATING;
	if ( ssh_authentication(username, password, passphrase)<0 ) 
		goto sftp_Close;

	if ( !(sftpSession=libssh2_sftp_init(sshSession)) ) { 
		rc = -6; goto sftp_Close; 
	}
	if ( libssh2_sftp_realpath(sftpSession, ".", realpath, 1024)<0 )
		*realpath=0;
	strcpy( homepath, realpath );

	comm_status=CONN_CONNECTED;
	comm_type=SFTP;
	tiny_Title(hostname);
	term_Parse("sftp> ", 6);
	while ( rc!=-1 ) {
		char cmd[256];
		if ( comm_Gets(cmd, FALSE)>0 ) { 
			term_Parse("\r\n", 2);
			rc = sftp_cmd(cmd);
		}
	}
	
	libssh2_sftp_shutdown(sftpSession);
	tiny_Title("");
	comm_type = 0;

sftp_Close:
	libssh2_session_disconnect(sshSession, "Normal Shutdown");
	libssh2_session_free(sshSession);
TCP_Close:
	closesocket(sock);
	comm_status=CONN_IDLE;
	dwReaderId = 0;
	return 1;
}