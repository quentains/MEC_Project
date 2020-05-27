#include "contiki.h"
#include "net/rime/rime.h"
#include "random.h"
#include "lib/list.h"
#include "lib/memb.h"
#include "lib/random.h"

#include <stdio.h>

#define MAX_RETRANSMISSIONS 4
#define MAX_ROUTES 10

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

};

MEMB(routes_memb, struct routes, MAX_ROUTES);
LIST(routes_list);

/*---------------------------------------------------------------------------*/
PROCESS(network_setup, "Network Setup");
PROCESS(receive_data, "Receive SRV messages");
PROCESS(send_orders, "Send COM messages");
AUTOSTART_PROCESSES(&network_setup, &receive_data, &send_orders);

/*---------------------------------------------------------------------------*/
static void
recv_child_announce(struct broadcast_conn *c, const linkaddr_t *from)
{
  char message[10];
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
  char message[10];
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

    struct routes *new_route;

    /* Check if we already know this routes. */
    for(new_route = list_head(routes_list); new_route != NULL; new_route = list_item_next(new_route)) 
    {

      // We break out of the loop if the address in the list matches with from
      if((int)new_route->id == (int)original_sender) 
      {
        if(linkaddr_cmp(&new_route->addr_fwd, from))
        {
          printf("[FORWARDING THREAD] POSSIBLE ERROR fwd_information from %d to %d was not removed\n", original_sender, from->u8[0]);
          break;
        }
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

      // Initialize the new_route.
      linkaddr_copy(&new_route->addr_fwd, from);
      new_route->id = original_sender;

      // Add the route into the list
      printf("[ROUTING] New route\n");
      list_add(routes_list, new_route);
    }

    printf("[DATA THREAD] Data (%d) from node %d received (%s)\n", air_quality, original_sender, message);

  }
  else
  {
    // DEBUG PURPOSE
    printf("[DATA THREAD] Weird message received from %d.%d\n", from->u8[0], from->u8[1]);
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

/*---------------------------------------------------------------------------*/

static const struct runicast_callbacks runicast_callbacks = {recv_ruc};
static struct runicast_conn runicast;

/*---------------------------------------------------------------------------*/
PROCESS_THREAD(receive_data, ev, data)
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

/*---------------------------------------------------------------------------*/

PROCESS_THREAD(send_orders, ev, data)
{
  char message[10];
  int order, destination;

  PROCESS_EXITHANDLER(runicast_close(&runicast);)
    
  PROCESS_BEGIN();

  runicast_open(&runicast, 144, &runicast_callbacks);

  while(1) {
    static struct etimer et;
    
    // Send the data to the parent
    if(!runicast_is_transmitting(&runicast)) {

      // TODO : Get this from the python server
      destination = 3;
      order = 1;

      sprintf(message, "COM%d%d", order, destination);
      packetbuf_copyfrom(message, strlen(message));

      linkaddr_t *next_node = NULL;

      struct routes *route;
      int found = 0;
      /* Check if we already know this routes. */
      for(route = list_head(routes_list); route != NULL; route = list_item_next(route)) 
      {
        // We break out of the loop if the address in the list matches with from
        if((int)route->id == (int)destination) 
        {
          found = 1;
          if(linkaddr_cmp(next_node, &route->addr_fwd))
          {
            break;
          }
        }

      }

      // If new_route is NULL, this node was not found in our list
      if(!found)
      {
        printf("[ORDER] No route for node %d\n", destination);
      }
      else 
      {
        runicast_send(&runicast, next_node, MAX_RETRANSMISSIONS);
        printf("[ORDER] Sending order %d to the node %d (%s)\n", order, destination, message);
      }
    }

    /* Delay 1 minute */
    etimer_set(&et, (random_rand()%20)*CLOCK_SECOND + 10);
    PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&et));

  }

  PROCESS_END();
}