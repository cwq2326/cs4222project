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
#include "sys/etimer.h"
#include "board-peripherals.h"
#define SAMPLING_INTERVAL (CLOCK_SECOND * 3) // Sampling interval of 60 seconds
#define NUM_READINGS 10                       // Number of readings to store

// Identification information of the node

// Configures the wake-up timer for neighbour discovery
#define WAKE_TIME RTIMER_SECOND / 10 // 10 HZ, 0.1s
static int rssi_threshold = -65;
static unsigned int nodes_size = 0;
static unsigned long nodes[10][4];
static unsigned long counter = 0;
#define ABSENT 0
#define PRESENT 1
#define SENDER 2
#define RECEIVER 3

#define SLEEP_CYCLE 9                 // 0 for never sleep
#define SLEEP_SLOT RTIMER_SECOND / 10 // sleep slot should not be too large to prevent overflow
#define MATRIX_SIZE 7
// For neighbour discovery, we would like to send message to everyone. We use Broadcast address:
linkaddr_t dest_addr;

#define NUM_SEND 2
/*---------------------------------------------------------------------------*/
typedef struct
{
  unsigned long src_id;
  unsigned long timestamp;
  unsigned long seq;
  unsigned long type;

} data_packet_struct;

typedef struct
{
  unsigned long requester_id;
  int reading[10];

} light_reading_struct;

typedef struct
{
  unsigned long requester_id;
} request_packet_struct;

/*---------------------------------------------------------------------------*/
// duty cycle = WAKE_TIME / (WAKE_TIME + SLEEP_SLOT * SLEEP_CYCLE)
/*---------------------------------------------------------------------------*/

// sender timer implemented using rtimer
static struct rtimer rt;

// Protothread variable
static struct pt pt;

// Array to store 10 light readings
static int light_reading[10] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0};

// timer to count number of light readings (and for reading 10 times)
static int counter_rtimer;

// timer to count every 3 second interval to sample light

static struct etimer timer_etimer;
// timer for callback light reading timeout function
static struct rtimer timer_rtimer;

// time for 0.25s for light sensor to re-arm
static rtimer_clock_t timeout_rtimer = RTIMER_SECOND / 4;

