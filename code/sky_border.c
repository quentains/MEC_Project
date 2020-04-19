#include "contiki.h"
#include "net/rime/rime.h"
#include "random.h"

#include <stdio.h>

/*---------------------------------------------------------------------------*/
PROCESS(child_discovery, "Child Discovery");
AUTOSTART_PROCESSES(&child_discovery);

/*---------------------------------------------------------------------------*/
static void
recv_child_announce(struct broadcast_conn *c, const linkaddr_t *from)
{
  char message[100];
  strcpy(message, (char *)packetbuf_dataptr());

  // Always respond to child announce
  if (message[0] == 'N' && message[1] == 'D' && message[2] == 'A')
  {
    printf("Child announce received : %s\n", message);

    // Respond to the child
    sprintf(message, "NDR%d", from->u8[0]);
    packetbuf_copyfrom(message, strlen(message));
    broadcast_send(c);
    printf("Reponse (NDR) sent : %s\n", message);
  }
  
}


static const struct broadcast_callbacks broadcast_call = {recv_child_announce};
static struct broadcast_conn broadcast;

/*---------------------------------------------------------------------------*/
PROCESS_THREAD(child_discovery, ev, data)
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