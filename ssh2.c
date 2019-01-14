//
// "$Id: ssh2.c 40088 2019-01-13 21:05:10 $"
//
// tinyTerm -- A minimal serail/telnet/ssh/sftp terminal emulator
//
// ssh2.c is the host communication implementation for
// ssh/sftp/netconf based on libssh2-1.8.1-20180930
//
// Copyright 2018-2019 by Yongchao Fan.
//
// This library is free software distributed under GNU LGPL 3.0,
// see the license at:
//
//	   https://github.com/yongchaofan/tinyTerm/blob/master/LICENSE
//
// Please report all bugs and problems on the following page:
//
//	   https://github.com/yongchaofan/tinyTerm/issues/new
//
#include <libssh2.h>
#include <libssh2_sftp.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <time.h>
#include <fcntl.h>
#include <dirent.h>
#include <fnmatch.h>
#include "tiny.h"

extern int host_type;		//all extern from host.c
extern int host_status;		//
extern SOCKET sock;			//for tcp connection
extern HANDLE hReaderThread;//reader thread handle

static HANDLE mtx;			//libssh2 reading/writing mutex
static HANDLE mtx_tun;		//tunnel list add/delete mutex
static LIBSSH2_SESSION *sshSession;
static LIBSSH2_CHANNEL *sshChannel=NULL;
static LIBSSH2_SFTP *sftpSession=NULL;

