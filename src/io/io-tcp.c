/* Copyright (C) 2017-2018 CounterFlow AI, Inc.
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

#define MAX_RETRIES (64)
#define MLE_TCP_PORT 2561
#define MLE_BACKLOG 16

static int tcp_running = 1;

/*
 * ---------------------------------------------------------------------------------------
 *
 * ---------------------------------------------------------------------------------------
 */

static void *tcp_handler (void *arg) 
{
    DF_HANDLE *dh = (DF_HANDLE *) arg;
    int handler_running = 1;
    read_callback callback = (read_callback) dh->ptr;

    while (handler_running && tcp_running)
    {
        int n = read(fd, buffer, len);
        if (n < 0)
        {
                if (errno==EINTR) return -1;
               
                handler_running = 0;
        }
        callback (dh->user, buffer, len);
    }
    close(dh->fd);
    free (dh);
    return NULL;
}

/*
 * ---------------------------------------------------------------------------------------
 *
 * ---------------------------------------------------------------------------------------
 */

static int tcp_set_nonblock(int fd)
{
        int fd_flags = fcntl(fd, F_GETFL);
        if (fd_flags < 0)
        {
                close(fd);
                syslog(LOG_ERR, "unable to fcntl: %s\n", strerror(errno));
                exit(EXIT_FAILURE);
        }
        if (fcntl(fd, F_SETFL, fd_flags | O_NONBLOCK) < 0)
        {
                close(fd);
                syslog(LOG_ERR, "unable to fcntl: %s\n", strerror(errno));
                exit(EXIT_FAILURE);
        }
        return fd;
}

/*
 * ---------------------------------------------------------------------------------------
 *
 * ---------------------------------------------------------------------------------------
 */
DF_HANDLE *tcp_open(const char *tcp_path, int spec)
{
        int socket_handle = -1;
        int io_type = -1;

        if ((socket_handle = socket(AF_INET, SOCK_STREAM, 0)) < 0)
        {
                syslog(LOG_ERR, "unable to create socket: %s\n", strerror(errno));
                return -1;
        }

        struct sockaddr_in addr;
        memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        add.sin_port = MLE_TCP_PORT;
        add.sin_addr.s_addr = INADDR_ANY;

        if ((spec & DF_IN) == DF_SERVER_TCP_TYPE)
        {
                io_type = DF_SERVER_TCP_TYPE;
                if (bind(socket_handle, (struct sockaddr *)&addr, sizeof(addr)) < 0)
                {
                        syslog(LOG_ERR, "unable to bind socket: %s\n", strerror(errno));
                        return -1;
                }
       
                if (listen (socket_handle, MLE_BACKLOG) < 0)
                {
                        syslog(LOG_ERR, "unable to listen: %s\n", strerror(errno));
                        return -1;    
                }

                tcp_set_nonblock (socket_handler);
        }

        DF_HANDLE *dh = (DF_HANDLE *)malloc(sizeof(DF_HANDLE));
        if (!dh)
        {
                syslog(LOG_ERR, "%s: malloc failed", __FUNCTION__);
                return NULL;
        }
        memset(dh, 0, sizeof(DF_HANDLE));

        dh->fd = socket_handle;
        dh->io_type = io_type;

        return dh;
}


/*
 * ---------------------------------------------------------------------------------------
 *
 * 
 * ---------------------------------------------------------------------------------------
 */
int tcp_dispatch_read (DF_HANDLE *dh, read_callback ptr);
{
        while (tcp_running)
        {
                if ((new_fd = accept(socket_handle, (struct sockaddr *)&their_addr, &sin_size)) == -1) 
                {
                        syslog(LOG_ERR, "unable to accept: %s\n", strerror(errno));
                        return -1; 
        	}
                // start handler

        }
        return 0;
}

/*
 * ---------------------------------------------------------------------------------------
 *
 * 
 * ---------------------------------------------------------------------------------------
 */
int tcp_read_message(DF_HANDLE *dh, char *buffer, int len)
{
        if (!tcp_running) return -1;
       
        int n = read(dh->fd, buffer, len);
        if (n < 0)
        {
                if (errno==EINTR) return -1;
                syslog(LOG_ERR, "read error: %s", strerror(errno));
                return -1;
        }
       
        return n;
}

/*
 * ---------------------------------------------------------------------------------------
 *
 * ---------------------------------------------------------------------------------------
 */
int tcp_write_message(DF_HANDLE *dh, char *buffer)
{
        int len = strnlen(buffer, DF_MAX_BUFFER_LEN);
        if (len == 0 || len == DF_MAX_BUFFER_LEN)
        {
                return -1;
        }
        
        int n = 0;
        int retries = 0;
        do
        {
                n = send(dh->fd, buffer, len, 0);
                if (n < 0)
                {
                        switch (errno)
                        {
                        case ETIMEDOUT:
                        case EAGAIN:
                                usleep(5000);
                                retries++;
                                break;
                        default:
                                syslog(LOG_ERR, "send error: %s", strerror(errno));
                                return -1;
                        }
                }
                else if (n == 0 || errno == EIO)
                {
                        tcp_reopen(dh);
                }
                else
                        break;
        } while (retries < MAX_RETRIES);
        return n;
}

/*
 * ---------------------------------------------------------------------------------------
 *
 * ---------------------------------------------------------------------------------------
 */
void tcp_close(DF_HANDLE *dh)
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
