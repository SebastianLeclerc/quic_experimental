#include <nng/nng.h>
#include <nng/supplemental/util/platform.h>
#include <nng/mqtt/mqtt_quic.h>
#include <nng/mqtt/mqtt_client.h>

#include "msquic.h"

#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <time.h>
#include <sys/mman.h>

#define CONN 1
#define SUB 2
#define PUB 3
#define MAX_BUFFERED_MSGS 100000

static nng_socket *g_sock;

static char *msg_buffer[MAX_BUFFERED_MSGS];
static int msg_count = 0;

extern char *msg_buffer[];
extern int msg_count;
extern nng_socket sock;

conf_quic config_user = {
	.tls = {
		.enable = false,
		.cafile = "",
		.certfile = "",
		.keyfile = "",
		.key_password = "",
		.verify_peer = true,
		.set_fail = true,
	},
	.multi_stream = false,
	.qos_first = false,
	.qkeepalive = 10,
	.qconnect_timeout = 60,
	.qdiscon_timeout = 30,
	.qidle_timeout = 30,
};

void handle_sigint(int sig)
{
	printf("\nCaught CTRL+C, saving buffered messages...\n");

	FILE *f = fopen("messages.log", "w");
	if (!f)
	{
		perror("file");
	}
	else
	{
		fprintf(f, "recv_timestamp,topic,seq,send_timestamp,random_data\n");
		for (int i = 0; i < msg_count; i++)
		{
			fprintf(f, "%s\n", msg_buffer[i]);
			free(msg_buffer[i]);
		}
		fclose(f);
		printf("Messages saved to messages.log\n");
	}

	nng_close(*g_sock);
	exit(0);
}

static void fatal(const char *msg, int rv)
{
	fprintf(stderr, "%s: %s\n", msg, nng_strerror(rv));
}

static nng_msg *mqtt_msg_compose(int type, int qos, char *topic, char *payload)
{
	// Mqtt connect message
	nng_msg *msg;
	nng_mqtt_msg_alloc(&msg, 0);

	if (type == CONN)
	{
		nng_mqtt_msg_set_packet_type(msg, NNG_MQTT_CONNECT);

		nng_mqtt_msg_set_connect_proto_version(msg, 4);
		nng_mqtt_msg_set_connect_keep_alive(msg, 10);
		nng_mqtt_msg_set_connect_clean_session(msg, true);
	}
	else if (type == SUB)
	{
		nng_mqtt_msg_set_packet_type(msg, NNG_MQTT_SUBSCRIBE);

		nng_mqtt_topic_qos subscriptions[] = {
			{.qos = qos,
			 .topic = {
				 .buf = (uint8_t *)topic,
				 .length = strlen(topic)}},
		};
		int count = sizeof(subscriptions) / sizeof(nng_mqtt_topic_qos);

		nng_mqtt_msg_set_subscribe_topics(msg, subscriptions, count);
	}

	return msg;
}

static int connect_cb(void *rmsg, void *arg)
{
	printf("[Connected][%s]...\n", (char *)arg);
	return 0;
}

static int disconnect_cb(void *rmsg, void *arg)
{
	printf("[Disconnected][%s]...\n", (char *)arg);
	return 0;
}

static int msg_send_cb(void *rmsg, void *arg)
{
	printf("[Msg Sent][%s]...\n", (char *)arg);
	return 0;
}

