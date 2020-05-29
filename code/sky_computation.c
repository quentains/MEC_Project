#include "contiki.h"
#include "net/rime/rime.h"
#include "random.h"
#include "lib/list.h"
#include "lib/memb.h"
#include "lib/random.h"

#include <stdio.h>
#include <stdlib.h>

#define MAX_RETRANSMISSIONS 4
#define MAX_ROUTES 30 // Adapt it for your network
#define INACTIVE_MESSAGE 20
#define NUMBER_OF_SAVED_VALUES 5
#define MAX_CHILDREN 5 // Adapt it for your network
#define ID_SIZE 3

// Used to correctly print the ID in the messages
#define STR_(X) #X
#define STR(X) STR_(X)

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

  // If = 0 this is a child
  // If = 1 this is not a child
  // If = 2 then collecting data before making child
  int is_child;

};

// Holding information about the children (the ones with data)
struct children {
  // The ->next pointer is needed for Contiki list
  struct children *next;

  // The child's id
  int id;

  // Values from sensors
  int last_values[NUMBER_OF_SAVED_VALUES];

  //number of values stored
  int nvalues;

};

// memory allocation for routes
MEMB(routes_memb, struct routes, MAX_ROUTES);
LIST(routes_list);

// memory allocation for children
MEMB(children_memb, struct children, MAX_CHILDREN);
LIST(children_list);

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

// Computing the slope of child values
float get_slope(int* last_values)
{
  printf("Computing slope...\n");
  size_t i;
  float sumX=0, sumY=0, sumX2=0, sumXY=0, a, b;
  for (i = 0 ; i < NUMBER_OF_SAVED_VALUES ; i++)
  {
    printf("last_values[%d] = %d\n", i, last_values[i]);
    sumX = sumX + (i+1);
    sumX2 = sumX2 + (i+1)*(i+1);
    sumY = sumY + last_values[i];
    sumXY = sumXY + (i+1)*last_values[i];
  }
  b = (NUMBER_OF_SAVED_VALUES*sumXY-sumX*sumY)/(NUMBER_OF_SAVED_VALUES*sumX2-sumX*sumX);
  a = (sumY - b*sumX)/NUMBER_OF_SAVED_VALUES;
  return -b/a;
}

//Utils to send order to a node
void send_order(int order, int id, struct runicast_conn *c)
{
  printf("[ORDER] Send order %d to node %d\n", order, id);

  char message[10];
  sprintf(message, "COM%d%0"STR(ID_SIZE)"d", order, id);
  packetbuf_copyfrom(message, strlen(message));

  struct routes *route;
  /* Check if we already know this routes. */
  for(route = list_head(routes_list); route != NULL; route = list_item_next(route)) 
  {
    // We break out of the loop if the address in the list matches with from
    if((int)route->id == (int)id) 
    {
      break;
    }

  }

  // If new_route is NULL, this node was not found in our list
  if(NULL == route)
  {
    printf("[ORDER] No route for node %d\n", id);
  }
  else 
  {
    runicast_send(c, &route->addr_fwd, MAX_RETRANSMISSIONS);
    printf("[ORDER] Sending order %d to the node %d (%s)\n", order, route->addr_fwd.u8[0], message);
  }

}


/*---------------------------------------------------------------------------*/
PROCESS(network_setup, "Network Setup");
PROCESS(forwarding_messages, "Forwarding SRV & COM");
AUTOSTART_PROCESSES(&network_setup, &forwarding_messages);

static linkaddr_t *parent_node;
static int not_connected = 1;
static int parent_signal = -9999;
static int number_of_children = 0;

// Used to get a children using RIME id. 
// When a new child is created, it might not be in the list already.
// This function creates any new child that does not yet exist.
struct children* get_children(int id)
{
  struct children *child;
  for(child = list_head(children_list); child != NULL; child = list_item_next(child))
  {
    if ( child->id == id )
    {
      break;
    }
  }
  // If the child does not exist, it needs to be created
  if ( child == NULL ) // From 2 to 0
  {
    printf("requested non-existing child %d\n", id);
  }
  return child;
}

