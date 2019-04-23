/* Copyright (C) 2017-2019 CounterFlow AI, Inc.
 *
 * You can copy, redistribute or modify this Program under the terms of
 * the GNU General Public License version 2 as published by the Free
 * Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * version 2 along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 */

/*
 *
 * author Randy Caldejon <rc@counterflowai.com>
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <time.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>
#include <syslog.h>
#include <pthread.h>
#include <errno.h>
#include <limits.h>
#include <sys/time.h>
#include <time.h>
#include <fcntl.h>

#include "dragonfly-io.h"
#include "config.h"

/*
 * ---------------------------------------------------------------------------------------
 *
 * ---------------------------------------------------------------------------------------
 */

static ssize_t writeLine (int fd, void *buffer, size_t len)
{
  	int n = 0;
        do
        {
                n = send(fd, buffer, len, MSG_NOSIGNAL);
                if (n < 0)
                {
                        switch (errno)
                        {
                        case ETIMEDOUT:
                                break;
                        case ENOBUFS:
                                break;
                        case EAGAIN:
                                break;
                        case EINVAL:
                                break;
                        default:
                                syslog(LOG_ERR, "nats send error: %s", strerror(errno));
                                perror("send");
                                exit(EXIT_FAILURE);
                        }
                }
                else if (n == 0 || errno == EIO)
                {
			return 0;
                }
                else
                        break;
        } while (1);
        return n;
}
/*************************************************************************\
*                  Copyright (C) Michael Kerrisk, 2018.                   *
*                                                                         *
* This program is free software. You may use, modify, and redistribute it *
* under the terms of the GNU Lesser General Public License as published   *
* by the Free Software Foundation, either version 3 or (at your option)   *
* any later version. This program is distributed without any warranty.    *
* See the files COPYING.lgpl-v3 and COPYING.gpl-v3 for details.           *
\*************************************************************************/

/* read_line.c

   Implementation of readLine().
*/

/* Read characters from 'fd' until a newline is encountered. If a newline
  character is not encountered in the first (n - 1) bytes, then the excess
  characters are discarded. The returned string placed in 'buf' is
  null-terminated and includes the newline character if it was read in the
  first (n - 1) bytes. The function return value is the number of bytes
  placed in buffer (which includes the newline character if encountered,
  but excludes the terminating null byte). */

static ssize_t readLine(int fd, void *buffer, size_t n)
{
    ssize_t numRead;                    /* # of bytes fetched by last read() */
    size_t totRead;                     /* Total bytes read so far */
    char *buf;
    char ch;

    if (n <= 0 || buffer == NULL) {
        errno = EINVAL;
        return -1;
    }

    buf = buffer;                       /* No pointer arithmetic on "void *" */

    totRead = 0;
    for (;;) {
        numRead = read(fd, &ch, 1);

        if (numRead == -1) {
            //if (errno == EINTR)         /* Interrupted --> restart read() */
	    if ((errno == EINTR)||(errno == EAGAIN)||(errno == EWOULDBLOCK))  
            {
	        usleep (500);
                continue;
            }
            else
                return -1;              /* Some other error */

        } else if (numRead == 0) {      /* EOF */
            if (totRead == 0)           /* No bytes read; return 0 */
                return 0;
            else                        /* Some bytes read; add '\0' */
                break;

        } else {                        /* 'numRead' must be 1 if we get here */
            if (totRead < n - 1) {      /* Discard > (n - 1) bytes */
                totRead++;
                *buf++ = ch;
            }

            if (ch == '\n')
                break;
        }
    }

    *buf = '\0';
    return totRead;
}

/*
 * ---------------------------------------------------------------------------------------
 *
 * ---------------------------------------------------------------------------------------
 */
static int nats_check_ping(int socket)
{
	char ping_buffer [64] = { '\0' };

        // PING\r\n
        int s = read(socket, ping_buffer, sizeof(ping_buffer));
        if ((s > 0) && (strncmp (ping_buffer, "PING", 4)==0))
        {
                ping_buffer[s]='\0';
                s=writeLine (socket, "PONG\r\n", 6);
                syslog(LOG_INFO, "%s: %s\n", __FUNCTION__, ping_buffer);
        }
	return s;
}

/*
 * ---------------------------------------------------------------------------------------
 *
 * ---------------------------------------------------------------------------------------
 */
static int nats_subscribe(int socket, const char *subject)
{
	int n;
        char command [PATH_MAX+PATH_MAX];
	snprintf (command, sizeof (command), "SUB %s %i\r\n", subject, socket);

        if ((n = write(socket, command, strlen(command))) < 0)
        {
                syslog(LOG_ERR, "socket write: [%s] %s\n", command, strerror(errno));
                return -1;
        }

        readLine (socket, command, sizeof (command));
        if (command[0]!='+')
        {
                syslog(LOG_ERR, "socket write: [%s] %s\n", command, strerror(errno));
                return  -1;
        } 
	return 0;
}


/*
 * ---------------------------------------------------------------------------------------
 *
 * ---------------------------------------------------------------------------------------
 */
