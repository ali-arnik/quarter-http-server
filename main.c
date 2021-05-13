#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>

#include <sys/stat.h>
#include <fcntl.h>

#include <err.h>
#include <errno.h>

#include <pthread.h>

#include <sqlite3.h>

#define POLL_LENGTH 4
#define PORT 8080
#define IP4 "127.0.0.1"

#define SHOWLOG 1
#define SHOWERR 1
#define SHOWWARN 1

#if SHOWERR
#define MERR(status, args_format, arg1, arg2, arg3) \
{ \
err(status, args_format, arg1, arg2, arg3); \
fflush(NULL); \
}
#else
#define MERR(status, args_format, arg1, arg2, arg3) {err(status, "");}
#endif

#if SHOWLOG
#define MLOG(args_format, arg1, arg2, arg3, arg4) { \
printf(args_format, arg1, arg2, arg3, arg4); \
fflush(NULL); \
}
#else
#define MLOG(args_format, arg1, arg2, arg3, arg4) {;}
#endif

#if SHOWWARN
#define MWARN(args_format, arg1, arg2, arg3, arg4) { \
warn(args_format, arg1, arg2, arg3, arg4); \
fflush(NULL); \
}
#else
#define MWARN(args_format, arg1, arg2, arg3, arg4) {;}
#endif

#define GET_SLASH "GET / HTTP/1.1\r\n"
#define GET_IMAGE "GET /store_front.jpeg HTTP/1.1\r\n"

/* SQLite query buffer  */
char *query;

struct conn_info
{
  int fd;
  pthread_t thread_id;
  struct sockaddr_in peer_addr;
  socklen_t socklen;
};

void* conn_handler(void *arg);

