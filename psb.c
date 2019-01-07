/*
l * psb_broker.c
 *
 *  Created on: Jan 20, 2018
 *      Author: alexo
 */

#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include "trie.h"
#include "threadqueue.h"
#include "platform.h"
#include "psb.h"

struct psb_broker
{
	psb_subscriber* subscriber_list;
	mutex_t mutex;
};

struct psb_subscriber
{
	struct ptrie* ptrie;
	struct threadqueue* thqueue;
	psb_subscriber* next;
	psb_subscriber* prev;
	psb_broker* broker;
};

static psb_broker g_global_psb_broker = {NULL, MUTEX_INITIALIZER};

static void slist_insert(psb_subscriber* list, psb_subscriber* entry);
static void slist_remove(psb_subscriber* entry);
static void* memdup(const void* mem, size_t size);

psb_broker* psb_new_broker()
{
	psb_broker* new_broker = (psb_broker*)malloc(sizeof(struct psb_broker));
	if (new_broker != NULL)
	{
		new_broker->subscriber_list = NULL;
		mutex_init(&new_broker->mutex);
	}

	return new_broker;
}

int psb_delete_broker(psb_broker* broker)
{
	if (broker == NULL)
	{
		broker = &g_global_psb_broker;
	}

	while (broker->subscriber_list->next != broker->subscriber_list)
	{
		psb_delete_subscriber(broker->subscriber_list->next);
	}
	psb_delete_subscriber(broker->subscriber_list);

	if (broker != &g_global_psb_broker)
	{
		free(broker);
	}

	return 0;
}

psb_subscriber* psb_new_subscriber(psb_broker* broker)
{
	psb_subscriber* new_sub;
	
	if (broker == NULL)
	{
		broker = &g_global_psb_broker;
	}

	new_sub = (psb_subscriber*)malloc(sizeof(struct psb_subscriber));
	if (new_sub == NULL)
	{
		return NULL;
	}

	new_sub->thqueue = (struct threadqueue*)malloc(sizeof(struct threadqueue));
	if (new_sub->thqueue == NULL)
	{
		free(new_sub);
		return NULL;
	}

	new_sub->ptrie = (struct ptrie*)malloc(sizeof(struct ptrie));
	if (new_sub->ptrie == NULL)
	{
		free(new_sub->thqueue);
		free(new_sub);
		return NULL;
	}

	if (thread_queue_init(new_sub->thqueue) != 0)
	{
		free(new_sub->thqueue);
		free(new_sub);
		return NULL;
	}

	ptrie_init(new_sub->ptrie);

	new_sub->prev = new_sub;
	new_sub->next = new_sub;
	new_sub->broker = broker;

	mutex_lock(&broker->mutex);

	if (broker->subscriber_list != NULL)
	{
		slist_insert(broker->subscriber_list, new_sub);
	}
	else
	{
		broker->subscriber_list = new_sub;
	}

	mutex_unlock(&broker->mutex);

	return new_sub;
}

void freedata(void* data)
{
	psb_message* msg = (psb_message*)data;
	psb_free_message(msg);
	free(msg);
}

int psb_delete_subscriber(psb_subscriber* subscriber)
{

	if (subscriber != NULL)
	{
		mutex_t* mutex = &subscriber->broker->mutex;
		mutex_lock(mutex);
		slist_remove(subscriber);
		ptrie_term(subscriber->ptrie);
		free(subscriber->ptrie);
		thread_queue_free(subscriber->thqueue, freedata);
		free(subscriber);
		mutex_unlock(mutex);

		return 0;
	}

	return -EINVAL;
}

int psb_subscribe(psb_subscriber* subscriber, char* channel_name)
{
	int rval = -EINVAL;
	if ((subscriber != NULL) && (channel_name != NULL))
	{
		mutex_lock(&subscriber->broker->mutex);

		// check that subscriber is not subscribed currently to channel
		if (ptrie_match_str(subscriber->ptrie, (uint8_t*)channel_name, strlen(channel_name)) == 0)
		{
			// subscribe
			if (ptrie_add_str(subscriber->ptrie, (uint8_t*)channel_name, strlen(channel_name)) == 1)
			{
				rval = 0;
			}
		}
		mutex_unlock(&subscriber->broker->mutex);
		return rval;
	}

	return rval;
}

