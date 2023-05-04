/*
 * CS4222/5422: Assignment 3b
 * Perform neighbour discovery
 */

#include "contiki.h"
#include "net/netstack.h"
#include "net/nullnet/nullnet.h"
#include "net/packetbuf.h"
#include "lib/random.h"
#include "net/linkaddr.h"
#include <string.h>
#include <stdio.h> 
#include "node-id.h"

// Identification information of the node


// Configures the wake-up timer for neighbour discovery 
#define WAKE_TIME RTIMER_SECOND/10    // 10 HZ, 0.1s
static int rssi_threshold = -65;
static unsigned int nodes_size = 0;
static unsigned long nodes[10][4];
static unsigned long counter = 0;
#define ABSENT 0
#define PRESENT 1
#define SENDER 2
#define RECEIVER 3

#define SLEEP_CYCLE  9        	      // 0 for never sleep
#define SLEEP_SLOT RTIMER_SECOND/10   // sleep slot should not be too large to prevent overflow
#define MATRIX_SIZE 7
// For neighbour discovery, we would like to send message to everyone. We use Broadcast address:
linkaddr_t dest_addr;

#define NUM_SEND 2
/*---------------------------------------------------------------------------*/
typedef struct {
	unsigned long src_id;
	unsigned long timestamp;
	unsigned long seq;
	unsigned long type;

} data_packet_struct;


typedef struct {
	unsigned long requester_id;
	int reading[10];

} light_reading_struct;

typedef struct {
	unsigned long requester_id;
} request_packet_struct;

/*---------------------------------------------------------------------------*/
// duty cycle = WAKE_TIME / (WAKE_TIME + SLEEP_SLOT * SLEEP_CYCLE)
/*---------------------------------------------------------------------------*/

// sender timer implemented using rtimer
static struct rtimer rt;

// Protothread variable
static struct pt pt;

// Structure holding the data to be transmitted
static data_packet_struct data_packet;

static request_packet_struct request_packet;

static light_reading_struct light_packet;

// Current time stamp of the node
unsigned long curr_timestamp;

// Starts the main contiki neighbour discovery process
PROCESS(nbr_discovery_process, "cc2650 neighbour discovery process");
AUTOSTART_PROCESSES(&nbr_discovery_process);

// Function called after reception of a packet
void receive_packet_callback(const void *data, uint16_t len, const linkaddr_t *src, const linkaddr_t *dest) 
{
	// light reading
	if (len == sizeof(light_packet)) {
		static light_reading_struct received_light_data;
		memcpy(&received_light_data, data, len);


		if (received_light_data.requester_id == node_id) {
			printf("Light: ");
			for (int i = 0; i < 10; i++) {
				printf("%d ", received_light_data.reading[i]);
			}
			printf("\n");
		}
	}
	// Check if the received packet size matches with what we expect it to be

	if(len == sizeof(data_packet)) {

		static data_packet_struct received_packet_data;

		// Copy the content of packet into the data structure
		memcpy(&received_packet_data, data, len);

		// ignore packets from receivers
		if (received_packet_data.type == RECEIVER) {
			return;
		}

		int index = -1; 
		for(int i = 0; i < nodes_size; i++)
		{
			if(nodes[i][0] == received_packet_data.src_id)
			{
				index = i;
				break;
			}
		}
		if(index == -1)
		{
			nodes[nodes_size][0] = received_packet_data.src_id;
			nodes[nodes_size][1] = ABSENT;
			nodes[nodes_size][2] = 0;
			nodes[nodes_size][3] = 0;
			index = nodes_size;
			nodes_size += 1;
		}

		signed short rssi_value = (signed short)packetbuf_attr(PACKETBUF_ATTR_RSSI);

		if (rssi_value < 0) {
			//printf("RSSI: %d\n", rssi_value);
			if (rssi_value >= rssi_threshold)
			{
				if((nodes[index][1] == ABSENT && nodes[index][2] == 0) || nodes[index][1] == PRESENT)
				{
					nodes[index][2] = clock_time();

					if (nodes[index][1] == PRESENT && ((clock_time() / CLOCK_SECOND) % 10 == 0)) {
						for (int i = 0; i < 10; i++) {
							// request for pkt transfer
							nullnet_buf = (uint8_t *)&request_packet; //data transmitted
							nullnet_len = sizeof(request_packet); //length of data transmitted

							request_packet.requester_id = node_id;

							NETSTACK_NETWORK.output(&dest_addr); //Packet transmission
						}
					}
				}
				else if (nodes[index][1] == ABSENT && (clock_time() - nodes[index][2]) / CLOCK_SECOND >= 15)
				{
					printf("%ld DETECT %ld\n", nodes[index][2] / CLOCK_SECOND, nodes[index][0]);
					nodes[index][1] = PRESENT;
					nodes[index][3] = 0;

					for (int i = 0; i < 10; i++) {
						// request for pkt transfer
						nullnet_buf = (uint8_t *)&request_packet; //data transmitted
						nullnet_len = sizeof(request_packet); //length of data transmitted

						request_packet.requester_id = node_id;

						NETSTACK_NETWORK.output(&dest_addr); //Packet transmission
					}

				}
			}
			else if(rssi_value < rssi_threshold)
			{
				if(nodes[index][1] == ABSENT)
				{
					nodes[index][2] = 0;
				}
				else // PRESENT
				{
					if(nodes[index][3] == 0) {
						nodes[index][3] = clock_time();
					}
					else if((clock_time() - nodes[index][3]) / CLOCK_SECOND >= 30) {
						printf("%ld ABSENT %ld\n", nodes[index][3] / CLOCK_SECOND, nodes[index][0]);
						nodes[index][1] = ABSENT;
						nodes[index][2] = 0;
					}        
				}

			}
		}
	}
}