int main()
{
  int sfd, ret;
  struct sockaddr_in addr;
  pthread_attr_t attr;
  struct conn_info *ci;
  
  pthread_mutex_t mutex;
  
  sqlite3 *db_conn_stat;
  
  sfd = socket(AF_INET, SOCK_STREAM, 0);
  if (sfd == -1)
    {
      MERR(EXIT_FAILURE, "%d: %s: %s", __LINE__, __func__, strerror(errno));
    }
  else
    {
      MLOG("%s: %d: Socket created and file descriptor is: %d\n",
	   __FILE__, __LINE__, sfd, NULL);
    }
  
  memset(&addr, '\0', sizeof(struct sockaddr_in));
 
  addr.sin_family = AF_INET;
  addr.sin_port = htons(PORT);
  if (!inet_pton(AF_INET, IP4, &addr.sin_addr))
    {
      MERR(EXIT_FAILURE, "%s: %d: Wrong ipv4 format.\n", __FILE__, __LINE__,
	   NULL);
    }

  if (bind(sfd, (struct sockaddr *) &addr, sizeof(struct sockaddr)) == -1)
    {
      MERR(EXIT_FAILURE, "%s: %d: %s", __FILE__, __LINE__, strerror(errno));
    }
  else
    {
      MLOG("%s: %d: Socket binded to %s ip and port is %d\n",
	     __FILE__, __LINE__, IP4, PORT);
    }
  
  if (listen(sfd, POLL_LENGTH) == -1)
    {
      MERR(EXIT_FAILURE, "%s: %d: %s", __FILE__, __LINE__, strerror(errno));
    }
  else
    {
      MLOG("%s: %d: Listening... .\n", __FILE__, __LINE__, NULL, NULL);
    }

  ret = pthread_attr_init(&attr);
  if (ret)
    {
      MERR(EXIT_FAILURE, "%s: %d: error number: %d", __FILE__, __LINE__, ret);
    }

  /* Open/Create sqlite connections statistics database  */
  ret = sqlite3_open_v2("./db_conn_stat", &db_conn_stat, SQLITE_OPEN_READWRITE,
			NULL);
	  
  if (ret == SQLITE_CANTOPEN)
    {
      MLOG("%s: %d: sqlite: %s, creating database ... \n", __FILE__, __LINE__,
	   sqlite3_errstr(ret), NULL);

      ret = sqlite3_open_v2("./db_conn_stat", &db_conn_stat,
			    SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, NULL);
      if (ret != SQLITE_OK)
	{
	  MERR(EXIT_FAILURE, "%s: %d: sqlite: %s\n", __FILE__, __LINE__,
	       sqlite3_errstr(ret));
	}

      MLOG("%s: %d: sqlite: database created.\n", __FILE__, __LINE__, NULL,
	   NULL);
      
      ret = asprintf(&query, "CREATE TABLE conn_stat(\
IP TEXT PRIMARY KEY NOT NULL,\
VIS_COUNT INT DEFAULT 0)");
      if (ret == -1)
	{
	  MERR(EXIT_FAILURE, "%s: %d: Could not allocate memory for query.\n",
	       __FILE__, __LINE__, NULL);
	}
      
      ret = sqlite3_exec(db_conn_stat, query, NULL, 0, NULL);
      free(query);
      query = NULL;
      
      if (ret != SQLITE_OK)
	{
	  MERR(EXIT_FAILURE, "%s: %d: Could not create conn_stat table: %s\n",
	       __FILE__, __LINE__, sqlite3_errstr(ret));
	}

      MLOG("%s: %d: sqlite: table conn_stat created.\n", __FILE__, __LINE__,
	   NULL, NULL);
    }
  
  pthread_mutex_init(&mutex, NULL);
  while (1)
    {
      ci = (struct conn_info*) malloc(sizeof(struct conn_info));
      if (ci == NULL)
	{
	  MWARN("%s: %d: malloc couldn't allocate memmory for ci struct!\n",
		__FILE__, __LINE__, NULL, NULL);
	  sleep(5);
	  continue;
	}
      
      MLOG("\n%s: %d: Waiting for a new connection ...\n", __FILE__, __LINE__,
	   NULL, NULL);
      ci->socklen = sizeof(ci->peer_addr);
      ci->fd = accept(sfd, (struct sockaddr *) &ci->peer_addr, &ci->socklen);
      if (ci->fd == -1)
	{
	  MWARN("\n%s: %d: Accept() returned -1\n", __FILE__, __LINE__, NULL,
		NULL);
	  continue;
	}
      
      MLOG("\n%s: %d: New connection from %s address with fd %d.\n",
	   __FILE__, __LINE__, inet_ntoa(ci->peer_addr.sin_addr), ci->fd);

      ret = asprintf(&query, "INSERT INTO conn_stat (ip, vis_count) VALUES \
('%s', 1) ON CONFLICT(ip) DO UPDATE SET vis_count=vis_count+1;",
		     inet_ntoa(ci->peer_addr.sin_addr));
      
        if (ret == -1)
	{
	  MERR(EXIT_FAILURE, "%s: %d: Could not allocate memory for query.\n",
	       __FILE__, __LINE__, NULL);
	}

      ret = sqlite3_exec(db_conn_stat, query, NULL, 0, NULL);
      free(query);
      query = NULL;
      
      if (ret != SQLITE_OK)
	{
	  MERR(EXIT_FAILURE, "%s: %d: Could update table conn_stat: %s\n",
	       __FILE__, __LINE__, sqlite3_errstr(ret));
	}
      
      pthread_mutex_lock(&mutex);
      if (!pthread_create(&ci->thread_id, &attr, &conn_handler, ci))
	{
	  MLOG("\n%s: %d: New thread created for %s address (%d fd).\n",
	       __FILE__, __LINE__, inet_ntoa(ci->peer_addr.sin_addr), ci->fd);
	}
      else
	{
	  MERR(EXIT_FAILURE, "%s: %d: %s", __FILE__, __LINE__, strerror(errno));
	}
      pthread_mutex_unlock(&mutex);
    }
}

