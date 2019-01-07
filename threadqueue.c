#include <stdlib.h>
#include <string.h>
#include <errno.h> 

#include "threadqueue.h"

#define MSGPOOL_SIZE 256

struct msglist
{
	struct threadmsg msg;
	struct msglist *next;
};

static struct msglist *get_msglist(struct threadqueue *queue)
{
	struct msglist *tmp;

	if (queue->msgpool != NULL)
	{
		tmp = queue->msgpool;
		queue->msgpool = tmp->next;
		queue->msgpool_length--;
	}
	else
	{
		tmp = (struct msglist*) malloc(sizeof *tmp);
	}

	return tmp;
}

static void release_msglist(struct threadqueue *queue, struct msglist *node)
{

	if (queue->msgpool_length > (queue->length / 8 + MSGPOOL_SIZE))
	{
		free(node);
	}
	else
	{
		node->msg.data = NULL;
		node->msg.msgtype = 0;
		node->next = queue->msgpool;
		queue->msgpool = node;
		queue->msgpool_length++;
	}
	if (queue->msgpool_length > (queue->length / 4 + MSGPOOL_SIZE * 10))
	{
		struct msglist *tmp = queue->msgpool;
		queue->msgpool = tmp->next;
		free(tmp);
		queue->msgpool_length--;
	}
}

int thread_queue_init(struct threadqueue *queue)
{
	if (queue == NULL)
	{
		return EINVAL;
	}
	memset(queue, 0, sizeof(struct threadqueue));
	cond_init(&queue->cond);

	mutex_init(&queue->mutex);

	return 0;

}

int thread_queue_put_msg(struct threadqueue *queue, void *data, long msgtype)
{
	int ret = 0;
	struct msglist *newmsg;

	mutex_lock(&queue->mutex);
	newmsg = get_msglist(queue);
	if (newmsg == NULL)
	{
		mutex_unlock(&queue->mutex);
		return ENOMEM;
	}
	newmsg->msg.data = data;
	newmsg->msg.msgtype = msgtype;

	newmsg->next = NULL;
	if (queue->last == NULL)
	{
		queue->last = newmsg;
		queue->first = newmsg;
	}
	else
	{
		queue->last->next = newmsg;
		queue->last = newmsg;
	}

	if (queue->length == 0)
		cond_broadcast(&queue->cond);
	queue->length++;
	mutex_unlock(&queue->mutex);

	return 0;

}

int thread_queue_get_msg(struct threadqueue *queue, const struct timespec *timeout, struct threadmsg *msg)
{
	struct msglist *firstrec;
	int ret = 0;

	if (queue == NULL || msg == NULL)
	{
		return EINVAL;
	}

#if defined(_WIN32_WINNT) && _WIN32_WINNT >= 0x0600
	mutex_lock(&queue->mutex);

	// Will wait until awakened by a signal or broadcast
	while (queue->first == NULL && ret != ERROR_TIMEOUT)
	{  //Need to loop to handle spurious wakeups
		if (timeout)
		{
			if(!SleepConditionVariableSRW(&queue->cond, &queue->mutex, timeout->tv_sec*1000 + timeout->tv_nsec/1000000, 0))
			{
				ret = GetLastError();
			}
		}
		else
		{
			cond_wait(&queue->cond, &queue->mutex);

		}
	}
	if (ret == ERROR_TIMEOUT)
	{
		mutex_unlock(&queue->mutex);
		return -ERROR_TIMEOUT;
	}

#else
	{
		struct timespec abstimeout;
		if (timeout)
		{
			struct timeval now;

			gettimeofday(&now, NULL);
			abstimeout.tv_sec = now.tv_sec + timeout->tv_sec;
			abstimeout.tv_nsec = (now.tv_usec * 1000) + timeout->tv_nsec;
			if (abstimeout.tv_nsec >= 1000000000)
			{
				abstimeout.tv_sec++;
				abstimeout.tv_nsec -= 1000000000;
			}
		}
		mutex_lock(&queue->mutex);

		// Will wait until awakened by a signal or broadcast
		while (queue->first == NULL && ret != ETIMEDOUT)
		{  //Need to loop to handle spurious wakeups
			if (timeout)
			{
				ret = cond_timedwait(&queue->cond, &queue->mutex, &abstimeout);
			}
			else
			{
				cond_wait(&queue->cond, &queue->mutex);

			}
		}
		if (ret == ETIMEDOUT)
		{
			mutex_unlock(&queue->mutex);
			return -ETIMEDOUT;
		}
	}
#endif

	firstrec = queue->first;
	queue->first = queue->first->next;
	queue->length--;

	if (queue->first == NULL)
	{
		queue->last = NULL;     // we know this since we hold the lock
		queue->length = 0;
	}

	msg->data = firstrec->msg.data;
	msg->msgtype = firstrec->msg.msgtype;
	msg->qlength = queue->length;

	release_msglist(queue, firstrec);
	mutex_unlock(&queue->mutex);

	return 0;
}

//maybe caller should supply a callback for cleaning the elements ?
int thread_queue_cleanup(struct threadqueue *queue, user_free_fn freedata)
{
	struct msglist *rec;
	struct msglist *next;
	struct msglist *recs[2];
	int i;
	if (queue == NULL)
	{
		return EINVAL;
	}

	mutex_lock(&queue->mutex);
	recs[0] = queue->first;
	recs[1] = queue->msgpool;
	for (i = 0; i < 2; i++)
	{
		rec = recs[i];
		while (rec)
		{
			next = rec->next;
			if (freedata)
			{
				freedata(rec->msg.data);
			}

			free(rec);
			rec = next;
		}
	}

	mutex_unlock(&queue->mutex);
	mutex_destroy(&queue->mutex);
	cond_destroy(&queue->cond);

	return 0;
}

long thread_queue_length(struct threadqueue *queue)
{
	long counter;
	// get the length properly
	mutex_lock(&queue->mutex);
	counter = queue->length;
	mutex_unlock(&queue->mutex);
	return counter;

}

struct threadqueue* thread_queue_alloc()
{
	struct threadqueue* queue;
	queue = (struct threadqueue*) malloc(sizeof(struct threadqueue));
	if (thread_queue_init(queue) != 0)
	{
		free(queue);
		return NULL;
	}

	return queue;
}

void thread_queue_free(struct threadqueue* queue, user_free_fn freedata)
{
	thread_queue_cleanup(queue, freedata);
	free(queue);
}
