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

#ifndef _DRAGONFLY_IO_H_
#define _DRAGONFLY_IO_H_

#include <zlib.h>

#define DF_IN 1
#define DF_OUT 2
#define DF_ERR 4
#define DF_CMD 8

#define DF_OUT_SYSLOG_TYPE 0
#define DF_IN_FILE_TYPE 1
#define DF_OUT_FILE_TYPE 2
#define DF_CLIENT_IPC_TYPE  3
#define DF_SERVER_IPC_TYPE  4
#define DF_IN_TAIL_TYPE 5
#define DF_IN_ZFILE_TYPE 6
#define DF_OUT_ZFILE_TYPE 7
#define DF_IN_NATS_TYPE 8
#define DF_OUT_NATS_TYPE 9
#define DF_SERVER_TCP_TYPE 10

#define DF_MAX_BUFFER_LEN 4096

typedef struct _DF_HANDLE_
{ 
    gzFile zfp;
    FILE *fp;
    int fd;
    int epoch;
    int interval;
    int io_type;
    char *path;
    char *tag;
    void *ptr;
    void *user;
} DF_HANDLE;

typedef int (*read_callback)(DF_HANDLE *dh, char *buffer, int len);

DF_HANDLE *dragonfly_io_open(const char *path, int spec);
int dragonfly_io_write(DF_HANDLE *dh, char *buffer);
int dragonfly_io_read(DF_HANDLE *dh, char *buffer, int max);
int dragonfly_io_dispatch_read (DF_HANDLE *dh, read_callback ptr);
void dragonfly_io_flush(DF_HANDLE *dh);
void dragonfly_io_close(DF_HANDLE *dh);
void dragonfly_io_rotate(DF_HANDLE *dh);
int dragonfly_io_is_file(DF_HANDLE *dh);
int dragonfly_io_is_tcp(DF_HANDLE *dh);

void dragonfly_io_set_rundir (const char* rundir);
const char* dragonfly_io_get_rundir ();

void dragonfly_io_set_logdir (const char* rundir);
const char* dragonfly_io_get_logdir ();

#endif

