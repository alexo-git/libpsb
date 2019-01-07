/*
 * PubSub broker
 * psb.h
 *
 *  Created on: Jan 20, 2018
 *      Author: alexo
 */

#ifndef PSB_H_
#define PSB_H_

#ifdef __cplusplus
extern "C"
{
#endif

/**
 * @defgroup PubSubBroker PubSubBroker
 *
 * Little API for multi-thread publisher - subscriber broker.
 * Broadcast messages to multiple destinations.
 *
 * Messages are sent by psb_publish_message() and will only be received by psb_get_message() that have subscribed to the matching channel.
 * Channel defined as string identifier. Call psb_publish_message() will determine whether a copy of message should be delivered to the subscriber(s)
 * by comparing the subscriber's channel name to the initial chars in channel name, up to the size of the channel name.
 * For example:
 * psb_subscribe(subscriber, "logger/");
 * Will match any message with intial chars being "logger/", for example, message "logger/error" or "logger/info" will match.
 *
 * Subscriber with zero length channel name matches any message.
 *
 * If the subscriber is subscribed to multiple channels, message matching any of them will be delivered.
 *
 * The subscriber with matched channel name will get copy of data passed to psb_publish_message(), not the data itself.
 *
 * The subscribers must call psb_free_message() for freeing message after processing the incoming message.
 *
 */

/**
 * @mainpage
 */


#define DEFAULT_BROKER		NULL

typedef struct psb_subscriber psb_subscriber;
typedef struct psb_broker psb_broker;
typedef struct psb_message psb_message;

struct psb_message
{
	void*	data;
	int		datalen;
	char*	channel;
};

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
psb_broker* psb_new_broker();

/**
 * Delete broker
 *
 * @ingroup PubSubBroker
 *
 * Delete broker and deallocate all linked publisher/subscriber.
 *
 * @param psb_broker*
 * @return 0 if success or negative value in case of error
 */
int psb_delete_broker(psb_broker* broker);

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
psb_subscriber* psb_new_subscriber(psb_broker* broker);

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
int psb_delete_subscriber(psb_subscriber* subscriber);

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
int psb_subscribe(psb_subscriber* subscriber, char* channel_name);

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
int psb_unsubscribe(psb_subscriber* subscriber, char* channel_name);

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
int psb_get_message(psb_subscriber* subscriber, psb_message* msg, int timeout_ms);

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
int psb_get_messages_count(psb_subscriber* subscriber);


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
int psb_free_message(psb_message* msg);

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
int psb_publish_message(psb_broker* broker, char* channel, void* data, int datalen);

#ifdef __cplusplus
}
#endif

#endif /* PSB_H_ */
