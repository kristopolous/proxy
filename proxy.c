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

#include <errno.h>

#include <arpa/inet.h>
#include <netinet/in.h>

#include <sys/time.h>
#include <sys/types.h>
#include <sys/socket.h>

#include <fcntl.h>
#include <netdb.h>
#include <signal.h>
#include <string.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#define LARGE 16384
#define MAX   128
#define HEX   "0123456789ABCDEF"

#define EMIT  emit(
#define END   ,_END);

extern int errno;

enum {
  FALSE, 
  TRUE, 

  TYPE,

  TEXT, 
  CONNECTION,
  
  _END
};

#define CHR(_size_)       (char*) malloc(sizeof(char)*(_size_))
#define ISA(_s_, _l_)     ( ( !strcmp(*argv, _s_) ) || ( !strcmp(*argv, _l_) ) )
#define GETMAX(_a_, _b_)  ( ( (_a_) > (_b_) ) ? (_a_) : (_b_) )
#define NULLFREE(_a_)     if((_a_)) { free((_a_)); (_a_) = 0; }

#define READSERVER    0x01
#define READCLIENT    0x02
#define WRITESERVER   0x04
#define WRITECLIENT   0x08

struct client {
  char  
    // IPADDR of client
    clientHost[INET6_ADDRSTRLEN],

    // The host to connect to
    *host, 

    // Response from host
    *toclient,

    // What to send to the server
    *toserver,  

    // Writing to the client
    *coffset,

    // Writing to the server
    *soffset,  

    // Is the client active
    active;

  int
    // To client in use size
    tcsize,

    // To server in use size
    tssize,

    // What to do
    todo,

    // File handle of client connected
    clientfd,

    // File handle of server
    serverfd,

    // What port of the host
    port;

  unsigned short
    // Port of client
    clientPort;
};

struct {
  char*host;
  int port;
} g_absolute;

char g_buf[LARGE * 3];

const char  *g_strhost = "Host: ";

int g_proxyfd;

fd_set  
  g_rg_fds, 
  g_wg_fds, 
  g_eg_fds;

struct client
  g_dbase[MAX],
  *g_forsig;

struct linger g_linger_t = { 1, 0 };

void emit(int firstarg, ...) {
  struct timeval tp;
  int type, connection;

  va_list ap;

  gettimeofday(&tp, 0);

  printf("{\"ts\":%d.%06d", (int) tp.tv_sec, (int) tp.tv_usec);

  va_start(ap, firstarg);

  for(type = firstarg; type != _END; type = va_arg(ap, int)) {

    if(type == TYPE) {
      char * tmp = va_arg(ap, char*);
      printf(",\"type\":\"%s\"", tmp);

    } else if(type == TEXT) {
      printf(",\"text\":\"%s\"", va_arg(ap, char*));

    } else if(type == CONNECTION) {
      connection = va_arg(ap, int); 
      printf(",\"id\":%d", connection);
      printf(",\"ip\":\"%s\"", g_dbase[connection].clientHost);
      printf(",\"port\":%d", g_dbase[connection].clientPort);
    }
  }

  va_end(ap);

  printf("}\n");
  fflush(0);
}

