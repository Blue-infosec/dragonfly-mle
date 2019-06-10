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

#define MAX_BENCH6_MESSAGES 200000
#define QUANTUM (MAX_BENCH6_MESSAGES / 20)


/*
 * ---------------------------------------------------------------------------------------
 *
 * ---------------------------------------------------------------------------------------
 */
void SELF_BENCH6(const char *dragonfly_root)
{
	fprintf(stdout, "\n\n%s: sending %u SET messages to redis in a C-loop\n", __FUNCTION__, MAX_BENCH6_MESSAGES);
	fprintf(stdout, "-------------------------------------------------------\n");

	signal(SIGPIPE, SIG_IGN);
	openlog("dragonfly", LOG_PERROR, LOG_USER);
#ifdef _GNU_SOURCE
	pthread_setname_np(pthread_self(), "dragonfly-bench6");
#endif

	sleep(1);

    redisContext *pContext = redisConnect("127.0.0.1", 6379);

    clock_t last_time = clock();
	for (unsigned long i = 0; i < MAX_BENCH6_MESSAGES; i++)
	{
        char rcmd[100];
        snprintf(rcmd, 100, "SET bench%lu %lu", i, i);
        redisReply *reply = redisCommand(pContext, rcmd);

        if ( reply == 0 || (reply->type == REDIS_REPLY_ERROR) )
        {
            fprintf(stderr,"Redis SET fail!\n");
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

    fprintf(stdout, "\n\n%s: sending %u GET messages to redis in a C-loop\n", __FUNCTION__, MAX_BENCH6_MESSAGES);
    fprintf(stdout, "-------------------------------------------------------\n");

    last_time = clock();
    for (unsigned long i = 0; i < MAX_BENCH6_MESSAGES; i++)
    {
        char rcmd[100];
        snprintf(rcmd, 100, "GET bench%lu", i);
        redisReply *reply = redisCommand(pContext, rcmd);

        if ( reply == 0 || (reply->type == REDIS_REPLY_ERROR) )
        {
            fprintf(stderr,"Redis 'GET bench%lu' fail!\n", i);
        } else {
            unsigned long rply = 0;
            sscanf(reply->str, "%lu", &rply);
            if( rply != i )
                fprintf(stdout, "reply: '%s' no match i=%lu\n", reply->str, i);
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


    fprintf(stdout, "\n\n%s: sending %u DEL messages to redis in a C-loop\n", __FUNCTION__, MAX_BENCH6_MESSAGES);
    fprintf(stdout, "-------------------------------------------------------\n");

    last_time = clock();
    for (unsigned long i = 0; i < MAX_BENCH6_MESSAGES; i++)
    {
        char rcmd[100];
        snprintf(rcmd, 100, "DEL bench%lu", i);
        redisReply *reply = redisCommand(pContext, rcmd);

        if ( reply == 0 || (reply->type == REDIS_REPLY_ERROR) )
        {
            fprintf(stderr,"Redis 'DEL bench%lu' fail!\n", i);
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
