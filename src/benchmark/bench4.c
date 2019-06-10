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
 * author Don J. Rude <dr@counterflowai.com>
 *
 */

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <limits.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <signal.h>
#include <syslog.h>
#include <pthread.h>
#include <assert.h>

#include <hiredis/hiredis.h>

#include "benchmark.h"

#define MAX_BENCH4_MESSAGES 100000
#define QUANTUM (MAX_BENCH4_MESSAGES / 10)


/*
 * ---------------------------------------------------------------------------------------
 *
 * ---------------------------------------------------------------------------------------
 */
void SELF_BENCH4(const char *dragonfly_root)
{
	fprintf(stdout, "\n\n%s: sending %u ping messages to redis in a C-loop\n", __FUNCTION__, MAX_BENCH4_MESSAGES);
	fprintf(stdout, "-------------------------------------------------------\n");

	signal(SIGPIPE, SIG_IGN);
	openlog("dragonfly", LOG_PERROR, LOG_USER);
#ifdef _GNU_SOURCE
	pthread_setname_np(pthread_self(), "dragonfly-bench4");
#endif

	sleep(1);

    redisContext *pContext = redisConnect("127.0.0.1", 6379);

    clock_t last_time = clock();
	for (unsigned long i = 0; i < MAX_BENCH4_MESSAGES; i++)
	{

        redisReply *reply = redisCommand(pContext, "PING");

        if ( reply == 0 || (reply->type == REDIS_REPLY_ERROR) || (strcasecmp(reply->str,"pong") != 0) )
        {
            fprintf(stderr,"Redis ping fail!\n");
        }

        freeReplyObject(reply);

        if ((i > 0) && (i % QUANTUM) == 0)
        {
            clock_t mark_time = clock();
            double elapsed_time = ((double)(mark_time - last_time)) / CLOCKS_PER_SEC; // in seconds
            double ops_per_sec = QUANTUM / elapsed_time;
            fprintf(stdout, "%6.2f /sec\n", ops_per_sec);
            last_time = clock();
        }
	}
    redisFree(pContext);

	sleep(1);
	closelog();

	fprintf(stderr, "-------------------------------------------------------\n\n");
	fflush(stderr);
}

/*
 * ---------------------------------------------------------------------------------------
 */
