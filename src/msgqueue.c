/*
 * ---------------------------------------------------------------------------------------
 *
 * 
 * ---------------------------------------------------------------------------------------
 */
#include <pthread.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <errno.h>
#include <limits.h>
#include <syslog.h>
#include <fcntl.h>
#include <sys/uio.h>

#ifdef __linux__
#include <error.h>
#endif

#include "msgqueue.h"

#define QUANTUM 5000

/*
 * ---------------------------------------------------------------------------------------
 *
 * ---------------------------------------------------------------------------------------
 */
queue_t *msgqueue_create(const char *queue_name, int msg_max, long queue_max)
{
	queue_t *q = (queue_t *)malloc(sizeof(queue_t));
	if (!q)
		return NULL;

	memset(q, 0, sizeof(queue_t));
	q->queue_name = strndup(queue_name, NAME_MAX);
	if (pipe(q->pipefd) < 0)
	{
		syslog(LOG_ERR, "pipe() error: %s", strerror(errno));
		free(q);
		exit(EXIT_FAILURE);
	}
	int flags = fcntl(q->pipefd[0], F_GETFD);
	flags |= O_NONBLOCK;
	if (fcntl(q->pipefd[0], F_SETFD, flags))
	{
		syslog(LOG_ERR, "pipe() F_SETFD error: %s", strerror(errno));
		free(q);
		exit(EXIT_FAILURE);
	}
	flags = fcntl(q->pipefd[1], F_GETFD);
	flags |= O_NONBLOCK;
	if (fcntl(q->pipefd[1], F_SETFD, flags))
	{
		syslog(LOG_ERR, "pipe() F_SETFD error: %s", strerror(errno));
		free(q);
		exit(EXIT_FAILURE);
	}
	return q;
}

/*
 * ---------------------------------------------------------------------------------------
 *
 * ---------------------------------------------------------------------------------------
 */
void msgqueue_cancel(queue_t *q)
{
	if (!q || q->cancel)
		return;
	q->cancel = 1;
	usleep(5000);
}

/*
 * ---------------------------------------------------------------------------------------
 *
 * ---------------------------------------------------------------------------------------
 */
void msgqueue_destroy(queue_t *q)
{
	if (!q)
		return;
	close(q->pipefd[0]);
	close(q->pipefd[1]);
	free(q->queue_name);
	free(q);
	q = NULL;
}

/*
 * ---------------------------------------------------------------------------------------
 *
 * ---------------------------------------------------------------------------------------
 */
int msgqueue_send(queue_t *q, const char *buffer, int length)
{
	if (!q || q->cancel)
		return -1;

	if (length > MAX_MESSAGE_SIZE)
	{
		length = MAX_MESSAGE_SIZE;
	}
	int n = 0;

	struct iovec iov[2];
	iov[0].iov_base = &length;
	iov[0].iov_len = sizeof(length);
	iov[1].iov_base = (void *)buffer;
	iov[1].iov_len = length;
	do
	{
		n = writev(q->pipefd[1], iov, 2);
		if (n <= 0)
		{
			if (n == 0)
				return 0;
			switch (errno)
			{
			case ETIMEDOUT:
			case EWOULDBLOCK:
				usleep(QUANTUM_SLEEP);
				break;
			default:
				if (!q->cancel)
				{
					syslog(LOG_ERR, "%s: error: %d - %s", __FUNCTION__, errno, strerror(errno));
					exit(EXIT_FAILURE);
				}
			}
		}
	} while (!q->cancel && (n < 0));
	return n;
}

/*
 * ---------------------------------------------------------------------------------------
 *
 * ---------------------------------------------------------------------------------------
 */
int msgqueue_recv(queue_t *q, char *buffer, int max_size)
{
	if (!q || q->cancel)
		return -1;

	if (max_size > MAX_MESSAGE_SIZE)
	{
		max_size = MAX_MESSAGE_SIZE;
	}
	memset(buffer, 0, max_size);
	int n = 0;
	int l = -1;
	do
	{
		if (l < 0)
		{
			n = read(q->pipefd[0], &l, sizeof(int));
			if (n <= 0)
			{
				if (n == 0)
					return 0;
				switch (errno)
				{
				case ETIMEDOUT:
				case EAGAIN:
					usleep(QUANTUM_SLEEP);
					break;
				default:
					if (!q->cancel)
					{
						syslog(LOG_ERR, "%s: error: %d - %s", __FUNCTION__, errno, strerror(errno));
						;
						exit(EXIT_FAILURE);
					}
					break;
				}
			}
			// need a better way to handle this
			if (l > max_size)
			{
				syslog(LOG_ERR, "%s: error: message bigger than buffer size", __FUNCTION__);
				exit(EXIT_FAILURE);
			}
			if (q->cancel)
				return 0;
			n = read(q->pipefd[0], buffer, l);
			if (n <= 0)
			{
				if (n == 0)
					return 0;
				switch (errno)
				{
				case ETIMEDOUT:
				case EAGAIN:
					usleep(QUANTUM_SLEEP);
					break;
				default:
					if (!q->cancel)
					{
						syslog(LOG_ERR, "%s: error: %d - %s", __FUNCTION__, errno, strerror(errno));
						exit(EXIT_FAILURE);
					}
					break;
				}
			}
		}
	} while (!q->cancel && (n < 0));
	return n;
}

/*
 * ---------------------------------------------------------------------------------------
 *
 * ---------------------------------------------------------------------------------------
 */