// Used to remove a child
// It is called when a child stops communication by remove_old_routes()
// This function selects a new child to replace the removed one from the list of routes
void remove_child(int id)
{

  // Looking for the child to delete
  struct children *child;
  for(child = list_head(children_list); child != NULL; child = list_item_next(child))
  {
    if ( child->id == id )
    {
      printf("removed child \n");
      // We don't really remove it now, maybe we will re-use it juste after
      break;
    }
  }


  struct routes *route;

  // If a sensor (*) communicate with the server, wait to have 30 data messages
  for(route = list_head(routes_list); route != NULL; route = list_item_next(route)) 
  {
    if(route->is_child == 1) // Can't just be !route->is_child
    {
      // As long as is_child = 2 (computation node does not
      // have NUMBER_OF_SAVED_VALUES data points), 
      // messages will be forwarded to the server

      // We re-use the previous child (the child to delete)
      route->is_child = 2; 
      child->id = route->id;
      child->nvalues = 0;
      break; // only select one
    }
  }
  // If there are no one (*) , remove a child from the counter
  // and we really delete the child
  if(route == NULL)
  {
    number_of_children--;
    list_remove(children_list, child);
  }
}

// This function is used to remove a node that has stopped communicating for a while.
// It uses the INACTIVE_MESSAGE constant
void remove_old_routes()
{
  struct routes *route;

  for(route = list_head(routes_list); route != NULL; route = list_item_next(route)) 
  {
    // If no more message since a long time, the route can be deleted
    if(INACTIVE_MESSAGE <= (int) route->age ) 
    {
      printf("removed route \n");
      if (route->is_child != 1) // Can't just be route->is_child
      {
        remove_child(route->id);
      }
      list_remove(routes_list, route);
    }
    // Else, increase its age
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
      sprintf(message, "NDR%0"STR(ID_SIZE)"d", from->u8[0]);
      packetbuf_copyfrom(message, strlen(message));
      broadcast_send(c);
      printf("[SETUP THREAD] Reponse (NDR) sent : %s\n", message);
    }
    
  }

  // If response to an announce
  else if (message[0] == 'N' && message[1] == 'D' && message[2] == 'R')
  {
    size_t i;
    int recipient = 0;

    // Compute the recipient from the radio message
    for(i = 0 ; i < ID_SIZE ; i++)
    {
      recipient = recipient + ((message[i+3]-48) * power(10,ID_SIZE-i-1));
    }

    // If the message is for this node (avoid broadcast loop)
    if(recipient == linkaddr_node_addr.u8[0])
    {
      //printf("[SETUP THREAD] Parent response received from %d with signal %d\n", from->u8[0], packetbuf_attr(PACKETBUF_ATTR_RSSI));

      if (packetbuf_attr(PACKETBUF_ATTR_RSSI) > parent_signal)
      {
        printf("[SETUP THREAD] This parent is better than %d\n", parent_signal);
        parent_signal = packetbuf_attr(PACKETBUF_ATTR_RSSI);
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

  printf("[COMPUTATION] I'm %d\n", linkaddr_node_addr.u8[0]);

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
  int original_sender = 0, data;
  size_t i;
  float slope;
  
  // If SRV message, need to forward it to the parent_node
  if (message[0] == 'S' && message[1] == 'R' && message[2] == 'V')
  {
    // Get the air_quality
    data = (message[3]-48)*10 + (message[4]-48);

    // Get the address of the original sender
    for(i = 0 ; i < ID_SIZE ; i++)
    {
      original_sender = original_sender + ((message[i+5]-48) * power(10,ID_SIZE-i-1));
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

      // IF there is space left for a child
      if (number_of_children < MAX_CHILDREN)
      {
        //Creating the child
        new_route->is_child = 0;
        struct children *new_child;
        new_child = memb_alloc(&children_memb);
        if (new_child == NULL) // If there is an error / no memory left
        {
          new_route->is_child = 1;
          number_of_children = MAX_CHILDREN;
        }
        else
        {
          // Giving values to child attributes
          new_child->id = original_sender;
          new_child->nvalues = 0;
          list_add(children_list, new_child);
        }
        // Always increment, if there was a memory problem
        // nevermind, this will set the number_of_children
        // to the MAX_CHILDREN
        number_of_children += 1;
      }
      else 
      { // If there are too many children
        new_route->is_child = 1;
      }
      printf("[ROUTING] New node\n");
    }
    new_route->age = 0; // used for deleting routes after they stop communicating
    if (!linkaddr_cmp(&new_route->addr_fwd, from)) // If routing has changed
    {
      // Initialize the new_route.
      linkaddr_copy(&new_route->addr_fwd, from);
      new_route->id = original_sender;

      // Add the route into the list
      printf("[ROUTING] New route\n");
      list_add(routes_list, new_route);
    }
    
    if ( new_route->is_child != 1 )
    {
      //STORE THE SENSOR INFO
      struct children *this_child;
      this_child = get_children(original_sender);

      // When the array is not full, append data
      if (this_child->nvalues < NUMBER_OF_SAVED_VALUES){
        this_child->last_values[this_child->nvalues] = data;
        this_child->nvalues += 1;
      }
      // When the array is full, shift all values left and then append
      else
      {
        new_route->is_child = 0;
        int value_index;
        for (value_index = 1; value_index < NUMBER_OF_SAVED_VALUES; value_index ++ )
        {
          this_child->last_values[value_index-1] = this_child->last_values[value_index];
        }
        this_child->last_values[this_child->nvalues-1] = data;
      }
    } // When the message comes from either a node that is not a child (1) or that is becomming a child (2)
    if ( new_route->is_child != 0 )
    {
      // Forward the message to the parent
      packetbuf_copyfrom(message, strlen(message));
      runicast_send(c, parent_node, MAX_RETRANSMISSIONS);

      printf("[FORWARDING THREAD] [TO SERVER] Forwarding from %d to %d (%s)\n", from->u8[0], parent_node->u8[0], message);
    } // When the node is a child
    if ( new_route->is_child == 0 )
    {  
      struct children *this_child;
      this_child = get_children(original_sender);
      // Check if there are enough values
      if (this_child->nvalues == NUMBER_OF_SAVED_VALUES)
      {
        printf("Enough data for child %d, computing the slope...\n", this_child->id);
        slope = get_slope(this_child->last_values);
        if( slope > 1.0 )
        {
          printf("The slope is > 1, opening the valve of node %d\n", this_child->id);
          // Send the order to open the valve
          send_order(1, this_child->id, c);
        }
      }
    }

  }

  // If order message, forward it
  else if (message[0] == 'C' && message[1] == 'O' && message[2] == 'M')
  {
    int recipient = 0;    
    int order = message[3] - '0';
    struct routes *route;

    // Get the address of the message recipient 
    for(i = 0 ; i < ID_SIZE ; i++)
    {
      recipient = recipient + ((message[i+4]-48) * power(10,ID_SIZE-i-1));
    }

    // gets the right route
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
    printf("[ROUTING] To contact %d (child:%d), I have to send to %d\n", route->id, route->is_child, route->addr_fwd.u8[0]);
    //list_remove(routes_list, route);
  }
  /* ================ */

  // Check if there are some old routes to delete
  remove_old_routes();

}

/*---------------------------------------------------------------------------*/

static const struct runicast_callbacks runicast_callbacks = {recv_ruc};
static struct runicast_conn runicast;

/*---------------------------------------------------------------------------*/
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
/*---------------------------------------------------------------------------*/
