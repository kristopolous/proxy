/* Simple proxy
 *
 * (C) 	Copyright 2005, 2008 Christopher J. McKenzie under the terms of the
 * 	GNU Public License, incorporated herein by reference
 *
 *	Proxies work like this
 *
 *	client->proxy->server
 *	server->proxy->client
 */

#include <fcntl.h>
#include <time.h>
#include <langinfo.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <signal.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>

#define SMALL	128 
#define MEDIUM	1024
#define LARGE	16384
#define MAX	32

enum
{
	HTTP, 
	HTTPS, 
	UNKNOWN
};
enum
{
	FALSE, 
	TRUE, 
};

#define CHR(sz)   (char*)malloc(sizeof(char)*(sz))
#define TRIES	5

#define READSERVER	0x01
#define READCLIENT	0x02
#define WRITESERVER	0x04
#define WRITECLIENT	0x08

struct client
{
	char	*reqtype,
		*host, 		//The host to connect to
		*oldhost, 	//The currently connected host
		
		*toclient, 	//Response from host
		*toserver, 	//What to send to the server

		*coffset, 	//Writing to the client
		*soffset, 	//Writing to the server

		active;		//Is the client active

	int
		tcsize, 	//To client in use size
		tssize, 	//To server in use size

		tcmax, 		//Buffer size
		tsmax, 		//Buffer size
	
		todo, 		//What to do

		clientfd, 	//File handle of client connected
		serverfd, 	//File handle of server
		proto, 		//What protocol is in use
		port;		//What port of the host
};

#define ISA(_s_, _l_) 		( ( !strcmp(*argv, _s_) ) || ( !strcmp(*argv, _l_) ) )
#define GETMAX(_a_, _b_)	( ( (_a_) > (_b_) ) ? (_a_) : (_b_) )
#define NULLFREE(_a_)		if((_a_)) { free((_a_)); (_a_) = 0; }

char		*g_absolute = 0,
		g_fds[128];

const char	*g_strhost = "Host: ";

int 	g_clientsize = 0, 
    	g_proxyfd,
	g_which;

fd_set	g_rg_fds, 
	g_wg_fds, 
	g_eg_fds;

struct client 	g_dbase[MAX],
		*g_forsig;

void done(struct client*);

#ifdef _DEBUG
FILE	*g_flog;
#define FBUF	75

char g_logall = 0,
     g_oldformat = 0,
     g_screen = 0,
     g_fast = 0;

unsigned char g_fbuf[FBUF];

const char 	g_spaces[] = "     ",
		*g_spend = g_spaces + 20;

int	g_marker = 0;

ssize_t wrapwrite(int fd, const void*buf, size_t count)
{
	ssize_t ret;	
	int ix = 0;

	if(g_logall == 0)
	{
		memset(g_fbuf, 0, FBUF);

		while(ix < FBUF && ix < count && ((const char*)buf)[ix] >= ' ')
		{
			ix++;
		}

		memcpy(g_fbuf, buf, ix);
	}

	ret = write(fd, buf, count);

	if(g_oldformat == 1)
	{
		fprintf(g_flog, "%s%-25s%06dW: < %s>\n", 
			g_spend - (g_which << 1), 
			g_dbase[g_which].host, 
			ret, 
			(g_logall == 1) ? (char*)buf : (char*)g_fbuf);

		fflush(g_flog);
	}

	return(ret);
}

