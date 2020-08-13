//
// "$Id: host.c 31308 2020-06-27 15:05:10 $"
//
// tinyTerm -- A minimal serail/telnet/ssh/sftp terminal emulator
//
// host.c is the host communication implementation
// serial communication is based on WIN32 API.
// telnet communication is based on socket API
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
#include <ws2tcpip.h>
#include <windows.h>
#include <io.h>
#include <time.h>

DWORD WINAPI stdio(void *pv);
DWORD WINAPI serial(void *pv);
DWORD WINAPI telnet(void *pv);
DWORD WINAPI ssh(void *pv);
DWORD WINAPI sftp(void *pv);
void stdio_Close(HOST *ph);

void host_Construct(HOST *ph )
{
	ph->sock = 0;
	ph->type = NONE;
	ph->status=IDLE;
	ph->homedir[0]=0;
	ssh2_Construct(ph);
}
void host_Open(HOST *ph, char *port)
{
	if (ph->status==IDLE ) 
	{
		if ( port!=NULL ) 
		{
			ph->cmdline[0] = 0;
			if ( strncmp(port, "netconf", 7)==0 )
				strcpy(ph->cmdline, "ssh -s ");
			strncat(ph->cmdline, port, 248);
			ph->cmdline[255] = 0;
		}
		port = ph->cmdline;

		LPTHREAD_START_ROUTINE reader = stdio;
		if ( strnicmp(port, "com",3)==0 ) 
			reader = serial;
		else if ( strncmp(port, "telnet", 6)==0 ) {
			reader = telnet;
		}
		else if ( strncmp(port, "ssh", 3)==0 ) {
			reader = ssh;
		}
		else if ( strncmp(port, "sftp", 4)==0 ){
			reader = sftp;
		}

		CreateThread(NULL, 0, reader, ph, 0, NULL);
	}
	else {
		if ( strnicmp(port, "disconn", 7)==0 ) host_Close(ph);
	}
}
void host_Send(HOST *ph, char *buf, int len)
{
	DWORD dwWrite;
	switch (ph->type ) {
	case STDIO: if ( *buf==3 && len==1 ) //CTRL+C
					stdio_Close(ph);
				else
					WriteFile(ph->hStdioWrite, buf, len, &dwWrite, NULL); 
				break;
	case SERIAL:WriteFile(ph->hSerial, buf, len, &dwWrite, NULL); break;
	case TELNET:if (ph->sock>0 ) send(ph->sock, buf, len, 0); break;
	case SSH:
	case SFTP:
	case NETCONF: ssh2_Send(ph, buf, len); break;
	default:	if (ph->status!=IDLE ) ssh2_Send(ph, buf, len);
	}
}
void host_Send_Size(HOST *ph, int w, int h)
{
	if (ph->type==SSH ) ssh2_Size(ph, w, h);
}
int host_Status(HOST *ph )
{
	return ph->status;
}
int host_Type(HOST *ph)
{
	return ph->type;
}
void host_Close(HOST *ph)
{
	switch (ph->type ) {
	case STDIO:	stdio_Close(ph); break;
	case SERIAL:SetEvent(ph->hExitEvent); break;
	case SFTP:	ssh2_Send(ph, "\r",1);
				ssh2_Send(ph, "bye\r",4); break;
	case SSH:
	case NETCONF:ssh2_Close(ph); break;
	case TELNET: closesocket(ph->sock); break;
	default: if (ph->sock!=0) closesocket(ph->sock);
	}
}
void host_Destory()
{
}

/***************************Serial*****************************/
const char SOH = 0x01;
const char STX = 0x02;
const char EOT = 0x04;
const char ACK = 0x06;
const char NAK = 0x15;
static unsigned char xmodem_buf[133];
static unsigned char xmodem_blk;
static int xmodem_timeout;
static FILE *xmodem_fp;
static BOOL bXmodem, xmodem_crc, xmodem_started;
void block_crc()
{
	unsigned short crc = 0;
	for ( int i=3; i<131; i++ ) {
		crc = crc ^ xmodem_buf[i] << 8;
		for ( int j=0; j<8; j++ ) {
			if (crc & 0x8000)
				crc = crc << 1 ^ 0x1021;
			else
				crc = crc << 1;
		}
	}
	xmodem_buf[131] = (crc>>8) & 0xff;
	xmodem_buf[132] = crc & 0xff;
 }
