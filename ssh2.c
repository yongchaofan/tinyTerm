//
// "$Id: ssh2.c 40780 2020-08-12 12:05:10 $"
//
// tinyTerm -- A minimal serail/telnet/ssh/sftp terminal emulator
//
// ssh2.c is the host communication implementation for
// ssh/sftp/netconf based on libssh2-1.9.0
//
// Copyright 2018-2020 by Yongchao Fan.
//
// This library is free software distributed under GNU GPL 3.0,
// see the license at:
//
//		https://github.com/yongchaofan/tinyTerm/blob/master/LICENSE
//
// Please report all bugs and problems on the following page:
//
//		https://github.com/yongchaofan/tinyTerm/issues/new
//
#include "tiny.h"
#include <ws2tcpip.h>
#include <time.h>
#include <fcntl.h>
#include <direct.h>
#include <shlwapi.h>
int fnmatch(char *pattern, char *file, int flag)
{
	return PathMatchSpecA(file, pattern) ? 0 : 1;
}
/****************************************************************************
 * local dirent implementation for compiling on vs2017
 ****************************************************************************/
#define DT_UNKNOWN	0
#define DT_DIR		1
#define DT_REG		2
#define DT_LNK		3

struct dirent {
	unsigned char d_type;
	char d_name[MAX_PATH*3];
};