ssize_t wraprecv(int s, void *buf, size_t len, int flags)
{	
	ssize_t ret;	
	int ix = 0;

	ret = recv(s, buf, len, flags);

	if(ret < len && ret > 0)
	{
		((char*)buf)[ret] = 0;
	}

	if(ret < 1)
	{
		return(ret);
	}
	if(g_logall == 0)
	{
		memset(g_fbuf, 0, FBUF);

		while(ix < FBUF && ix < len && ((const char*)buf)[ix] >= ' ')
		{
			ix++;
		}
		memcpy(g_fbuf, buf, ix);
	}


	if(g_oldformat == 1)
	{
		fprintf(g_flog, "%s%-25s%06dr: < %s>\n", 
			g_spend - (g_which << 1), 
			g_dbase[g_which].host, 
			ret, 
			(g_logall == 1) ? (char*)buf : (char*)g_fbuf );
	}
	else
	{
		if(g_fast == 1)
		{
			fprintf(g_flog, "%03d ----\n%s\n", g_which, (g_logall == 1) ? (char*)buf : (char*)g_fbuf);
		}
		else
		{
			char *ptr = (char*)buf;
			
			fprintf(g_flog, "\n----------\n");
			fprintf(g_flog, "%3d %3d ", g_marker, g_which);
			for(ix = 0 ; ix < ret; ix++)
			{
				if(ptr[ix] < 127)
				{
					if(ptr[ix] < ' ')
					{
						switch(ptr[ix])
						{
							case '\n':
							case '\r':
							case '\v':
							case '\t':
								fprintf(g_flog, "%c", ptr[ix]);
								break;

							default:
								fprintf(g_flog, ".");
								break;
						}
					}
					else
					{
						fprintf(g_flog, "%c", ptr[ix]);
					}
				}
				else
				{
					fprintf(g_flog, ".");
				}

				if(ptr[ix] == '\n')
				{
					fprintf(g_flog, "%3d %3d ", g_marker, g_which);
				}
			}
		}
	}
	fflush(g_flog);

	return(ret);
}

void debug(char*td)
{
	fprintf(g_flog, "%s", td);

	fflush(g_flog);
}

void setmarker()
{
	g_marker++;
}

#define write	wrapwrite
#define recv	wraprecv

#endif // _DEBUG

void done(struct client*cur)
{	
	shutdown(cur->clientfd, SHUT_RDWR);
	shutdown(cur->serverfd, SHUT_RDWR);

	if(cur->toserver)	
	{
		memset(cur->toserver, 0, cur->tsmax);
	}	
	if(cur->toclient)
	{
		memset(cur->toclient, 0, cur->tcmax);
	}
	cur->active = FALSE;
	cur->tcsize = 0;
	cur->tssize = 0;
	cur->todo = 0;
	cur->clientfd = 0;
	cur->serverfd = 0;
	cur->coffset = cur->toclient;
	cur->soffset = cur->toserver;

	NULLFREE(cur->host);
	NULLFREE(cur->oldhost);
	NULLFREE(cur->reqtype);
}

void handle_bp(int in)
{
	done(g_forsig);
}

