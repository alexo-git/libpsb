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

// Declare broker object structure
struct psb_broker
{
	psb_subscriber* subscriber_list;	// reference to subscriber's list
	mutex_t mutex;				// mutex for thread access share
};

// Declare subscribers object structure
struct psb_subscriber
{
	struct ptrie* ptrie;		// exclusive ptrie object (used for channel name search)
	struct threadqueue* thqueue;	// exclusive message queue 
	psb_subscriber* next;		// next subscribers (double linked list)
	psb_subscriber* prev;		// prev subscribers (double linked list)
	psb_broker* broker;		// pointer to the broker (owner)
};

// Global broker - simplify code in case only broker in program
static psb_broker g_global_psb_broker = {NULL, MUTEX_INITIALIZER};

// insert new subscriber to subscriber's double-linked list
static void slist_insert(psb_subscriber* list, psb_subscriber* entry);

// remove subscriber from subscriber's double-linked list
static void slist_remove(psb_subscriber* entry); 

// duplicate memory object
static void* memdup(const void* mem, size_t size);

/**
 * Create new broker
 *
 * @ingroup PubSubBroker
 *
 * psb_new_broker() allocate and initializes a new broker.
 * A new broker must always be initialized before it is used.
 *
 * @param none
 * @return allocated psb_broker or NULL in case of error
 */
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

/**
 * Delete broker
 *
 * @ingroup PubSubBroker
 *
 * Delete broker and deallocate all linked subscriber.
 *
 * @param psb_broker*
 * @return 0 if success or negative value in case of error
 */
int psb_delete_broker(psb_broker* broker)
{
	// If the broker is not defined use global broker
	if (broker == NULL)
	{
		broker = &g_global_psb_broker;
	}

	// remove all subscribers
	while (broker->subscriber_list->next != broker->subscriber_list)
	{
		psb_delete_subscriber(broker->subscriber_list->next);
	}
	psb_delete_subscriber(broker->subscriber_list);

	// if broker is not global, freeing memory
	if (broker != &g_global_psb_broker)
	{
		free(broker);
	}

	return 0;
}

/**
 * Create new subscriber
 *
 * @ingroup PubSubBroker
 *
 * psb_subscriber() initializes a new subscriber. A new subscriber must always
 * be initialized before it is used.
 *
 * @param parent broker
 * @return allocated psb_subscriber or NULL in case of error
 */
psb_subscriber* psb_new_subscriber(psb_broker* broker)
{
	psb_subscriber* new_sub;
	
	// If the broker is not defined use global broker
	if (broker == NULL)
	{
		broker = &g_global_psb_broker;
	}

	// allocate subscriber
	new_sub = (psb_subscriber*)malloc(sizeof(struct psb_subscriber));
	if (new_sub == NULL)
	{
		return NULL;
	}

	// allocate message queue
	new_sub->thqueue = (struct threadqueue*)malloc(sizeof(struct threadqueue));
	if (new_sub->thqueue == NULL)
	{
		// freeing and return NULL in case of error allocation
		free(new_sub);
		return NULL;
	}

	// allocate ptrie object
	new_sub->ptrie = (struct ptrie*)malloc(sizeof(struct ptrie));
	if (new_sub->ptrie == NULL)
	{
		// freeing and return NULL in case of error allocation
		free(new_sub->thqueue);
		free(new_sub);
		return NULL;
	}

	// initialize message queue	
	if (thread_queue_init(new_sub->thqueue) != 0)
	{
		// freeing and return NULL in case of error allocation
		free(new_sub->thqueue);
		free(new_sub);
		return NULL;
	}

	// initialize ptrie
	ptrie_init(new_sub->ptrie);

	// initialize private vars to safe state
	new_sub->prev = new_sub;
	new_sub->next = new_sub;
	new_sub->broker = broker;

	// enter critical section
	mutex_lock(&broker->mutex);

	// insert new subscriber to subscriber list
	if (broker->subscriber_list != NULL)
	{
		slist_insert(broker->subscriber_list, new_sub);
	}
	else
	{
		broker->subscriber_list = new_sub;
	}

	// leave critical section
	mutex_unlock(&broker->mutex);

	return new_sub;
}