typedef struct tagDIR {
	struct dirent dd_dir; 
	HANDLE dd_handle;     
	int dd_stat; 
} DIR;
static void finddata2dirent(struct dirent *ent, WIN32_FIND_DATA *fdata)
{
	wchar_to_utf8(fdata->cFileName, -1, ent->d_name, sizeof(ent->d_name));

	if (fdata->dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
		ent->d_type = DT_DIR;
	else
		ent->d_type = DT_REG;
}

DIR *opendir(const char *name)
{
	WCHAR pattern[MAX_PATH + 2]; 
	int len = utf8_to_wchar(name, -1, pattern, MAX_PATH+2);
	if ( len < 0) return NULL;
	if ( len && pattern[len - 1]!=L'/' ) wcscat(pattern, L"\\");
	wcscat(pattern, L"*.*");

	WIN32_FIND_DATA fdata;
	HANDLE h = FindFirstFile(pattern, &fdata);
	if ( h == INVALID_HANDLE_VALUE ) return NULL;

	DIR *dir = (DIR *)malloc(sizeof(DIR));
	dir->dd_handle = h;
	dir->dd_stat = 0;
	finddata2dirent(&dir->dd_dir, &fdata);
	return dir;
}

struct dirent *readdir(DIR *dir)
{
	if (!dir) return NULL;

	if (dir->dd_stat) {
		WIN32_FIND_DATA fdata;
		if (FindNextFile(dir->dd_handle, &fdata))
			finddata2dirent(&dir->dd_dir, &fdata);
		else
			return NULL;
	}

	++dir->dd_stat;
	return &dir->dd_dir;
}

int closedir(DIR *dir)
{
	if (dir) {
		FindClose(dir->dd_handle);
		free(dir);
	}
	return 0;
}
/******************************************************************************/
void ssh2_Construct(HOST *ph)
{
	ph->session = NULL;
	ph->channel  =NULL;
	ph->sftp = NULL;
	ph->bReturn = TRUE;
	ph->mtx = CreateMutex(NULL, FALSE, L"channel mutex");
	ph->tunnel_list = NULL;
	ph->mtx_tun = CreateMutex(NULL, FALSE, L"tunnel mutex");
	*ph->homedir = 0;
	const char *home = getenv("USERPROFILE");
	if ( home!=NULL ) {
		strcpy(ph->homedir, home);
		strcat(ph->homedir, "/.ssh/");
	}
}
int ssh_wait_socket(HOST *ph)
{
	TIMEVAL tv = {0, 100000};
	fd_set fds, *rfd=NULL, *wfd=NULL;
	FD_ZERO(&fds); FD_SET(ph->sock, &fds);
	int dir = libssh2_session_block_directions(ph->session);
	if ( dir==0 ) return 1;
	if ( dir & LIBSSH2_SESSION_BLOCK_INBOUND ) rfd = &fds;
	if ( dir & LIBSSH2_SESSION_BLOCK_OUTBOUND ) wfd = &fds;
	return select(ph->sock+1, rfd, wfd, NULL, &tv);
}
char *ssh2_Gets(HOST *ph, char *prompt, BOOL bEcho)
{
	BOOL save_edit = FALSE;		//save local edit mode, disable it for password
	if ( (ph->bPassword=!bEcho) ) save_edit = tiny_Edit(FALSE);

	term_Disp(ph->term, prompt);
	ph->bReturn=FALSE;
	ph->bGets = TRUE;
	ph->keys[0]=0;
	ph->cursor=0;
	int old_cursor=0;
	for ( int i=0; i<600&&ph->bGets&&(!ph->bReturn); i++ ) {
		if ( ph->cursor>old_cursor ) {
			old_cursor=ph->cursor;
			i=0;
		}
		Sleep(100);
	}
	if ( ph->bPassword ) tiny_Edit(save_edit);	//restore local edit mode
	if ( ph->bReturn ) {
		term_Disp(ph->term, "\r\n");
		return ph->keys;
	}
	else
		return NULL;
}
void ssh2_Send(HOST *ph, char *buf, int len)
{
	if ( !ph->bReturn ) {
		for ( char *p=buf; p<buf+len; p++ ) switch( *p ) {
		case '\015':ph->keys[ph->cursor]=0;
					ph->bReturn=TRUE; 
					break;
		case '\010':
		case '\177':if ( --ph->cursor<0 ) 
					ph->cursor=0;
					else
					if ( !ph->bPassword ) term_Disp(ph->term, "\010 \010");
					break;
		default:	ph->keys[ph->cursor++]=*p;
					if ( ph->cursor>255 ) ph->cursor=255;
					if ( !ph->bPassword ) term_Parse(ph->term, p, 1);
		}
	}
	else if ( ph->channel!=NULL ) {
		int total=0, cch=0;
		while ( total<len ) {
			if ( WaitForSingleObject(ph->mtx, INFINITE)!=WAIT_OBJECT_0 ) break;
			cch=libssh2_channel_write(ph->channel, buf+total, len-total);
			ReleaseMutex(ph->mtx);
			if ( cch>=0 )
				total += cch;
			else
				if ( cch!=LIBSSH2_ERROR_EAGAIN || ssh_wait_socket(ph)<0 ) break;
		}
	}
}
void ssh2_Size(HOST *ph, int w, int h)
{
	if ( WaitForSingleObject(ph->mtx, INFINITE)==WAIT_OBJECT_0 ) {
		if ( ph->channel!=NULL ) {
			libssh2_channel_request_pty_size(ph->channel, w, h);
		}
		ReleaseMutex(ph->mtx);
	}
}
void ssh2_Close(HOST *ph )
{
	ph->bGets = FALSE;
	if ( WaitForSingleObject(ph->mtx, INFINITE)==WAIT_OBJECT_0 ) {
		if ( ph->channel!=NULL )
			libssh2_channel_send_eof(ph->channel);
		if ( ph->session!=NULL )
			libssh2_session_disconnect(ph->session, "disconn");
		ReleaseMutex(ph->mtx);
	}
}

void tun_closeall(HOST *ph);
const char *keytypes[]={"unknown", "rsa", "dss", "ecdsa256", 
							"ecdsa384", "ecdsa521", "ed25519"};
int ssh_parameters(  HOST *ph, char *p )
{
	ph->username = ph->password = ph->passphrase = NULL;
	ph->hostname = ph->subsystem = NULL;
	while ( (p!=NULL) && (*p!=0) ) {
		while ( *p==' ' ) p++;
		if ( *p=='-' ) {
			switch ( p[1] ) {
			case 'l': p+=3; ph->username = p; break;
			case 'p': if ( p[2]=='w' ) { p+=4; ph->password = p; }
					  if ( p[2]=='p' ) { p+=4; ph->passphrase = p; }
					  break;
			case 'P': p+=3; ph->port = atoi(p); break;
			case 's': p+=3; ph->subsystem = p; break;
			}
			p = strchr( p, ' ');
			if ( p!=NULL ) *p++ = 0;
		}
		else { 
			if (*p ) ph->hostname = p; 
			break;
		}
	}
	if ( ph->username==NULL && ph->hostname!=NULL ) {
		p = strchr(ph->hostname, '@');
		if ( p!=NULL ) {
			ph->username = ph->hostname;
			ph->hostname = p+1;
			*p=0;
		}
	}
	if ( ph->hostname==NULL ) {
		term_Disp(ph->term,  "hostname or ip required!\r\n");
		return -1;
	}
	p = strchr(ph->hostname, ':');
	char *q = strrchr(ph->hostname, ':');
	if ( p!=NULL && p==q ) {
		*p = 0;
		ph->port = atoi(p+1);
	}
	return 0;
}
int ssh_knownhost(HOST *ph)
{
	int type, check, buff_len;
	size_t len;
	char knownhostfile[MAX_PATH];

	strcpy(knownhostfile, ph->homedir);
	struct stat sb;
	if ( stat(knownhostfile, &sb)!=0 ) mkdir(knownhostfile);
	strcat(knownhostfile, "known_hosts");

	char keybuf[256];
	const char *key = libssh2_session_hostkey(ph->session, &len, &type);
	if ( key==NULL ) {
		term_Disp(ph->term, "hostkey ");
		return -4;
	}
	buff_len=sprintf(keybuf, "%s key fingerprint", keytypes[type]);
	if ( type>0 ) type++;

	const char *fingerprint;
	fingerprint = libssh2_hostkey_hash(ph->session, LIBSSH2_HOSTKEY_HASH_SHA1);
	for(int i = 0; i < 20; i++) {
		sprintf(keybuf+buff_len+i*3, ":%02x", (unsigned char)fingerprint[i]);
	}

	LIBSSH2_KNOWNHOSTS *nh = libssh2_knownhost_init(ph->session);
	if ( nh==NULL ) {
		term_Disp(ph->term, "known_hosts ");
		return -4;
	}
	libssh2_knownhost_readfile(nh, knownhostfile,
							   LIBSSH2_KNOWNHOST_FILE_OPENSSH);
	struct libssh2_knownhost *knownhost;
	check = libssh2_knownhost_check(nh, ph->hostname, key, len,
								LIBSSH2_KNOWNHOST_TYPE_PLAIN|
								LIBSSH2_KNOWNHOST_KEYENC_RAW, &knownhost);
	int rc = -4;
	char *p = NULL;
	const char *msg = "Disconnected!";
	switch ( check ) {
	case LIBSSH2_KNOWNHOST_CHECK_MATCH: rc=0; msg=""; break;
	case LIBSSH2_KNOWNHOST_CHECK_MISMATCH:
		if ( type==((knownhost->typemask&LIBSSH2_KNOWNHOST_KEY_MASK)
								  >>LIBSSH2_KNOWNHOST_KEY_SHIFT) ) {
			term_Print(ph->term, "%s\r\n\033[31m!!!hostkey changed!!!\r\n",
																	keybuf);
			p=ssh2_Gets(ph, "Update hostkey and continue?(Yes/No):", TRUE);
			if ( p==NULL ) {
				term_Print(ph->term, "\r\n");
				break;
			}
			if ( *p!='y' && *p!='Y' ) break;
			libssh2_knownhost_del(nh, knownhost);//delete old hostkey
			msg = "\033[32mhostkey updated";
		}
		//fall through if hostkey type different, or to update hostkey
	case LIBSSH2_KNOWNHOST_CHECK_NOTFOUND:
		if ( p == NULL ) {
			term_Print(ph->term, "%s\r\n\033[33mhostkey unknown!", keybuf);
			p = ssh2_Gets( ph, "Add to .ssh/known_hosts?(Yes/No):", TRUE);
		}
		if ( p==NULL ) {
			term_Print(ph->term, "\r\n");
			break;
		}
		if ( *p!='y' && *p!='Y' ) break;
		rc = 0;
		libssh2_knownhost_addc(nh, ph->hostname, "", key, 
									len,"**tinyTerm**", 12,
									LIBSSH2_KNOWNHOST_TYPE_PLAIN|
									LIBSSH2_KNOWNHOST_KEYENC_RAW|
									(type<<LIBSSH2_KNOWNHOST_KEY_SHIFT), 
									&knownhost);
		if ( libssh2_knownhost_writefile(nh, knownhostfile,
									LIBSSH2_KNOWNHOST_FILE_OPENSSH)==0 ) {
			if ( *msg=='D' ) msg = "\033[32mhostkey added";
		}
		else
			msg = "\033[33mfailed to update hostkey file";
	}
	term_Print(ph->term, "%s\r\n", msg);
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
		const char *p = tiny_Gets( prompt, prompts[i].echo);
		free(prompt);
		if ( p!=NULL ) {
			responses[i].text = strdup(p);
			responses[i].length = strlen(p);
		}
  }
} 
int ssh_authentication(HOST *ph)
{
	char user[256], pw[256];
	if ( ph->username==NULL ) {
		char *p = ssh2_Gets( ph, "\r\nusername: ", TRUE);
		if ( p==NULL ) return -5;
		if ( *p==0 ) return -5;
		strcpy(user, p); 
		ph->username = user;
	}
	char *authlist = 
		libssh2_userauth_list(ph->session,ph->username,strlen(ph->username));
	if ( authlist==NULL ) return 0;					//null authentication

	if ( strstr(authlist, "publickey")!=NULL ) {	// try public key
		char pubkeyfile[MAX_PATH], privkeyfile[MAX_PATH];
		strcpy(pubkeyfile, ph->homedir);
		strcat(pubkeyfile, "id_rsa.pub");
		strcpy(privkeyfile, ph->homedir);
		strcat(privkeyfile, "id_rsa");
		struct stat buf;
		if ( stat(pubkeyfile, &buf)==0 && stat(privkeyfile, &buf)==0 ) {
			if ( !libssh2_userauth_publickey_fromfile(ph->session,
					ph->username, pubkeyfile, privkeyfile, ph->passphrase) ) {
				term_Print(ph->term,"\033[32mpublic key(RSA) authenticated\r\n");
				return 0;
			}
		}
	}
	if ( strstr(authlist, "password")!=NULL ) {
		for ( int rep=0; rep<3; rep++ ) {
			if ( ph->password==NULL ) {
				char *p = ssh2_Gets( ph, "password: ", FALSE);
				if ( p==NULL ) continue;
				strcpy(pw, p);
				ph->password = pw;
			}
			if ( !libssh2_userauth_password(ph->session, ph->username,
														ph->password) ) {
				return 0;
			}
			ph->password=NULL;
		}
		return -5;	//waited 3 times, no password input
	}
	else if ( strstr(authlist, "keyboard-interactive")!=NULL ) {
		for ( int i=0; i<3; i++ )
			if (!libssh2_userauth_keyboard_interactive(ph->session, 
														ph->username,
														&kbd_callback) ) {
				return 0;
			}
	}
	return -5;
}
const char *IETF_HELLO="<?xml version=\"1.0\" encoding=\"UTF-8\"?>\
<hello xmlns=\"urn:ietf:params:xml:ns:netconf:base:1.0\">\
<capabilities><capability>urn:ietf:params:netconf:base:1.0</capability>\
</capabilities></hello>]]>]]>";
DWORD WINAPI ssh(void *pv)
{
	int rc;
	HOST *ph = (HOST *)pv;
	char port[256];
	strcpy( port, ph->cmdline+4);

	ph->port = 22;
	if ( strstr(port, "-s netconf")!=NULL ) ph->port = 830;
	if ( ssh_parameters( ph, port )<0 ) goto TCP_Close;

	ph->type = (ph->subsystem==NULL ) ? SSH : NETCONF;
	ph->status=CONNECTING;
	term_Title(ph->term, ph->hostname);
	if ( host_tcp(ph)==-1 ) goto TCP_Close;

	ph->session = libssh2_session_init();
	do {
		rc = libssh2_session_handshake(ph->session, ph->sock);
	} while ( rc==LIBSSH2_ERROR_EAGAIN ) ;
	if ( rc!=0 ) {
		term_Error(ph->term, "session failure");
		goto Session_Close;
	}
	
	const char *banner = libssh2_session_banner_get(ph->session);
	if ( banner!=NULL ) {
		term_Disp(ph->term, "\r\n");
		term_Disp(ph->term, banner);
	}
	
	ph->status=AUTHENTICATING;
	if ( ssh_knownhost( ph )<0 ) {
		term_Error(ph->term, "verification failure");
		goto Session_Close;
	}
	if ( ssh_authentication( ph )<0 ) {
		term_Error(ph->term,"authentication failure");
		goto Session_Close;
	}
	ph->channel = libssh2_channel_open_session(ph->session);
	if ( !ph->channel ) {
		term_Error(ph->term, "channel failure");
		goto Session_Close;
	}
	if ( ph->subsystem==NULL ) {
		if (libssh2_channel_request_pty(ph->channel, "xterm")) {
			term_Error(ph->term, "pty failure");
			goto Channel_Close;
		}
		if (libssh2_channel_shell(ph->channel)) {
			term_Error(ph->term, "shell failure");
			goto Channel_Close;
		}
	}
	else {
		if (libssh2_channel_subsystem(ph->channel, ph->subsystem)) {
			term_Error(ph->term, "subsystem failure");
			goto Channel_Close;
		}
		libssh2_channel_write(ph->channel, IETF_HELLO, strlen(IETF_HELLO));
	}
	libssh2_session_set_blocking(ph->session, 0);

	ph->status=CONNECTED;
	term_Title(ph->term, ph->hostname);
	while ( libssh2_channel_eof(ph->channel) == 0 ) {
		char buf[32768];
		if ( WaitForSingleObject(ph->mtx, INFINITE)==WAIT_OBJECT_0 ) {
			int cch=libssh2_channel_read(ph->channel, buf, 32768);
			ReleaseMutex(ph->mtx);
			if ( cch >= 0 ) {
				if ( ph->subsystem==NULL ) 
					term_Parse(ph->term, buf, cch);
				else
					term_Parse_XML(ph->term, buf, cch);
			}
			else {
				if ( cch!=LIBSSH2_ERROR_EAGAIN || ssh_wait_socket( ph )<0 ) {
					char *errmsg;
					libssh2_session_last_error(ph->session, &errmsg, NULL, 0);
					term_Print(ph->term, "\r\n\033[31m%s error\r\n", errmsg);
					break;
				}
			}
		}
	}
	tun_closeall( ph);
	ph->type = NONE;
	term_Error(ph->term, "Disconnected");

Channel_Close:
	if ( ph->channel!=NULL ) {
		libssh2_channel_close(ph->channel);
		ph->channel = NULL;
	}
Session_Close:
	if ( ph->session!=NULL ) {
		libssh2_session_free(ph->session);
		ph->session = NULL;
	}
	closesocket(ph->sock);
	ph->sock = 0;

TCP_Close:
	ph->status=IDLE;
	term_Title(ph->term, "");
	return 1;
}
void print_total(TERM *pt, time_t start, long total)
{
	double duration = difftime(time(NULL), start);
	term_Print(pt, "\033[12D%ld bytes", total);
	if ( duration>0 ) 
		term_Print(pt, ", %dMB/s", (int)((total>>20)/duration));
}
int scp_read_one(HOST *ph, const char *rpath, const char *lpath)
{
	LIBSSH2_CHANNEL *scp_channel;
	libssh2_struct_stat fileinfo;
	term_Print(ph->term, "\r\n\033[32mscp: %s\t\t\t", lpath);
	int err_no=0;
	do {
		WaitForSingleObject(ph->mtx, INFINITE);
		scp_channel = libssh2_scp_recv2(ph->session, rpath, &fileinfo);
		if ( !scp_channel ) err_no = libssh2_session_last_errno(ph->session);
		ReleaseMutex(ph->mtx);
		if (!scp_channel) {
			if ( err_no!=LIBSSH2_ERROR_EAGAIN || ssh_wait_socket(ph)<0 ) {
				term_Print(ph->term,"\033[31mcouldn't open remote file");
				return -1;
			}
		}
	} while (!scp_channel);

	FILE *fp = fopen_utf8(lpath, "wb");
	if ( fp==NULL ) {
		term_Print(ph->term, "\033[31mcouldn't write to local file");
		goto Close;
	}

	int blocks = 0;
	time_t start = time(NULL);
	libssh2_struct_stat_size total = 0;
	libssh2_struct_stat_size fsize = fileinfo.st_size;
	while  ( total<fsize ) {
		char mem[1024*32];
		int amount=sizeof(mem);
		if ( (fsize-total) < amount) {
			amount = (int)(fsize-total);
		}
		WaitForSingleObject(ph->mtx, INFINITE);
		int rc = libssh2_channel_read(scp_channel, mem, amount);
		ReleaseMutex(ph->mtx);
		if ( rc==0 ) continue;
		if ( rc>0) {
			int nwrite = fwrite(mem, 1, rc, fp);
			if ( nwrite>0 ) {
				total += nwrite;
				if ( ++blocks==32 ) {
					blocks = 0;
					term_Print(ph->term, "\033[12D% 10lldKB", total>>10);
				}
			}
			if ( nwrite!=rc ) {
				term_Print(ph->term, ", \033[31merror writing to file");
				break;
			}
		}
		else {
			if ( rc!=LIBSSH2_ERROR_EAGAIN || ssh_wait_socket(ph)<0 ) {
				term_Print(ph->term, ", \033[31merror reading from host");
				break;
			}
		}
	}
	fclose(fp);
	if ( total==fsize ) print_total(ph->term, start, total);
	
Close:
	WaitForSingleObject(ph->mtx, INFINITE);
	libssh2_channel_close(scp_channel);
	libssh2_channel_free(scp_channel);
	ReleaseMutex(ph->mtx);
	return 0;
}
int scp_write_one(HOST *ph, const char *lpath, const char *rpath)
{
	LIBSSH2_CHANNEL *scp_channel;
	struct _stat fileinfo;
	term_Print(ph->term, "\r\n\033[32mscp: %s\t\t\t", rpath);
	FILE *fp =fopen_utf8(lpath, "rb");
	if ( !fp ) {
		term_Print(ph->term, "\033[31mcouldn't read local file");
		return -1;
	}
	stat_utf8(lpath, &fileinfo);

	int err_no = 0;
	do {
		WaitForSingleObject(ph->mtx, INFINITE);
		scp_channel = libssh2_scp_send(ph->session, rpath,
					fileinfo.st_mode&0777, (unsigned long)fileinfo.st_size);
		if ( !scp_channel ) err_no = libssh2_session_last_errno(ph->session);
		ReleaseMutex(ph->mtx);
		if ( !scp_channel ) {
			if ( err_no!=LIBSSH2_ERROR_EAGAIN || ssh_wait_socket(ph)<0 ) {
				term_Print(ph->term, "\033[31mcouldn't create remote file");
				fclose(fp);
				return -2;
			}
		}
	} while ( !scp_channel);

	char mem[1024*32];
	time_t start=time(NULL);
	size_t nread=0;
	long total=0;
	int blocks=0;
	while ( (nread=fread(mem, 1, sizeof(mem), fp)) >0 ) {
		char *ptr = mem;
		int rc;
		while ( nread>0 ) {
			WaitForSingleObject(ph->mtx, INFINITE);
			rc = libssh2_channel_write(scp_channel, ptr, nread);
			ReleaseMutex(ph->mtx);
			if ( rc>0 ) {
				ptr += rc;
				nread -= rc;
				total += rc;
			}
			else {
				if ( rc!=LIBSSH2_ERROR_EAGAIN || ssh_wait_socket(ph)<0 ) break;
			}
		}
		if ( rc>0 ) {
			if ( ++blocks==32 ) {
				blocks = 0;
				term_Print(ph->term, "\033[12D% 10ldKB", total>>10);
			}
		}
		else {
			term_Print(ph->term, ", \033[31merror writing to host");
			break;
		}
	}
	if ( nread==0 ) //end of file
		print_total(ph->term, start, total);
	fclose(fp);

	int rc;
	do {
		WaitForSingleObject(ph->mtx, INFINITE);
		rc = libssh2_channel_send_eof(scp_channel);
		ReleaseMutex(ph->mtx);
	} while (rc==LIBSSH2_ERROR_EAGAIN);
	do {
		WaitForSingleObject(ph->mtx, INFINITE);
		rc = libssh2_channel_wait_eof(scp_channel);
		ReleaseMutex(ph->mtx);
	} while (rc==LIBSSH2_ERROR_EAGAIN);
	do {
		WaitForSingleObject(ph->mtx, INFINITE);
		rc = libssh2_channel_wait_closed(scp_channel);
		ReleaseMutex(ph->mtx);
	} while (rc==LIBSSH2_ERROR_EAGAIN);
	WaitForSingleObject(ph->mtx, INFINITE);
	libssh2_channel_close(scp_channel);
	libssh2_channel_free(scp_channel);
	ReleaseMutex(ph->mtx);
	return 0;
}
void scp_read(HOST *ph, char *lpath, char *rfiles)
{
	char lfile[4096];
	strncpy(lfile, lpath, 1024);
	lfile[1024] = 0;

	struct _stat statbuf;
	if ( stat_utf8(lpath, &statbuf)!=-1 ) {
		if ( (statbuf.st_mode & S_IFMT) == S_IFDIR ) 
			strcat(lfile, "/");
	}
	char *ldir = lfile+strlen(lfile);

	char *p1, *p2, *p = rfiles;
	while ( (p1=strchr(p, '\012'))!=NULL ) {
		*p1++ = 0;
		if ( ldir[-1]=='/' ) {		//lpath is a directory
			p2 = strrchr(p, '/');
			if ( p2==NULL ) p2=p; else p2++;
			strcpy(ldir, p2);
		}
		scp_read_one( ph, p, lfile);
		p = p1;
		*ldir = 0;
	}
}
void scp_write(HOST *ph, char *lpath, char *rpath)
{
	DIR *dir;
	struct dirent *dp;
	struct _stat statbuf;

	if ( stat_utf8(lpath, &statbuf)!=-1 ) {	//lpath exist
		char rfile[4096];
		strncpy(rfile, rpath, 1024);
		rfile[1024] = 0;
		if ( rpath[strlen(rpath)-1]=='/' ) {//rpath specifies a directory
			char *p = strrchr(lpath, '/');	//build rfile
			if ( p==NULL ) p=lpath; else p++;
			strcat(rfile, p);
		}
		scp_write_one( ph, lpath, rfile);
	}
	else {									//lpath specifies a pattern
		char *ldir=".";
		char *lpattern = strrchr(lpath, '/');
		if ( lpattern!=NULL ) {
			*lpattern++ = 0;
			if ( *lpath ) ldir = lpath;
		}
		else 
			lpattern = lpath;
		int cnt = 0;
		if ( (dir=opendir(ldir) ) != NULL ) {
			while ( (dp=readdir(dir)) != NULL ) {
				if ( dp->d_type!=DT_REG ) continue;
				if ( fnmatch(lpattern, dp->d_name, 0)==0 ) {
					cnt++;
					char lfile[1024], rfile[1024];
					strcpy(lfile, ldir);
					strcat(lfile, "/");
					strcat(lfile, dp->d_name);
					strcpy(rfile, rpath);
					if ( rpath[strlen(rpath)-1]=='/' )
						strcat(rfile, dp->d_name);
					scp_write_one( ph, lfile, rfile);
				}
			}
			closedir(dir);
		}
		if ( cnt==0 ) 
			term_Print(ph->term, "\r\n\033[31mscp: %s/%s no mathcing file",
												ldir, lpattern);
	}
}