// function protocols
static void init_opt_reading(void);
static void get_light_reading(void);

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

  if (len == sizeof(request_packet))
  {
    static request_packet_struct received_request_packet_data;

    memcpy(&received_request_packet_data, data, len);

    nullnet_buf = (uint8_t *)&light_packet; // data transmitted
    nullnet_len = sizeof(light_packet);     // length of data transmitted
    // light reading
    memcpy(&light_packet.reading, &light_reading, sizeof(light_reading));

    light_packet.requester_id = received_request_packet_data.requester_id;

    NETSTACK_NETWORK.output(&dest_addr); // Packet transmission
  }
  // Check if the received packet size matches with what we expect it to be

  if (len == sizeof(data_packet))
  {

    static data_packet_struct received_packet_data;

    // Copy the content of packet into the data structure
    memcpy(&received_packet_data, data, len);

    if (received_packet_data.type == SENDER)
    {
      return;
    }

    int index = -1;
    for (int i = 0; i < nodes_size; i++)
    {
      if (nodes[i][0] == received_packet_data.src_id)
      {
        index = i;
        break;
      }
    }
    if (index == -1)
    {
      nodes[nodes_size][0] = received_packet_data.src_id;
      nodes[nodes_size][1] = ABSENT;
      nodes[nodes_size][2] = 0;
      nodes[nodes_size][3] = 0;
      index = nodes_size;
      nodes_size += 1;
    }

    signed short rssi_value = (signed short)packetbuf_attr(PACKETBUF_ATTR_RSSI);

    if (rssi_value < 0)
    {
      // printf("RSSI: %d\n", rssi_value);
      if (rssi_value >= rssi_threshold)
      {
        if ((nodes[index][1] == ABSENT && nodes[index][2] == 0) || nodes[index][1] == PRESENT)
        {
          nodes[index][2] = clock_time();
        }
        else if (nodes[index][1] == ABSENT && (clock_time() - nodes[index][2]) / CLOCK_SECOND >= 15)
        {
          printf("%ld DETECT %ld\n", nodes[index][2] / CLOCK_SECOND, nodes[index][0]);
          nodes[index][1] = PRESENT;
          nodes[index][3] = 0;
        }
      }
      else if (rssi_value < rssi_threshold)
      {
        if (nodes[index][1] == ABSENT) //(node_id, is_present, present_timestamp, absent_timestamp)
        {
          nodes[index][2] = 0;
        }
        else // PRESENT
        {
          if (nodes[index][3] == 0)
          {
            nodes[index][3] = clock_time();
          }
          else if ((clock_time() - nodes[index][3]) / CLOCK_SECOND >= 30)
          {
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

void do_rtimer_timeout(struct rtimer *timer, void *ptr)
{
  /* Re-arm rtimer. Starting up the sensor takes around 125ms */
  /* rtimer period 2s */

  counter_rtimer++;
  get_light_reading();
}

static void
init_opt_reading(void)
{
  SENSORS_ACTIVATE(opt_3001_sensor);
}

// Scheduler function for the sender of neighbour discovery packets
char sender_scheduler(struct rtimer *t, void *ptr)
{

  static uint16_t i = 0;

  // Begin the protothread
  PT_BEGIN(&pt);

  // Get the current time stamp
  curr_timestamp = clock_time();

  printf("Start clock %lu ticks, timestamp %3lu.%03lu\n", curr_timestamp, curr_timestamp / CLOCK_SECOND,
         ((curr_timestamp % CLOCK_SECOND) * 1000) / CLOCK_SECOND);

  while (1)
  {
    if (etimer_expired(&timer_etimer))
    {
      rtimer_set(&timer_rtimer, RTIMER_NOW() + timeout_rtimer, 0, do_rtimer_timeout, NULL);
      PT_YIELD(&pt);
    }

    etimer_reset(&timer_etimer);

    if ((curr_timestamp - counter) / CLOCK_SECOND > 5)
    {
      counter = curr_timestamp;

      for (int i = 0; i < nodes_size; i++)
      {
        if ((curr_timestamp - nodes[i][2]) / CLOCK_SECOND >= 30 && nodes[i][1] == PRESENT)
        {
          printf("%ld ABSENT %ld\n", nodes[i][2] / CLOCK_SECOND, nodes[i][0]);
          nodes[i][1] = ABSENT;
          nodes[i][2] = 0;
        }
      }
    }

    if (curr_row == row || curr_col == col)
    {

      // radio on
      NETSTACK_RADIO.on();

      // send NUM_SEND number of neighbour discovery beacon packets
      for (i = 0; i < NUM_SEND; i++)
      {

        // Initialize the nullnet module with information of packet to be trasnmitted
        nullnet_buf = (uint8_t *)&data_packet; // data transmitted
        nullnet_len = sizeof(data_packet);     // length of data transmitted

        data_packet.seq++;

        curr_timestamp = clock_time();

        data_packet.timestamp = curr_timestamp;

        printf("Send seq# %lu  @ %8lu ticks   %3lu.%03lu\n", data_packet.seq, curr_timestamp, curr_timestamp / CLOCK_SECOND, ((curr_timestamp % CLOCK_SECOND) * 1000) / CLOCK_SECOND);

        NETSTACK_NETWORK.output(&dest_addr); // Packet transmission

        // wait for WAKE_TIME before sending the next packet
        if (i != (NUM_SEND - 1))
        {

          rtimer_set(t, RTIMER_TIME(t) + WAKE_TIME, 1, (rtimer_callback_t)sender_scheduler, ptr);
          PT_YIELD(&pt);
        }
      }

      NETSTACK_RADIO.off();
    }

    else
    {
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

static void get_light_reading()
{
  int value;

  value = opt_3001_sensor.value(0);
  if (value != CC26XX_SENSOR_READING_ERROR)
  {
    light_reading[(counter_rtimer - 1 % NUM_READINGS)] = value / 100;
  }
  else
  {
    printf("OPT: Light Sensor's Warming Up\n\n");
  }

  
  rtimer_set(&rt, RTIMER_NOW() + (RTIMER_SECOND / 1000), 1, (rtimer_callback_t)sender_scheduler, NULL);

  init_opt_reading();
}

// Main thread that handles the neighbour discovery process
PROCESS_THREAD(nbr_discovery_process, ev, data)
{
  PROCESS_BEGIN();
  init_opt_reading();
  etimer_set(&timer_etimer, SAMPLING_INTERVAL);

  row = random_rand() % MATRIX_SIZE;
  col = random_rand() % MATRIX_SIZE;

  // initialize data packet sent for neighbour discovery exchange
  data_packet.src_id = node_id; // Initialize the node ID
  data_packet.seq = 0;          // Initialize the sequence number of the packet
  data_packet.type = SENDER;

  nullnet_set_input_callback(receive_packet_callback); // initialize receiver callback
  linkaddr_copy(&dest_addr, &linkaddr_null);

  printf("CC2650 neighbour discovery\n");
  printf("Node %d will be sending packet of size %d Bytes\n", node_id, (int)sizeof(data_packet_struct));

  // Start sender in one millisecond.
  rtimer_set(&rt, RTIMER_NOW() + (RTIMER_SECOND / 1000), 1, (rtimer_callback_t)sender_scheduler, NULL);

  PROCESS_END();
}
