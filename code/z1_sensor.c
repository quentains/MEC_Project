#include "contiki.h"
#include "net/rime/rime.h"
#include "random.h"
#include <stdio.h>

int pow(int a, int b)
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
AUTOSTART_PROCESSES(&network_setup);

linkaddr_t *parent_node = NULL;
int parent_signal = -9999;

/*---------------------------------------------------------------------------*/
static void
recv_bdcst(struct broadcast_conn *c, const linkaddr_t *from)
{
  char message[100];
  strcpy(message, (char *)packetbuf_dataptr());


  // If announce from a new node
  if (message[0] == 'N' && message[1] == 'D' && message[2] == 'A')
  {
    // If this node is connected to the server
    printf("Announce received : %s\n", message);
    if (parent_node != NULL)
    {
      // Respond to the child
      sprintf(message, "NDR%d", from->u8[0]);
      packetbuf_copyfrom(message, strlen(message));
      broadcast_send(c);
      printf("Reponse (NDR) sent : %s\n", message);
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
      recipient = recipient + ((message[i+3]-48) * pow(10,size-i-1));
    }

    // If the message is for this node (avoid broadcast loop)
    if(recipient == linkaddr_node_addr.u8[0])
    {
      printf("Parent response received from %d with signal %d\n", from->u8[0], packetbuf_attr(PACKETBUF_ATTR_RSSI));

      if (packetbuf_attr(PACKETBUF_ATTR_RSSI) > parent_signal)
      {
        printf("This parent is better than %d\n", parent_signal);
        parent_signal = packetbuf_attr(PACKETBUF_ATTR_RSSI);
        parent_node = from;
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
  char message[100];
  PROCESS_EXITHANDLER(broadcast_close(&broadcast);)

  PROCESS_BEGIN();

  broadcast_open(&broadcast, 129, &broadcast_call);

  printf("[SENSOR] I'm %d\n", linkaddr_node_addr.u8[0]);

  while(1) {

    // If the node is not connected to the network, try to connect
    if (parent_node == NULL)
    {
      sprintf(message, "NDA");
      packetbuf_copyfrom(message, strlen(message));
      broadcast_send(&broadcast);
      printf("Announce (NDA) sent : %s\n", message);
    }

    /* Delay 2-4 seconds */
    etimer_set(&et, CLOCK_SECOND * 4 + random_rand() % (CLOCK_SECOND * 4));
    PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&et));

    
  }

  PROCESS_END();
}
/*---------------------------------------------------------------------------*/