void ssh2_Init( )
{
	libssh2_init (0);
	mtx = CreateMutex( NULL, FALSE, NULL );
	mtx_tun = CreateMutex( NULL, FALSE, L"tunnel list mtx" );
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
static char keys[256];
static int cursor, bReturn=TRUE, bPassword;
char *ssh2_Gets(char *prompt, BOOL bEcho)
{
	BOOL save_edit = FALSE;		//save local edit mode, disable it for password
	if ( (bPassword=!bEcho) ) save_edit = tiny_Edit(FALSE);

	term_Disp(prompt);
	bReturn=FALSE;
	keys[0]=0;
	cursor=0;
	int old_cursor=0;
	for ( int i=0; i<1800&&bReturn==FALSE; i++ ) {
		if ( cursor>old_cursor ) { old_cursor=cursor; i=0; }
		Sleep(100);
	}
	char *res = bReturn ? keys : NULL;	//NULL is timed out
	bReturn = TRUE;
	term_Disp("\n");

	if ( bPassword && save_edit ) tiny_Edit(TRUE);	//restore local edit mode
	return res;
}
void ssh2_Send( char *buf, int len )
{
	if ( !bReturn ) {
		for ( char *p=buf; p<buf+len; p++ ) switch( *p ) {
		case '\015': keys[cursor]=0; bReturn=TRUE; break;
		case '\010':
		case '\177': if ( --cursor<0 ) cursor=0;
					  else
						if ( !bPassword ) term_Disp("\010 \010");
					  break;
		default: keys[cursor++]=*p;
				  if ( cursor>255 ) cursor=255;
				  if ( !bPassword ) term_Parse(p, 1);
		}
	}
	else if ( sshChannel!=NULL ) {
		int total=0, cch=0;
		while ( total<len ) {
			if ( WaitForSingleObject(mtx, INFINITE)==WAIT_OBJECT_0 ) {
				cch=libssh2_channel_write( sshChannel, buf+total, len-total);
				ReleaseMutex(mtx);
			}
			else break;
			if ( cch<0 ) {
				if ( cch==LIBSSH2_ERROR_EAGAIN ) {
					if ( ssh_wait_socket() ) continue;
				}
				break;
			}
			else
				total += cch;
		}
	}
}
void ssh2_Size( int w, int h )
{
	libssh2_channel_request_pty_size( sshChannel, w, h);
}
void ssh2_Close( )
{
	bReturn = TRUE;
	libssh2_channel_send_eof( sshChannel );
}
void ssh2_Exit( )
{
	libssh2_exit();
}

void tun_closeall();
const char *keytypes[]={"unknown", "ssh-rsa", "ssh-dss", "ssh-ecdsa"};
static char homedir[MAX_PATH];
static char *subsystem;
static char *username;
static char *password;
static char *passphrase;
static char *hostname;
static short port;
int ssh_parameters(char *p)
{
	subsystem =	 username = password = passphrase = hostname = NULL;
	while ( (p!=NULL) && (*p!=0) ) {
		while ( *p==' ' ) p++;
		if ( *p=='-' ) {
			switch ( p[1] ) {
			case 'l': p+=3; username = p; break;
			case 'p': if ( p[2]=='w' ) { p+=4; password = p; }
						if ( p[2]=='p' ) { p+=4; passphrase = p; }
						break;
			case 'P': p+=3; port = atoi(p); break;
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
		term_Disp( "hostname or ip required!\n");
		return -1;
	}
	p = strchr(hostname, ':');
	if ( p!=NULL ) {
		*p = 0;
		port = atoi(p+1);
	}
	return 0;
}
int ssh_knownhost()
{
	int type, check, buff_len;
	size_t len;
	char knownhostfile[MAX_PATH];

	const char *dir = getenv("USERPROFILE");
	if ( dir!=NULL ) {
		strncpy(homedir, dir, MAX_PATH-64);
		homedir[MAX_PATH-64] = 0;
	}
	else
		homedir[0]=0;

	strcpy(knownhostfile, homedir);
	strcat(knownhostfile, "/.ssh");
	struct stat sb;
	if ( stat(knownhostfile, &sb)!=0 ) mkdir(knownhostfile);
	strcat(knownhostfile, "/known_hosts");

	char buf[256];
	const char *key = libssh2_session_hostkey(sshSession, &len, &type);
	if ( key==NULL ) {
		term_Disp("hostkey failure!");
		return -4;
	}
	buff_len=sprintf(buf, "hostkey type %s\nfingerprint", keytypes[type]);
	if ( type>0 ) type++;

	const char *fingerprint;
	fingerprint = libssh2_hostkey_hash(sshSession, LIBSSH2_HOSTKEY_HASH_SHA1);
	for(int i = 0; i < 20; i++) {
		sprintf(buf+buff_len+i*3, ":%02X", (unsigned char)fingerprint[i]);
	}
	term_Disp( buf ); term_Disp("\n");

	LIBSSH2_KNOWNHOSTS *nh = libssh2_knownhost_init(sshSession);
	if ( nh==NULL ) {
		term_Disp("known hosts failure!\n");
		return -4;
	}
	libssh2_knownhost_readfile(nh, knownhostfile,
							   LIBSSH2_KNOWNHOST_FILE_OPENSSH);
	struct libssh2_knownhost *knownhost;
	check = libssh2_knownhost_check(nh, hostname, key, len,
								LIBSSH2_KNOWNHOST_TYPE_PLAIN|
								LIBSSH2_KNOWNHOST_KEYENC_RAW, &knownhost);
	int rc = 0;
	char *p = NULL;
	switch ( check ) {
	case LIBSSH2_KNOWNHOST_CHECK_MATCH:
		break;
	case LIBSSH2_KNOWNHOST_CHECK_MISMATCH:
		if ( type==((knownhost->typemask&LIBSSH2_KNOWNHOST_KEY_MASK)
								  >>LIBSSH2_KNOWNHOST_KEY_SHIFT) ) {
			term_Print("\033[31m!!!Danger, hostkey changed!!!\n");
			p=ssh2_Gets("Update hostkey and continue with the risk?(Yes/No):",
						TRUE);
			if ( p!=NULL ) {
				if ( strcmp(p, "Yes")==0 ) 
					libssh2_knownhost_del(nh, knownhost);
				else { 
					rc = -4; 
					term_Print("\033[32mDisconnected, stay safe\n");
					break; 
				}
			}
		}
		//fall through if hostkey type mismatch, or hostkey deleted for update
	case LIBSSH2_KNOWNHOST_CHECK_NOTFOUND:
		if ( p == NULL ) {
			term_Print("\033[33munknown hostkey!\n");
			p = ssh2_Gets("Add entry to .ssh/known_hosts?(Yes/No):", TRUE);
		}
		if ( p!=NULL ) {
			if ( strcmp(p, "Yes")==0 ) {
				libssh2_knownhost_addc(nh, hostname, "", key, 
										len,"**tinyTerm**", 12,
										LIBSSH2_KNOWNHOST_TYPE_PLAIN|
										LIBSSH2_KNOWNHOST_KEYENC_RAW|
										(type<<LIBSSH2_KNOWNHOST_KEY_SHIFT), 
										&knownhost);
				if ( libssh2_knownhost_writefile(nh, knownhostfile,
										LIBSSH2_KNOWNHOST_FILE_OPENSSH)==0 )
					term_Print("\033[32mhostkey added/updated\n");
				else
					term_Print("\033[33mcouldn't write hostkey file\n");
			}
		}
	}

	term_Disp("\n");
	libssh2_knownhost_free(nh);
	return rc;
}
static void kbd_callback(const char *name, int name_len,
                         const char *instruction, int instruction_len,
                         int num_prompts,
                         const LIBSSH2_USERAUTH_KBDINT_PROMPT *prompts,
                         LIBSSH2_USERAUTH_KBDINT_RESPONSE *responses,
                         void **abstract)
{
    for ( int i=0; i<num_prompts; i++) {
		char *prompt = strdup(prompts[i].text);
		prompt[prompts[i].length] = 0;
		const char *p = ssh2_Gets(prompt, prompts[i].echo);
		free(prompt);
		if ( p!=NULL ) {
			responses[i].text = strdup(p);
			responses[i].length = strlen(p);
		}
    }
} 
int ssh_authentication(char *username, char *password, char *passphrase)
{
	char user[256], pw[256];
	if ( username==NULL ) {
		char *p = ssh2_Gets("username:", TRUE);
		if ( p!=NULL ) {
			strcpy(user, p); username = user;
		}
		else 
			return -5;
	}
	char *authlist=libssh2_userauth_list(sshSession,username,strlen(username));
	if ( authlist==NULL ) return 0;	//null authentication
	
	if ( strstr(authlist, "publickey")!=NULL ) {	// try public key
		char pubkeyfile[MAX_PATH], privkeyfile[MAX_PATH];
		strcpy(pubkeyfile, homedir);
		strcat(pubkeyfile, "/.ssh/id_rsa.pub");
		strcpy(privkeyfile, homedir);
		strcat(privkeyfile, "/.ssh/id_rsa");
		struct stat buf;
		if ( stat(pubkeyfile, &buf)==0 && stat(privkeyfile, &buf)==0 ) {
			if ( !libssh2_userauth_publickey_fromfile(sshSession,
					username, pubkeyfile, privkeyfile, passphrase) ) {
				term_Print("\033[32mpublic key authentication passed\n");
				return 0;
			}
		}
	}
	if ( strstr(authlist, "password")!=NULL ) {
		for ( int rep=0; rep<3; rep++ ) {
			if ( password==NULL ) {
				char *p = ssh2_Gets("password:", FALSE);
				if ( p!=NULL ) {
					strcpy(pw, p);
					password = pw;
				}
			}
			if ( !libssh2_userauth_password(sshSession, username, password) ) {
				term_Disp("\n");
				return 0;
			}
			password=NULL;
		}
	}
	else if ( strstr(authlist, "keyboard-interactive")!=NULL ) {
		for ( int i=0; i<3; i++ )
			if (!libssh2_userauth_keyboard_interactive(sshSession, username,
                                              			&kbd_callback) ) {
				term_Print("\033[32minteractive authentication passed\n");
				return 0;
			}
	}
	term_Print("\033[31mAuthentication failure!\n");
	return -5;
}
DWORD WINAPI ssh( void *pv )
{
	int rc;

	port = 22;
	if ( ssh_parameters( (char *)pv)<0 ) goto TCP_Close;
	if ( (sock=tcp(hostname, port))==-1 ) goto TCP_Close;

	sshSession = libssh2_session_init();
	while ((rc = libssh2_session_handshake(sshSession, sock)) ==
													LIBSSH2_ERROR_EAGAIN);
	if ( rc!=0 ) {
		term_Disp("sshSession failed!\n");
		goto Session_Close;
	}

	host_status=HOST_AUTHENTICATING;
	if ( ssh_knownhost()<0 )
		goto Session_Close;
	
	if ( ssh_authentication(username, password, passphrase)<0 ) 
		goto Session_Close;


	if (!(sshChannel = libssh2_channel_open_session(sshSession))) {
		term_Disp("sshChannel failure!\n");
		goto Session_Close;
	}
	if ( subsystem == NULL ) {
		if (libssh2_channel_request_pty(sshChannel, "xterm")) {
			term_Disp("pty failure!\n");
			goto Channel_Close;
		}
		if (libssh2_channel_shell(sshChannel)) {
			term_Disp("shell failure!\n");
			goto Channel_Close;
		}
	}
	else {
		if (libssh2_channel_subsystem(sshChannel, subsystem)) {
			term_Disp("subsystem failure!\n");
			goto Channel_Close;
		}
	}
	libssh2_session_set_blocking(sshSession, 0);
//	libssh2_keepalive_config(sshSession, FALSE, 60);

	host_status=HOST_CONNECTED;
	host_type=SSH;
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
	host_type = 0;
	tun_closeall();

Channel_Close:
	libssh2_channel_close(sshChannel);
//	libssh2_channel_free(sshChannel);
	sshChannel = NULL;

Session_Close:
	libssh2_session_disconnect(sshSession, "Normal Shutdown");
	libssh2_session_free(sshSession);
	closesocket(sock);

TCP_Close:
	host_status=HOST_IDLE;
	hReaderThread = NULL;

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
			term_Print("\n\033[31mSCP: couldn't open \033[32m%s",rpath);
			return -1;
		}
	} while (!scp_channel);

	FILE *fp = fopen_utf8(lpath, "wb");
	if ( fp==NULL ) {
		term_Print("\n\033[31mSCP: couldn't write to \033[32m%s", lpath);
		goto Close;
	}
	term_Print("\n\033[32mSCP: %s\t ", lpath);

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
			if ( ++nmeg%32==0 ) term_Print(".");
		}
		else {
			if ( rc==LIBSSH2_ERROR_EAGAIN )
				if ( ssh_wait_socket()>0 ) continue;
			term_Print("\033[31minterrupted at %ld bytes", total);
			break;
		}
	}
	fclose(fp);

	int duration = (int)(time(NULL)-start);
	term_Print(" %lld bytes in %d seconds", got, duration);
