//
// "$Id: ftpd.c 14496 2018-11-21 21:05:10 $"
//
// tinyTerm -- A minimal serail/telnet/ssh/sftp terminal emulator
//
// ftpd.c implements minimal ftp and tftp server using posix API.
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
#include <direct.h>
#include <time.h>
#include <fcntl.h>
#include <winsock2.h>
#include "tiny.h"


/**********************************FTPd*******************************/
static SOCKET ftp_s0 = INVALID_SOCKET;
static SOCKET ftp_s1 = INVALID_SOCKET;
int sock_select( SOCKET s, int secs )
{
	struct timeval tv = { 0, 0 };
	tv.tv_sec = secs;
	FD_SET svrset;
	FD_ZERO( &svrset );
	FD_SET( s, &svrset );
	return select( 1, &svrset, NULL, NULL, &tv );
}
void sock_send( char *reply )
{
	send( ftp_s1, reply, strlen(reply), 0 );
}
DWORD WINAPI ftpd(LPVOID p)
{
	unsigned long  dwIp;
	unsigned short wPort;
	unsigned int c[6], ii[2];
	char *param=0, szBuf[4096], fn[MAX_PATH], svrRoot[MAX_PATH];
	BOOL bPassive;

	SOCKET s2=-1, s3=-1; 		// s2 data connection, s3 data listen
	struct sockaddr_in svraddr, clientaddr;		// for data connection
	int iRootLen, addrsize=sizeof(clientaddr);

	strcpy( svrRoot, (char*)p );
	iRootLen = strlen( svrRoot ) - 1;
	term_Disp( "FTPd started\r\n" );

	int ret0, ret1;
	while( (ret0=sock_select(ftp_s0, 900)) == 1 ) {
		ftp_s1 = accept( ftp_s0, (struct sockaddr*)&clientaddr, &addrsize );
		if ( ftp_s1 ==INVALID_SOCKET ) continue;

		sock_send( "220 Welcome\n");
		getpeername(ftp_s1, (struct sockaddr *)&clientaddr, &addrsize);
		term_Print("FTPd: connected from %s\n", 
						inet_ntoa(clientaddr.sin_addr));

		FILE *fp;
		bPassive = FALSE;
		BOOL bUser=FALSE, bPass=FALSE;
		while ( (ret1=sock_select(ftp_s1, 300)) == 1 ) {
			int cnt=recv( ftp_s1, szBuf, 1024, 0 );
			if ( cnt<=0 ) {
				term_Disp( "FTPd: client disconnected\n");
				break;
			}
			szBuf[cnt--]=0;
			term_Disp(szBuf); 
			while (szBuf[cnt]=='\r' || szBuf[cnt]=='\n' ) szBuf[cnt--]=0;
			if ( (param=strchr(szBuf, ' '))!=NULL ) 
				*param++=0; 
			else
				param = szBuf+cnt+1;

			// *** Process FTP commands ***
			if (stricmp("user", szBuf) == 0){
				sock_send( "331 Password required\n");
				bUser = strncmp( param, "tiny", 4 )==0 ? TRUE : FALSE;
				continue;
			}
			if (stricmp("pass", szBuf) == 0){
				bPass = bUser && strncmp(param, "term", 4)==0;
				sock_send( bPass?"230 Logged in okay\n": 
									"530 Login incorrect\n");
				continue;
			}
			if ( !bPass ) {
				sock_send( "530 Login required\n");
				 continue;
			}
			fn[0]=0;
			if ( *param=='/' ) strcpy(fn, svrRoot);
			strcat(fn, param);
			if (stricmp("syst", szBuf) ==0 ){
				sock_send( "215 UNIX emulated by tinyTerm\n");
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
				sock_send( "200 PORT command successful\n");
			}
			else if(stricmp("type", szBuf) == 0){
				sock_send( "200 Type can only be binary\n");
			}
			else if(stricmp("pwd", szBuf) == 0 || stricmp("xpwd", szBuf) == 0){
				_getcwd(fn, MAX_PATH); fn[iRootLen] = '/';
				sprintf( szBuf, "257 \"%s\" is current directory\n", 
																fn+iRootLen );
				sock_send( szBuf);
			}
			else if(stricmp("cwd", szBuf) == 0){
				fn[0]=0;
				if ( *param=='/' || *param=='\\' || *param==0) 
					strcpy(fn, svrRoot);
				strcat(fn, param);
				_getcwd(szBuf, MAX_PATH);
				if ( _chdir(fn) != 0 )
					sock_send( "550 No such file or directory\n");
				else {
					_getcwd(fn, MAX_PATH);
					if ( strncmp(fn, svrRoot, strlen(svrRoot))!=0 ) {
						sock_send( "550 Invalid directory name\n");
						_chdir(szBuf);
					}
					else
						sock_send( "250 CWD command sucessful\n");
				}
			}
			else if(stricmp("cdup", szBuf) == 0){
				_getcwd(fn, MAX_PATH);
				if ( strcmp(fn, svrRoot)>0 )
					if(_chdir("..") == 0){
						sock_send( "250 CWD command sucessful\n");
						continue;
					}
				sock_send( "550 No such file or directory.\n");
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
				sock_send( szBuf);
				bPassive=TRUE;
			}
			else if( stricmp("nlst", szBuf)==0 || stricmp("list", szBuf)==0 ){
				struct _finddata_t  ffblk;
				if ( *param=='/' && *(param+1)=='/' ) param++;
				if ( *(param+strlen(param)-1)=='/') strcat(param, "*.*");
				if ( *param==0 ) strcpy(param, "*.*");
				int nCode = _findfirst(param, &ffblk);
				if ( nCode==-1 ) {
					sock_send( "550 No such file or directory\n");
					continue;
				}
				sock_send( "150 Opening ASCII connection for list\n");
				if ( bPassive ) {
					s2 = accept(s3, (struct sockaddr*)&clientaddr, &addrsize);
				}
				else {
					s2 = socket(AF_INET, SOCK_STREAM, 0);
					connect(s2, (struct sockaddr *)&clientaddr, 
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
				} while ( _findnext(nCode,&ffblk)==0 );
				_findclose(nCode);
				sock_send( "226 Transfer complete.\n");
				closesocket(s2);
			}
			else if(stricmp("stor", szBuf) == 0){
				fp = NULL; 
				if ( strstr(param, ".." )==NULL ) 
					fp = fopen_utf8(fn, MODE_WB);
				if(fp == NULL){
					sock_send( "550 Unable to create file\n");
					continue;
				}
				sock_send( "150 Opening BINARY data connection\n");
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
					nLen = recv(s2, szBuf, 4096, 0);
					if ( nLen>0 ) {
						lSize += nLen;
						fwrite(szBuf, nLen, 1, fp);
					}
					if ( ++nCnt==256 ) {
						term_Print("\r%lu bytes received", lSize);
						nCnt = 0;
					}
				}while ( nLen!=0);
				fclose(fp);
				term_Print("\r%lu bytes received\n", lSize);
				sock_send( "226 Transfer complete\n");
				closesocket(s2);
			}
			else if(stricmp("retr", szBuf) == 0){
				fp = NULL; 
				if ( strstr(param, ".." )==NULL ) 
					fp = fopen_utf8(fn, MODE_RB);
				if(fp == NULL) {
					sock_send( "550 No such file or directory\n");
					continue;
				}
				sock_send( "150 Opening BINARY data connection\n");
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
					nLen = fread(szBuf, 1, 4096, fp);
					if ( send(s2, szBuf, nLen, 0) == 0) break;
					lSize += nLen;
					if ( ++nCnt==256 ) {
						term_Print("\r%lu bytes sent", lSize);
						nCnt = 0;
					}
				}
				while ( nLen==4096);
				fclose(fp);
				term_Print("\r%lu bytes sentd\n", lSize);
				sock_send( "226 Transfer complete\n");
				closesocket(s2);
			}
			else if(stricmp("size", szBuf) == 0){
				struct _finddata_t  ffblk;
				int  nCode = _findfirst(fn, &ffblk);
				if ( nCode==-1 )
					sock_send( "550 No such file or directory\n");
				else {
					sprintf(szBuf, "213 %lu\n", ffblk.size);
					sock_send( szBuf );
					_findclose(nCode);
				}
			}
			else if(stricmp("mdtm", szBuf) == 0) {
				struct _finddata_t ffblk;
				int nCode = _findfirst(fn, &ffblk);
				if ( nCode==-1 )
					 sock_send( "550 No such file or directory\n");
				else {
					struct tm *t_mod = localtime( &ffblk.time_write);
					sprintf(szBuf, "213 %4d%02d%02d%02d%02d%02d\n", 
							t_mod->tm_year+1900, t_mod->tm_mon+1, 
							t_mod->tm_mday, t_mod->tm_hour, 
							t_mod->tm_min, t_mod->tm_sec );
					sock_send( szBuf );
					_findclose(nCode);
				}
			}
			else if(stricmp("quit", szBuf) == 0){
				sock_send( "221 Bye!\n");
				break;
			}
			else {
				sock_send( "500 Command not supported\n");
			}
		}
		if( ret1 == 0 ) {
			sock_send( "500 Timeout\n");
			term_Disp( "FTPd: client timed out\n");
		}
		closesocket(ftp_s1);
	}
	term_Disp( ret0==0? "FTPd timed out\n" : "FTPd stopped\n" );
	closesocket(ftp_s0);
	ftp_s0 = INVALID_SOCKET;
	return 0;
}
BOOL ftp_Svr(char *root) 
{
	if ( ftp_s0 != INVALID_SOCKET ) {
		closesocket( ftp_s0 );
		ftp_s0 = INVALID_SOCKET;
	}
	else {
		if ( _chdir(root)==0 ) {
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
				term_Disp("Couldn't bind to FTP port\n");
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
		term_Disp("#");
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
	} while ( len==512 );
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
			ntmp=dataBuf[2];
			ntmp=(ntmp<<8)+dataBuf[3];
			if ( dataBuf[1]==3 && ntmp==nCnt+1 ) {
				fwrite(dataBuf+4, 1, nLen-4, fp);
				nRetry=0;
				nCnt++;
			}
			else if ( ++nRetry==5 ) break;
		}
		else if ( ++nRetry==5 ) break;
	} 
}
DWORD WINAPI tftpd(LPVOID p)
{
	char dataBuf[516];
	struct sockaddr_in clientaddr;
	int addrsize=sizeof(clientaddr);

	char svrRoot[MAX_PATH], fn[MAX_PATH];
	strcpy( svrRoot, (char *)p );
	term_Disp( "TFTPd started\r\n" );

	int ret;
	while ( (ret=sock_select( tftp_s0, 300 )) == 1 ) {
		ret = recvfrom( tftp_s0, dataBuf, 516, 0, 
						(struct sockaddr *)&clientaddr, &addrsize );
		if ( ret==SOCKET_ERROR ) break;
		connect(tftp_s1, (struct sockaddr *)&clientaddr, addrsize);
		if ( dataBuf[1]==1  || dataBuf[1]==2 ) {
			BOOL bRead = dataBuf[1]==1; 
			term_Print("TFTPd: %cRQ from %s\n", bRead?'R':'W', 
									inet_ntoa(clientaddr.sin_addr) ); 
			strcpy(fn, svrRoot); 
			strcat(fn, dataBuf+2);
			FILE *fp = fopen_utf8(fn,  bRead?MODE_RB:MODE_WB);
			if ( fp == NULL ) {
				dataBuf[3]=dataBuf[1]; dataBuf[0]=0; 
				dataBuf[1]=5; dataBuf[2]=0; 
				int len = sprintf( dataBuf+4, "Couldn't open %s", fn );
				send( tftp_s1, dataBuf, len+4, 0 );
			}
			else {
				bRead ? tftp_Read(fp) : tftp_Write(fp);
				fclose(fp);
			}
		}
	}
	term_Disp( ret==0 ? "TFTPD timed out\n" : "TFTPd stopped\n" );
	closesocket( tftp_s1 );
	closesocket( tftp_s0 );
	tftp_s0 = INVALID_SOCKET;
	return 0;
}
BOOL tftp_Svr( char *root )
{
	struct sockaddr_in svraddr;
	int addrsize=sizeof(svraddr);

	if ( tftp_s0 != INVALID_SOCKET ) {
		closesocket(tftp_s1);
		closesocket( tftp_s0 );
		tftp_s0 = INVALID_SOCKET;
	}
	else {
		if ( _chdir(root)==0 ) {
			tftp_s0 = socket(AF_INET, SOCK_DGRAM, 0);
			tftp_s1 = socket(AF_INET, SOCK_DGRAM, 0);
			if ( tftp_s0==INVALID_SOCKET || tftp_s1==INVALID_SOCKET ) {
				term_Disp( "Couldn't create sockets for TFTPd\n");
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
					CreateThread( NULL, 0, tftpd, (LPVOID)root, 0, &dwId);
					return TRUE;
				}
			}
			else
				term_Disp( "Couldn't bind TFTPd port\n");
		}
	}
	return FALSE;
}