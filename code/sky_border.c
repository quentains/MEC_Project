#include "contiki.h"
#include "net/rime/rime.h"
#include "random.h"
#include "lib/list.h"
#include "lib/memb.h"
#include "lib/random.h"

#include <stdio.h>

#define MAX_RETRANSMISSIONS 4

int power(int a, int b)
{
  int res = 1;
  size_t i;
  for(i = 0 ; i < b ; i++)
  {
    res = res * a;
  }
  return res;
}

/*---------------------------------------------------------------------------*/
PROCESS(network_setup, "Network Setup");
PROCESS(send_sensor_information, "Send Sensor Information");
AUTOSTART_PROCESSES(&network_setup, &send_sensor_information);

//TODO add a list of all the child nodes using list_neighbors, LIST and MEMB

/*---------------------------------------------------------------------------*/
static void
recv_child_announce(struct broadcast_conn *c, const linkaddr_t *from)
{
  char message[100];
  strcpy(message, (char *)packetbuf_dataptr());

  // Always respond to child announce
  if (message[0] == 'N' && message[1] == 'D' && message[2] == 'A')
  {
    printf("[SETUP THREAD] Child announce received : %s\n", message);

    // Respond to the child
    sprintf(message, "NDR%d", from->u8[0]);
    packetbuf_copyfrom(message, strlen(message));
    broadcast_send(c);
    printf("[SETUP THREAD] Reponse (NDR) sent : %s\n", message);
  }
  
}


static const struct broadcast_callbacks broadcast_call = {recv_child_announce};
static struct broadcast_conn broadcast;

/*---------------------------------------------------------------------------*/
PROCESS_THREAD(network_setup, ev, data)
{
  static struct etimer et;

  PROCESS_EXITHANDLER(broadcast_close(&broadcast);)

  PROCESS_BEGIN();

  broadcast_open(&broadcast, 129, &broadcast_call);

  printf("[BORDER] I'm %d\n", linkaddr_node_addr.u8[0]);

  while(1) {

    /* Delay 2-4 seconds */
    etimer_set(&et, CLOCK_SECOND * 4 + random_rand() % (CLOCK_SECOND * 4));
    PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&et));
  }

  PROCESS_END();
}

/*---------------------------------------------------------------------------*/

/* This function is called for every incoming unicast packet. */
static void
recv_ruc(struct runicast_conn *c, const linkaddr_t *from, uint8_t seqno)
{
  char message[100];
  strcpy(message, (char *)packetbuf_dataptr());
  int original_sender = 0;
  size_t size, i;

  // If SRV message, need to forward it to the parent_node
  if (message[0] == 'S' && message[1] == 'R' && message[2] == 'V')
  {
    // Get the air quality
    int air_quality = (message[3]-48) * 10 + (message[4]-48);

    // Get the size of the address (e.g. "1" or "78" or "676")
    size = strlen(message) - 5;

    // Get the address of the original sender
    for(i = 0 ; i < size ; i++)
    {
      original_sender = original_sender + ((message[i+5]-48) * power(10,size-i-1));
    }

    //printf("[DATA THREAD] Unicast received from %d : Sensor %d - Quality = %d\n", 
    //  from->u8[0], original_sender, air_quality);

    printf("[DATA THREAD] Data (%d) from node %d received\n", air_quality, original_sender);

  }
  else
  {
    // DEBUG PURPOSE
    printf("Weird message received from %d.%d\n", from->u8[0], from->u8[1]);
  }

}

/*---------------------------------------------------------------------------*/

static const struct runicast_callbacks runicast_callbacks = {recv_ruc};
static struct runicast_conn runicast;

/*---------------------------------------------------------------------------*/
PROCESS_THREAD(send_sensor_information, ev, data)
{
  PROCESS_EXITHANDLER(runicast_close(&runicast);)
    
  PROCESS_BEGIN();

  runicast_open(&runicast, 144, &runicast_callbacks);

  while(1) {

    // Wait for SRV / CMD to forward
    PROCESS_WAIT_EVENT();
  
  }

  PROCESS_END();
}