ssize_t wraprecv(int socket, void *buf, size_t len, int flags, int which) {  
  int ix = 0;

  char 
    *binbuf = g_buf,
    *ptr = (char*)buf;

  ssize_t ret = recv(socket, buf, len, flags);

  if(ret < len && ret > 0) {
    ptr[ret] = 0;
  }

  for(ix = 0; ix < ret; ix++) {
    if(ptr[ix] < 32 || ptr[ix] == '"' || ptr[ix] == '\\' || ptr[ix] == 0x7F) {
      binbuf[0] = '\\';

      switch(ptr[ix]) {
        case '"' : binbuf[1] = '"'; binbuf += 2; break;
        case '\\': binbuf[1] = '\\'; binbuf += 2; break;
        case '\b': binbuf[1] = 'b'; binbuf += 2; break;
        case '\f': binbuf[1] = 'f'; binbuf += 2; break;
        case '\n': binbuf[1] = 'n'; binbuf += 2; break;
        case '\r': binbuf[1] = 'r'; binbuf += 2; break;
        case '\t': binbuf[1] = 't'; binbuf += 2; break;

        default:
          binbuf[1] = 'u';
          binbuf[2] = HEX[(unsigned char)ptr[ix] >> 4];
          binbuf[3] = HEX[ptr[ix] & 0xf];

          if(ix < ret) {
            ix++;
            binbuf[4] = HEX[(unsigned char)ptr[ix] >> 4];
            binbuf[5] = HEX[ptr[ix] & 0xf];
          } else {
            binbuf[4] = '0';
            binbuf[5] = '0';
          }

          binbuf += 6;
          break;
      }
    } else {
      binbuf[0] = ptr[ix];
      binbuf += 1;
    }
  }

  binbuf[0] = 0;

  EMIT
    TYPE, "payload",
    CONNECTION, which,
    TEXT, g_buf
  END

  return(ret);
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

    NULLFREE(cur->toserver);
    NULLFREE(cur->toclient);
    NULLFREE(cur->host);

    EMIT
      TYPE, "close",
      CONNECTION, id
    END

    memset(cur, 0, sizeof(struct client));
  }
}


void closeAll(int in) {
  int ix;
  struct client* pClient = g_dbase;

  for(ix = 0; ix < MAX; ix++, pClient++) {

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
  char *ptr;

  for(ptr = *ptr_in + 1; *ptr >= '0' && *ptr <= '9'; ptr++) {
    number *= 10;
    number += *ptr - '0';
  }

  return number;
}

void buf_emit(char* type) {
  EMIT
    TYPE, type,
    TEXT, g_buf
  END
}

void error_con(int connection, char*message) {
  sprintf(g_buf, message, strerror(errno));
  EMIT
    TYPE, "error",
    CONNECTION, connection,
    TEXT, g_buf
  END
}

void error_gen(int test, char*message) {
  if(test) {
    sprintf(g_buf, message, strerror(errno));
    buf_emit("error");
  }
}

int relaysetup(struct client*t) {
  struct sockaddr_in name;
  struct hostent *hp;

  if (t->active) {
    if(t->serverfd) {
      EMIT
        TYPE, "shutdown"
      END

      shutdown(t->serverfd, SHUT_RDWR);
      close(t->serverfd);
    }

    t->serverfd = 0;
  }

  g_forsig = t;

  // We don't have an active connection to the server yet
  hp = gethostbyname(t->host);

  // Couldn't resolve
  if(!hp) {
    sprintf(g_buf, "Couldn't resolve host %s : %s", t->host, strerror(errno));
    buf_emit("error");

    return(0);  // This is not a stupendous response
  }

  t->serverfd = socket(AF_INET, SOCK_STREAM, 0);

  error_gen(
    setsockopt(t->serverfd, SOL_SOCKET, SO_LINGER, &g_linger_t, sizeof(struct linger)),
    "Couldn't set socket options: %s"
  );

  name.sin_family = AF_INET;
  name.sin_port = htons(t->port);
  memcpy(&name.sin_addr.s_addr, hp->h_addr, hp->h_length);

  error_gen(
    connect(t->serverfd, (const struct sockaddr*)&name, sizeof(name)),
    "Couldn't connect: %s"
  );

  sprintf(g_buf, "Connecting to %s:%d", t->host, t->port);
  buf_emit("info");

  fcntl(t->serverfd, F_SETFL, O_NONBLOCK);

  t->coffset = t->toclient;
  t->todo |= WRITESERVER;

  return(1);
}


// Parse a HTTP request
void process(struct client*toprocess) {  
  char   
    *ptr, 
    *path_start, 
    *location_start,
    *host_start,
    *host_compare = 0,
    *payload_start = toprocess->toserver,
    *payload_end = toprocess->toserver + toprocess->tssize;

  // The request is something like:
  // GET http://website/page HTTP/1.1 ...
  // We try to get the hostname out of this and replace it with
  // GET /page HTTP/1.1 ...
  // This just copies the part GET << THIS>> HTTP/1.0 to req

  if(g_absolute.host) {

    if(!toprocess->host) {
      toprocess->host = copybytes(g_absolute.host, g_absolute.host + strlen(g_absolute.host));
      toprocess->port = g_absolute.port;
      relaysetup(toprocess);
    }
  } else {  
    ptr = payload_start;

    // TYPE[ ]Request
    for(; ptr[0] != ' '; ptr++);

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
          host_compare = copybytes(host_start, ptr);

          // This forwards the pointer
          toprocess->port = my_atoi(&ptr);
          break;
        }

        // We assume we are at the end of the HOST
        if(*ptr == '/') {
          host_compare = copybytes(host_start, ptr);
  
          toprocess->port = 80;
          break;
        }
      }

      if(!toprocess->host) {
        toprocess->host = host_compare;

        relaysetup(toprocess);
      } else if (strcmp(host_compare, toprocess->host)) {

        free(toprocess->host);

        toprocess->host = host_compare;
        relaysetup(toprocess);
      }

      path_start = ptr;

      // This shifts the request to exclude the host and proto info in the GET part
      memmove(location_start, path_start, payload_end - path_start + 1);

      toprocess->tssize = (payload_end - path_start) + (location_start - payload_start);
    }
  }
}