void xmodem_block(HOST *ph)
{
	xmodem_buf[0] = SOH;
	xmodem_buf[1] = ++xmodem_blk;
	xmodem_buf[2] = 255-xmodem_blk;
	int cnt = fread( xmodem_buf+3, 1, 128, xmodem_fp);
	if ( cnt <= 0 ) {
		xmodem_buf[0] = EOT;
		fclose(xmodem_fp);
	}
	if ( cnt>0 && cnt<128 ) 
		for ( int i=cnt+3; i<131; i++ ) xmodem_buf[i] = 0;
	if ( xmodem_crc ) {
		block_crc();
	}
	else {
		unsigned char chksum = 0;
		for ( int i=3; i<131; i++ ) chksum += xmodem_buf[i];
		xmodem_buf[131] = chksum;
	}
}
void xmodem_send(HOST *ph)
{
	xmodem_started = TRUE;
	if ( xmodem_buf[0]==EOT )
		host_Send(ph, (char *)xmodem_buf, 1);
	else
		host_Send(ph, (char *)xmodem_buf, xmodem_crc?133:132);
}
void xmodem_recv(HOST *ph, char op)
{
	switch( op ) {
	case 0:		//nothing received,resend every 10 seconds
				if ( ++xmodem_timeout%10000==0 && xmodem_started ) {
					xmodem_send(ph);
					term_Disp(ph->term, "R");
				}
				if ( xmodem_timeout>60000 ) {	//timeout after 60 seconds
					bXmodem = FALSE;
					fclose(xmodem_fp);
					term_Disp(ph->term, "Aborted\r\n");
				}
				break;
	case 0x06:	xmodem_timeout = 0;				//ACK
				if ( xmodem_buf[0] == EOT ) {
					term_Disp(ph->term, "Completed\r\n");
					bXmodem = FALSE;
					return;
				}
				xmodem_block(ph);
				xmodem_send(ph); 
				if ( xmodem_blk==0 ) term_Disp(ph->term, ".");
				break;
	case 0x15:	term_Disp(ph->term, "N");		//NAK
				xmodem_send(ph);
				break;
	case 'C':	term_Disp(ph->term, "CRC");		//start CRC
				xmodem_crc = TRUE; 
				block_crc();
				xmodem_send(ph);
				break;
	}
}
void xmodem_init(HOST *ph, FILE *fp)
{
	xmodem_fp = fp;
	bXmodem = TRUE;
	xmodem_crc = FALSE;
	xmodem_started = FALSE;
	xmodem_timeout = 0;
	xmodem_blk = 0;
	xmodem_block(ph);
	term_Disp(ph->term, "xmodem");
}
DWORD WINAPI serial(void *pv)
{
	HOST *ph = (HOST *)pv;
	char port[256] = "\\\\.\\";
	strcat( port, ph->cmdline);

	char *p = strchr( port, ':');
	if ( p!=NULL ) *p++ = 0;
	if ( p==NULL || *p==0 ) p = "9600,n,8,1";

	ph->hSerial = CreateFileA( port, GENERIC_READ|GENERIC_WRITE,
								0, NULL, OPEN_EXISTING, 0, NULL);
	if (ph->hSerial==INVALID_HANDLE_VALUE ) {
		term_Disp(ph->term, "Couldn't open serial port\r\n");
		goto comm_close;
	}
	COMMTIMEOUTS timeouts = { 1, 0, 1, 0, 0 };
							//ReadIntervalTimeout = 1
							//ReadTotalTimeoutMultiplier = 0
							//ReadTotalTimeoutConstant = 1
							//WriteTotalTimeoutMultiplier = 0
							//WriteTotalTimeoutConstant = 0
	if ( SetCommTimeouts(ph->hSerial, &timeouts)==0 ) {
		term_Disp(ph->term, "couldn't set serial port timeout\r\n");
		CloseHandle(ph->hSerial);
		goto comm_close;
	}
	SetupComm(ph->hSerial, 4096, 1024);	//comm buffer sizes

	DCB dcb;							// comm port settings
	memset(&dcb, 0, sizeof(dcb));
	dcb.DCBlength = sizeof(dcb);
	BuildCommDCBA(p, &dcb);
	if ( SetCommState(ph->hSerial, &dcb )==0 ) {
		term_Disp(ph->term, "Invalid serial port settings\r\n");
		CloseHandle(ph->hSerial);
		goto comm_close;
	}

	ph->hostname = ph->cmdline;
	ph->type = SERIAL;
	ph->status=CONNECTED;
	bXmodem = FALSE;
	term_Title(ph->term, ph->cmdline);
	term_Disp(ph->term, "connected\r\n");
	ph->hExitEvent = CreateEventA( NULL, TRUE, FALSE, "COM exit");
	while ( WaitForSingleObject(ph->hExitEvent, 0) == WAIT_TIMEOUT ) {
		char buf[256];
		DWORD dwCCH;
		if ( ReadFile(ph->hSerial, buf, 256, &dwCCH, NULL ) ) {
			if ( bXmodem ) { 
				char op = 0;
				if ( dwCCH>0 ) op = buf[dwCCH-1];
				xmodem_recv(ph, op);
				continue;
			}
			if ( dwCCH > 0 )
				term_Parse(ph->term, buf, dwCCH);
			else
				Sleep(1);//give WriteFile a chance to complete
		}
		else
			if ( !ClearCommError(ph->hSerial, NULL, NULL) ) break;
	}
	term_Error(ph->term, "Disconnected");
	CloseHandle(ph->hExitEvent);
	CloseHandle(ph->hSerial);
	ph->type = NONE;
	ph->status=IDLE;

comm_close:
	term_Title(ph->term, "");
	return 1;
}