struct Tunnel *tun_add(HOST *ph, int tun_sock,
						LIBSSH2_CHANNEL *tun_channel,
						char *localip, unsigned short localport,
						char *remoteip, unsigned short remoteport)
{
	struct Tunnel *tun = (struct Tunnel *)malloc(sizeof(struct Tunnel));
	if ( tun!=NULL ) {
		tun->host = ph;
		tun->socket = tun_sock;
		tun->channel = tun_channel;
		tun->localip = strdup(localip);
		tun->localport = localport;
		tun->remoteip = strdup(remoteip);
		tun->remoteport = remoteport;
		if ( WaitForSingleObject(ph->mtx_tun, INFINITE)==WAIT_OBJECT_0 ) {
			tun->next = ph->tunnel_list;
			ph->tunnel_list = tun;
			ReleaseMutex(ph->mtx_tun);
		}
		term_Print(ph->term, "\r\n\033[32mtunnel %d %s:%d %s:%d\r\n", tun_sock,
							localip, localport, remoteip, remoteport);
	}
	return tun;
}
void tun_del(HOST *ph, int tun_sock)
{
	if ( WaitForSingleObject(ph->mtx_tun, INFINITE)==WAIT_OBJECT_0 ) {
		struct Tunnel *tun_pre = NULL;
		struct Tunnel *tun = ph->tunnel_list;
		while ( tun!=NULL ) {
			if ( tun->socket==tun_sock ) {
				free(tun->localip);
				free(tun->remoteip);
				if ( tun_pre!=NULL )
					tun_pre->next = tun->next;
				else
					ph->tunnel_list = tun->next;
				free(tun);
				term_Print(ph->term, "\r\n\033[32mtunnel %d closed\r\n", tun_sock);
				break;
			}
			tun_pre = tun;
			tun = tun->next;
		}
		ReleaseMutex(ph->mtx_tun);
	}
}
void tun_closeall(HOST *ph)
{
	if ( WaitForSingleObject(ph->mtx_tun, INFINITE)==WAIT_OBJECT_0 ) {
		struct Tunnel *tun = ph->tunnel_list;
		while ( tun!=NULL ) {
			closesocket(tun->socket);
			tun = tun->next;
		}
	}
	ReleaseMutex(ph->mtx_tun);
}
DWORD WINAPI tun_worker(void *pv)
{
	struct Tunnel *tun = (struct Tunnel *)pv;
	LIBSSH2_CHANNEL *tun_channel = tun->channel;
	HOST *ph = (HOST *)(tun->host);
	int tun_sock = tun->socket;
	char buff[16384];
	while ( TRUE ) {
		fd_set fds;
		FD_ZERO(&fds);
		FD_SET(ph->sock, &fds);
		FD_SET(tun_sock, &fds);
		int rc = select(tun_sock+1, &fds, NULL, NULL, NULL);//blocking call
		if ( rc==-1 ) break;
		if ( rc==0 ) continue;
		if ( FD_ISSET(tun_sock, &fds) ) {
			int len = recv(tun_sock, buff, sizeof(buff), 0);
			if ( len<=0 ) break;
			for ( int wr=0, i=0; wr<len; wr+=i ) {
				WaitForSingleObject(ph->mtx, INFINITE);
				i = libssh2_channel_write(tun_channel, buff+wr, len-wr);
				ReleaseMutex(ph->mtx);
				if ( i==LIBSSH2_ERROR_EAGAIN ){ i=0; continue; }
				if ( i<=0 ) goto shutdown;
			}
		}
		if ( FD_ISSET(ph->sock, &fds) ) while ( TRUE ) {
			WaitForSingleObject(ph->mtx, INFINITE);
			int len = libssh2_channel_read(tun_channel, buff, sizeof(buff));
			ReleaseMutex(ph->mtx);
			if ( len==LIBSSH2_ERROR_EAGAIN ) break;
			if ( len<=0 ) goto shutdown;
			for ( int wr=0, i=0; wr<len; wr+=i ) {
				i = send(tun_sock, buff+wr, len-wr, 0);
				if ( i<=0 ) break;
			}
		}
	}
shutdown:
	WaitForSingleObject(ph->mtx, INFINITE);
	libssh2_channel_close(tun_channel);
	libssh2_channel_free(tun_channel);
	ReleaseMutex(ph->mtx);
	closesocket(tun_sock);
	tun_del(ph, tun_sock);
	return 0;
}
DWORD WINAPI tun_local(void *pv)
{
	HOST *ph = (HOST *)pv;
	char  *lpath, *rpath;
	char *cmd = ph->tunline;
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

	struct sockaddr_in sin;
	int sinlen=sizeof(sin);
	struct addrinfo *ainfo;
	if ( getaddrinfo(shost, NULL, NULL, &ainfo)!=0 ) {
		term_Print(ph->term, "\r\n\033[31minvalid address: %s\r\n", shost);
		return -1;
	}
	int listensock = socket(ainfo->ai_family, SOCK_STREAM, 0);
	((struct sockaddr_in *)(ainfo->ai_addr))->sin_port = htons(sport);
	int rc = bind(listensock, ainfo->ai_addr, ainfo->ai_addrlen);
	freeaddrinfo(ainfo);
	if ( rc==-1 ) {
		term_Print(ph->term, "\r\n\033[31mport %d invalid or in use\r\n", sport);
		closesocket(listensock);
		return -1;
	}
	if ( listen(listensock, 2)==-1 ) {
		term_Print(ph->term, "\r\n\033[31mlisten error\r\n");
		closesocket(listensock);
		return -1;
	}
	tun_add( ph, listensock, NULL, shost, sport, dhost, dport);

	int tun_sock;
	LIBSSH2_CHANNEL *tun_channel;
	while ((tun_sock=accept(listensock,(struct sockaddr*)&sin,&sinlen))!=-1) {
		client_host = inet_ntoa(sin.sin_addr);
		client_port = ntohs(sin.sin_port);
		do {
			int rc = 0;
			WaitForSingleObject(ph->mtx, INFINITE);
			tun_channel = libssh2_channel_direct_tcpip_ex(ph->session,
									dhost, dport, client_host, client_port);
			if (!tun_channel) rc = libssh2_session_last_errno(ph->session);
			ReleaseMutex(ph->mtx);
			if ( !tun_channel ) {
				if ( rc!=LIBSSH2_ERROR_EAGAIN || ssh_wait_socket(ph)<0 ) {
					term_Print(ph->term, "\033[31mCouldn't tunnel, is it supported?\r\n");
					closesocket(tun_sock);
					goto shutdown;
				}
			}
		} while ( !tun_channel);
		void *tun = tun_add( ph, tun_sock, tun_channel,
								client_host, client_port, dhost, dport);
		CreateThread( NULL, 0, tun_worker, tun, 0, NULL);
	}
shutdown:
	closesocket(listensock);
	tun_del( ph, listensock);
	return 0;
}
void ssh2_Tun(HOST *ph, char *cmd)
{
	if ( *cmd==' ' ) {
		char *p = strchr(++cmd, ' ');
		if ( p!=NULL ) {				//close a tunnel
			DWORD dwTunnelId;			//open new tunnel
			strncpy(ph->tunline, cmd, 255);
			ph->tunline[255] = 0;
			term_Print(ph->term, "tunnel:%s\r\n", ph->tunline);
			CreateThread( NULL, 0, tun_local, ph, 0, &dwTunnelId);
			Sleep(100);
		}
		else {
			int sock = atoi(cmd);
			struct Tunnel *tun = ph->tunnel_list;
			while ( tun!=NULL ) {
				if ( tun->socket==sock ) closesocket(sock);
				tun = tun->next;
			}
		}
	}
	else {								//list existing tunnels
		struct Tunnel *tun = ph->tunnel_list;
		int listen_cnt = 0, active_cnt = 0;
		term_Print(ph->term, "\r\nTunnels:\r\n");
		while ( tun!=NULL ) {
			term_Print(ph->term, tun->channel==NULL?"listen":"active");
			term_Print(ph->term, " socket %d\t%s:%d\t%s:%d\r\n",
								tun->socket, tun->localip, tun->localport,
								tun->remoteip, tun->remoteport);
			if ( tun->channel!=NULL )
				active_cnt++;
			else
				listen_cnt++;
			tun = tun->next;
		}
		term_Print(ph->term, "\t%d listenning, %d active\r\n", 
							listen_cnt, active_cnt);
	}
	ssh2_Send( ph, "\r", 1);
}
/*******************sftpHost*******************************/
void sftp_lcd(HOST *ph, char *cmd)
{
	char buf[4096];
	if ( cmd!=NULL && *cmd!=0 ) {
		if ( chdir(cmd)!=0 )
			term_Print(ph->term, "\033[31mcouldn't change working dir!\r\n");
	}
	if ( _getcwd(buf, 4096)!=NULL ) 
		term_Print(ph->term, "\033[32m%s is local working dir\r\n", buf);
}
void sftp_cd(HOST *ph, char *path)
{
	char newpath[1024];
	if ( path!=NULL ) {
		LIBSSH2_SFTP_HANDLE *sftp_handle;
		if ( (sftp_handle=libssh2_sftp_opendir(ph->sftp, path))!=NULL ) {
			libssh2_sftp_closedir(sftp_handle);
			if ( libssh2_sftp_realpath(ph->sftp, path, newpath, 1024)>0 ) 
				strcpy(ph->realpath, newpath);
		}
		else {
			term_Print(ph->term, "\033[31mCouldn't change dir to %s\r\n",path);
		}
	}
	term_Print(ph->term, "\033[32m%s\033[37m\r\n", ph->realpath);
}
void sftp_ls(HOST *ph, char *path, int ll)
{
	WaitForSingleObject(ph->mtx, INFINITE);
	LIBSSH2_SFTP_HANDLE *sftp_handle = libssh2_sftp_opendir(ph->sftp, path);
	char *pattern = NULL;
	if ( sftp_handle==NULL ) {
		pattern = strrchr(path, '/');
		if ( pattern!=path )
			*pattern = 0;
		else
			path = "/";
		pattern++;
		sftp_handle = libssh2_sftp_opendir(ph->sftp, path);
	}
	if ( sftp_handle!=NULL ) {
		LIBSSH2_SFTP_ATTRIBUTES attrs;
		int count = 0;
		char mem[256], longentry[256];
		char *shortlist[256], *longlist[256];
		while ( libssh2_sftp_readdir_ex(sftp_handle, mem, sizeof(mem),
								longentry, sizeof(longentry), &attrs)>0 ) {
			if ( pattern==NULL || fnmatch(pattern, mem, 0)==0 ) {
				int i;
				for ( i=0; i<count; i++ ) {
					if ( strcmp(mem, shortlist[i])<0 ) {
						for ( int j=254; j>i; j-- ) {
							shortlist[j] = shortlist[j-1];
							longlist[j] = longlist[j-1];
						}
						shortlist[i] = strdup(mem);
						longlist[i] = strdup(longentry);
						break;
					}
				}
				if ( i==count ) {
					shortlist[i] = strdup(mem);
					longlist[i] = strdup(longentry);
				}
				if ( ++count==256 ) break;
			}
		}
		for (int i=0; i<count; i++ ) {
			term_Print(ph->term, "%s\r\n", ll ? longlist[i] : shortlist[i]);
			free(longlist[i]); free(shortlist[i]);
		}	
		libssh2_sftp_closedir(sftp_handle);
	}
	else {
		term_Print(ph->term, "\033[31mCouldn't open dir %s\r\n", path);
	}
	ReleaseMutex(ph->mtx);
}
void sftp_rm(HOST *ph, char *path)
{
	if ( strchr(path, '*')==NULL && strchr(path, '?')==NULL ) {
		if ( libssh2_sftp_unlink(ph->sftp, path) )
			term_Print(ph->term, "\033[31mcouldn't delete %s\r\n", path);
		return;
	}
	char mem[512], rfile[1024];
	LIBSSH2_SFTP_ATTRIBUTES attrs;
	LIBSSH2_SFTP_HANDLE *sftp_handle;
	char *pattern = strrchr(path, '/');
	if ( pattern!=NULL && pattern!=path ) *pattern++ = 0;
	if ( (sftp_handle=libssh2_sftp_opendir(ph->sftp, path))!=NULL ) {
		while ( libssh2_sftp_readdir(sftp_handle, mem, sizeof(mem), &attrs)>0 ) {
			if ( fnmatch(pattern, mem, 0)==0 ) {
				strcpy(rfile, path);
				strcat(rfile, "/");
				strcat(rfile, mem);
				if ( libssh2_sftp_unlink(ph->sftp, rfile) )
					term_Print(ph->term, "\033[31mcouldn't delete %s\r\n", rfile);
			}
		}
		libssh2_sftp_closedir(sftp_handle);
	}
	else {
		term_Print(ph->term, "\033[31munable to open %s\r\n", path);
	}

}
void sftp_md(HOST *ph, char *path)
{
	int rc = libssh2_sftp_mkdir(ph->sftp, path,
							LIBSSH2_SFTP_S_IRWXU|
							LIBSSH2_SFTP_S_IRGRP|LIBSSH2_SFTP_S_IXGRP|
							LIBSSH2_SFTP_S_IROTH|LIBSSH2_SFTP_S_IXOTH);
	if ( rc ) {
		term_Print(ph->term, "\033[31mcouldn't create dir \033[32m%s\r\n", path);
	}
}
void sftp_rd(HOST *ph, char *path)
{
	if ( libssh2_sftp_rmdir(ph->sftp, path) )
		term_Print(ph->term, "\033[31mcouldn't remove dir \033[32m%s\r\n", path);
}
void sftp_ren(HOST *ph, char *src, char *dst)
{
	if ( libssh2_sftp_rename(ph->sftp, src, dst) )
		term_Print(ph->term, "\033[31mcouldn't rename file\033[32m%s\r\n", src);
}
void sftp_get_one(HOST *ph, char *src, char *dst)
{
	term_Print(ph->term, "get %s\t\t\t", dst);
	LIBSSH2_SFTP_HANDLE *sftp_handle=libssh2_sftp_open(ph->sftp,
											src, LIBSSH2_FXF_READ, 0);

	if (!sftp_handle) {
		term_Print(ph->term, "\033[31mUnable to read remote file\r\n");
		return;
	}
	FILE *fp = fopen_utf8(dst, "wb");
	if ( fp==NULL ) {
		term_Print(ph->term, "\033[31munable to create local file\r\n");
		libssh2_sftp_close(sftp_handle);
		return;
	}

	long total=0;
	time_t start = time(NULL);
	char mem[1024*64];
	int rc, blocks=0;
	while ( (rc=libssh2_sftp_read(sftp_handle, mem, sizeof(mem)))>0 ) {
		int nwrite = fwrite(mem, 1, rc, fp);
		if ( nwrite>0 ) {
			total += nwrite;
			if ( ++blocks==32 ) {
				blocks = 0;
				term_Print(ph->term, "\033[12D% 10ldKB", total>>10);
			}
		}
		if ( nwrite<rc ) {
			term_Print(ph->term, "\033[31merror writing to file");
			break;
		}
	}
	if ( rc==0 ) print_total(ph->term, start, total);
	term_Print(ph->term, "\r\n");
	fclose(fp);
	libssh2_sftp_close(sftp_handle);
}
void sftp_put_one(HOST *ph, char *src, char *dst)
{
	term_Print(ph->term, "put %s\t\t\t", dst);
	LIBSSH2_SFTP_HANDLE *sftp_handle = libssh2_sftp_open(ph->sftp, dst,
					  LIBSSH2_FXF_WRITE|LIBSSH2_FXF_CREAT|LIBSSH2_FXF_TRUNC,
					  LIBSSH2_SFTP_S_IRUSR|LIBSSH2_SFTP_S_IWUSR|
					  LIBSSH2_SFTP_S_IRGRP|LIBSSH2_SFTP_S_IROTH);
	if (!sftp_handle) {
		term_Print(ph->term, "\033[31mcouldn't create remote file\r\n");
		return;
	}
	FILE *fp = fopen_utf8(src, "rb");
	if ( fp==NULL ) {
		term_Print(ph->term, "\033[31mcouldn't read local file\r\n");
		return;
	}

	char mem[1024*64];
	int nread, blocks = 0;
	long total=0;
	time_t start = time(NULL);
	while ( (nread=fread(mem, 1, sizeof(mem), fp))>0 ) {
		int rc=0;
		for ( int nwrite=0; nwrite<nread && rc>=0; ){
			rc=libssh2_sftp_write(sftp_handle, mem+nwrite, nread-nwrite);
			if ( rc>0 ) {
				nwrite += rc;
				total += rc;
			}
		}
		if ( rc>0 ) {
			if ( ++blocks==32 ) {
				blocks = 0;
				term_Print(ph->term, "\033[12D% 10ldKB", total>>10);
			}
		}
		else {
			term_Print(ph->term, "\033[31merror writing to host");
			break;
		}
	}
	if ( nread==0 ) print_total(ph->term, start, total);
	term_Print(ph->term, "\r\n");
	fclose(fp);
	libssh2_sftp_close(sftp_handle);
}
void sftp_get(HOST *ph, char *src, char *dst)
{
	char mem[512];
	LIBSSH2_SFTP_ATTRIBUTES attrs;
	LIBSSH2_SFTP_HANDLE *sftp_handle;

	WaitForSingleObject(ph->mtx, INFINITE);
	if ( strchr(src,'*')==NULL && strchr(src, '?')==NULL ) {
		char lfile[1024];
		strcpy(lfile, *dst?dst:".");
		struct _stat statbuf;
		if ( stat_utf8(lfile, &statbuf)!=-1 ) {
			if ( (statbuf.st_mode & S_IFMT) == S_IFDIR ) {
				strcat(lfile, "/");
				char *p = strrchr(src, '/');
				if ( p!=NULL ) p++; else p=src;
				strcat(lfile, p);
			}
		}
		sftp_get_one( ph, src, lfile);
	}
	else {
		char *pattern = strrchr(src, '/');
		if ( pattern!=NULL ) *pattern++ = 0;
		if ( (sftp_handle=libssh2_sftp_opendir(ph->sftp, src))!=NULL ) {
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
					sftp_get_one( ph, rfile, lfile);
				}
			}
			libssh2_sftp_closedir(sftp_handle);
		}
		else {
			term_Print(ph->term, "\033[31mcould't open remote dir %s\r\n", src);
		}
	}
	ReleaseMutex(ph->mtx);
}
void sftp_put(HOST *ph, char *src, char *dst)
{
	DIR *dir;
	struct dirent *dp;
	struct _stat statbuf;

	WaitForSingleObject(ph->mtx, INFINITE);
	if ( stat_utf8(src, &statbuf)!=-1 ) {
		char rfile[1024];
		strcpy(rfile, *dst?dst:".");
		LIBSSH2_SFTP_ATTRIBUTES attrs;
		if ( libssh2_sftp_stat(ph->sftp, rfile, &attrs)==0 ) {
			if ( LIBSSH2_SFTP_S_ISDIR(attrs.permissions) ) {
				char *p = strrchr(src, '/');
				if ( p!=NULL ) p++; else p=src;
				strcat(rfile, "/");
				strcat(rfile, p);
			}
		}
		sftp_put_one( ph, src, rfile);
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

		if ( (dir=opendir(lfile) )!=NULL ) {
			strcat(lfile, "/");
			int llen = strlen(lfile);
			strcpy(rfile, dst);
			if ( *rfile!='/' || strlen(rfile)>1 ) strcat(rfile, "/");
			int rlen = strlen(rfile);
			while ( (dp=readdir(dir)) != NULL ) {
				if ( fnmatch(pattern, dp->d_name, 0)==0 ) {
					strcpy(lfile+llen, dp->d_name);
					strcpy(rfile+rlen, dp->d_name);
					sftp_put_one( ph, lfile, rfile);
				}
			}
		}
		else
			term_Print(ph->term, "\033[31mcouldn't open \033[32m%s\r\n",lfile);
	}
	ReleaseMutex(ph->mtx);
}
int sftp_cmd(HOST *ph, char *cmd)
{
	char *p1, *p2, src[1024], dst[1024];
	for ( p1=cmd; *p1; p1++ ) if ( *p1=='\\' && p1[1]!=' ' ) *p1='/';

	p1 = strchr(cmd, ' ');		//p1 is first parameter of the command
	if ( p1==NULL ) p1 = cmd+strlen(cmd);
	while ( *p1==' ' ) p1++;		

	p2 = NULL;					//p2 is second parameter of the command
	char *p = strchr(p1, ' ');
	while ( p!=NULL ) {
		if ( p[-1]=='\\' ) 
			for ( char *q=p-1; *q; q++ ) *q=q[1];
		else {
			*p++ = 0;
			p2 = p;
		}
		p = strchr(p, ' ');
	}
	if ( p2==NULL ) p2 = p1+strlen(p1);

	strcpy(src, p1);			//src is remote source file
	if ( *p1!='/') {
		strcpy(src, ph->realpath);
		if ( *p1!=0 ) {
			if ( *src!='/' || strlen(src)>1 ) strcat(src, "/");
			strcat(src, p1);
		}
	}

	strcpy(dst, p2);			//dst is remote destination file
	if ( *p2!='/' ) {
		strcpy( dst, ph->realpath);
		if ( *p2!=0 ) {
			if ( *dst!='/' || strlen(dst)>1 ) strcat( dst, "/");
			strcat( dst, p2);
		}
	}
	if ( strncmp(cmd, "lpwd",4)==0 ) sftp_lcd(ph, NULL);
	else if ( strncmp(cmd, "lcd",3)==0 ) sftp_lcd(ph, p1);
	else if ( strncmp(cmd, "pwd",3)==0 ) sftp_cd(ph, NULL);
	else if ( strncmp(cmd, "cd", 2)==0 ) sftp_cd(ph, *p1==0?ph->homepath:src);
	else if ( strncmp(cmd, "ls", 2)==0 ) sftp_ls(ph, src, FALSE);
	else if ( strncmp(cmd, "dir",3)==0 ) sftp_ls(ph, src, TRUE);
	else if ( strncmp(cmd, "mkdir",5)==0 ) sftp_md(ph, src);
	else if ( strncmp(cmd, "rmdir",5)==0 ) sftp_rd(ph, src);
	else if ( strncmp(cmd, "rm", 2)==0
			||strncmp(cmd, "del",3)==0)  sftp_rm(ph, src);
	else if ( strncmp(cmd, "ren",3)==0)  sftp_ren(ph, src, dst);
	else if ( strncmp(cmd, "get",3)==0 ) sftp_get(ph, src, p2);
	else if ( strncmp(cmd, "put",3)==0 ) sftp_put(ph, p1, dst);
	else if ( strncmp(cmd, "bye",3)==0 ) {
		term_Disp(ph->term, "Logout!");
		return -1;
	}
	else if ( *cmd )
		term_Print(ph->term, "\033[31m%s is not valid command, try %s\r\n\t%s\r\n",
					cmd, "\033[37mlcd, lpwd, cd, pwd,",
					"ls, dir, get, put, ren, rm, del, mkdir, rmdir, bye");
	return 0;
}
DWORD WINAPI sftp(void *pv)
{
	HOST *ph = (HOST *)pv;
	char port[256];
	strcpy( port, ph->cmdline+5);

	ph->port = 22;
	if ( ssh_parameters( ph, port )<0 ) goto TCP_Close;

	ph->status=CONNECTING;
	term_Title(ph->term, ph->hostname);
	if ( host_tcp(ph)==-1 ) goto TCP_Close;

	ph->session = libssh2_session_init();
	int rc;
	do {
		rc = libssh2_session_handshake(ph->session, ph->sock);
	} while ( rc==LIBSSH2_ERROR_EAGAIN ) ;
	if ( rc!=0 ) {
		term_Error(ph->term, "session failure");
		goto sftp_Close;
	}
	const char *banner = libssh2_session_banner_get(ph->session);
	if ( banner!=NULL ) term_Print(ph->term, "\n%s", banner);

	ph->status=AUTHENTICATING;
	if ( ssh_knownhost( ph )<0 ) {
		term_Error(ph->term, "mverification failure");
		goto sftp_Close;
	}
	if ( ssh_authentication( ph )<0 ) {
		term_Error(ph->term,"authentication failure");
		goto sftp_Close;
	}

	ph->sftp = libssh2_sftp_init(ph->session);
	if ( !ph->sftp ) {
		term_Error(ph->term, "subsystem failure");
		goto sftp_Close;
	}
	if ( libssh2_sftp_realpath(ph->sftp, ".", ph->realpath, 1024)<0 )
		*ph->realpath=0;
	strcpy(ph->homepath, ph->realpath);

	char cwd[4096];
	_getcwd(cwd, 4096);
	ph->type = SFTP;
	ph->status = CONNECTED;
	ph->sftp_running = TRUE;
	while ( ph->sftp_running ) {
		char *cmd = ssh2_Gets(ph, "\033[32msftp> \033[37m", TRUE);
		for ( int i=0; i<10 && cmd==NULL && ph->sftp_running; i++ )
			cmd = ssh2_Gets(ph, "", TRUE);
		if ( cmd==NULL ) {
			term_Disp(ph->term, "Time Out!");
			break;
		}
		if ( sftp_cmd( ph, cmd)==-1 ) break;
	}
	libssh2_sftp_shutdown(ph->sftp);
	term_Error(ph->term, "Disconnected");
	ph->type = NONE;
	chdir(cwd);

sftp_Close:
	libssh2_session_disconnect(ph->session, "Normal Shutdown");
	libssh2_session_free(ph->session);
	closesocket(ph->sock);
	ph->sock = 0;

TCP_Close:
	ph->status=IDLE;
	term_Title(ph->term, "");
	return 1;
}