// Associates new connection with a database entry
void newconnection(int connectionID) {  
  struct client*cur;
  int which;

  fcntl(connectionID, F_SETFL, O_NONBLOCK);

  for(
    which = 0;
    (which < MAX && g_dbase[which].active);
    which++
  );

  cur = &g_dbase[which];
  memset(cur, 0, sizeof(struct client));

  cur->active   = TRUE;
  cur->clientfd = connectionID;
  cur->todo     = READCLIENT;
  cur->toserver = CHR(LARGE);
  cur->toclient = CHR(LARGE);

  // Inspired from http://beej.us/guide/bgnet/output/html/multipage/getpeernameman.html
  {
    union {
      struct sockaddr addr;
      struct sockaddr_storage storage;
      struct sockaddr_in in;
    } client;

    socklen_t len = sizeof(client);

    getpeername(
      connectionID, 
      &client.addr, 
      &len
    );

    inet_ntop(
      AF_INET, 
      &client.in.sin_addr, 

      // Set the hostname
      cur->clientHost,  
      sizeof(cur->clientHost)
    );

    cur->clientPort = ntohs(client.in.sin_port);
  }

  return;
}

// Some things can be enqueued
void sendstuff() {  

  int 
    which,
    size;

  struct client *t = g_dbase;

  for(which = 0;which < MAX;which++, t++) {  
    if(t->active) {
      
      // Reading from the client
      if(FD_ISSET(t->clientfd, &g_rg_fds)) {
        t->tssize = wraprecv(t->clientfd, t->toserver, LARGE, 0, which);
        t->soffset = t->toserver;

        if(t->tssize > 0) {
          t->todo |= WRITESERVER;
          process(t);

        } else if (t->tssize == -1) {
          error_con(which, "Failed to read from client : %s");
        } else {
          done(t, which);
        }

        // Make sure we write this entire payload to the server
        // before we try to read any more information from the 
        // client
        t->todo &= ~READCLIENT;
      }

      // Writing to the server
      if(FD_ISSET(t->serverfd, &g_wg_fds)) {  
        size = write(t->serverfd, t->soffset, t->tssize - (t->soffset - t->toserver));

        if (size == -1) {
          error_con(which, "Failed to write to server : %s");
        } else {

          t->soffset += size;

          // we are done
          if(t->soffset - t->toserver == t->tssize) {
            t->todo |= READSERVER;
            t->todo &= ~WRITESERVER;
          }
        }
      }

      // Reading from the server
      if(FD_ISSET(t->serverfd, &g_rg_fds)) {  
        t->tcsize = wraprecv(t->serverfd, t->toclient, LARGE, 0, which);
        t->coffset = t->toclient;

        if(t->tcsize > 0) {
          t->todo |= WRITECLIENT;
        } else if(t->tcsize == -1) {
          error_con(which, "Failed to read from server : %s");
        } else {
          done(t, which);
        }

        t->todo &= ~READSERVER;
      }

      // Write to client
      if(FD_ISSET(t->clientfd, &g_wg_fds)) {  
        size = write(t->clientfd, t->coffset, t->tcsize - (t->coffset - t->toclient));

        if(size == -1) {
          error_con(which, "Failed to write to client : %s");
        } else {
          t->coffset += size;
        }

        if(t->coffset - t->toclient == t->tcsize) {  
          t->todo |= READCLIENT;
          t->todo &= ~WRITECLIENT;
          t->todo |= READSERVER;
        }
      }
      
      if(FD_ISSET(t->clientfd, &g_eg_fds) || FD_ISSET(t->serverfd, &g_eg_fds)) {  
        done(t, which);
      }
    }
  }
}