/***************Telnet*******************************/
int host_tcp(HOST *ph )
{
	int rc = 0;
	struct addrinfo *ainfo;
	if ( getaddrinfo(ph->hostname, NULL, NULL, &ainfo)!=0 ) {
		term_Disp(ph->term, "invalid ip address or hostname\r\n");
		return -1;
	}
	((struct sockaddr_in *)(ainfo->ai_addr))->sin_port = htons(ph->port);

	ph->sock= socket(ainfo->ai_family, SOCK_STREAM, 0);
	term_Disp(ph->term, "Trying...");
	if ( connect(ph->sock, ainfo->ai_addr, ainfo->ai_addrlen)==SOCKET_ERROR ) {
		switch ( WSAGetLastError() ) {
		case WSAEHOSTUNREACH:
		case WSAENETUNREACH: 
			term_Error(ph->term, "host unreachable"); break;
		case WSAECONNRESET:  
			term_Error(ph->term, "connection reset"); break;
		case WSAETIMEDOUT:   
			term_Error(ph->term, "connection timeout");break;
		case WSAECONNREFUSED:
			term_Error(ph->term, "connection refused");break;
		default: 
			term_Error(ph->term, "connection failure");
		}
		closesocket(ph->sock);
		ph->sock = 0;
		rc = -1;
	}
	freeaddrinfo(ainfo);

	return rc;
}
DWORD WINAPI telnet(void *pv)
{
	HOST *ph = (HOST *)pv;
	char port[256];
	strcpy( port, ph->cmdline+7);
	ph->hostname = port;
	ph->port = 23;
	char *p=strchr(port, ':');
	char *q=strrchr(port, ':');
	if ( p!=NULL && p==q ) {
		*p++=0; 
		ph->port=atoi(p);
	}

	ph->status=CONNECTING;
	term_Title(ph->term, ph->hostname);
	if ( host_tcp(ph)!=-1 ) 
	{
		ph->type=TELNET;
		ph->status=CONNECTED;
		term_Disp(ph->term, "connected\r\n");

		char buf[4096];
		int cnt;
		while ( (cnt=recv(ph->sock, buf, 4096, 0)) > 0 ) {
			term_Parse(ph->term, buf, cnt);
		}
		closesocket(ph->sock);
		ph->sock = 0;

		ph->type = NONE;
		term_Error(ph->term, "Disconnected");
	}

	ph->status=IDLE;
	term_Title(ph->term, "");
	return 1;
}

/***************************STDIO*******************************/
static PROCESS_INFORMATION piStd;
DWORD WINAPI stdio( void *pv)
{
	HOST *ph = (HOST *)pv;
	HANDLE Stdin_Rd, Stdin_Wr ;
	HANDLE Stdout_Rd, Stdout_Wr, Stderr_Wr;
	memset(&piStd, 0, sizeof(PROCESS_INFORMATION));

	SECURITY_ATTRIBUTES saAttr;
	saAttr.nLength = sizeof(SECURITY_ATTRIBUTES);
	saAttr.bInheritHandle = TRUE;			//Set the bInheritHandle flag
	saAttr.lpSecurityDescriptor = NULL;		//so pipe handles are inherited

	CreatePipe(&Stdout_Rd, &Stdout_Wr, &saAttr, 0);//pipe for child's STDOUT
	SetHandleInformation(Stdout_Rd, HANDLE_FLAG_INHERIT, 0);
	// Ensure the read handle to the pipe for STDOUT is not inherited
	CreatePipe(&Stdin_Rd, &Stdin_Wr, &saAttr, 0);	//pipe for child's STDIN
	SetHandleInformation(Stdin_Wr, HANDLE_FLAG_INHERIT, 0);
	// Ensure the write handle to the pipe for STDIN is not inherited
	DuplicateHandle(GetCurrentProcess(),Stdout_Wr,
					GetCurrentProcess(),&Stderr_Wr,0,
					TRUE,DUPLICATE_SAME_ACCESS);
	DuplicateHandle(GetCurrentProcess(),Stdout_Rd,
					GetCurrentProcess(),&ph->hStdioRead,0,
					TRUE,DUPLICATE_SAME_ACCESS);
	DuplicateHandle(GetCurrentProcess(),Stdin_Wr,
					GetCurrentProcess(),&ph->hStdioWrite,0,
					TRUE,DUPLICATE_SAME_ACCESS);
	CloseHandle(Stdin_Wr);
	CloseHandle(Stdout_Rd);

	struct _STARTUPINFOA siStartInfo;
	memset(&siStartInfo, 0, sizeof(STARTUPINFO));	// Set STARTUPINFO
	siStartInfo.cb = sizeof(STARTUPINFO);
	siStartInfo.hStdError = Stderr_Wr;
	siStartInfo.hStdOutput = Stdout_Wr;
	siStartInfo.hStdInput = Stdin_Rd;
	siStartInfo.dwFlags |= STARTF_USESTDHANDLES;

	if ( CreateProcessA(NULL,			// Create the child process.
						ph->cmdline,	// command line
						NULL,			// process security attributes
						NULL,			// primary thread security attributes
						TRUE,			// handles are inherited
						CREATE_NO_WINDOW,// creation flags
						NULL,			// use parent's environment
						NULL,			// use parent's current directory
						&siStartInfo,	// STARTUPINFO pointer
						&piStd) ) {		// receives PROCESS_INFORMATION
		CloseHandle(Stdin_Rd);
		CloseHandle(Stdout_Wr);
		CloseHandle(Stderr_Wr);

		ph->type = STDIO;
		ph->status=CONNECTED;
		while ( TRUE ) {
			DWORD dwCCH;
			char buf[4096];
			if ( ReadFile(ph->hStdioRead, buf, 4096, &dwCCH, NULL) > 0 ) {
				if ( dwCCH > 0 )
					term_Parse(ph->term, buf, dwCCH);
				else
					Sleep(1);
			}
			else
				break;
		}
		ph->status=IDLE;
		ph->type = NONE;
	}
	else
		term_Disp(ph->term, "Invalid command\r\n");

	CloseHandle(ph->hStdioRead);
	CloseHandle(ph->hStdioWrite);
	term_Title(ph->term, "");
	return 1;
}
void stdio_Close(HOST *ph)
{
	if ( WaitForSingleObject(piStd.hProcess, 100)==WAIT_TIMEOUT )
		TerminateProcess(piStd.hProcess,0);
	CloseHandle(piStd.hThread);
	CloseHandle(piStd.hProcess);
}