// freeing message's memory
void freedata(void* data)
{
	psb_message* msg = (psb_message*)data;
	psb_free_message(msg);
	free(msg);
}

/**
 * Delete psb_subscriber
 *
 * @ingroup PubSubBroker
 *
 * Delete psb_subscriber and deallocate all queued messages
 *
 * @param psb_subscriber*
 * @return 0 if success or negetive value in case of error
 */
int psb_delete_subscriber(psb_subscriber* subscriber)
{
	// remove all linked objects - mutex, queue, ptrie
	if (subscriber != NULL)
	{
		mutex_t* mutex = &subscriber->broker->mutex;
		mutex_lock(mutex);		// enter to critical section
		slist_remove(subscriber);	// remove subscriber from list
		ptrie_term(subscriber->ptrie);	// remove ptrie object
		free(subscriber->ptrie);	// freeing ptrie memory
		thread_queue_free(subscriber->thqueue, freedata);	// freeing queue (and all queued messages)
		free(subscriber);	// freeing subscriber memory
		mutex_unlock(mutex);	// leave critical section

		return 0;	// success
	}

	return -EINVAL;
}

/**
 * Subscribe to channel
 *
 * @ingroup PubSubBroker
 *
 * psb_subscribe() bind subscriber with channel 'channel_name'.
 * A subscriber can be subscribed to many channels.
 * if subscriber already subscribed to channel error EINVAL returned
 *
 * @param  subscriber
 * @param  channel_name
 * @return 0 if success or negetive value EINVAL if channel already subscribed
 */
int psb_subscribe(psb_subscriber* subscriber, char* channel_name)
{
	int rval = -EINVAL;
	if ((subscriber != NULL) && (channel_name != NULL))
	{
		// enter critical section
		mutex_lock(&subscriber->broker->mutex);

		// check that subscriber is not already subscribed to channel
		if (ptrie_match_str(subscriber->ptrie, (uint8_t*)channel_name, strlen(channel_name)) == 0)
		{
			// subscribe to channel: add channel name to ptrie object
			if (ptrie_add_str(subscriber->ptrie, (uint8_t*)channel_name, strlen(channel_name)) == 1)
			{
				rval = 0;
			}
		}
		
		// leave critical section
		mutex_unlock(&subscriber->broker->mutex);
		return rval;
	}

	return rval;
}

/**
 * Unsubscribe channel
 *
 * @ingroup PubSubBroker
 *
 * psb_unsubscribe() unbind subscriber from channel 'channel_name'.
 * if subscriber is not subscribed to channel error EINVAL returned
 *
 * @param  subscriber
 * @param  channel_name
 * @return 0 if success or negetive value EINVAL if channel is not subscribed to channel
 */
int psb_unsubscribe(psb_subscriber* subscriber, char* channel_name)
{
	int rval = -EINVAL;

	if ((subscriber != NULL) && (channel_name != NULL))
	{
		// enter critical section
		mutex_lock(&subscriber->broker->mutex);

		// unsubscribe from channel: remove channel name from ptrie object
		if (ptrie_remove_str(subscriber->ptrie, (uint8_t*)channel_name, strlen(channel_name)) == 1)
		{
			rval = 0;
		}
		
		// leave critical section
		mutex_unlock(&subscriber->broker->mutex);
		return rval;
	}

	return rval;
}

/**
 * Gets a messages from all channels subscribed.
 *
 * @ingroup PubSubBroker
 *
 * psb_get_message gets a message from all channels subscribed.
 * It will block the caling thread untill a message arrives, or the (optional) timeout occurs.
 * If timeout is NULL, there will be no timeout, and psb_get_message will wait
 * untill a message arrives.
 *
 * @param subscriber Pointer to the subscriber.
 * @param msg pointer to psb_message allocated in heap. The msg should be deallocated with psb_free_message()
 * @param timeout timeout on how long to wait on a message in milliseconds
 *
 * @return 0 on success EINVAL if queue is NULL and ETIMEDOUT (or ERROR_TIMEOUT for windows) if timeout occurs
 */