void doselect() {  
  int
    highestID, 
    i;  

  char toggle[5] = {0};

  struct client *pClient = g_dbase;

  FD_ZERO(&g_rg_fds);
  FD_ZERO(&g_eg_fds);
  FD_ZERO(&g_wg_fds);
  FD_SET(g_proxyfd, &g_rg_fds);

  highestID = g_proxyfd;

  for(i = 0;i < MAX; i++, pClient++) {  
    if(pClient->active == TRUE) {
      memset(toggle, 32, 4);

      if(pClient->todo & READCLIENT) {
        FD_SET(pClient->clientfd, &g_rg_fds);
        toggle[0] = 'r';
      }

      if(pClient->todo & READSERVER) {
        FD_SET(pClient->serverfd, &g_rg_fds);
        toggle[1] = 'R';
      }

      if(pClient->todo & WRITECLIENT) {
        FD_SET(pClient->clientfd, &g_wg_fds);
        toggle[2] = 'w';
      }

      if(pClient->todo & WRITESERVER)  {
        FD_SET(pClient->serverfd, &g_wg_fds);
        toggle[3] = 'W';
      }

      FD_SET(pClient->serverfd, &g_eg_fds);
      FD_SET(pClient->clientfd, &g_eg_fds);

      highestID = GETMAX( 
        GETMAX(pClient->clientfd, pClient->serverfd), 
        highestID
      );

      // print the current state of affairs
      EMIT
        TYPE, "status",
        CONNECTION, i,
        TEXT, toggle
      END
    }
  }

  select(highestID + 1, &g_rg_fds, &g_wg_fds, &g_eg_fds, 0);
}

int main(int argc, char*argv[]) {   
  int port = htons(8080);

  struct sockaddr addr;
  struct sockaddr_in   proxy;
  socklen_t addrlen = sizeof(addr);

  char 
    *progname = argv[0],
    *ptr;

  signal(SIGPIPE, handle_bp);
  signal(SIGQUIT, closeAll);
  signal(SIGINT, closeAll);

  FD_ZERO(&g_rg_fds);
  FD_ZERO(&g_eg_fds);
  FD_ZERO(&g_wg_fds);

  memset(g_dbase, 0, sizeof(g_dbase));

  g_absolute.host = 0;

  for(; argc > 0; argc--, argv++) {
    if(ISA("-p", "--port")) {
      if(--argc)  {
        port = htons(atoi(*++argv));
      }
    } else if(ISA("-a", "--absolute")) {
      if(--argc) {
        argv++;

        for(ptr = argv[0];*ptr != ':';ptr++);
        ptr[0] = 0;
        g_absolute.host = copybytes(argv[0], ptr);
        g_absolute.port = my_atoi(&ptr);
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

  error_gen(
    setsockopt(g_proxyfd, SOL_SOCKET, SO_LINGER, &g_linger_t, sizeof(struct linger)),
    "setsockopt: "
  );

  while(bind(g_proxyfd, (struct sockaddr*)&proxy, sizeof(proxy)) < 0) {  
    sprintf(g_buf, "Failed to bind to port %d", ntohs(proxy.sin_port));
    buf_emit("info");

    proxy.sin_port += htons(1);
    sprintf(g_buf, "Trying port %d", ntohs(proxy.sin_port));
    buf_emit("info");
  }

  error_gen(
    getsockname(g_proxyfd, &addr, &addrlen),
    "getsockname: "
  ); 

  error_gen(
    listen(g_proxyfd, MAX),
    "listen: "
  );

  sprintf(g_buf, "Listening on port %d", ntohs(proxy.sin_port));
  buf_emit("info");

  if(g_absolute.host) {
    sprintf(g_buf, "Forwarding requests to %s", g_absolute.host);
    buf_emit("info");
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
