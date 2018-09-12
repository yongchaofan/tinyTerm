#include <winsock2.h>
#include <windows.h>
#include <libssh2.h>
#include "tiny.h"
extern BOOL bEcho;
#define TELNET 	1
#define SERIAL 	2
#define SSH		4
#define STDIO  	8

int comm_status=CONN_IDLE;
static char cCommType = 0;
static SOCKET sock;
static HANDLE mtx;	//libssh2 reading/writing mutex
static LIBSSH2_SESSION *sshSession;
static LIBSSH2_CHANNEL *sshChannel;
static HANDLE hExitEvent, hWrite;
static DWORD dwReaderId = 0;
static char keys[32];
static int cursor, bReturn=TRUE, bPassword;
DWORD WINAPI telnet( void *pv );
DWORD WINAPI serial( void *pv );
DWORD WINAPI ssh( void *pv );
DWORD WINAPI stdio( void *pv);
void stdio_Close( void );
unsigned char * telnet_Options( unsigned char *buf );

int comm_Init( void )
{
	WSADATA wsaData;
	WSAStartup(MAKEWORD(2, 2), &wsaData);
	libssh2_init (0);
	mtx = CreateMutex( NULL, FALSE, NULL ); 
	return http_Svr("127.0.0.1");
}
void comm_Open( char *port )
{
static char Port[256];

	LPTHREAD_START_ROUTINE reader = telnet;
	if ( *port=='!' ) { port++; reader = stdio; }
	if ( strnicmp(port, "ssh", 3)==0 ) reader = ssh;
	if ( strnicmp(port, "COM", 3)==0 ) reader = serial;
	if ( dwReaderId==0 ) {
		strncpy(Port, port, 255);
		comm_status=CONN_CONNECTING;
		tiny_Connecting();
		CreateThread( NULL,0,reader, Port,0,&dwReaderId );
	}
}
BOOL comm_Connected( void )
{
	return comm_status==CONN_CONNECTED;
}
int ssh_wait_socket() 
{
//	struct timeval timeout = {10,0};
	fd_set rfd, wfd;
	FD_ZERO(&rfd); FD_ZERO(&wfd);
	int dir = libssh2_session_block_directions(sshSession);
	if ( dir & LIBSSH2_SESSION_BLOCK_INBOUND ) FD_SET(sock, &rfd);;
    if ( dir & LIBSSH2_SESSION_BLOCK_OUTBOUND ) FD_SET(sock, &wfd);;
	return select(sock+1, &rfd, &wfd, NULL, NULL);
}
void comm_Send( char *buf, int len )
{
	if ( comm_status==CONN_CONNECTED ) {
		DWORD dwWrite;
		switch ( cCommType ) {
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
	if ( comm_status==CONN_AUTHENTICATING ) {
		if ( !bReturn ) switch( *buf ) {
		case '\015': keys[cursor]=0; bReturn=TRUE; break;
		case '\010':
		case '\177': if ( --cursor<0 ) cursor=0; 
					  else
						if ( !bPassword ) term_Parse("\010 \010", 3);
					  break;
		default: keys[cursor++]=*buf;
				  if ( cursor>31 ) cursor=31;
				  if ( !bPassword ) term_Parse(buf, 1);
	    }
	}
}
void comm_Size( int w, int h )
{
	if ( cCommType==SSH ) 
		libssh2_channel_request_pty_size( sshChannel, w, h);
}
void comm_Close( void )
{
	switch ( cCommType ) {
	case STDIO:  stdio_Close(); break;
	case SERIAL: SetEvent( hExitEvent ); break;
	case SSH:    libssh2_channel_send_eof( sshChannel );
	case TELNET: closesocket( sock ); break;
	}
}
void comm_Destory( void )
{
	if ( cCommType!=0 ) comm_Close( );
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
		cCommType = SERIAL;
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
		cCommType = 0;
	}
	CloseHandle(hCommPort);
	comm_status=CONN_IDLE;
	dwReaderId = 0;
	return 1;
}

/***************Telnet*******************************/
int tcp(const char *host, short port)
{
	struct sockaddr_in neaddr;
	int addrsize = sizeof(neaddr);
	LPHOSTENT lpstHost = NULL;
	sock = socket(AF_INET, SOCK_STREAM, 0);
	memset(&neaddr, 0, addrsize);
	neaddr.sin_family=AF_INET;
	neaddr.sin_port=htons(port);
	ULONG lAddr = inet_addr(host);
	if ( lAddr==INADDR_NONE ) {
		if ( (lpstHost=gethostbyname(host)) )
			lAddr = *(u_long*)lpstHost->h_addr;
		else {
			term_Disp("Couldn't resolve address\r\n");
			return -1;
		}
	}
	neaddr.sin_addr.s_addr = lAddr;

	if ( connect(sock, (struct sockaddr *)&neaddr, addrsize)==SOCKET_ERROR ) {
		term_Disp( "connection failed!\r\n" );
		return -1;
	}
	return 0;
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
	cCommType=TELNET;
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
	cCommType = 0;

socket_close:
	closesocket(sock);
	comm_status=CONN_IDLE;
	dwReaderId = 0;

	return 1;
}

/***************************SSH*******************************/
const char *keytypes[]={"unknown", "ssh-rsa", "ssh-dss", "ssh-ecdsa"};
char homedir[MAX_PATH];
DWORD WINAPI ssh( void *pv )
{
	int rc, i;
	char *subsystem = NULL;
	char *username = NULL;
	char *password = NULL;
	char *passphrase = NULL;
	char *hostname = NULL;
	char *port = "22";
	char user[32], pass[32];
	
	BOOL bAuthed = FALSE;

	char *p = (char *)pv+4;
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
		goto TCP_Close;
	}
	p = strchr(hostname, ':');
	if ( p!=NULL ) {
		*p = 0; 
		port = p+1;
	}
	if ( tcp(hostname, atoi(port))==-1 ) goto TCP_Close;

	sshSession = libssh2_session_init();
	while ((rc = libssh2_session_handshake(sshSession, sock)) ==
													LIBSSH2_ERROR_EAGAIN);
	if ( rc!=0 ) {
		term_Disp("sshSession failed!\r\n");
		goto Session_Close;
	}

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
	for(i = 0; i < 20; i++) {
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


	comm_status=CONN_AUTHENTICATING;

	if ( username==NULL ) {
		bReturn=FALSE; bPassword=FALSE; cursor=0; keys[0]=0;
		term_Parse("\r\nusername:", 11);
		tiny_Focus();
		while ( bReturn==FALSE ) Sleep(100);
		strncpy( user, keys, 31 );
		username = user;
	}
	if ( NULL==libssh2_userauth_list(sshSession, username, strlen(username)) )
		goto authenticated;
	if ( password==NULL) {	// try public key
		char pubkeyfile[MAX_PATH], privkeyfile[MAX_PATH];
		strcpy(pubkeyfile, homedir);
		strcat(pubkeyfile, ".ssh/id_rsa.pub");
		strcpy(privkeyfile, homedir);
		strcat(privkeyfile, ".ssh/id_rsa");
		bAuthed = !libssh2_userauth_publickey_fromfile(sshSession, username, 
										pubkeyfile, privkeyfile, passphrase);
		if ( bAuthed ) goto authenticated;
	}
	for ( int rep=0; rep<3; rep++ ) {			
		if ( password==NULL ) {
			bReturn=FALSE; bPassword=TRUE; cursor=0; keys[0]=0;
			term_Parse("\r\npassword:", 11);
			tiny_Focus();
			int old_cursor=0;
			for ( int i=0; i<300&&bReturn==FALSE; i++ ) { 
				if ( cursor>old_cursor ) { old_cursor=cursor; i=0; }
				Sleep(100);
			}
			strncpy( pass, keys, 31 );
			password = pass;
		}
		bAuthed = !libssh2_userauth_password(sshSession, username, password);
		if ( !bAuthed ) 
			password=NULL;
		else {
			term_Parse("\r\n", 2);
			goto authenticated;
		}
	} 
	if ( !bAuthed ) {
		term_Disp("too many tries!\r\n");
		goto Session_Close;
	}
	
authenticated:
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
	cCommType=SSH;
	tiny_Title(hostname);
	while ( libssh2_channel_eof(sshChannel) == 0 ) {
		char buf[1536];
		int cch=0;
		
		if ( WaitForSingleObject(mtx, INFINITE)==WAIT_OBJECT_0 ) {
			cch=libssh2_channel_read(sshChannel, buf, 1500);
			ReleaseMutex(mtx);
		}
		if ( cch > 0 ) {
			buf[cch]=0;
			term_Parse( buf, cch );
		}
		else {
			if ( cch==LIBSSH2_ERROR_EAGAIN ) {
				if ( ssh_wait_socket()<0 ) break;
			}
			else
				break;
		}
	}
	tiny_Title("");
	cCommType = 0;

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
/***************************STDIO*******************************/
static HANDLE Std_IN_Rd = NULL;
static HANDLE Std_IN_Wr = NULL;
static HANDLE Std_OUT_Rd = NULL;
static HANDLE Std_OUT_Wr = NULL;
static PROCESS_INFORMATION piStd; 
DWORD WINAPI stdio( void *pv)
{
	memset( &piStd, 0, sizeof(PROCESS_INFORMATION) );	//Set up PROCESS_INFORMATION 

	SECURITY_ATTRIBUTES saAttr; 
	saAttr.nLength = sizeof(SECURITY_ATTRIBUTES); 
	saAttr.bInheritHandle = TRUE;						//Set the bInheritHandle flag
	saAttr.lpSecurityDescriptor = NULL; 				//so pipe handles are inherited

	CreatePipe(&Std_OUT_Rd, &Std_OUT_Wr, &saAttr, 0);	//pipe for child's STDOUT 
	SetHandleInformation(Std_OUT_Rd, HANDLE_FLAG_INHERIT, 0);	// Ensure the read handle to the pipe for STDOUT is not inherited
	CreatePipe(&Std_IN_Rd, &Std_IN_Wr, &saAttr, 0);	//pipe for child's STDIN 
	SetHandleInformation(Std_IN_Wr, HANDLE_FLAG_INHERIT, 0);	// Ensure the write handle to the pipe for STDIN is not inherited

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
	cCommType = STDIO;
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
	cCommType = 0;

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

				if ( comm_Connected() ) do {
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
				if ( strncmp(cmd,"TL1?",4)==0 || strncmp(cmd,"CLI?",4)==0 ) {
					replen = term_TL1( cmd+4, &reply );
				} else
				if ( strncmp(cmd,"Exec?",5)==0 ) {
					reply = term_Exec( cmd+5 );
				} else
				if ( strncmp(cmd,"Send?",5)==0 ) {
					reply = term_Send( cmd+5, strlen(cmd+5) );
				} else
				if ( strncmp(cmd,"Disp?",5)==0 ) {
					reply = term_Disp( cmd+5 );
				} else
				if ( strncmp(cmd,"Recv?",5)==0 ) { 
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