static int msg_recv_cb(void *rmsg, void *arg)
{
	int silent = *(int *)arg; // pass silent flag by pointer

	nng_msg *msg = rmsg;
	uint32_t topicsz, payloadsz;

	char *topic = (char *)nng_mqtt_msg_get_publish_topic(msg, &topicsz);
	char *payload = (char *)nng_mqtt_msg_get_publish_payload(msg, &payloadsz);

	// --- take recv timestamp as close to network stack as possible ---
	struct timespec ts;
	clock_gettime(CLOCK_REALTIME, &ts);
	uint64_t recv_ns = (uint64_t)ts.tv_sec * 1000000000ULL + ts.tv_nsec;

	if (silent == 0)
	{
		printf("[Msg Arrived]\n");
		printf("topic   => %.*s\n", topicsz, topic);
		printf("payload => %.*s\n", payloadsz, payload);
	}
	else
	{
		// --- store recv_timestamp,topic,payload ---
		if (msg_count < MAX_BUFFERED_MSGS)
		{

			// recv timestamp as string
			char tbuf[64];
			snprintf(tbuf, sizeof(tbuf), "%llu",
					 (unsigned long long)recv_ns);

			size_t tlen = strlen(tbuf);
			size_t tplen = topicsz;
			size_t plen = payloadsz;

			// total length: ts + ',' + topic + ',' + payload + '\0'
			size_t entry_len = tlen + 1 + tplen + 1 + plen;

			msg_buffer[msg_count] = malloc(entry_len + 1);
			char *ptr = msg_buffer[msg_count];

			// copy recv timestamp
			memcpy(ptr, tbuf, tlen);
			ptr += tlen;
			*ptr++ = ',';

			// copy topic (binary-safe)
			memcpy(ptr, topic, tplen);
			ptr += tplen;
			*ptr++ = ',';

			// copy payload (also binary-safe)
			memcpy(ptr, payload, plen);
			ptr += plen;

			// null terminate
			*ptr = '\0';

			msg_count++;
		}
	}

	return 0;
}

int client(int type, const char *url, const char *qos, const char *topic, int silent)
{
	nng_socket sock;
	int rv, sz, q;
	nng_msg *msg;
	const char *arg = "CLIENT FOR QUIC";

	if ((rv = nng_mqtt_quic_client_open_conf(&sock, url, &config_user)) != 0)
	{
		printf("error in quic client open.\n");
	}

	if (silent == 0)
	{
		if (0 != nng_mqtt_quic_set_connect_cb(&sock, connect_cb, (void *)arg) ||
			0 != nng_mqtt_quic_set_disconnect_cb(&sock, disconnect_cb, (void *)arg) ||
			0 != nng_mqtt_quic_set_msg_recv_cb(&sock, msg_recv_cb, (void *)arg) ||
			0 != nng_mqtt_quic_set_msg_send_cb(&sock, msg_send_cb, (void *)arg))
		{
			printf("error in quic client cb set.\n");
		}
	}
	else
	{

		int silent_copy = silent; // must persist
		nng_mqtt_quic_set_msg_recv_cb(&sock, msg_recv_cb, &silent_copy);
	}
	g_sock = &sock;

	// MQTT Connect...
	msg = mqtt_msg_compose(CONN, 0, NULL, NULL);
	nng_sendmsg(sock, msg, NNG_FLAG_ALLOC);

	if (qos)
	{
		q = atoi(qos);
		if (q < 0 || q > 2)
		{
			printf("Qos should be in range(0~2).\n");
			q = 0;
		}
	}

	msg = mqtt_msg_compose(SUB, q, (char *)topic, NULL);
	nng_sendmsg(*g_sock, msg, NNG_FLAG_ALLOC);

	for (;;)
		nng_msleep(1000);

	nng_close(sock);
	fprintf(stderr, "Done.\n");

	return (0);
}

int main(int argc, char **argv)
{
	mlockall(MCL_CURRENT | MCL_FUTURE);
	signal(SIGINT, handle_sigint);
	int rc;

	if (argc < 3)
	{
		goto error;
	}
	else if (0 == strncmp(argv[1], "sub", 3) && argc == 6)
	{
		int silent = atoi(argv[5]);
		client(SUB, argv[2], argv[3], argv[4], silent);
	}
	else
	{
		goto error;
	}

	return 0;

error:
	fprintf(stderr, "Usage: sub  <url> <qos> <topic> <silent>\n");
	return 0;
}