//
// Doesn't do any socket stuff
//
void process(struct client*toprocess)
{	
	char 	*req = 0, 
		*ptr, 
		*tptr = 0, 
		*doc, 
		*beg, 
		*end,
		*reqtype;

	ptr = toprocess->toserver;
	// These are the default values
	toprocess->proto = HTTP;
	toprocess->port = 80;

	// The request is something like:
	// GET http://website/page HTTP/1.1 ...
	// We try to get the hostname out of this and replace it with
	// GET /page HTTP/1.1 ...
	// This just copies the part GET << THIS>> HTTP/1.0 to req

	if(g_absolute != 0)
	{
		char *ptr;

		toprocess->host = (char*)malloc(strlen(g_absolute));
		memcpy(toprocess->host, g_absolute, strlen(g_absolute));
		for(ptr = toprocess->host;*ptr != ':';ptr++);
		ptr[0] = 0;

		toprocess->port = 0;
		while(ptr++)
		{
			if(*ptr < '0' || *ptr>'9')
			{
				break;
			}
			toprocess->port *= 10;
			toprocess->port += *ptr-'0';
		}
	}
	else
	{	
		// reqtype is either GET or PUT usually
		reqtype = ptr;

		// At the end of the GET or PUT, we put a \0 for string termination
		while(ptr++)
		{	
			if(*ptr == ' ')
			{ 
				// Copy the GET or PUT out and then null it
				if(toprocess->reqtype)
				{
					free(toprocess->reqtype);
				}

				toprocess->reqtype = malloc(ptr - reqtype + 1);
				memcpy(toprocess->reqtype, reqtype, ptr-reqtype);
				toprocess->reqtype[ptr-reqtype] = 0;

				// Get the command out
				tptr = beg = ptr+1;
				while(ptr++)
				{
					if(*ptr == ' ')
					{
						break;
					}
				}

				end = ptr;
				req = (char*)malloc(ptr-tptr+1);
				memcpy(req, tptr, ptr-tptr);
				req[ptr-tptr] = 0;
				break;
			}
		}

		// Now req should be the "http://website/page" part of the request
		if(req[0] != '/')	//Host is included (ie, http://blah)
		{	
			if(req[4] == 's')
			// Look for https requests
			{
				toprocess->proto = HTTPS;
				tptr = req + 8;
			}
			else
			{
				tptr = req + 7;
			}
			// We forward the pointer past the http:// part
			ptr = tptr;

			while(ptr++)
			// Since http://host:port/something is valid
			{
				if(*ptr == ':')//port
				{	
					toprocess->port = 0;
					toprocess->host = (char*)malloc(ptr - tptr + 1);
					memcpy(toprocess->host, tptr, ptr - tptr);
					toprocess->host[ptr-tptr] = 0;

					//An atoi routine
					while(ptr++)
					{
						if(*ptr < '0' || *ptr > '9')
						{
							break;
						}
						toprocess->port *= 10;
						toprocess->port += *ptr - '0';
					}
					break;
				}
				// We assume we are at the end of the request
				if(*ptr == '/')
				{
					toprocess->host = (char*)malloc(ptr-tptr+1);
					memcpy(toprocess->host, tptr, ptr-tptr);
					toprocess->host[ptr-tptr] = 0;
					break;
				}
			}
			tptr = ptr;
			ptr = req+strlen(req);
			doc = (char*)malloc(ptr-tptr+1);
			memcpy(doc, tptr, ptr-tptr);
			doc[ptr-tptr] = 0;
			// This shifts the request to exclude the host and proto info in the GET part
			memmove(beg+strlen(doc), end, toprocess->tssize-(end-toprocess->toserver)+1);
			// memset(beg+toprocess->tssize-(end-toprocess->toserver), 0, toprocess->tssize-(end-toprocess->toserver));
			memcpy(beg, doc, strlen(doc));
			free(doc);
		}
	}

	if(g_absolute == 0)
	{
		//
		// This part checks the "Host:" section of the HTTP request
		//
		// Really - if we get everything from above then why?
		beg = (char*)g_strhost;
		ptr = toprocess->toserver;
		while(ptr++)
		{
			if(*ptr == *beg)
			{
				beg++;
			}
			else
			{
				beg = (char*)g_strhost;
			}
			if(*beg == 0)
			{	
				ptr++;
				tptr = ptr;
				while(*ptr > ' ')
				{
					ptr++;
				}
				if(!toprocess->host)
				{	
					toprocess->host = (char*)malloc(ptr-tptr+1);
					memcpy(toprocess->host, tptr, ptr-tptr);
					toprocess->host[ptr-tptr] = 0;
				}
				break;
			}
		}
	}
	if(!req)
	{
		free(req);
	}	
}


//
// Associates new connection with a database entry
//
void newconnection(int c)
{	
	socklen_t addrlen; 
	struct sockaddr addr;
	struct client*cur;
	int g_which;

	getpeername(c, &addr, &addrlen);

	for(g_which = 0;g_which < MAX;g_which++)
	{	
		if(g_dbase[g_which].active == FALSE)
		{
			break;
		}
	}
	cur = &g_dbase[g_which];
	cur->active = TRUE;
	cur->clientfd = c;
	cur->tcmax = LARGE;
	cur->tsmax = LARGE;
	cur->todo |= READCLIENT;

	// If memory has been allocated before
	if(cur->toserver)
	{
		// Just zero it
		memset(cur->toserver, 0, cur->tsmax);
	}
	else
	{
		// If not, than malloc
		cur->toserver = CHR(cur->tsmax);
	}
	fcntl(c, F_SETFL, O_NONBLOCK);

	return;
}

int relaysetup(struct client*t)
{
	struct sockaddr_in  name;
	struct hostent *hp;
	int ret;

	g_forsig = t;
	// We don't have an active connection to the server yet
	hp = gethostbyname(t->host);
	if(!hp)	//Couldn't resolve
	{
		printf("Couldn't resolve %s!", t->host);
		fflush(stdout);	
		return(0);	//This is not a stupendous response
	}

	t->serverfd = socket(AF_INET, SOCK_STREAM, 0);
	name.sin_family = AF_INET;
	name.sin_port = htons(t->port);
	memcpy(&name.sin_addr.s_addr, hp->h_addr, hp->h_length);
	ret = connect(t->serverfd, (const struct sockaddr*)&name, sizeof(name));
	fcntl(t->serverfd, F_SETFL, O_NONBLOCK);

	if(!t->toclient)
	{
		t->coffset = t->toclient = CHR(t->tcmax);
	}

	t->todo |= WRITESERVER;

	return(1);
}

