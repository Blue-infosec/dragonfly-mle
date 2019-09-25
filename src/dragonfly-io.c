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

#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <pthread.h>
#include <errno.h>

#include "config.h"

#include "dragonfly-io.h"

#include "io-file.h"
#include "io-tail.h"
#include "io-pipe.h"
#include "io-zfile.h"
#include "io-nats.h"
#include "io-syslog.h"

static char *g_run_dir = NULL;
static char *g_log_dir = NULL;

/*
 * ---------------------------------------------------------------------------------------
 *
 * ---------------------------------------------------------------------------------------
 */
void dragonfly_io_set_rundir(const char *rundir)
{
        if (g_run_dir)
        {
                free (g_run_dir);
        }
        g_run_dir = strndup(rundir, PATH_MAX);
}

/*
 * ---------------------------------------------------------------------------------------
 *
 * ---------------------------------------------------------------------------------------
 */
const char *dragonfly_io_get_rundir()
{
        return (g_run_dir == NULL) ? DRAGONFLY_RUN_DIR : g_run_dir;
}

/*
 * ---------------------------------------------------------------------------------------
 *
 * ---------------------------------------------------------------------------------------
 */
void dragonfly_io_set_logdir(const char *rundir)
{
        if (g_log_dir)
        {
                free (g_log_dir);
        }
        g_log_dir = strndup(rundir, PATH_MAX);
}

/*
 * ---------------------------------------------------------------------------------------
 *
 * ---------------------------------------------------------------------------------------
 */
const char *dragonfly_io_get_logdir()
{
        return (g_log_dir == NULL) ? DRAGONFLY_LOG_DIR : g_log_dir;
}

/*
 * ---------------------------------------------------------------------------------------
 *
 * ---------------------------------------------------------------------------------------
 */
DF_HANDLE *dragonfly_io_open(const char *uri, int spec)
{
        if (strncmp("file://", uri, 7) == 0)
        {
                return file_open(((const char *)uri + 7), spec);
        }
        else if (strncmp("tail://", uri, 7) == 0)
        {
                return tail_open(((const char *)uri + 7), spec);
        }
        else if (strncmp("ipc://", uri, 6) == 0)
        {
                return ipc_open(((const char *)uri + 6), spec);
        }
        else if (strncmp("zfile://", uri, 8) == 0)
        {
                return zfile_open(((const char *)uri + 8), spec);
        }
        else if (strncmp("syslog://", uri, 9) == 0)
        {
                return ipc_open(((const char *)uri + 9), spec);
        }
        else if (strncmp("nats://", uri, 7) == 0)
        {
                return nats_open(((const char *)uri + 7), spec);
	}
        else
        {
                syslog(LOG_ERR, "%s: invalid file specifier", __FUNCTION__);
        }
        return NULL;
}

/*
 * ---------------------------------------------------------------------------------------
 *
 * ---------------------------------------------------------------------------------------
 */
int dragonfly_io_write(DF_HANDLE *dh, char *buffer)
{
        if (!dh)
                return -1;
        if ((dh->io_type == DF_SERVER_IPC_TYPE) ||
            (dh->io_type == DF_CLIENT_IPC_TYPE))
        {
                return ipc_write_message(dh, buffer);
        }
        else if (dh->io_type == DF_OUT_FILE_TYPE)
        {
                return file_write_line(dh, buffer);
        }
        else if (dh->io_type == DF_OUT_SYSLOG_TYPE)
        {
                return syslog_write_message(dh, buffer);
        }
        else if (dh->io_type == DF_OUT_NATS_TYPE)
        {
                return nats_write_message(dh, buffer);
        }
        return -1;
}

/*
 * ---------------------------------------------------------------------------------------
 *
 * ---------------------------------------------------------------------------------------
 */
int dragonfly_io_read(DF_HANDLE *dh, char *buffer, int len)
{
        if (!dh)
                return -1;

        if ((dh->io_type == DF_SERVER_IPC_TYPE) ||
            (dh->io_type == DF_CLIENT_IPC_TYPE))
        {
                return ipc_read_message(dh, buffer, len);
        }
        else if (dh->io_type == DF_IN_TAIL_TYPE)
        {
                return tail_read_line(dh, buffer, len);
        }
        else if (dh->io_type == DF_IN_FILE_TYPE)
        {
                return file_read_line(dh, buffer, len);
        }
        else if (dh->io_type == DF_IN_ZFILE_TYPE)
        {
                return zfile_read_line(dh, buffer, len);
        }
        else if (dh->io_type == DF_IN_NATS_TYPE)
        {
                return nats_read_message(dh, buffer, len);
        }
        return -1;
}

/*
 * ---------------------------------------------------------------------------------------
 *
 * ---------------------------------------------------------------------------------------
 */
void dragonfly_io_flush(DF_HANDLE *dh)
{
        if (!dh)
                return;

        if (dh->io_type == DF_OUT_FILE_TYPE)
        {
                fflush(dh->fp);
        }
}

/*
 * ---------------------------------------------------------------------------------------
 *
 * ---------------------------------------------------------------------------------------
 */
void dragonfly_io_close(DF_HANDLE *dh)
{
        if (!dh)
                return;
        if ((dh->io_type == DF_SERVER_IPC_TYPE) ||
            (dh->io_type == DF_CLIENT_IPC_TYPE))
        {
                ipc_close(dh);
        }
        else if (dh->io_type == DF_IN_TAIL_TYPE)
        {
                tail_close(dh);
        }
        else if ((dh->io_type == DF_IN_FILE_TYPE) ||
                 (dh->io_type == DF_OUT_FILE_TYPE))
        {
                file_close(dh);
        }
        else if (dh->io_type == DF_IN_ZFILE_TYPE)
        {
                return zfile_close(dh);
        }
        free(dh->path);
        dh->path = NULL;
        free(dh);
        dh = NULL;
}

/*
 * ---------------------------------------------------------------------------------------
 *
 * ---------------------------------------------------------------------------------------
 */
int dragonfly_io_isfile(DF_HANDLE *dh)
{
        if (dh &&
            ((dh->io_type == DF_OUT_FILE_TYPE) ||
             (dh->io_type == DF_IN_FILE_TYPE)))
        {
                return 1;
        }
        return 0;
}

/*
 * ---------------------------------------------------------------------------------------
 *
 * ---------------------------------------------------------------------------------------
 */
void dragonfly_io_rotate(DF_HANDLE *dh)
{
        // only output files get rotated
        if (dh && (dh->io_type == DF_OUT_FILE_TYPE))
        {
                file_rotate(dh);
        }
}

/*
 * ---------------------------------------------------------------------------------------
 */