DF_HANDLE *nats_open(const char *uri, int spec)
{
        int n = 0;
        int socket_handle = -1;
	int io_type = -1;
            
        char subject [PATH_MAX]; 
        char buffer [PATH_MAX];
        char connection [PATH_MAX];
        struct sockaddr_in addr;
        memset(&addr, 0, sizeof(addr));
   
        strcpy (buffer, uri);
        char *host = buffer;
        char *ptr = strchr(buffer, ':');
        *ptr = '\0'; ptr++;
        char *port = ptr;
        ptr = strchr(port+1, '/');
        *ptr = '\0'; ptr++;
	strncpy (subject, ptr, sizeof (subject)-1);

        addr.sin_family = AF_INET; 
        addr.sin_addr.s_addr = inet_addr(host); 
        addr.sin_port = htons(atoi (port)); 

        //if ((socket_handle = socket(AF_INET, SOCK_STREAM, 0)) < 0)
        if ((socket_handle = socket(AF_INET, SOCK_STREAM, 0)) < 0)
        {
                syslog(LOG_ERR, "unable to create socket: %s\n", strerror(errno));
                return NULL;
        }

        fprintf (stderr,"Connecting to NATS %s %s %s\n", host, port, subject);
        if (connect (socket_handle, (struct sockaddr*) &addr, sizeof(addr)) != 0) 
        { 
                syslog(LOG_ERR, "unable to connect to %s: %s\n", uri, strerror(errno));
                return NULL;
        } 

        readLine (socket_handle, buffer, sizeof (buffer));
        if (buffer[0]!='I' || buffer[1]!='N' || buffer[2]!='F' || buffer[3]!='O')
        {
                syslog(LOG_ERR, "erro reading INFO response %s: %s\n", uri, strerror(errno));
                return NULL;    
        }

	snprintf (connection, sizeof (connection), "CONNECT {\"verbose\":true,\"pedantic\":false,\"name\":\"DRAGONFLY\",\"protocol\":1}\r\n");
        if ((n = write(socket_handle, connection, strlen(connection))) < 0)
        {
                syslog(LOG_ERR, "error sending CONNECT command to %s: %s\n", uri, strerror(errno));
                return NULL;
        }

        readLine (socket_handle, buffer, sizeof (buffer));
        if (buffer[0]!='+')
        {
                syslog(LOG_ERR, "error reading CONNECT response %s: %s\n", uri, strerror(errno));
                return NULL;    
        }

        if ((spec & DF_IN) == DF_IN)
        {
		if (nats_subscribe (socket_handle, subject) < 0)
		{
                	syslog(LOG_ERR, "error subscribing to %s: %s\n", uri, strerror(errno));
                	return NULL;    
		}
		io_type = DF_IN_NATS_TYPE;
        }
	else
	{
		io_type = DF_OUT_NATS_TYPE;
	}

	fcntl(socket_handle, F_SETFL, O_NONBLOCK); 

        DF_HANDLE *dh = (DF_HANDLE *)malloc(sizeof(DF_HANDLE));
        if (!dh)
        {
                syslog(LOG_ERR, "%s: malloc failed", __FUNCTION__);
                return NULL;
        }
        memset(dh, 0, sizeof(DF_HANDLE));

        dh->fd = socket_handle;
        dh->io_type = io_type;
        dh->path = strndup(uri, PATH_MAX);
	dh->tag = strndup (subject, sizeof(subject));
        return dh;
}

/*
 * ---------------------------------------------------------------------------------------
 *
 * 
 * ---------------------------------------------------------------------------------------
 */
int nats_read_message(DF_HANDLE *dh, char *buffer, int len)
{
        char status [128];
        int n = readLine (dh->fd, status, sizeof (status)-1);
        if (n < 0)
        {
                if (errno==EINTR) return -1;
                syslog(LOG_ERR, "read error: %s", strerror(errno));
                perror("read");
                exit(EXIT_FAILURE);
        }
        if (n==0) return 0;

        char *saveptr = NULL;
        char *response = strtok_r(status, " ", &saveptr);
        if (!response)
        {
                syslog(LOG_ERR, "nats read error: %s", strerror(errno));
		return 0;
        }
        char *subject = strtok_r(NULL, " ", &saveptr);
        if (!subject)
        {
                syslog(LOG_ERR, "nats read error: %s", strerror(errno));
		return 0;
        }

        char *id = strtok_r(NULL, " ", &saveptr);
        if (!id)
        {
                 syslog(LOG_ERR, "nats read error: %s", strerror(errno));
		return 0;
        }

        char *count = strtok_r(NULL, " ", &saveptr);
        if (!count)
        {
                 syslog(LOG_ERR, "nats read error: %s", strerror(errno));
		return 0;
        }
        int i = atoi (count) + 2;

        n = read(dh->fd, buffer, i);
        if (n != i)
        {
                if (errno==EINTR) return -1;
                syslog(LOG_ERR, "read error: %s", strerror(errno));
                perror("read");
                exit(EXIT_FAILURE);
        }
        return n;
}

/*
 * ---------------------------------------------------------------------------------------
 *
 * ---------------------------------------------------------------------------------------
 */
int nats_write_message(DF_HANDLE *dh, char *buffer)
{
	char command [256];
        int len = strnlen(buffer, DF_MAX_BUFFER_LEN);
        if (len == 0 || len == DF_MAX_BUFFER_LEN)
        {
                return -1;
        }

	nats_check_ping (dh->fd);

	int i=snprintf (command, sizeof(command)-1, "PUB %s %i\r\n", dh->tag, len);
	int n = writeLine (dh->fd, command, i);
	if (n <= 0) return n;
	n = writeLine (dh->fd, buffer, len);
	if (n <= 0) return n;
	n=writeLine (dh->fd, "\r\n", 2);

        return len;
}

/*
 * ---------------------------------------------------------------------------------------
 *
 * ---------------------------------------------------------------------------------------
 */
void nats_close(DF_HANDLE *dh)
{
        if (dh)
        {
                close(dh->fd);
                dh->fd = -1;
        }
}

/*
 * ---------------------------------------------------------------------------------------
 */