static unsigned long curr_row = 0;
static unsigned long curr_col = 0;
static unsigned long row = 0;
static unsigned long col = 0;

// Scheduler function for the sender of neighbour discovery packets
char sender_scheduler(struct rtimer *t, void *ptr) {

	static uint16_t i = 0;

	// Begin the protothread
	PT_BEGIN(&pt);

	// Get the current time stamp
	curr_timestamp = clock_time();

	printf("Start clock %lu ticks, timestamp %3lu.%03lu\n", curr_timestamp, curr_timestamp / CLOCK_SECOND, 
			((curr_timestamp % CLOCK_SECOND)*1000) / CLOCK_SECOND);

	while(1){
		if ((curr_timestamp - counter) / CLOCK_SECOND > 5)
		{
			counter = curr_timestamp;

			for (int i = 0; i < nodes_size; i++) {
				if ((curr_timestamp - nodes[i][2]) / CLOCK_SECOND >= 30 && nodes[i][1] == PRESENT) {
					printf("%ld ABSENT %ld\n", nodes[i][2] / CLOCK_SECOND, nodes[i][0]);
					nodes[i][1] = ABSENT;
					nodes[i][2] = 0;
				}
			}
		}

		if (curr_row == row || curr_col == col) {

			// radio on
			NETSTACK_RADIO.on();

			// send NUM_SEND number of neighbour discovery beacon packets
			for(i = 0; i < NUM_SEND; i++){



				// Initialize the nullnet module with information of packet to be trasnmitted
				nullnet_buf = (uint8_t *)&data_packet; //data transmitted
				nullnet_len = sizeof(data_packet); //length of data transmitted

				data_packet.seq++;

				curr_timestamp = clock_time();

				data_packet.timestamp = curr_timestamp;

				printf("Send seq# %lu  @ %8lu ticks   %3lu.%03lu\n", data_packet.seq, curr_timestamp, curr_timestamp / CLOCK_SECOND, ((curr_timestamp % CLOCK_SECOND)*1000) / CLOCK_SECOND);

				NETSTACK_NETWORK.output(&dest_addr); //Packet transmission


				// wait for WAKE_TIME before sending the next packet
				if(i != (NUM_SEND - 1)){

					rtimer_set(t, RTIMER_TIME(t) + WAKE_TIME, 1, (rtimer_callback_t)sender_scheduler, ptr);
					PT_YIELD(&pt);

				}

			}

			NETSTACK_RADIO.off();
		}

		else {
			// sleep
			rtimer_set(t, RTIMER_TIME(t) + SLEEP_SLOT, 1, (rtimer_callback_t)sender_scheduler, ptr);
			PT_YIELD(&pt);
		}

		if (curr_col < MATRIX_SIZE - 1)
		{
			curr_col += 1;
		}
		else
		{
			if (curr_row < MATRIX_SIZE - 1)
			{
				curr_row += 1;
				curr_col = 0;
			}
			else
			{
				curr_row = 0;
				curr_col = 0;
			}
		}
	}

	PT_END(&pt);
}


// Main thread that handles the neighbour discovery process
PROCESS_THREAD(nbr_discovery_process, ev, data)
{

	// static struct etimer periodic_timer;

	PROCESS_BEGIN();

	row = random_rand() % MATRIX_SIZE;
	col = random_rand() % MATRIX_SIZE;

	// initialize data packet sent for neighbour discovery exchange
	data_packet.src_id = node_id; //Initialize the node ID
	data_packet.seq = 0; //Initialize the sequence number of the packet
	data_packet.type = RECEIVER;

	nullnet_set_input_callback(receive_packet_callback); //initialize receiver callback
	linkaddr_copy(&dest_addr, &linkaddr_null);



	printf("CC2650 neighbour discovery\n");
	printf("Node %d will be sending packet of size %d Bytes\n", node_id, (int)sizeof(data_packet_struct));

	// Start sender in one millisecond.
	rtimer_set(&rt, RTIMER_NOW() + (RTIMER_SECOND / 1000), 1, (rtimer_callback_t)sender_scheduler, NULL);



	PROCESS_END();
}
