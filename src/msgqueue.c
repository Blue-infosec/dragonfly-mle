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

#define QUANTUM 50000

// NOTE: the zmq context is shared across threads for the inproc transport
static void *g_context = NULL;

/*
 * ---------------------------------------------------------------------------------------
 *
 * ---------------------------------------------------------------------------------------
 */
void* msgqueue_init ()
{
	void *g_context = zmq_ctx_new ();

	if (!g_context)
	{
		syslog(LOG_ERR, "%s: zmq_ctx_new error: %d - %s", __FUNCTION__, errno, zmq_strerror (errno));
	}
	return g_context;
}

/*
 * ---------------------------------------------------------------------------------------
 *
 * ---------------------------------------------------------------------------------------
 */
void msgqueue_term ()
{
	if (g_context)
		zmq_ctx_destroy (g_context);
}

/*
 * ---------------------------------------------------------------------------------------
 *
 * ---------------------------------------------------------------------------------------
 */
queue_t *msgqueue_create(void *context, const char *queue_name, int msg_max, long queue_max)
{
#ifdef __DEBUG3__
	fprintf(stderr, "%s:%i\n", __FUNCTION__, __LINE__);
#endif
	queue_t *q = (queue_t *)malloc(sizeof(queue_t));
	if (!q)
		return NULL;

	memset(q, 0, sizeof(queue_t));
	q->queue_name = strndup(queue_name, NAME_MAX);
	q->context = context;

	char uri [NAME_MAX];
	char *p = q->queue_name;
	if (*p=='/') p++;

	snprintf (uri, NAME_MAX-1, "inproc://%s", p);

	// pull end of queue
    q->pull = zmq_socket (context, ZMQ_PAIR);
	if (!q->pull)
	{
		syslog(LOG_ERR, "%s: zmq_socket error: %d - %s", __FUNCTION__, errno, zmq_strerror (errno));
		return NULL;
	}

	int rc = zmq_bind(q->pull, uri); 
	if (rc < 0) 
	{
		syslog(LOG_ERR, "%s: zmq_bind (%s) error: %d - %s", __FUNCTION__, uri, errno, zmq_strerror (errno));
		return NULL;
	}

	// push end of queue
    q->push = zmq_socket (context, ZMQ_PAIR);
	if (!q->push)
	{
		syslog(LOG_ERR, "%s: zmq_socket error: %d - %s", __FUNCTION__, errno, zmq_strerror (errno));
		return NULL;
	}

	rc = zmq_connect(q->push, uri); 
	if (rc < 0)
	{
		syslog(LOG_ERR, "%s: zmq_connect (%s) error: %d - %s", __FUNCTION__, uri, errno, zmq_strerror (errno));
		return NULL;
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
#ifdef __DEBUG3__
	fprintf(stderr, "%s:%i\n", __FUNCTION__, __LINE__);
#endif
	if (!q || q->cancel)
		return;
	q->cancel = 1;
	usleep(25000);
}

/*
 * ---------------------------------------------------------------------------------------
 *
 * ---------------------------------------------------------------------------------------
 */
void msgqueue_destroy(queue_t *q)
{
#ifdef __DEBUG3__
	fprintf(stderr, "%s:%i\n", __FUNCTION__, __LINE__);
#endif
	if (!q)
		return;
	zmq_close(q->pull);
	zmq_close(q->push);
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
#ifdef __DEBUG3__
	fprintf(stderr, "%s:%i\n", __FUNCTION__, __LINE__);
#endif
	if (!q || q->cancel)
		return -1;

	if (length > MAX_MESSAGE_SIZE)
	{
		length = MAX_MESSAGE_SIZE;
	}

	int n = 0;
	do
	{
		n = zmq_send (q->push, buffer, length, ZMQ_DONTWAIT);
		if (n <= 0)
		{
			if (n == 0)
				return 0;
			switch (errno)
			{
			case EAGAIN:
				usleep(QUANTUM_SLEEP);
				break;
			case EINTR:
				q->cancel=1;
				n = -1;
				break;
				
			default:
				if (!q->cancel)
				{
					syslog(LOG_ERR, "%s: error: %d - %s", __FUNCTION__, errno, zmq_strerror(errno));
					return -1;
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
#ifdef __DEBUG3__
	fprintf(stderr, "%s:%i\n", __FUNCTION__, __LINE__);
#endif
	if (!q || q->cancel)
		return -1;

	if (max_size > MAX_MESSAGE_SIZE)
	{
		max_size = MAX_MESSAGE_SIZE;
	}
	memset(buffer, 0, max_size);

	int n = 0;
	do
	{
		n = zmq_recv (q->pull, buffer, (max_size-1), ZMQ_DONTWAIT);
		if (n <= 0)
		{
			if (n == 0)
				return 0;
			switch (errno)
			{
			case EAGAIN:
				usleep(QUANTUM_SLEEP);
				break;
			case EINTR:
				q->cancel=1;
				n = -1;
				break;
				
			default:
				if (!q->cancel)
				{
					syslog(LOG_ERR, "%s: error: %d - %s", __FUNCTION__, errno, zmq_strerror(errno));
					return -1;
				}
				break;
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
