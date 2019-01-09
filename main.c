#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "threadqueue.h"
#include "psb.h"
#include "platform.h"

/*********************************** TEST **********************************/
#include <stdio.h>

#define NPUB		5
#define NSUB		25
#define NCH			10
#define NMSG		1000

#if defined(_WIN32) || defined(_WIN64) // use the native win32 API on windows
#define DEFINE_THREAD(NAME, PARAM)  DWORD WINAPI NAME( LPVOID PARAM )
void usleep(DWORD waitTime)
{
	Sleep(waitTime/1000);
}
#else
#include <unistd.h>
#define DEFINE_THREAD(NAME, PARAM)  void* NAME(void* PARAM)
#endif

char* channel_list[] =
{
	"ch1/topic1",
	"ch1/topic2",
	"ch2/topic1",
	"ch2/topic2",
	"ch3/topic1/item0",
	"ch3/topic2/item1",
	"ch1",
	"ch2",
	"ch3/topic1/item0",
	"ch3/topic10"
};

int get_thread_id(void)
{
#if defined(_WIN32) || defined(_WIN64) // use the native win32 API on windows

	return GetCurrentThreadId();
#else
	return pthread_self();
#endif
}

DEFINE_THREAD(pub_fn, dummyPtr)
{
	int i;
	int thread_id = get_thread_id();

	usleep(1000000);    // force timeouts in subscribers ...

	for (i=0; i<NMSG; i++)
	{
		int ir = rand() % (NCH);
		int np = psb_publish_message(DEFAULT_BROKER, channel_list[ir], channel_list[ir], strlen(channel_list[ir])+1);
		printf("PUBLISHER[%08d]: publish %d messages for channel %s\n", thread_id, np, channel_list[ir]);
		usleep(ir*1000);
	}

	return NULL;
}

DEFINE_THREAD(sub_fn, dummyPtr)
{
	psb_message msg;
	int rval;
	int i,k;
	int thread_id = get_thread_id();
	int sub_ch_idx[10] = {-1,-1,-1,-1,-1,-1,-1,-1,-1,-1};

	psb_subscriber* subscriber = psb_new_subscriber(DEFAULT_BROKER);

	for (i=0; i<NMSG; i++)
	{
		int ir = rand() % NCH;

		for (k = 9; k > 0; k--)
		{
			sub_ch_idx[k]=sub_ch_idx[k-1];
		}
		sub_ch_idx[0] = ir;

		rval = psb_subscribe(subscriber, channel_list[ir]);
		if (rval == 0)
		{
			printf("SUBSCRIBER[%08d]: subscribe to channel %s - SUCCESS\n", thread_id, channel_list[ir]);
		}
		else
		{
			printf("SUBSCRIBER[%08d]: subscribe to channel %s - ALREADY SUBSCRIBED\n", thread_id, channel_list[ir]);
		}

		if (sub_ch_idx[9] != -1)
		{
			ir = sub_ch_idx[9];
			rval = psb_unsubscribe(subscriber, channel_list[ir]);
			if (rval == 0)
			{
				printf("SUBSCRIBER[%08d]: unsubscribe from channel %s - SUCCESS\n", thread_id, channel_list[ir]);
			}
			else
			{
				printf("SUBSCRIBER[%08d]: unsubscribe from channel %s - NOT EXIST\n", thread_id, channel_list[ir]);
			}
		}

		if (psb_get_message(subscriber, &msg, 100) == 0)
		{
			printf("SUBSCRIBER[%08d]: Got message from channel %s (data: %s)\n", thread_id, msg.channel, (char*)msg.data);
			psb_free_message(&msg);
		}
		else
		{
			printf("SUBSCRIBER[%08d]: Timeout\n", thread_id);
		}

		usleep(ir*1000);
	}

	return NULL;
}

void psb_test_init(void)
#if defined(_WIN32) || defined(_WIN64) // use the native win32 API on windows
{
	HANDLE  pub_thread_id[NPUB];
	HANDLE  sub_thread_id[NSUB];
	int i;

	printf("MultiThread Sub/Pub test started.\n");

	for (i = 0; i < NPUB; i++)
	{
		pub_thread_id[i] = CreateThread(
			NULL,                   // default security attributes
			0,                      // use default stack size
			pub_fn,				    // thread function name
			NULL,		            // argument to thread function
			0,                      // use default creation flags
			NULL); 					// returns the thread identifier

		if (pub_thread_id[i] == NULL)
		{
			printf("CreateThread() failed.\n");
			ExitProcess(3);
		}
	}

	for (i = 0; i < NSUB; i++)
	{
		sub_thread_id[i] = CreateThread(
			NULL,                   // default security attributes
			0,                      // use default stack size
			sub_fn,				    // thread function name
			NULL,		            // argument to thread function
			0,                      // use default creation flags
			NULL); 					// returns the thread identifier

		if (sub_thread_id[i] == NULL)
		{
			printf("CreateThread() failed.\n");
			ExitProcess(3);
		}
	}

	// Wait until all threads have terminated.
	for(i=0; i<NSUB; i++)
	{
		WaitForSingleObject(sub_thread_id[i], INFINITE);
		CloseHandle(sub_thread_id[i]);
	}
	for(i=0; i<NPUB; i++)
	{
		WaitForSingleObject(pub_thread_id[i], INFINITE);
		CloseHandle(pub_thread_id[i]);
	}

	psb_delete_broker(DEFAULT_BROKER);

	printf("MultiThread Sub/Pub test finished.\n");
}