/**********************************HTTPd*******************************/
extern TERM *pt;
const char *RFC1123FMT="%a, %d %b %Y %H:%M:%S GMT";
const char *exts[]={".txt",
					".htm", ".html",
					".js",
					".jpg", ".jpeg",
					".png",
					".css"
					};
const char *mime[]={"text/plain",
					"text/html", "text/html",
					"text/javascript",
					"image/jpeg", "image/jpeg",
					"image/png",
					"text/css"
					};
const char HEADER[]="HTTP/1.1 200 Ok\nServer: tinyTerm\n\
Access-Control-Allow-Origin: *\nContent-Type: text/plain\n\
Cache-Control: no-cache\nContent-length: %d\n\n";

void httpFile( int s1, char *file)
{
	char reply[4096], timebuf[128];
	int len, i, j;
	time_t now;

	now = time(NULL);
	strftime(timebuf, sizeof(timebuf), RFC1123FMT, gmtime( &now ));

    struct stat sb;
	if ( stat( file, &sb )==-1 || strstr(file, ".." )!=NULL ) {
		len=sprintf(reply, "HTTP/1.1 404 not found\nDate: %s\n", timebuf);
		len+=sprintf(reply+len, "Server: tinyTerm\nConnection: close\n");
	    len+=sprintf(reply+len, "Content-Type: text/html\nContent-Length: 14");
	    len+=sprintf(reply+len, "\n\nfile not found");
		send(s1, reply, len, 0);
		return;
	}

	FILE *fp = fopen( file, "rb");	
	if ( fp!=NULL ) {
		len=sprintf(reply, "HTTP/1.1 200 Ok\nDate: %s\n", timebuf);
		len+=sprintf(reply+len, "Server: tinyTerm\nConnection: close\n");
		
		const char *filext=file+strlen(file)-1;
		while ( *filext!='.' ) filext--;
		for ( i=0, j=0; j<8; j++ ) if ( strcmp(filext, exts[j])==0 ) i=j;
		len+=sprintf(reply+len, "Content-Type: %s\n", mime[i]);

		long filesize = sb.st_size;
		len+=sprintf(reply+len, "Content-Length: %ld\n", filesize);
		strftime(timebuf, sizeof(timebuf), RFC1123FMT, gmtime( &sb.st_mtime ));
		len+=sprintf(reply+len, "Last-Modified: %s\n\n", timebuf);

		send(s1, reply, len, 0);
		while ( (len=fread(reply, 1, 4096, fp))>0 )
			if ( send(s1, reply, len, 0)==-1)	break;
		fclose(fp);
	}
}
int url_decode(char *url)
{
	char *p = url, *q = url;
	while ( *p ) {
		if ( *p=='%' && isdigit(p[1]) ) {
			int a;
			sscanf( ++p, "%02x", &a);
			*(++p) = a;
		}
		*q++ = *p++;
	}
	*q = 0;
	return q-url;
}