int psb_get_message(psb_subscriber* subscriber, psb_message* msg, int timeout_ms)
{
	int rval = -EINVAL;
	struct threadmsg tmsg;
	struct timespec ts;
	struct timespec* pts;

	// fill 'ts' structure
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

	// if subscriber valid, get message from queue
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

/**
 * Gets the count of messages in subscriber's queue
 *
 * @ingroup PubSubBroker
 *
 * psb_get_messages_count returns the number of messages waiting in the subscriber's queue
 *
 * @param subscriber Pointer to the subscriber
 * @return the number of pending messages in the subscriber's queue
 */
int psb_get_messages_count(psb_subscriber* subscriber)
{
	int rval = -EINVAL;

	if (subscriber != NULL)
	{
		rval = thread_queue_length(subscriber->thqueue);
	}

	return rval;
}

/**
 * Freeing a memory allocated for messages.
 *
 * @ingroup PubSubBroker
 *
 * psb_free_message freeing memory allocated or channel name and data but not for psb_message itself
 * use it after psb_get_message().
 *
 * @param msg Pointer to the message.
 * @return 0 on success EINVAL if msg is NULL
 */
int psb_free_message(psb_message* msg)
{
	int rval = -EINVAL;
	if (msg)
	{
		// freeing message's channel name
		if (msg->channel)
		{
			free(msg->channel);
		}

		// freeing data
		if (msg->data)
		{
			free(msg->data);
		}

		rval = 0;
	}

	return rval;
}

/**
 * Publish the data object within channel.
 *
 * @ingroup PubSubBroker
 *
 * psb_publish_message() search for all subscribers that subscribe on channel and
 * copy data object to subscriber's queue.
 *
 * @param broker Pointer to the pub/sub broker.
 * @param channel Pointer to the channel name to publish.
 * @param data Pointer to the data object.
 * @param datalen data object size.
 * @return total count of subscribers with matched channels or negative value in case of error
 */
int psb_publish_message(psb_broker* broker, char* channel, void* data, int datalen)
{
	int cnt = 0;
	int res;

	// If the broker is not defined use global broker
	if (broker == NULL)
	{
		broker = &g_global_psb_broker;
	}

	// check arguments
	if ((channel != NULL) && (data != NULL) && (datalen > 0))
	{
		
		// enter critical section
		mutex_lock(&broker->mutex);
		
		if (broker->subscriber_list != NULL)
		{
			// iterate over all subscribers in list and check channel name match
			psb_subscriber* iterator=broker->subscriber_list;
			do
			{
				res = ptrie_match_str(iterator->ptrie, (uint8_t*)channel, strlen(channel));

				// if channel name match, duplicate data and put it to queue
				if (res)
				{
					psb_message* msg = (psb_message*)malloc(sizeof(struct psb_message));
					if (msg)
					{
						msg->channel = strdup(channel);
						msg->data = memdup(data, datalen);
						msg->datalen = datalen;
						thread_queue_put_msg(iterator->thqueue, msg, 0);
						cnt++;	// increment counter
					}
					else
					{
						// leave critical section
						mutex_unlock(&broker->mutex);
						return -ENOMEM;
					}
				}

				iterator = iterator->next;
			}
			while (iterator != broker->subscriber_list);
		}
		
		// leave critical section
		mutex_unlock(&broker->mutex);
	}
	else
	{
		return -EINVAL;
	}

	return cnt;
}

// insert new subscriber to subscriber's double-linked list
static void slist_insert(psb_subscriber* list, psb_subscriber* entry)
{
	entry->prev = list;
	entry->next = list->next;

	list->next->prev = entry;
	list->next       = entry;
}

// remove subscriber from subscriber's double-linked liststatic void slist_remove(psb_subscriber* entry)
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

// duplicate memory object
static void* memdup(const void* mem, size_t size)
{
	void* out = malloc(size);

	if(out != NULL)
	{
		memcpy(out, mem, size);
	}

	return out;
}