#else

{
	pthread_t pub_thread_id[NPUB];
	pthread_t sub_thread_id[NSUB];
	int i;

	printf("MultiThread Sub/Pub test started.\n");

	for (i = 0; i < NPUB; i++)
	{
		pthread_create(&pub_thread_id[i], NULL, pub_fn, NULL);
	}

	for (i = 0; i < NSUB; i++)
	{
		pthread_create(&sub_thread_id[i], NULL, sub_fn, NULL);
	}

	for (i = 0; i < NPUB; i++)
	{
		pthread_join(pub_thread_id[i], NULL);
	}

	for (i = 0; i < NSUB; i++)
	{
		pthread_join(sub_thread_id[i], NULL);
	}

	psb_delete_broker(DEFAULT_BROKER);

	printf("MultiThread Sub/Pub test finished.\n");


}
#endif

void psb_test()
{
	volatile int rval = 0;
	psb_message msg;
	char* data1 = "data1";
	char* data2 = "data2";
	char* data3 = "data3";

	psb_broker* broker = DEFAULT_BROKER;
	broker = psb_new_broker();

	psb_subscriber* subscriber1 = psb_new_subscriber(broker);
	psb_subscriber* subscriber2 = psb_new_subscriber(broker);
	psb_subscriber* subscriber3 = psb_new_subscriber(broker);
	psb_subscriber* subscriber4 = psb_new_subscriber(broker);

	rval = psb_subscribe(subscriber1, "ch1");
	rval = psb_subscribe(subscriber1, "ch2");

	rval = psb_subscribe(subscriber2, "ch1/topic1");
	rval = psb_subscribe(subscriber3, "ch1/topic2");
	rval = psb_subscribe(subscriber4, "ch2/topic1");

	rval = psb_publish_message(broker, "ch1/topic1", data1, strlen(data1)+1);
	rval = psb_publish_message(broker, "ch1/topic2", data2, strlen(data2)+1);
	rval = psb_publish_message(broker, "ch2/topic1", data3, strlen(data3)+1);
	rval = psb_publish_message(broker, "void", data3, strlen(data3)+1);

	rval = psb_get_messages_count(subscriber1); 
	rval = psb_get_messages_count(subscriber2);
	rval = psb_get_messages_count(subscriber3);
	rval = psb_get_messages_count(subscriber4);

	rval = psb_get_message(subscriber1, &msg, -1); psb_free_message(&msg);
	rval = psb_get_message(subscriber1, &msg, -1); psb_free_message(&msg);
	rval = psb_get_message(subscriber1, &msg, -1); psb_free_message(&msg);
	rval = psb_get_message(subscriber2, &msg, -1); psb_free_message(&msg);
	rval = psb_get_message(subscriber3, &msg, -1); psb_free_message(&msg);
	rval = psb_get_message(subscriber4, &msg, -1); psb_free_message(&msg);

	rval = psb_get_message(subscriber4, &msg, 1000);  // here we are get timeout

	rval = psb_get_messages_count(subscriber1);
	rval = psb_get_messages_count(subscriber2);
	rval = psb_get_messages_count(subscriber3);
	rval = psb_get_messages_count(subscriber4);

	rval = psb_unsubscribe(subscriber1, "ch2");
	rval = psb_publish_message(broker, "ch1/topic1", data1, strlen(data1)+1);
	rval = psb_publish_message(broker, "ch1/topic2", data2, strlen(data2)+1);
	rval = psb_publish_message(broker, "ch2/topic1", data3, strlen(data3)+1);

	rval = psb_get_messages_count(subscriber1);
	rval = psb_get_messages_count(subscriber2);
	rval = psb_get_messages_count(subscriber3);
	rval = psb_get_messages_count(subscriber4);

	//rval = psb_delete_subscriber(subscriber2);

	rval = psb_delete_broker(broker);
}



int main(int argc, char** argv)
{
	psb_test_init();
	psb_test();
	return 0;
}