DWORD WINAPI httpd( void *pv )
{
	char buf[4096], *cmd, *reply;
	struct sockaddr_in cltaddr;
	int addrsize=sizeof(cltaddr), cmdlen, replen;
	SOCKET http_s0 = *(SOCKET *)pv;

	while ( TRUE ) {
		SOCKET http_s1 = accept(http_s0, (struct sockaddr*)&cltaddr, &addrsize);
		if ( http_s1 == INVALID_SOCKET ) break;
		cmdlen=recv(http_s1, buf, 4095, 0);
		if ( cmdlen>0 ) {
			if ( strncmp(buf, "GET /", 5)!=0 ) {//TCP connection
				term_Send(pt, buf, cmdlen);
				do {
					if ( host_Status(pt->host)==CONNECTED ) {
						replen = term_Recv(pt,  &reply);
						if ( replen > 0 ) send(http_s1, reply, replen, 0);
					}
					struct timeval tv = { 0, 100 }; //tv_sec=0, tv_usec=100
					FD_SET readset;
					FD_ZERO(&readset);
					FD_SET(http_s1, &readset);
					if ( select(1, &readset, NULL, NULL, &tv)>0 ) {
						cmdlen = recv(http_s1, buf, 4095, 0);
						if ( cmdlen>0 ) 
							term_Send(pt, buf, cmdlen);
					}
				} while (cmdlen>0);
			}
			else {								//HTTP connection
				do {
					buf[cmdlen] = 0;
					cmd = buf+5;
					char *p=strchr(cmd, ' ');
					if ( p!=NULL ) *p = 0;
					url_decode(cmd);

					if ( *cmd=='?' ) {	//get CGI, cmd+1 points to command
						replen = term_Cmd(pt,  cmd+1, &reply);
						int len = sprintf(buf, HEADER, replen);
						send(http_s1, buf, len, 0);
						if ( replen>0 ) send(http_s1, reply, replen, 0);
					}
					else {				//get file, cmd points to filename
						httpFile(http_s1, cmd);
					}
					cmdlen = recv(http_s1, buf, 4095, 0);
				} while ( cmdlen>0);
			}
		}
		shutdown(http_s1, SD_SEND);
		closesocket(http_s1);
	}
	return 0;
}
int http_Svr(char *intf)
{
static SOCKET http_s0=INVALID_SOCKET;

	if ( http_s0 !=INVALID_SOCKET ) {
		closesocket(http_s0);
		http_s0 = INVALID_SOCKET;
		return 0;
	}

	http_s0 = socket(AF_INET, SOCK_STREAM, 0);
	if ( http_s0==INVALID_SOCKET ) return 0;

	struct sockaddr_in svraddr;
	int addrsize=sizeof(svraddr);
	memset(&svraddr, 0, addrsize);
	svraddr.sin_family=AF_INET;
	svraddr.sin_addr.s_addr=inet_addr(intf);
	int p;
	for ( p=8080; p<8099; p++ ) {
		svraddr.sin_port=htons(p);
		if ( bind(http_s0, (struct sockaddr*)&svraddr, addrsize)!=SOCKET_ERROR ) {
			if ( listen(http_s0, 1)!=SOCKET_ERROR){
				DWORD dwThreadId;
				CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)httpd,
											&http_s0, 0, &dwThreadId);
				return p;
			}
		}
	}
	closesocket(http_s0);
	http_s0 = INVALID_SOCKET;
	return 0;
}
/**********************************FTPd*******************************/
static SOCKET ftp_s0 = INVALID_SOCKET;
static SOCKET ftp_s1 = INVALID_SOCKET;
int sock_select( SOCKET s, int secs )
{
	struct timeval tv = { 0, 0 };
	tv.tv_sec = secs;
	FD_SET svrset;
	FD_ZERO(&svrset);
	FD_SET(s, &svrset);
	return select(1, &svrset, NULL, NULL, &tv);
}
void sock_send(char *reply)
{
	send(ftp_s1, reply, strlen(reply), 0);
}
BOOL is_directory(char *root)
{
	struct stat sb;
	if ( stat(root, &sb)==-1 ) {
		term_Print(pt, "FTPD: %s doesn't exist\n", root);
		return FALSE;
	}
	if ( (sb.st_mode&S_IFMT)!=S_IFDIR ) {
		term_Print(pt, "FTPD: %s is not a directory\n", root);
		return FALSE;
	}
	return TRUE;
}
DWORD WINAPI ftpd(LPVOID p)
{
	unsigned long  dwIp;
	unsigned short wPort;
	unsigned int c[6], ii[2];
	char *param=0, szBuf[32768];
	char fn[MAX_PATH], workDir[MAX_PATH], rootDir[MAX_PATH];
	BOOL bPassive;

	SOCKET s2=-1, s3=-1; 		// s2 data connection, s3 data listen
	struct sockaddr_in svraddr, clientaddr;		// for data connection
	int addrsize=sizeof(clientaddr);

	strcpy( rootDir, (char*)p);
	term_Print(pt,  "FTPd started at %s\r\n", rootDir);

	int ret0, ret1;
	while( (ret0=sock_select(ftp_s0, 900)) == 1 ) {
		strcpy(workDir, "\\");;
		ftp_s1 = accept( ftp_s0, (struct sockaddr*)&clientaddr, &addrsize);
		if ( ftp_s1 ==INVALID_SOCKET ) continue;

		sock_send("220 Welcome\n");
		getpeername(ftp_s1, (struct sockaddr *)&clientaddr, &addrsize);
		term_Print(pt, "FTPd: connected from %s\n",
						inet_ntoa(clientaddr.sin_addr));

		FILE *fp;
		bPassive = FALSE;
		BOOL bUser=FALSE, bPass=FALSE;
		while ( (ret1=sock_select(ftp_s1, 300)) == 1 ) {
			int cnt=recv(ftp_s1, szBuf, 1024, 0);
			if ( cnt<=0 ) {
				term_Disp(pt,  "FTPd: client disconnected\n");
				break;
			}
			szBuf[cnt--]=0;
			term_Disp(pt, szBuf);
			while (szBuf[cnt]=='\r' || szBuf[cnt]=='\n' ) szBuf[cnt--]=0;
			if ( (param=strchr(szBuf, ' '))!=NULL )
				*param++=0;
			else
				param = szBuf+cnt+1;

			// *** Process FTP commands ***
			if (stricmp("user", szBuf) == 0){
				sock_send("331 Password required\n");
				bUser = strncmp(param, "tiny", 4 )==0 ? TRUE : FALSE;
				continue;
			}
			if (stricmp("pass", szBuf) == 0){
				bPass = bUser && strncmp(param, "term", 4)==0;
				sock_send(bPass ?  "230 Logged in okay\n":
									"530 Login incorrect\n");
				continue;
			}
			if ( !bPass ) {
				sock_send("530 Login required\n");
				continue;
			}
			strcpy(fn, rootDir);
			strcat(fn, workDir);
			if ( workDir[strlen(workDir)-1]!='\\' ) strcat(fn, "\\");
			strcat(fn, param);

			if (stricmp("syst", szBuf) ==0 ){
				sock_send("215 tinyTerm ftpd\n");
			}
			else if(stricmp("port", szBuf) == 0){
				sscanf(param, "%d,%d,%d,%d,%d,%d",
							&c[0], &c[1], &c[2], &c[3], &c[4], &c[5]);
				ii[0] = c[1]+(c[0]<<8);
				ii[1] = c[3]+(c[2]<<8);
				wPort = c[5]+(c[4]<<8);
				dwIp = ii[1]+(ii[0]<<16);
				clientaddr.sin_addr.s_addr = htonl(dwIp);
				clientaddr.sin_port=htons(wPort);
				sock_send("200 PORT command successful\n");
			}
			else if(stricmp("type", szBuf) == 0){
				sock_send("200 Type can only be binary\n");
			}
			else if(stricmp("pwd", szBuf) == 0 || stricmp("xpwd", szBuf) == 0){
				sprintf( szBuf, "257 \"%s\" is current directory\n", workDir);
				sock_send(szBuf);
			}
			else if(stricmp("cwd", szBuf) == 0){
				char *pPart, fullDir[MAX_PATH];
				if ( GetFullPathNameA(fn, MAX_PATH, fullDir, &pPart)==0 ) {
					sock_send("550 Invalid path\n");
					continue;
				}
				if ( strncmp(rootDir, fullDir, strlen(rootDir))!=0 ) {
					sock_send("550 Invalid path\n");
					continue;
				}
				if ( !is_directory( fullDir ) ) {
					sock_send("550 No such directory\n");
					continue;
				}
				sock_send("250 CWD sucessful\n");
				strcpy(workDir, fullDir+strlen(rootDir));
			}
			else if(stricmp("cdup", szBuf) == 0) {
				char *p = strrchr(workDir, '\\');
				if( p!=NULL && p!=workDir ) {
					*p = 0;
					sock_send("250 CWD sucessful\n");
				}
				else
					sock_send("550 Invalid path\n");
			}
			else if(stricmp("pasv", szBuf)==0 || stricmp("epsv", szBuf)==0 ){
				getsockname(ftp_s1, (struct sockaddr *)&svraddr, &addrsize);
				svraddr.sin_port = 0;
				s3 = socket(AF_INET, SOCK_STREAM, 0);
				bind(s3, (struct sockaddr *)&svraddr, addrsize);
				listen(s3, 1);

				getsockname(s3, (struct sockaddr *)&svraddr, &addrsize);
				dwIp = svraddr.sin_addr.s_addr;
				wPort = svraddr.sin_port;

				if ( *szBuf=='p' || *szBuf=='P'  ) {
					ii[1]=HIWORD(dwIp); ii[0]=LOWORD(dwIp);
					c[1]=HIBYTE(ii[0]); c[0]=LOBYTE(ii[0]);
					c[3]=HIBYTE(ii[1]); c[2]=LOBYTE(ii[1]);
					c[5]=HIBYTE(wPort); c[4]=LOBYTE(wPort);
					sprintf(szBuf, "227 PASV Mode (%d,%d,%d,%d,%d,%d)\n",
										c[0], c[1], c[2], c[3], c[4], c[5]);
				}
				else sprintf(szBuf, "229 EPSV Mode (|||%d|)\n", ntohs(wPort));
				sock_send(szBuf);
				bPassive=TRUE;
			}
			else if( stricmp("nlst", szBuf)==0 || stricmp("list", szBuf)==0 ){
				struct _finddata_t  ffblk;
				if ( *(param+strlen(param)-1)=='/') strcat(fn, "*.*");
				if ( *param==0 ) strcat(fn, "\\*.*");
				intptr_t hFile = _findfirst(fn, &ffblk);
				if ( hFile==-1 ) {
					sock_send("550 No such file or directory\n");
					continue;
				}
				sock_send("150 Opening ASCII connection for list\n");
				if ( bPassive ) {
					s2 = accept(s3, (struct sockaddr*)&clientaddr, &addrsize);
				}
				else {
					s2 = socket(AF_INET, SOCK_STREAM, 0);
					connect(s2, (struct sockaddr*)&clientaddr,
											sizeof(clientaddr));
				}
				BOOL bNlst = szBuf[0]=='n' || szBuf[0]=='N';
				do {
					if ( ffblk.name[0]=='.' ) continue;
					if ( bNlst ) {
						sprintf(szBuf, "%s\r\n", ffblk.name);
					}
					else {
						char buf[256];
						strcpy(buf, ctime(&ffblk.time_write));
						buf[24]=0;
						if ( ffblk.attrib&_A_SUBDIR )
							sprintf(szBuf, "%s %s %s\r\n", buf+4,
										"       <DIR>", ffblk.name);
						else
							sprintf(szBuf, "%s % 12ld %s\r\n", buf+4,
											ffblk.size, ffblk.name);
					}
					send(s2, szBuf, strlen(szBuf), 0);
				} while ( _findnext(hFile, &ffblk)==0);
				_findclose(hFile);
				sock_send("226 Transfer complete.\n");
				closesocket(s2);
			}
			else if(stricmp("stor", szBuf) == 0){
				fp = NULL;
				if ( strstr(param, ".." )==NULL )
					fp = fopen_utf8(fn, "wb");
				if(fp == NULL){
					sock_send("550 Unable write file\n");
					continue;
				}
				sock_send("150 Opening BINARY data connection\n");
				if ( bPassive ) {
					s2 = accept(s3, (struct sockaddr*)&clientaddr, &addrsize);
				}
				else {
					s2 = socket(AF_INET, SOCK_STREAM, 0);
					connect(s2, (struct sockaddr *)&clientaddr,
											sizeof(clientaddr));
				}
				unsigned long  lSize = 0;
				unsigned int   nLen=0, nCnt=0;
				do {
					nLen = recv(s2, szBuf, 32768, 0);
					if ( nLen>0 ) {
						lSize += nLen;
						fwrite(szBuf, nLen, 1, fp);
					}
					if ( ++nCnt==256 ) {
						term_Print(pt, "\r%lu bytes received", lSize);
						nCnt = 0;
					}
				}while ( nLen!=0);
				fclose(fp);
				term_Print(pt, "\r%lu bytes received\n", lSize);
				sock_send("226 Transfer complete\n");
				closesocket(s2);
			}
			else if(stricmp("retr", szBuf) == 0){
				fp = NULL;
				if ( strstr(param, ".." )==NULL )
					fp = fopen_utf8(fn, "rb");
				if(fp == NULL) {
					sock_send("550 Unable to read file\n");
					continue;
				}
				sock_send("150 Opening BINARY data connection\n");
				if ( bPassive ) {
					s2 = accept(s3, (struct sockaddr*)&clientaddr, &addrsize);
				}
				else {
					s2 = socket(AF_INET, SOCK_STREAM, 0);
					connect(s2, (struct sockaddr *)&clientaddr,
											sizeof(clientaddr));
				}
				unsigned long  lSize = 0;
				unsigned int   nLen=0, nCnt=0;
				do {
					nLen = fread(szBuf, 1, 32768, fp);
					if ( send(s2, szBuf, nLen, 0) == 0) break;
					lSize += nLen;
					if ( ++nCnt==32 ) {
						term_Print(pt, "\r%lu bytes sent", lSize);
						nCnt = 0;
					}
				}
				while ( nLen==32768);
				fclose(fp);
				term_Print(pt, "\r%lu bytes sent\n", lSize);
				sock_send("226 Transfer complete\n");
				closesocket(s2);
			}
			else if(stricmp("size", szBuf) == 0){
				struct _finddata_t  ffblk;
				intptr_t  hFile = _findfirst(fn, &ffblk);
				if ( hFile!=-1 )
				{
					sprintf(szBuf, "213 %lu\n", ffblk.size);
					sock_send(szBuf);
					_findclose(hFile);
				}
				else
					sock_send("550 No such file or directory\n");
			}
			else if(stricmp("mdtm", szBuf) == 0) {
				struct _finddata_t ffblk;
				intptr_t hFile = _findfirst(fn, &ffblk);
				if ( hFile!=-1 )
				{
					struct tm *t_mod = localtime( &ffblk.time_write);
					sprintf(szBuf, "213 %4d%02d%02d%02d%02d%02d\n",
							t_mod->tm_year+1900, t_mod->tm_mon+1,
							t_mod->tm_mday, t_mod->tm_hour,
							t_mod->tm_min, t_mod->tm_sec);
					sock_send(szBuf);
					_findclose(hFile);
				}
				else
					 sock_send("550 No such file or directory\n");
			}
			else if(stricmp("quit", szBuf) == 0){
				sock_send("221 Bye!\n");
				break;
			}
			else {
				sock_send("500 Command not supported\n");
			}
		}
		if( ret1 == 0 ) {
			sock_send("500 Timeout\n");
			term_Disp(pt,  "FTPd: client timed out\n");
		}
		closesocket(ftp_s1);
	}
	term_Disp(pt,  ret0==0? "FTPd timed out\n" : "FTPd stopped\n");
	closesocket(ftp_s0);
	ftp_s0 = INVALID_SOCKET;
	ftpd_quit();
	return 0;
}
BOOL ftp_Svr(char *root)
{
	if ( root==NULL ) {
		if ( ftp_s0 != INVALID_SOCKET ) {
			closesocket(ftp_s1);
			closesocket(ftp_s0);
			ftp_s0 = INVALID_SOCKET;
		}
	}
	else {
		if ( is_directory( root ) ) {
			struct sockaddr_in serveraddr;
			int addrsize=sizeof(serveraddr);
			ftp_s0 = socket(AF_INET, SOCK_STREAM, 0);
			memset(&serveraddr, 0, addrsize);
			serveraddr.sin_family=AF_INET;
			serveraddr.sin_addr.s_addr=ADDR_ANY;
			serveraddr.sin_port=htons(21);
			if ( bind(ftp_s0, (struct sockaddr*)&serveraddr,
									addrsize) != SOCKET_ERROR ) {
				if ( listen(ftp_s0, 1) != SOCKET_ERROR ) {
					DWORD dwId;
					CreateThread( NULL, 0, ftpd, (LPVOID)root, 0, &dwId);
					return TRUE;
				}
			}
			else
				term_Disp(pt, "FTPD: Couldn't bind to TCP port 21\n");
		}
	}
	return FALSE;
}

