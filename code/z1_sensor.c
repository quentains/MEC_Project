#include "contiki.h"
#include "net/rime/rime.h"
#include "random.h"
#include "lib/list.h"
#include "lib/memb.h"
#include "lib/random.h"

#include <stdio.h>

#define MAX_RETRANSMISSIONS 4
#define MAX_ROUTES 10
#define INACTIVE_DATA_TRANSFERS 4

// Utils function for computing the Rime ID
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


/* This structure holds information about the routes. */
struct routes {

  // The ->next pointer is needed for Contiki list
  struct routes *next;

  // The id that we want to reach
  int id;

  // Where to forward the message
  linkaddr_t addr_fwd;

  // int used to determine whether or not the node is still active
  int age;

};

MEMB(routes_memb, struct routes, MAX_ROUTES);
LIST(routes_list);

/*---------------------------------------------------------------------------*/
PROCESS(network_setup, "Network Setup");
PROCESS(send_sensor_data, "Send Sensor Data");
PROCESS(forwarding_messages, "Forwarding SRV & COM");
AUTOSTART_PROCESSES(&network_setup, &send_sensor_data, &forwarding_messages);

static linkaddr_t *parent_node;
static int not_connected = 1;
static int parent_signal = -9999;

void remove_old_routes()
{
  struct routes *route;

  for(route = list_head(routes_list); route != NULL; route = list_item_next(route)) 
  {
    if(INACTIVE_DATA_TRANSFERS <= (int) route->age ) 
    {
      printf("removed route \n");
      list_remove(routes_list, route);
    }
    else 
    {
      route->age += 1;
    }
  }
}

/*---------------------------------------------------------------------------*/
static void
recv_bdcst(struct broadcast_conn *c, const linkaddr_t *from)
{
  char message[10];
  strcpy(message, (char *)packetbuf_dataptr());


  // If announce from a new node
  if (message[0] == 'N' && message[1] == 'D' && message[2] == 'A')
  {
    // If this node is connected to the server
    printf("[SETUP THREAD] Announce received : %s\n", message);
    if (!not_connected)
    {
      // Respond to the child
      sprintf(message, "NDR%d", from->u8[0]);
      packetbuf_copyfrom(message, strlen(message));
      broadcast_send(c);
      printf("[SETUP THREAD] Reponse (NDR) sent : %s\n", message);
    }
    
  }

  // If response to an announce
  else if (message[0] == 'N' && message[1] == 'D' && message[2] == 'R')
  {
    size_t size, i;
    int recipient = 0;
    size = strlen(message) - 3;

    // Compute the recipient from the radio message 
    for(i = 0 ; i < size ; i++)
    {
      recipient = recipient + ((message[i+3]-48) * power(10,size-i-1));
    }

    // If the message is for this node (avoid broadcast loop)
    if(recipient == linkaddr_node_addr.u8[0])
    {
      printf("[SETUP THREAD] Parent response received from %d with signal %d\n", from->u8[0], packetbuf_attr(PACKETBUF_ATTR_RSSI));

      if (packetbuf_attr(PACKETBUF_ATTR_RSSI) > parent_signal)
      {
        printf("[SETUP THREAD] This parent is better than %d\n", parent_signal);
        parent_signal = packetbuf_attr(PACKETBUF_ATTR_RSSI);
        //from = memb_alloc(&linkaddr_memb);
        linkaddr_copy(parent_node, from);
        not_connected = 0;
      }
    }
    
  }
  
}

static const struct broadcast_callbacks broadcast_call = {recv_bdcst};
static struct broadcast_conn broadcast;