//
// Some things can be enqueued
//
void sendstuff()
{	
	int size,
	    tsize;

	struct client*t;

	// Check stdin
#ifdef _DEBUG
	if(FD_ISSET(0, &g_rg_fds))
	{
		getchar();
		setmarker();
	}
#endif // _DEBUG

	for(g_which = 0;g_which < MAX;g_which++)
	{	
		if(g_dbase[g_which].active == TRUE)
		{
			t = &g_dbase[g_which];
			
			// Reading from the client
			if(FD_ISSET(t->clientfd, &g_rg_fds))
			{
				size = recv(t->clientfd, t->toserver, t->tsmax, 0);
				g_fds[t->clientfd] = 'r';
				
				if(size == 0)
				{
					done(t);
				}

				while(size > 0)
				{	
					t->todo |= WRITESERVER;

					if(t->tsmax - size < 10)
					{
						t->toserver = realloc(t->toserver, t->tsmax << 1);
						memset(t->toserver+t->tsmax, 0, t->tsmax);
						t->tsmax <<= 1;
					}
					tsize = recv(t->clientfd, t->toserver + size, t->tsmax - size, 0);
					if(tsize < 1)
					{
						break;
					}
					else
					{
						size += tsize;
					}
				}

				t->tssize = size;
				t->soffset = t->toserver;
				process(t);

				if(!t->serverfd)
				{
					relaysetup(t);
				}
				t->todo &= ~READCLIENT;
			}
			// Writing to the server
			if(FD_ISSET(t->serverfd, &g_wg_fds))
			{	
				g_fds[t->serverfd] = 'W';
				size = write(t->serverfd, t->soffset, t->tssize - (t->soffset - t->toserver));
				t->soffset += size;

				if(t->soffset - t->toserver == t->tssize)//we are done
				{
					t->todo |= READSERVER;
					t->todo &= ~WRITESERVER;
				}
			}
			// Reading from the server
			if(FD_ISSET(t->serverfd, &g_rg_fds))
			{	
				g_fds[t->serverfd] = 'R';
				// size = recv(t->serverfd, t->toclient, t->tcmax, 0);
				t->tcsize = recv(t->serverfd, t->toclient, t->tcmax, 0);
				t->coffset = t->toclient;

				if(t->tcsize>0)
				{
					t->todo |= WRITECLIENT;
				}
				else
				{
					done(t);
				}

				t->todo &= ~READSERVER;
			}
			if(FD_ISSET(t->clientfd, &g_wg_fds))
			{	
				size = write(t->clientfd, t->coffset, t->tcsize-(t->coffset-t->toclient));
				g_fds[t->clientfd] = 'w';
				t->coffset += size;

				if(t->tcsize == t->coffset-t->toclient)
				{	
					t->todo |= READCLIENT;
					t->todo &= ~WRITECLIENT;
					t->todo |= READSERVER;
				}
			}
			if(FD_ISSET(t->clientfd, &g_eg_fds) || FD_ISSET(t->serverfd, &g_eg_fds))
			{	
				done(t);
			}
			fflush(stdout);
		}
	}
}

