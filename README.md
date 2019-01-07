# libpsb
 Little API for multi-thread publisher - subscriber broker.
 Broadcast messages to multiple destinations.

 Messages are sent by psb_publish_message() and will only be received by psb_get_message() that have subscribed to the matching channel.
 Channel defined as string identifier. Call psb_publish_message() will determine whether a copy of message should be delivered to the subscriber(s)
 by comparing the subscriber's channel name to the initial chars in channel name, up to the size of the channel name.

 For example:
 psb_subscribe(subscriber, "logger/");
 Will match any message with intial chars being "logger/", for example, message "logger/error" or "logger/info" will match.

 Subscriber with zero length channel name matches any message.

 If the subscriber is subscribed to multiple channels, message matching any of them will be delivered.

 The subscriber with matched channel name will get copy of data passed to psb_publish_message(), not the data itself.

 The subscribers must call psb_free_message() for freeing message after processing the incoming message.

 The libray was tested in Linux and Windows environment (GCC and VS2015), for other platform please check platform.h file
