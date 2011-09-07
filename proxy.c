/* 
 * Debugging proxy
 * See https://github.com/kristopolous/proxy for more details
 *
 * (c) Copyright 2005, 2008, 2011 Christopher J. McKenzie 
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy 
 * of this software and associated documentation files (the "Software"), to deal 
 * in the Software without restriction, including without limitation the rights 
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies 
 * of the Software, and to permit persons to whom the Software is furnished to do 
 * so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all 
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, 
 * INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A 
 * PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT 
 * HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION 
 * OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE 
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include <arpa/inet.h>

#include <netinet/in.h>

#include <sys/stat.h>
#include <sys/timeb.h>
#include <sys/types.h>
#include <sys/socket.h>

#include <fcntl.h>
#include <langinfo.h>
#include <netdb.h>
#include <signal.h>
#include <string.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "cjson/cJSON.h"

#define LARGE  16384
#define MAX  128

#define EMIT emit(
#define END ,_END);

enum {
  FALSE, 
  TRUE, 

  TYPE,

  TEXT, 
  CONNECTION,
  
  _END
};

#define CHR(sz)   (char*)malloc(sizeof(char)*(sz))

#define READSERVER  0x01
#define READCLIENT  0x02
#define WRITESERVER  0x04
#define WRITECLIENT  0x08

struct client {
  char  
    *reqtype,
    *host,     //The host to connect to
    *oldhost,   //The currently connected host
    
    *toclient,   //Response from host
    *toserver,   //What to send to the server

    *coffset,   //Writing to the client
    *soffset,   //Writing to the server

    active;    //Is the client active

  int
    tcsize,   //To client in use size
    tssize,   //To server in use size

    tcmax,     //Buffer size
    tsmax,     //Buffer size
  
    todo,     //What to do

    clientfd,   //File handle of client connected
    serverfd,   //File handle of server
    proto,     //What protocol is in use
    port;    //What port of the host
};

#define ISA(_s_, _l_)     ( ( !strcmp(*argv, _s_) ) || ( !strcmp(*argv, _l_) ) )
#define GETMAX(_a_, _b_)  ( ( (_a_) > (_b_) ) ? (_a_) : (_b_) )
#define NULLFREE(_a_)    if((_a_)) { free((_a_)); (_a_) = 0; }

char
  g_buf[LARGE],
  *g_absolute = 0,
  g_fds[MAX];

const char  *g_strhost = "Host: ";

int 
  g_clientsize = 0, 
  g_proxyfd,
  g_which;

fd_set  
  g_rg_fds, 
  g_wg_fds, 
  g_eg_fds;

struct client
  g_dbase[MAX],
  *g_forsig;

struct linger g_linger_t = { 1, 0 };

void done(struct client*, int id);

void emit(int firstarg, ...) {
	cJSON *root = cJSON_CreateObject();	

  struct timeb tp;
  double ts;

  va_list ap;

  char 
    *string,
    *output;

  int 
    ix,
    len,
    type;

  ftime(&tp);
  ts = tp.time * 1000 + tp.millitm;

  cJSON_AddNumberToObject(root, "timestamp", ts / 1000);

  va_start(ap, firstarg);

  for(type = firstarg; type != _END; type = va_arg(ap, int)) {

    if(type == TYPE) {
      cJSON_AddStringToObject(root, "type", va_arg(ap, char*));
    } else if(type == TEXT) {
      cJSON_AddStringToObject(root, "text", va_arg(ap, char*));
    } else if(type == CONNECTION) {
      cJSON_AddNumberToObject(root, "connection", va_arg(ap, int));
    }
  }

  va_end(ap);

  output = cJSON_PrintUnformatted(root);
  printf("%s\n", output);
  fflush(0);

  cJSON_Delete(root);
  free(output);
}

ssize_t wrapwrite(int fd, const void*buf, size_t count) {
  ssize_t ret;  

  ret = write(fd, buf, count);

  return(ret);
}

ssize_t wraprecv(int s, void *buf, size_t len, int flags) {  
  ssize_t ret;  
  int ix = 0;
  char *binbuf, *binbufStart;

  ret = recv(s, buf, len, flags);

  char *ptr = (char*)buf;
  if(ret < len && ret > 0) {
    ptr[ret] = 0;
  }

  if(ret < 1) {
    return(ret);
  }

  binbufStart = binbuf = (char*) malloc(ret * 4 + 1);

  for(ix = 0; ix < ret; ix++) {
    if( (ptr[ix] < 32 || ptr[ix] > 127) && 
        (ptr[ix] != '\n' && ptr[ix] != '\r' && ptr[ix] != '\t')
      ) {
      sprintf(binbuf, "\\u%02x", ptr[ix]);
      binbuf += 4;
    } else {
      binbuf[0] = ptr[ix];
      binbuf += 1;
    }
  }

  binbuf[0] = 0;

  EMIT
    TYPE, "payload",
    CONNECTION, g_which,
    TEXT, binbufStart
  END

  free(binbufStart);
  return(ret);
}

#define write  wrapwrite
#define recv  wraprecv

void closeAll(int in) {
  int ix;
  struct client* pClient;

  for(ix = 0; ix < MAX; ix++) {
    pClient = &g_dbase[ix];

    if(pClient->active) {

      EMIT
        TYPE, "shutdown",
        CONNECTION, ix
      END

      done(pClient, ix);
    }
  }

  EMIT
    TYPE, "shutdown",
    CONNECTION, -1
  END

  shutdown(g_proxyfd, SHUT_RDWR);
  close(g_proxyfd);

  exit(0);
}

void done(struct client*cur, int id) {
  if (cur->active) {
    if(cur->clientfd) {
      shutdown(cur->clientfd, SHUT_RDWR);
      close(cur->clientfd);
    }

    if(cur->serverfd) {
      shutdown(cur->serverfd, SHUT_RDWR);
      close(cur->serverfd);
    }

    if(cur->toserver)  {
      memset(cur->toserver, 0, cur->tsmax);
    }  

    if(cur->toclient) {
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

    EMIT
      TYPE, "close",
      CONNECTION, id
    END
  }
}

void handle_bp(int in) {
  done(g_forsig, 0);
}

char* copybytes(char*start, char*end) {
  char* copy;
  copy = (char*)malloc(end - start + 1);
  memcpy(copy, start, end - start);
  copy[end - start] = 0;
  return copy;
}

int my_atoi(char**ptr_in) {
  int number = 0;
  char *ptr = *ptr_in;

  while(ptr++) {
    if(*ptr < '0' || *ptr > '9') {
      break;
    }

    number *= 10;
    number += *ptr-'0';
  }
  return number;
}

//
// Doesn't do any socket stuff
//
void process(struct client*toprocess) {  
  char   
    *ptr, 
    *path_start, 
    *location_start,
    *host_start,
    *payload_start = toprocess->toserver,
    *payload_end = toprocess->toserver + toprocess->tssize,
    *end;

  // The request is something like:
  // GET http://website/page HTTP/1.1 ...
  // We try to get the hostname out of this and replace it with
  // GET /page HTTP/1.1 ...
  // This just copies the part GET << THIS>> HTTP/1.0 to req

  if(g_absolute) {
    toprocess->host = (char*)malloc(strlen(g_absolute));
    memcpy(toprocess->host, g_absolute, strlen(g_absolute));
    for(ptr = toprocess->host;*ptr != ':';ptr++);
    ptr[0] = 0;

    toprocess->port = my_atoi(&ptr);
  } else {  
    ptr = payload_start;

    // TYPE[ ]Request
    for(; ptr[0] != ' '; ptr++);

    // Copy the GET or PUT out and then null it
    if(toprocess->reqtype) {
      free(toprocess->reqtype);
    }

    toprocess->reqtype = copybytes(payload_start, ptr);

    // Get the start of the location
    ptr++;
    location_start = ptr;

    if(ptr[0] != '/')  {  

      // We forward the pointer past the http:// part
      if(ptr[4] == 's') {

        // Look for https requests
        ptr += 8;
      } else {
        ptr += 7;
      }

      host_start = ptr;

      // PROTO://[host:port]/path
      while(ptr++) {

        // port
        if(*ptr == ':') {  
          toprocess->host = copybytes(host_start, ptr);

          // This forwards the pointer
          toprocess->port = my_atoi(&ptr);
          break;
        }

        // We assume we are at the end of the HOST
        if(*ptr == '/') {
          toprocess->host = copybytes(host_start, ptr);
  
          toprocess->port = 80;
          break;
        }
      }

      path_start = ptr;

      // This shifts the request to exclude the host and proto info in the GET part
      memmove(location_start, path_start, payload_end - path_start);
    }

  }
}


// Associates new connection with a database entry
void newconnection(int c) {  
  socklen_t addrlen; 
  struct sockaddr addr;
  struct client*cur;
  int g_which;

  getpeername(c, &addr, &addrlen);

  for(g_which = 0;g_which < MAX;g_which++) {  
    if(g_dbase[g_which].active == FALSE) {
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
  if(cur->toserver) {
    // Just zero it
    memset(cur->toserver, 0, cur->tsmax);
  } else {
    // If not, than malloc
    cur->toserver = CHR(cur->tsmax);
  }

  fcntl(c, F_SETFL, O_NONBLOCK);

  return;
}

int relaysetup(struct client*t) {
  struct sockaddr_in  name;
  struct hostent *hp;
  int ret;

  g_forsig = t;

  // We don't have an active connection to the server yet
  hp = gethostbyname(t->host);

  // Couldn't resolve
  if(!hp)  {
    EMIT
      TYPE, "error",
      TEXT, "Couldn't resolve host!"
    END

    return(0);  //This is not a stupendous response
  }

  t->serverfd = socket(AF_INET, SOCK_STREAM, 0);

  ret = setsockopt(t->serverfd, SOL_SOCKET, SO_LINGER, &g_linger_t, sizeof(struct linger));
  if(ret) {

    EMIT
      TYPE, "error",
      TEXT, "setsockopt"
    END
  }

  name.sin_family = AF_INET;
  name.sin_port = htons(t->port);
  memcpy(&name.sin_addr.s_addr, hp->h_addr, hp->h_length);
  ret = connect(t->serverfd, (const struct sockaddr*)&name, sizeof(name));

  if(ret) {

    EMIT
      TYPE, "error",
      TEXT, "connect"
    END
  }

  fcntl(t->serverfd, F_SETFL, O_NONBLOCK);

  if(!t->toclient) {
    t->coffset = t->toclient = CHR(t->tcmax);
  }

  t->todo |= WRITESERVER;

  return(1);
}

// Some things can be enqueued
void sendstuff() {  

  int 
    size,
    tsize;

  struct client*t;

  for(g_which = 0;g_which < MAX;g_which++) {  
    if(g_dbase[g_which].active == TRUE) {
      t = &g_dbase[g_which];
      
      // Reading from the client
      if(FD_ISSET(t->clientfd, &g_rg_fds)) {
        size = recv(t->clientfd, t->toserver, t->tsmax, 0);
        g_fds[t->clientfd] = 'r';
        
        if(size == 0) {
          done(t, g_which);
        }

        while(size > 0) {  
          t->todo |= WRITESERVER;

          if(t->tsmax - size < 10) {
            t->toserver = realloc(t->toserver, t->tsmax << 1);
            memset(t->toserver + t->tsmax, 0, t->tsmax);
            t->tsmax <<= 1;
          }

          tsize = recv(t->clientfd, t->toserver + size, t->tsmax - size, 0);

          if(tsize < 1) {
            break;
          } else {
            size += tsize;
          }
        }

        t->tssize = size;
        t->soffset = t->toserver;
        process(t);

        if(!t->serverfd) {
          relaysetup(t);
        }
      }
      // Writing to the server
      if(FD_ISSET(t->serverfd, &g_wg_fds)) {  

        g_fds[t->serverfd] = 'W';
        size = write(t->serverfd, t->soffset, t->tssize - (t->soffset - t->toserver));
        t->soffset += size;

        // we are done
        if(t->soffset - t->toserver == t->tssize) {
          t->todo |= READSERVER;
          t->todo &= ~WRITESERVER;
        }
      }

      // Reading from the server
      if(FD_ISSET(t->serverfd, &g_rg_fds)) {  
        g_fds[t->serverfd] = 'R';
        t->tcsize = recv(t->serverfd, t->toclient, t->tcmax, 0);
        t->coffset = t->toclient;

        if(t->tcsize>0) {
          t->todo |= WRITECLIENT;
        } else {
          done(t, g_which);
        }

        t->todo &= ~READSERVER;
      }

      if(FD_ISSET(t->clientfd, &g_wg_fds)) {  
        size = write(t->clientfd, t->coffset, t->tcsize-(t->coffset-t->toclient));
        g_fds[t->clientfd] = 'w';
        t->coffset += size;

        if(t->tcsize == t->coffset-t->toclient) {  
          t->todo |= READCLIENT;
          t->todo &= ~WRITECLIENT;
          t->todo |= READSERVER;
        }
      }
      
      if(FD_ISSET(t->clientfd, &g_eg_fds) || FD_ISSET(t->serverfd, &g_eg_fds)) {  
        done(t, g_which);
      }
    }
  }
}

void doselect() {  
  int
    hi, 
    i;  

  char toggle[5] = {0};

  struct client *c;

  FD_ZERO(&g_rg_fds);
  FD_ZERO(&g_eg_fds);
  FD_ZERO(&g_wg_fds);
  FD_SET(g_proxyfd, &g_rg_fds);

  hi = g_proxyfd;

  for(i = 0;i < MAX;i++) {  
    if(g_dbase[i].active == TRUE) {
      memset(toggle, 32, 4);

      c = &g_dbase[i];

      if(c->todo & READCLIENT) {
        FD_SET(c->clientfd, &g_rg_fds);
        toggle[0] = 'r';
      }

      if(c->todo & READSERVER) {
        FD_SET(c->serverfd, &g_rg_fds);
        toggle[1] = 'R';
      }

      if(c->todo & WRITECLIENT) {
        FD_SET(c->clientfd, &g_wg_fds);
        toggle[2] = 'w';
      }

      if(c->todo & WRITESERVER)  {
        FD_SET(c->serverfd, &g_wg_fds);
        toggle[3] = 'W';
      }

      FD_SET(c->serverfd, &g_eg_fds);
      FD_SET(c->clientfd, &g_eg_fds);
      hi = GETMAX( GETMAX(c->clientfd, c->serverfd) , hi);

      // print the current state of affairs
      EMIT
        TYPE, "status",
        CONNECTION, i,
        TEXT, toggle
      END
    }
  }

  memset(g_fds, 0, MAX);

  select(hi + 1, &g_rg_fds, &g_wg_fds, &g_eg_fds, 0);
}

int main(int argc, char*argv[]) {   
  int 
    ret, 
    port = htons(8080);

  struct sockaddr addr;
  struct sockaddr_in   proxy;
  socklen_t addrlen = sizeof(addr);

  char *progname = argv[0];

  signal(SIGPIPE, handle_bp);
  signal(SIGQUIT, closeAll);
  signal(SIGINT, closeAll);

  FD_ZERO(&g_rg_fds);
  FD_ZERO(&g_eg_fds);
  FD_ZERO(&g_wg_fds);

  memset(g_dbase, 0, sizeof(g_dbase));

  for(; argc > 0; argc--, argv++) {
    if(ISA("-p", "--port")) {
      if(--argc)  {
        port = htons(atoi(*++argv));
      }
    } else if(ISA("-a", "--absolute")) {
      if(--argc) {
        g_absolute = *++argv;
      }
    } else if(ISA("-H", "--help")) {
      printf("%s\n", progname);
      printf("\t-p --port\tWhat part to run on\n");
      printf("\t-a --absolute\tAbsolute address for proxying to\n");
      exit(0);
    }
  }

  addr.sa_family = AF_INET;
  strcpy(addr.sa_data, "somename");
  proxy.sin_family = AF_INET;
  proxy.sin_port = port;
  proxy.sin_addr.s_addr = INADDR_ANY;
  g_proxyfd = socket(PF_INET, SOCK_STREAM, 0);

  if ( setsockopt(g_proxyfd, SOL_SOCKET, SO_LINGER, &g_linger_t, sizeof(struct linger)) ) {
    EMIT
      TYPE, "error",
      TEXT, "setsockopt"
    END
  }

  while(bind(g_proxyfd, (struct sockaddr*)&proxy, sizeof(proxy)) < 0) {  
    sprintf(g_buf, "Failed to bind to port %d", ntohs(proxy.sin_port));
    EMIT
      TYPE, "info",
      TEXT, g_buf
    END

    proxy.sin_port += htons(1);
    sprintf(g_buf, "Trying port %d", ntohs(proxy.sin_port));

    EMIT
      TYPE, "info",
      TEXT, g_buf
    END
  }

  if ( getsockname(g_proxyfd, &addr, &addrlen) ) {
    EMIT
      TYPE, "error",
      TEXT, "getsockname"
    END
  }

  if ( listen(g_proxyfd, MAX) ) {
    EMIT
      TYPE, "error",
      TEXT, "listen"
    END
  }

  sprintf(g_buf, "Listening on port %d", ntohs(proxy.sin_port));

  EMIT
    TYPE, "info",
    TEXT, g_buf
  END

  if(g_absolute) {
    sprintf(g_buf, "Forwarding requests to %s", g_absolute);
    EMIT
      TYPE, "info",
      TEXT, g_buf
    END
  }

  for(;;) {
    doselect();

    if(FD_ISSET(g_proxyfd, &g_rg_fds)) {
      newconnection(accept(g_proxyfd, 0, 0));
    }

    sendstuff();
  }

  exit(0);
}