int psb_unsubscribe(psb_subscriber* subscriber, char* channel_name)
{
	int rval = -EINVAL;

	if ((subscriber != NULL) && (channel_name != NULL))
	{
		mutex_lock(&subscriber->broker->mutex);
		//if (ptrie_match_str(subscriber->ptrie, (uint8_t*)channel_name, strlen(channel_name)) == 1)
		{
			if (ptrie_remove_str(subscriber->ptrie, (uint8_t*)channel_name, strlen(channel_name)) == 1)
			{
				rval = 0;
            }
		}
		mutex_unlock(&subscriber->broker->mutex);
		return rval;
	}

	return rval;
}

int psb_get_message(psb_subscriber* subscriber, psb_message* msg, int timeout_ms)
{
	int rval = -EINVAL;
	struct threadmsg tmsg;
	struct timespec ts;
	struct timespec* pts;

	if (timeout_ms > 0)
	{
		ts.tv_sec = timeout_ms / 1000;
		ts.tv_nsec = (timeout_ms % 1000) * 1000000;
		pts = &ts;
	}
	else
	{
		pts = NULL;
	}

	if ((subscriber != NULL) && (msg != NULL))
	{
		rval = thread_queue_get_msg(subscriber->thqueue, pts, &tmsg);
		if (rval == 0)
		{
			*msg = *((psb_message*)tmsg.data);
			free(tmsg.data);
		}
	}

	return rval;
}

int psb_get_messages_count(psb_subscriber* subscriber)
{
	int rval = -EINVAL;

	if (subscriber != NULL)
	{
		rval = thread_queue_length(subscriber->thqueue);
	}

	return rval;
}

int psb_free_message(psb_message* msg)
{
	int rval = -EINVAL;
	if (msg)
	{
		if (msg->channel)
		{
			free(msg->channel);
		}

		if (msg->data)
		{
			free(msg->data);
		}

		rval = 0;
	}

	return rval;
}


int psb_publish_message(psb_broker* broker, char* channel, void* data, int datalen)
{
	int cnt = 0;
	int res;

	if (broker == NULL)
	{
		broker = &g_global_psb_broker;
	}

	if ((channel != NULL) && (data != NULL) && (datalen > 0))
	{
		if (broker->subscriber_list != NULL)
		{
			psb_subscriber* iterator=broker->subscriber_list;
			do
			{
				mutex_lock(&broker->mutex);
				res = ptrie_match_str(iterator->ptrie, (uint8_t*)channel, strlen(channel));
				mutex_unlock(&broker->mutex);

				if (res)
				{
					psb_message* msg = (psb_message*)malloc(sizeof(struct psb_message));
					if (msg)
					{
						msg->channel = strdup(channel);
						msg->data = memdup(data, datalen);
						msg->datalen = datalen;
						thread_queue_put_msg(iterator->thqueue, msg, 0);
						cnt++;
					}
					else
					{
						return -ENOMEM;
					}
				}

				iterator = iterator->next;
			}
			while (iterator != broker->subscriber_list);
		}
	}
	else
	{
		return -EINVAL;
	}

	return cnt;
}

static void slist_insert(psb_subscriber* list, psb_subscriber* entry)
{
  entry->prev = list;
  entry->next = list->next;

  list->next->prev = entry;
  list->next       = entry;
}

static void slist_remove(psb_subscriber* entry)
{
  entry->next->prev = entry->prev;
  entry->prev->next = entry->next;

  /*
   * Set the entry to point to itself, so that any further calls to
   * slist_remove() are harmless.
   */

  entry->prev = entry;
  entry->next = entry;
}

static void* memdup(const void* mem, size_t size)
{
   void* out = malloc(size);

   if(out != NULL)
   {
       memcpy(out, mem, size);
   }

   return out;
}