Close:
	WaitForSingleObject(mtx, INFINITE);
	libssh2_channel_close(scp_channel);
	libssh2_channel_free(scp_channel);
	ReleaseMutex(mtx);
	return 0;
}
int scp_write_one(const char *lpath, const char *rpath)
{
	LIBSSH2_CHANNEL *scp_channel;
	struct _stat fileinfo;
	FILE *fp =fopen_utf8(lpath, "rb");
	if ( !fp ) {
		term_Print("\n\033[31mSCP: couldn't read from \033[32m%s", lpath);
		return -1;
	}
	stat_utf8(lpath, &fileinfo);

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
			term_Print("\n\033[31mSCP: couldn't create \033[32m%s",rpath);
			fclose(fp);
			return -2;
		}
	} while ( !scp_channel );

	term_Print("\n\033[32mSCP: %s\t", rpath);
	time_t start = time(NULL);
	size_t nread = 0, total = 0;
	int rc, nmeg = 0;
	while ( nread==0 ) {
		char mem[1024*32];
		if ( (nread=fread(mem, 1, sizeof(mem), fp))<=0 ) break;// end of file
		total += nread;
		if ( ++nmeg%32==0 ) term_Print(".");

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
				term_Print("\033[31minterrupted at %ld bytes", total);
				break;
			}
		}
	}/* only continue if nread was drained */
	fclose(fp);
	int duration = (int)(time(NULL)-start);
	term_Print("%ld bytes in %d seconds", total, duration);

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
	libssh2_channel_close(scp_channel);
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
char *scp_read( char *lpath, char *rpath )
{
	char rnames[4096]="ls -1 ", *rdir=rnames+6, *rlist;
	if ( *rpath!='/' && *rpath!='~' ) {
		scp_pwd(rdir);
		strcat(rnames, "/");
	}
	strcat(rnames, rpath);

	char *reply = term_Mark_Prompt();
	if ( strchr(rpath,'*')==NULL && strchr(rpath, '?')==NULL ) {
		char lfile[1024];
		strcpy(lfile, lpath);
		struct _stat statbuf;
		if ( stat_utf8(lpath, &statbuf)!=-1 ) {
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
		reply = term_Mark_Prompt();
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
	return reply;
}
char *scp_write( char *lpath, char *rpath )
{
	DIR *dir;
	struct dirent *dp;
	struct _stat statbuf;

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

	char *reply = term_Mark_Prompt();
	if ( stat_utf8(lpath, &statbuf)!=-1 ) {
		char rfile[1024];
		strcpy(rfile, rnames+7);

		char *p = strrchr(rnames, '/');
		int rpath_is_dir = (p[1]==0);
		if ( !rpath_is_dir ) {
			strcat(rnames, "\r");
			char *rlist;
			if ( term_TL1(rnames, &rlist )>0 ) {
				p = strchr(rlist, 0x0a);
				if ( p!=NULL ) {
					if ( p[1]=='d' ) {
						rpath_is_dir = TRUE;
						strcat(rfile, "/");
					}
				}
			}
		}
		reply = term_Mark_Prompt();
		if ( rpath_is_dir ) {
			char *p = strrchr(lpath, '/');
			if ( p==NULL ) p=lpath; else p++;
			strcat(rfile, p);
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
			term_Print("\n\033[31mSCP: couldn't open \033[32m%s\n", ldir);
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
	return reply;
}
int scp_cmd(char *cmd, char **preply)
{
	char *p = strchr(cmd, ' ');
	if ( p==NULL ) return 0;
	for ( char *q=cmd; *q; q++ ) if ( *q=='\\' ) *q='/';

	char *reply =term_Mark_Prompt();
	*p++ = 0;
	if ( *cmd==':' )
		reply = scp_read(p, cmd+1);
	else
		reply = scp_write(cmd, p+1);	//*p is expected to be ':' here

	if ( preply!=NULL ) *preply = reply;
	ssh2_Send("\r", 1);
	return term_Waitfor_Prompt();
}

struct Tunnel
{
	int socket;
	char *localip;
	char *remoteip;
	unsigned short localport;
	unsigned short remoteport;
	LIBSSH2_CHANNEL *channel;
	struct Tunnel *next;
};
struct Tunnel *tunnel_list = NULL;
struct Tunnel *tun_add( int tun_sock, LIBSSH2_CHANNEL *tun_channel,
						char *localip, unsigned short localport,
						char *remoteip, unsigned short remoteport)
{
	struct Tunnel *tun = (struct Tunnel *)malloc(sizeof(struct Tunnel));
	if ( tun!=NULL ) {
		tun->socket = tun_sock;
		tun->channel = tun_channel;
		tun->localip = strdup(localip);
		tun->localport = localport;
		tun->remoteip = strdup(remoteip);
		tun->remoteport = remoteport;
		if ( WaitForSingleObject(mtx_tun, INFINITE)==WAIT_OBJECT_0 ) {
			tun->next = tunnel_list;
			tunnel_list = tun;
			ReleaseMutex(mtx_tun);
		}
		term_Print("\n\033[32mtunnel %d %s:%d %s:%d\n", tun_sock,
							localip, localport, remoteip, remoteport);
	}
	return tun;
}
void tun_del(int tun_sock)
{
	if ( WaitForSingleObject(mtx_tun, INFINITE)==WAIT_OBJECT_0 ) {
		struct Tunnel *tun_pre = NULL;
		struct Tunnel *tun = tunnel_list;
		while ( tun!=NULL ) {
			if ( tun->socket==tun_sock ) {
				free(tun->localip);
				free(tun->remoteip);
				if ( tun_pre!=NULL )
					tun_pre->next = tun->next;
				else
					tunnel_list = tun->next;
				free(tun);
				term_Print("\n\033[32mtunnel %d closed\n", tun_sock);
				break;
			}
			tun_pre = tun;
			tun = tun->next;
		}
		ReleaseMutex(mtx_tun);
	}
}
void tun_closeall()
{
	if ( WaitForSingleObject(mtx_tun, INFINITE)==WAIT_OBJECT_0 ) {
		struct Tunnel *tun = tunnel_list;
		while ( tun!=NULL ) {
			closesocket(tun->socket);
			tun = tun->next;
		}
	}
	ReleaseMutex(mtx_tun);
}
DWORD WINAPI tun_worker( void *pv )
{
	struct Tunnel *tun = (struct Tunnel *)pv;
	int tun_sock = tun->socket;
	LIBSSH2_CHANNEL *tun_channel = tun->channel;

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
		rc = select(tun_sock+1, &fds, NULL, NULL, &tv);
		if ( rc==0 ) continue;
		if ( rc==-1 ) {
			term_Print("\n\033[31mselect error\n");
			break;
		}
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
		if ( FD_ISSET(sock, &fds) ) while ( TRUE ) {
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
	WaitForSingleObject(mtx, INFINITE);
	libssh2_channel_close(tun_channel);
	libssh2_channel_free(tun_channel);
	ReleaseMutex(mtx);
	closesocket(tun_sock);
	tun_del(tun_sock);
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

	char shost[256], dhost[256], *client_host;
	unsigned short sport, dport, client_port;

	strncpy(shost, lpath, 255); shost[255]=0;
	strncpy(dhost, rpath, 255); dhost[255]=0;
	if ( (p=strchr(shost, ':'))==NULL ) return -1;
	*p = 0; sport = atoi(++p);
	if ( (p=strchr(dhost, ':'))==NULL ) return -1;
	*p = 0; dport = atoi(++p);
	free(cmd);

	struct sockaddr_in sin;
	int sinlen=sizeof(sin);
	struct addrinfo *ainfo;
	if ( getaddrinfo(shost, NULL, NULL, &ainfo)!=0 ) {
		term_Print("\n\033[31minvalid address: %s\n", shost);
		return -1;
	}
	int listensock = socket(ainfo->ai_family, SOCK_STREAM, 0);
	((struct sockaddr_in *)(ainfo->ai_addr))->sin_port = htons(sport);
	int rc = bind(listensock, ainfo->ai_addr, ainfo->ai_addrlen);
	freeaddrinfo(ainfo);
	if ( rc==-1 ) {
		term_Print("\n\033[31mport %d invalid or in use\n", sport);
		closesocket(listensock);
		return -1;
	}
	if ( listen(listensock, 2)==-1 ) {
		term_Print("\n\033[31mlisten error\n");
		closesocket(listensock);
		return -1;
	}
	tun_add(listensock, NULL, shost, sport, dhost, dport);

	int tun_sock;
	LIBSSH2_CHANNEL *tun_channel;
	while ((tun_sock=accept(listensock,(struct sockaddr*)&sin,&sinlen))!=-1) {
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
				term_Print("\033[31mCouldn't tunnel, is it supported?\n");
				closesocket(tun_sock);
				goto shutdown;
			}
		} while ( !tun_channel );
		void *tun = tun_add(tun_sock, tun_channel,
									client_host, client_port, dhost, dport);
		CreateThread( NULL, 0, tun_worker, tun, 0, NULL );
	}
shutdown:
	closesocket(listensock);
	tun_del(listensock);
	return 0;
}
int tun_cmd(char *cmd, char **preply)
{
	char *reply = term_Mark_Prompt();
	if ( preply!=NULL ) *preply = reply;
	if ( *cmd==' ' ) {
		char *p = strchr(++cmd, ' ');
		if ( p==NULL )
			closesocket(atoi(cmd));
		else {
			DWORD dwTunnelId;
			CreateThread( NULL, 0, tun_local, strdup(cmd), 0, &dwTunnelId );
		}
	}
	else {
		struct Tunnel *tun = tunnel_list;
		int listen_cnt = 0, active_cnt = 0;
		term_Print("\nTunnels:\n");
		while ( tun!=NULL ) {
			term_Print(tun->channel==NULL?"listen":"active");
			term_Print(" socket %d\t%s:%d\t%s:%d\n", tun->socket,
						tun->localip, tun->localport,
						tun->remoteip, tun->remoteport);
			if ( tun->channel!=NULL )
				active_cnt++;
			else
				listen_cnt++;
			tun = tun->next;
		}
		term_Print("\t%d listenning, %d active\n", listen_cnt, active_cnt);
	}
	ssh2_Send("\r", 1);
	return term_Waitfor_Prompt();
}

/*******************sftpHost*******************************/
static char homepath[MAX_PATH], realpath[MAX_PATH];
int sftp_lcd(char *cmd)
{
	if ( cmd==NULL || *cmd==0 ) {
		char buf[4096];
		if ( getcwd(buf, 4096)!=NULL ) 
			term_Print("\033[32m%s ", buf);
		term_Print("is local directory\n");
	}
	else {
		while ( *cmd==' ' ) cmd++;
		term_Print("\033[32m%s ", cmd);
		term_Print( chdir(cmd)==0 ?	"is now local directory!\n"
								  : "\033[31m is not accessible!\n");
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
			term_Print("\033[31mCouldn't change dir to\033[32m%s\n", path);
			return 0;
		}
		libssh2_sftp_closedir(sftp_handle);
		int rc = libssh2_sftp_realpath(sftpSession, path, newpath, 1024);
		if ( rc>0 ) strcpy( realpath, newpath );
	}
	term_Print("\033[32m%s\033[37m\n", realpath);
	return 0;
}
int sftp_ls(char *path, int ll)
{
	char *pattern = NULL;
	LIBSSH2_SFTP_ATTRIBUTES attrs;
	LIBSSH2_SFTP_HANDLE *sftp_handle = libssh2_sftp_opendir(sftpSession, path);
	if (!sftp_handle) {
		if ( strchr(path, '*')==NULL && strchr(path, '?')==NULL ) {
			term_Print("\033[31mUnable to open dir \033[32m%s\n", path);
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
			term_Print("\033[31munable to open dir \033[32m%s\n", path);
			return 0;
		}
	}

	char mem[256], longentry[256];
	while ( libssh2_sftp_readdir_ex(sftp_handle, mem, sizeof(mem),
							longentry, sizeof(longentry), &attrs)>0 ) {
		if ( pattern==NULL || fnmatch(pattern, mem, 0)==0 )
			term_Print("%s\n", ll ? longentry : mem);
	}
	libssh2_sftp_closedir(sftp_handle);
	return 0;
}
int sftp_rm(char *path)
{
	if ( strchr(path, '*')==NULL && strchr(path, '?')==NULL ) {
		if ( libssh2_sftp_unlink(sftpSession, path) )
			term_Print("\033[31mcouldn't delete file\033[32m%s\n", path);
		return 0;
	}
	char mem[512], rfile[1024];
	LIBSSH2_SFTP_ATTRIBUTES attrs;
	LIBSSH2_SFTP_HANDLE *sftp_handle;
	char *pattern = strrchr(path, '/');
	if ( pattern!=path ) *pattern++ = 0;
	sftp_handle = libssh2_sftp_opendir(sftpSession, path);
	if ( !sftp_handle ) {
		term_Print("\033[31munable to open dir\033[32m%s\n", path);
		return 0;
	}

	while ( libssh2_sftp_readdir(sftp_handle, mem, sizeof(mem), &attrs)>0 ) {
		if ( fnmatch(pattern, mem, 0)==0 ) {
			strcpy(rfile, path);
			strcat(rfile, "/");
			strcat(rfile, mem);
			if ( libssh2_sftp_unlink(sftpSession, rfile) )
				term_Print("\033[31mcouldn't delete file\033[32m%s\n", rfile);
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
		term_Print("\033[31mcouldn't create directory\033[32m%s\n", path);
	}
	return 0;
}
int sftp_rd(char *path)
{
	int rc = libssh2_sftp_rmdir(sftpSession, path);
	if ( rc ) {
		term_Print("\033[31mcouldn't remove directory\033[32m%s\n", path);
	}
	return 0;
}
int sftp_ren(char *src, char *dst)
{
	int rc = libssh2_sftp_rename(sftpSession, src, dst);
	if ( rc )
		term_Print("\033[31mcouldn't rename file\033[32m%s\n", src);
	return 0;
}
int sftp_get_one(char *src, char *dst)
{
	LIBSSH2_SFTP_HANDLE *sftp_handle=libssh2_sftp_open(sftpSession,
											src, LIBSSH2_FXF_READ, 0);

	if (!sftp_handle) {
		term_Print("\033[31mUnable to read file\033[32m%s\n", src);
		return 0;
	}
	FILE *fp = fopen_utf8(dst, "wb");
	if ( fp==NULL ) {
		term_Print("\033[31munable to create local file\033[32m%s\n", dst);
		libssh2_sftp_close(sftp_handle);
		return 0;
	}
	term_Print("\033[32m%s ", dst);
	char mem[1024*64];
	unsigned int rc, block=0;
	long total=0;
	time_t start = time(NULL);
	while ( (rc=libssh2_sftp_read(sftp_handle, mem, sizeof(mem)))>0 ) {
		if ( fwrite(mem, 1, rc, fp)<rc ) break;
		total += rc;
		block +=rc;
		if ( block>1024*1024 ) { block=0; term_Print("."); }
	}
	int duration = (int)(time(NULL)-start);
	term_Print("%ld bytes %d seconds\n", total, duration);
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
		term_Print("\033[31mcouldn't open remote file\033[32m%s\n", dst);
		return 0;
	}
	FILE *fp = fopen_utf8(src, "rb");
	if ( fp==NULL ) {
		term_Print("\033[31mcouldn't open local file\033[32m%s\n", src);
		return 0;
	}
	term_Print("\033[32m%s ", dst);
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
		if ( block>1024*1024 ) { block=0; term_Print("."); }
	}
	int duration = (int)(time(NULL)-start);
	fclose(fp);
	term_Print("%ld bytes %d seconds\n", total, duration);
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
		struct _stat statbuf;
		if ( stat_utf8(lfile, &statbuf)!=-1 ) {
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
			term_Print("\033[31mcould't open remote diretory\033[32m%s\n", src);
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
	struct _stat statbuf;

	if ( stat_utf8(src, &statbuf)!=-1 ) {
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
			term_Print("\033[31mcouldn't open \033[32m%s\n",lfile);
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
int sftp_cmd(char *cmd)
{
	char *p1, *p2, src[1024], dst[1024];

	for ( p1=cmd; *p1; p1++ ) if ( *p1=='\\' ) *p1='/';

	p1 = strchr(cmd, ' ');		//p1 is first parameter of the command
	if ( p1==NULL )
		p1 = cmd+strlen(cmd);
	else
		while ( *p1==' ' ) p1++;

	p2 = strchr(p1, ' ');		//p2 is second parameter of the command
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
	if ( strncmp(cmd, "lpwd",4)==0 ) sftp_lcd(NULL);
	else if ( strncmp(cmd, "lcd",3)==0 ) sftp_lcd(p1);
	else if ( strncmp(cmd, "pwd",3)==0 ) sftp_cd(NULL);
	else if ( strncmp(cmd, "cd", 2)==0 ) sftp_cd(*p1==0?homepath:src);
	else if ( strncmp(cmd, "ls", 2)==0 ) sftp_ls(src, FALSE);
	else if ( strncmp(cmd, "dir",3)==0 ) sftp_ls(src, TRUE);
	else if ( strncmp(cmd, "mkdir",5)==0 ) sftp_md(src);
	else if ( strncmp(cmd, "rmdir",5)==0 ) sftp_rd(src);
	else if ( strncmp(cmd, "rm", 2)==0
			||strncmp(cmd, "del",3)==0)  sftp_rm(src);
	else if ( strncmp(cmd, "ren",3)==0)  sftp_ren(src, dst);
	else if ( strncmp(cmd, "get",3)==0 ) sftp_get(src, p2);
	else if ( strncmp(cmd, "put",3)==0 ) sftp_put(p1, dst);
	else if ( strncmp(cmd, "bye",3)==0 ) return -1;
	else term_Print("\033[31m%s is not supported command, try %s\n\t%s\n",
				cmd, "\033[37mlcd, lpwd, cd, pwd,",
				"ls, dir, get, put, ren, rm, del, mkdir, rmdir, bye");
	return 0;
}
DWORD WINAPI sftp( void *pv )
{
	int rc;

	port = 22;
	if ( ssh_parameters( (char *)pv)<0 ) goto TCP_Close;
	if ( (sock=tcp(hostname, port))==-1 ) goto TCP_Close;

	sshSession = libssh2_session_init();
	while ((rc = libssh2_session_handshake(sshSession, sock)) ==
													LIBSSH2_ERROR_EAGAIN);
	if ( rc!=0 ) {
		term_Disp("sshSession failed!\n");
		goto sftp_Close;
	}

	if ( ssh_knownhost()<0 ) goto sftp_Close;
	host_status=HOST_AUTHENTICATING;
	if ( ssh_authentication(username, password, passphrase)<0 )
		goto sftp_Close;

	if ( !(sftpSession=libssh2_sftp_init(sshSession)) ) {
		rc = -6; goto sftp_Close;
	}
	if ( libssh2_sftp_realpath(sftpSession, ".", realpath, 1024)<0 )
		*realpath=0;
	strcpy( homepath, realpath );

	host_status=HOST_CONNECTED;
	host_type=SFTP;
	tiny_Title(hostname);
	char prompt[4096], *cmd;
	while ( rc!=-1 ) {
		sprintf(prompt, "sftp %s> ", realpath);
		cmd = ssh2_Gets(prompt, TRUE);
		if ( cmd!=NULL ) {
			term_Disp("\n");
			if ( *cmd ) 
				if ( sftp_cmd(cmd)==-1 ) break;
		}
		else {
			term_Disp("\033[31m\nTime Out\033[37m");
			break;
		}
	}

	libssh2_sftp_shutdown(sftpSession);
	tiny_Title("");
	host_type = 0;

sftp_Close:
	libssh2_session_disconnect(sshSession, "Normal Shutdown");
	libssh2_session_free(sshSession);
	closesocket(sock);

TCP_Close:
	host_status=HOST_IDLE;
	hReaderThread = NULL;
	return 1;
}
/*********************************confHost*******************************/
const char *IETF_HELLO="<?xml version=\"1.0\" encoding=\"UTF-8\"?>\
<hello xmlns=\"urn:ietf:params:xml:ns:netconf:base:1.0\">\
<capabilities><capability>urn:ietf:params:netconf:base:1.0</capability>\
</capabilities></hello>]]>]]>";
const char *IETF_MSG="<?xml version=\"1.0\" encoding=\"UTF-8\"?>\r\n\
<rpc xmlns=\"urn:ietf:params:xml:ns:netconf:base:1.0\" message-id=\"%d\">\r\n\
%s</rpc>]]>]]>";
static int msg_id;
#define BUFLEN 65535
DWORD WINAPI netconf( void *pv )
{
	port = 830;
	if ( ssh_parameters( (char *)pv)<0 ) goto TCP_Close;
	if ( (sock=tcp(hostname, port))==-1 ) goto TCP_Close;

	int rc;
	sshSession = libssh2_session_init();
	while ((rc = libssh2_session_handshake(sshSession, sock)) ==
													LIBSSH2_ERROR_EAGAIN);
	if ( rc!=0 ) {
		term_Disp("sshSession failed!\n");
		goto Session_Close;
	}

	if ( ssh_knownhost()<0 ) goto Session_Close;
	host_status=HOST_AUTHENTICATING;
	if ( ssh_authentication(username, password, passphrase)<0 )
		goto Session_Close;

	if (!(sshChannel = libssh2_channel_open_session(sshSession))) {
		term_Disp("sshChannel failed!\n");
		goto Session_Close;
	}
	if ( libssh2_channel_subsystem(sshChannel, "netconf") ) {
		term_Disp("netconf subsystem failed!\n");
		goto Channel_Close;
	}
	//must be nonblocking to read/write async from multiple threads
	libssh2_session_set_blocking(sshSession, 0);
	libssh2_channel_write( sshChannel, IETF_HELLO, strlen(IETF_HELLO) );
	msg_id = 0;

	host_status=HOST_CONNECTED;
	host_type=NETCONF;
	tiny_Title(hostname);

	char reply[BUFLEN+1], *delim;
	int rd = 0;
	while ( libssh2_channel_eof(sshChannel) == 0 ) {
		if ( WaitForSingleObject(mtx, INFINITE)==WAIT_OBJECT_0 ) {
			int len=libssh2_channel_read(sshChannel,reply+rd,BUFLEN-rd);
			ReleaseMutex(mtx);
			if ( len>=0 ) {
				rd += len;
				reply[rd] = 0;
				while ( (delim=strstr(reply, "]]>]]>")) != NULL ) {
					*delim=0;
					term_Parse_XML(reply, delim-reply);
					delim+=6;
					rd -= delim-reply;
					memmove(reply, delim, rd+1);
				}
				if ( rd==BUFLEN ) {
					term_Parse_XML(reply, BUFLEN);
					rd = 0;
				}
			}
			else {
				if ( len==LIBSSH2_ERROR_EAGAIN )
					if ( ssh_wait_socket()>0 )
						continue;
				break;
			}
		}
	}
	tiny_Title("");
	host_type = 0;

Channel_Close:
	libssh2_channel_close(sshChannel);
	libssh2_channel_free(sshChannel);
	sshChannel = NULL;

Session_Close:
	libssh2_session_disconnect(sshSession, "Normal Shutdown");
	libssh2_session_free(sshSession);
	closesocket(sock);

TCP_Close:
	host_status=HOST_IDLE;
	hReaderThread = NULL;

	return 1;
}
void netconf_Send(char *msg, int len)
{
	char buf[8192];
	len = sprintf(buf, IETF_MSG, ++msg_id, msg);
	term_Parse("\n", 2);
	term_Parse_XML(buf, len-6);
	ssh2_Send(buf, len);
}