void doselect()
{	
	int 	hi, 
		i;	

	struct client *c;

	FD_ZERO(&g_rg_fds);
	FD_ZERO(&g_eg_fds);
	FD_ZERO(&g_wg_fds);
	FD_SET(g_proxyfd, &g_rg_fds);

	// Stdin
	FD_SET(0, &g_rg_fds);

	hi = g_proxyfd;

	for(i = 0;i < MAX;i++)
	{	
		if(g_dbase[i].active == TRUE)
		{
			c = &g_dbase[i];

			if(c->todo & READCLIENT)
			{
				FD_SET(c->clientfd, &g_rg_fds);
			}
			if(c->todo & READSERVER)
			{
				FD_SET(c->serverfd, &g_rg_fds);
			}
			if(c->todo & WRITECLIENT)
			{
				FD_SET(c->clientfd, &g_wg_fds);
			}
			if(c->todo & WRITESERVER)	
			{
				FD_SET(c->serverfd, &g_wg_fds);
			}

			FD_SET(c->serverfd, &g_eg_fds);
			FD_SET(c->clientfd, &g_eg_fds);
			hi = GETMAX( GETMAX(c->clientfd, c->serverfd) , hi);
		}
	}
	if(g_screen == 1)
	{
		for(i = 0;i < 128;i++)
		{	
			if(g_fds[i])
			{
				for(i = 0;i < 60;i++)
				{	
					if(g_fds[i])
					{
						printf("%c", g_fds[i]);
					}
					else
					{
						printf(" ");
					}
				}
				printf("\n");
				break;
			}
		}
	}

	memset(g_fds, 0, 128);

	select(hi + 1, &g_rg_fds, &g_wg_fds, &g_eg_fds, 0);
}

int main(int argc, char*argv[])
{ 	
	socklen_t	addrlen;

	int 	ret, 
		port = 0, 
		try = 0;

	struct	sockaddr addr;
	struct 	sockaddr_in 	proxy, *ptr;
	struct	hostent *gethostbyaddr();

	char *progname = argv[0];

	signal(SIGPIPE, handle_bp);

	FD_ZERO(&g_rg_fds);
	FD_ZERO(&g_eg_fds);
	FD_ZERO(&g_wg_fds);

#ifdef _DEBUG
	g_flog = fopen("/dev/stdout", "w");

	if(!g_flog)
	{
		exit(0);
	}
#endif // _DEBUG

	addr.sa_family = AF_INET;
	strcpy(addr.sa_data, "somename");
	memset(g_dbase, 0, sizeof(g_dbase));
	while(argc > 0)
	{
		if(ISA("-P", "--port"))
		{
			if(--argc)  
			{
				port = htons(atoi(*++argv));
			}
		}
#ifdef _DEBUG
		else if(ISA("-S", "--screen"))
		{
			g_screen = 1;
		}
		else if(ISA("-v", "--verbose"))
		{
			g_logall = 1;
		}
		else if(ISA("-F", "--fast"))
		{
			g_fast = 1;
		}
#endif // _DEBUG
		else if(ISA("-T", "--try"))
		{
			try = 1;
		}
		else if(ISA("-A", "--absolute"))
		{
			if(--argc)
			{
				g_absolute = *++argv;
			}
		}
		else if(ISA("-H", "--help"))
		{
			printf("%s\n", progname);
			printf("\t-P --port\tWhat part to run on\n");
			printf("\t-T --try\tIncrementally try ports\n");
			printf("\t-A --absolute\tAbsolute address for proxying to\n");
#ifdef _DEBUG
			printf("\t-v --verbose\tVerbose logging\n");
			printf("\t-S --screen\tScreen printing\n");
			printf("\t-F --fast\tSkip over lots of helpful formatting tricks\n");
#endif // _DEBUG
			exit(0);
		}

		argv++;
		if(argc)argc--;
	}
	if(!port)
	{
		port = htons(8080);	//8080 is a default proxy port
	}

	proxy.sin_family = AF_INET;
	proxy.sin_port = port;
	proxy.sin_addr.s_addr = INADDR_ANY;
	g_proxyfd = socket(PF_INET, SOCK_STREAM, 0);

	while(bind(g_proxyfd, (struct sockaddr*)&proxy, sizeof(proxy)) < 0)
	{	
		fprintf(stderr, ".");

		if(!try)
		{
			usleep(100000);
		}
		else
		{	
			proxy.sin_port += htons(1);
			fprintf(stderr, "Trying port %d...", ntohs(proxy.sin_port));
		}
	}

	addrlen = sizeof(addr);
	ret = getsockname(g_proxyfd, &addr, &addrlen);
	ptr = (struct sockaddr_in*)&addr;
	listen(g_proxyfd, SMALL);

	if(try)
	{
		printf("Listening on port %d", ntohs(proxy.sin_port));
		fflush(0);
	}

	// This is the main loop
	for(;;)
	{
		doselect();

		if(FD_ISSET(g_proxyfd, &g_rg_fds))
		{
			newconnection(accept(g_proxyfd, 0, 0));
		}

		sendstuff();
	}
	return(0);
}