static SOCKET tftp_s0=INVALID_SOCKET, tftp_s1;
void tftp_Read( FILE *fp )
{
	char dataBuf[516], ackBuf[516];
	unsigned short nCnt, nRetry=0;
	int nLen, len;

	nCnt=1;
	len = nLen = fread(dataBuf+4, 1, 512 , fp);
	do {
		dataBuf[0]=0; dataBuf[1]=3;
		dataBuf[2]=(nCnt>>8)&0xff; dataBuf[3]=nCnt&0xff;
		send(tftp_s1, dataBuf, nLen+4, 0);
		if ( nCnt%512==0 ) term_Disp(pt, "#");
		if ( sock_select( tftp_s1, 5 ) == 1 ) {
			if ( recv(tftp_s1, ackBuf, 516, 0)==SOCKET_ERROR ) break;
			if ( ackBuf[1]==4 && ackBuf[2]==dataBuf[2] &&
								 ackBuf[3]==dataBuf[3]) {
				nRetry=0;
				nCnt++;
				len = nLen;
				nLen = fread(dataBuf+4, 1, 512 , fp);
			}
			else if ( ++nRetry==5 ) break;
		}
		else if ( ++nRetry==5 ) break;
	} while ( len==512);
	if ( nCnt>=512 ) term_Disp(pt, "\n");
}
void tftp_Write( FILE *fp )
{
	char dataBuf[516], ackBuf[516];
	unsigned short ntmp, nCnt=0, nRetry=0;
	int nLen=516;
	while ( nLen > 0 ) {
		ackBuf[0]=0; ackBuf[1]=4;
		ackBuf[2]=(nCnt>>8)&0xff; ackBuf[3]=nCnt&0xff;
		send(tftp_s1, ackBuf, 4, 0);
		if ( nLen!=516 ) break;
		if ( sock_select( tftp_s1, 5) == 1 ) {
			nLen = recv(tftp_s1, dataBuf, 516, 0);
			if ( nLen == SOCKET_ERROR ) break;
			ntmp=(unsigned char)dataBuf[2];
			ntmp=(ntmp<<8)+(unsigned char)dataBuf[3];
			if ( dataBuf[1]==3 && ntmp==nCnt+1 ) {
				fwrite(dataBuf+4, 1, nLen-4, fp);
				nRetry=0;
				nCnt++;
				if ( nCnt%512==0 ) term_Disp(pt, "#");
			}
			else if ( ++nRetry==5 ) break;
		}
		else if ( ++nRetry==5 ) break;
	}
	if ( nCnt>=512 ) term_Disp(pt, "\n");
}
DWORD WINAPI tftpd(LPVOID p)
{
	char dataBuf[516];
	struct sockaddr_in clientaddr;
	int addrsize=sizeof(clientaddr);

	char rootDir[MAX_PATH], fn[MAX_PATH];
	strcpy( rootDir, (char *)p);
	term_Print(pt,  "TFTPd started at %s\r\n", rootDir);
	strcat(rootDir, "\\");

	int ret;
	while ( (ret=sock_select( tftp_s0, 300 )) == 1 ) {
		ret = recvfrom(tftp_s0, dataBuf, 516, 0,
						(struct sockaddr *)&clientaddr, &addrsize);
		if ( ret==SOCKET_ERROR ) break;
		connect(tftp_s1, (struct sockaddr *)&clientaddr, addrsize);
		if ( dataBuf[1]==1  || dataBuf[1]==2 ) {
			BOOL bRead = dataBuf[1]==1;
			term_Print(pt, "TFTPd: %cRQ %s from %s\n", bRead?'R':'W',
							dataBuf+2, inet_ntoa(clientaddr.sin_addr));
			strcpy(fn, rootDir);
			strcat(fn, dataBuf+2);
			FILE *fp = NULL;
			if ( strstr(fn, "..")==NULL ) 
				fp = fopen_utf8(fn,  bRead?"rb":"wb");
			if ( fp == NULL ) {
				dataBuf[3]=dataBuf[1]; dataBuf[0]=0;
				dataBuf[1]=5; dataBuf[2]=0;
				int len = sprintf( dataBuf+4, "Couldn't open %s", fn);
				send(tftp_s1, dataBuf, len+4, 0);
			}
			else {
				bRead ? tftp_Read(fp) : tftp_Write(fp);
				fclose(fp);
			}
		}
	}
	term_Disp(pt,  ret==0 ? "TFTPD timed out\n" : "TFTPd stopped\n");
	closesocket( tftp_s1);
	closesocket(tftp_s0);
	tftp_s0 = INVALID_SOCKET;
	tftpd_quit();
	return 0;
}
BOOL tftp_Svr( char *root )
{
	struct sockaddr_in svraddr;
	int addrsize=sizeof(svraddr);

	if ( root==NULL ) {
		if ( tftp_s0 != INVALID_SOCKET ) {
			closesocket(tftp_s1);
			closesocket(tftp_s0);
			tftp_s0 = INVALID_SOCKET;
		}
	}
	else {
		if ( is_directory(root) ) {
			tftp_s0 = socket(AF_INET, SOCK_DGRAM, 0);
			tftp_s1 = socket(AF_INET, SOCK_DGRAM, 0);
			if ( tftp_s0==INVALID_SOCKET || tftp_s1==INVALID_SOCKET ) {
				term_Disp(pt,  "Couldn't create sockets for TFTPd\n");
				return FALSE;
			}
			memset(&svraddr, 0, addrsize);
			svraddr.sin_family=AF_INET;
			svraddr.sin_addr.s_addr=ADDR_ANY;
			svraddr.sin_port=htons(69);
			if ( bind(tftp_s0, (struct sockaddr*)&svraddr,
									addrsize)!=SOCKET_ERROR ) {
				svraddr.sin_port=0;
				if ( bind(tftp_s1, (struct sockaddr *)&svraddr,
										addrsize)!=SOCKET_ERROR ) {
					DWORD dwId;
					CreateThread(NULL, 0, tftpd, (LPVOID)root, 0, &dwId);
					return TRUE;
				}
			}
			else
				term_Disp(pt,  "Couldn't bind TFTPd port\n");
		}
	}
	return FALSE;
}