/*---------------------------------------------------------------------------*/
PROCESS_THREAD(network_setup, ev, data)
{
  static struct etimer et;
  char message[10];
  PROCESS_EXITHANDLER(broadcast_close(&broadcast);)

  PROCESS_BEGIN();

  broadcast_open(&broadcast, 129, &broadcast_call);

  printf("[SENSOR] I'm %d\n", linkaddr_node_addr.u8[0]);

  while(1) {

    // If the node is not connected to the network, try to connect
    if (not_connected)
    {
      sprintf(message, "NDA");
      packetbuf_copyfrom(message, strlen(message));
      broadcast_send(&broadcast);
      printf("[SETUP THREAD] Announce (NDA) sent : %s\n", message);
    }

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
  char message[10];
  strcpy(message, (char *)packetbuf_dataptr());
  int original_sender = 0;
  size_t size, i;

  // If SRV message, need to forward it to the parent_node
  if (message[0] == 'S' && message[1] == 'R' && message[2] == 'V')
  {
    // Get the air quality
    //int air_quality = (message[3]-48) * 10 + (message[4]-48);

    // Get the size of the address (e.g. "1" or "78" or "676")
    size = strlen(message) - 5;

    // Get the address of the original sender
    for(i = 0 ; i < size ; i++) 
    {
      original_sender = original_sender + ((message[i+5]-48) * power(10,size-i-1));
    }

    struct routes *new_route;

    /* Check if we already know this routes. */
    for(new_route = list_head(routes_list); new_route != NULL; new_route = list_item_next(new_route)) 
    {

      // We break out of the loop if the address in the list matches with from
      if((int)new_route->id == (int)original_sender) 
      {
          break;
      }

    }

    // If new_route is NULL, this node was not found in our list
    if(new_route == NULL) 
    {
      new_route = memb_alloc(&routes_memb);

      // If allocation failed, we give up.
      if(new_route == NULL) 
      {
        return;
      }
    }
    new_route->age = 0;
    if (!linkaddr_cmp(&new_route->addr_fwd, from)) // If routing has changed
    {
      // Initialize the new_route.
      linkaddr_copy(&new_route->addr_fwd, from);
      new_route->id = original_sender;
      // Add the route into the list
      printf("[ROUTING] New route\n");
      list_add(routes_list, new_route);
    }

    //printf("[DATA THREAD] Unicast received from %d : Sensor %d - Quality = %d\n", 
    //  from->u8[0], original_sender, air_quality);
    if (from->u8[0] != parent_node->u8[0])
    {
      // Forward the message to the parent
      packetbuf_copyfrom(message, strlen(message));
      runicast_send(c, parent_node, MAX_RETRANSMISSIONS);

      printf("[FORWARDING THREAD] Forwarding from %d to %d (%s)\n", from->u8[0], parent_node->u8[0], message);
    }

  }
  else if (message[0] == 'C' && message[1] == 'O' && message[2] == 'M')
  {
    // Get the size of the address (e.g. "1" or "78" or "676")
    size = strlen(message) - 5;
    int recipient = 0;    
    int order = message[3] - '0';

    // Get the address of the original sender
    for(i = 0 ; i < size ; i++)
    {
      recipient = recipient + ((message[i+5]-48) * power(10,size-i-1));
    }

    if(recipient == linkaddr_node_addr.u8[0])
    {
      printf("I was ordered by %d to follow order %d (%s)\n", from->u8[0], order, message);
    }
    else
    {
      struct routes *route;

      // Print the current routes
      for(route = list_head(routes_list); route != NULL; route = list_item_next(route)) 
      {
        // We break out of the loop if the address in the list matches with from
        if((int)route->id == (int)recipient) 
        {
          break;
        }
      }

      // Forward the message to the parent
      packetbuf_copyfrom(message, strlen(message));
      runicast_send(c, &route->addr_fwd, MAX_RETRANSMISSIONS);
      
      printf("[FORWARDING THREAD] [TO NODE] Order: %d received from %d for %d (%s)\n", order, from->u8[0], recipient, message);
    }
    
  }
  else
  {
    // DEBUG PURPOSE
    printf("[FORWARDING THREAD] Weird message received from %d.%d\n", from->u8[0], from->u8[1]);
  }

  /* ===== DEBUG ==== */
  struct routes *route;

  // Print the current routes
  for(route = list_head(routes_list); route != NULL; route = list_item_next(route)) 
  {
    printf("[ROUTING] To contact %d, I have to send to %d\n", route->id, route->addr_fwd.u8[0]);
    //list_remove(routes_list, route);
  }
  /* ================ */

}

static void
sent_runicast(struct runicast_conn *c, const linkaddr_t *to, uint8_t retransmissions)
{
  //printf("runicast message sent to %d.%d, retransmissions %d\n",
  // to->u8[0], to->u8[1], retransmissions);
}
static void
timedout_runicast(struct runicast_conn *c, const linkaddr_t *to, uint8_t retransmissions)
{
  // If connecion timeout, re-run the network setup to find a new parent
  printf("[FORWARDING THREAD] Impossible to send data, disconnected from network.");
  not_connected = 1;
  parent_signal = -9999;
}

/*---------------------------------------------------------------------------*/

static const struct runicast_callbacks runicast_callbacks = {recv_ruc, sent_runicast, timedout_runicast};
static struct runicast_conn runicast;

/*---------------------------------------------------------------------------*/

// Sending data thread
PROCESS_THREAD(send_sensor_data, ev, data)
{
  char message[10];
  int air_quality;
  static struct etimer before_start;

  PROCESS_EXITHANDLER(runicast_close(&runicast);)
    
  PROCESS_BEGIN();

  runicast_open(&runicast, 144, &runicast_callbacks);

  /* Wait random seconds to simulate the different time
  of the node installation (0-59min) */
  printf("[DATA THREAD] Waiting before start ...\n");
  etimer_set(&before_start, random_rand()%60 * CLOCK_SECOND);
  PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&before_start));
  printf("[DATA THREAD] Starting ...\n");

  while(1) {
    static struct etimer et;
    
    // Send the data to the parent
    if(!not_connected && !runicast_is_transmitting(&runicast)) {

      // Generate random sensor data
      air_quality = random_rand() % 99 + 1;
      
      sprintf(message, "SRV%02d%d", air_quality, linkaddr_node_addr.u8[0]);
      packetbuf_copyfrom(message, strlen(message));

      runicast_send(&runicast, parent_node, MAX_RETRANSMISSIONS);

      printf("[DATA THREAD] Sending data (%d) to the server\n", air_quality);
    }
    remove_old_routes();

    /* Delay 1 minute */
    etimer_set(&et, 60*CLOCK_SECOND);
    PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&et));

  }

  PROCESS_END();
}

/*---------------------------------------------------------------------------*/

// Only forwarding thread
PROCESS_THREAD(forwarding_messages, ev, data)
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