void* conn_handler(void *arg)
{
  int ret, fd;
  char buffer[1000];
  int size, cnt;
  struct conn_info *ci = (struct conn_info *) arg;
  char *uszbuf1, *uszbuf2;
  char status_line[100], filename[100];
  
  MLOG("\n%s: %d: Handling the connection with %s address and file %d.\n",
       __FILE__, __LINE__, inet_ntoa(ci->peer_addr.sin_addr), ci->fd);

  size = 0;
  while ((ret = recv(ci->fd, buffer, sizeof(buffer), 0)) != 0)
    {
      if ( ret < 0 )
	{
	  MERR(EXIT_FAILURE, "%s: %d: %s", __FILE__, __LINE__, strerror(errno));
	}
      
      size += ret;
      
      uszbuf1 = realloc(uszbuf1, size);
      if (uszbuf1 == NULL)
	{
	  MERR(EXIT_FAILURE, "%s: %d: %s", __FILE__, __LINE__, strerror(errno));
	}
      memcpy(uszbuf1+size-ret, buffer, ret);

      
      if (strstr(uszbuf1, "\r\n\r\n") != NULL)
	break;
    }
  
  // MLOG("\n%s\n", uszbuf1, NULL, NULL, NULL);

  if (strncmp(uszbuf1, GET_SLASH, sizeof(GET_SLASH)-1) == 0)
    {
      strcpy(status_line, "HTTP/1.1 200 OK");
      strcpy(filename, "index.html");
    }
  else if (strncmp(uszbuf1, GET_IMAGE, sizeof(GET_IMAGE)-1) == 0)
    {
      strcpy(status_line, "HTTP/1.1 200 OK");
      strcpy(filename, "store_front.jpeg");
    }
  else
    {
      strcpy(status_line, "HTTP/1.1 404 NOT FOUND");
      strcpy(filename, "404.html");
    }

  free(uszbuf1);
  uszbuf1 = NULL;
  
  fd = open(filename, O_RDONLY);
  if (fd == -1)
    {
      MERR(EXIT_FAILURE, "%s: %d: %s", __FILE__, __LINE__, strerror(errno));
    }
  
  size = 0;
  while ((ret = read(fd, buffer, sizeof(buffer))) != 0)
    {
      if (ret < 0)
	{
	  MERR(EXIT_FAILURE, "%s: %d: %s", __FILE__, __LINE__, strerror(errno));
	}
      
      size += ret;
      uszbuf1 = realloc(uszbuf1, size);
      if (uszbuf1 == NULL)
	{
	  MERR(EXIT_FAILURE, "%s: %d: %s", __FILE__, __LINE__, strerror(errno));
	}
      memcpy(uszbuf1+size-ret, buffer, ret);
    }
  close(fd);
  
  ret = asprintf(&uszbuf2, "%s\r\nContent-Length: %d\r\n\r\n", status_line,
		  size);

  size += ret;
  uszbuf2 = realloc(uszbuf2, size);
  memcpy(uszbuf2+ret, uszbuf1, size-ret);
  
  if (size == -1)
    {
      MERR(EXIT_FAILURE, "%s: %d: Could not allocate memory for query.\n",
	   __FILE__, __LINE__, NULL);
    }

  // MLOG("\n%s\n", uszbuf2, NULL, NULL, NULL);
  
  cnt = 0;
  while ((ret = send(ci->fd, uszbuf2+cnt, size, 0)) != 0)
    {
      if (ret <= 0)
	{
	  MERR(EXIT_FAILURE, "%s: %d: %s", __FILE__, __LINE__, strerror(errno));
	}
      size -= ret;
      cnt += ret;
    }
  
  MLOG("\n%s: %d: Connection handled and data sent.\n", __FILE__, __LINE__,
       NULL, NULL);

  free(uszbuf1);
  free(uszbuf2);
  uszbuf1 = NULL;
  uszbuf2 = NULL;
  
  close(ci->fd);
  free(ci);
}

