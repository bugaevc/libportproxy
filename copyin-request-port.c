#include "portproxy.h"

error_t
portproxy_copyin_request_port (void *proxy)
{
  struct portproxy *p = proxy;

  portproxy_rdlock (proxy);

  // FIXME: should this chase?
  // p = potrproxy_chase (proxy);

  switch (p->type)
    {
    case PORTPROXY_TYPE_RECEIVE:
      return 0;

    case PORTPROXY_TYPE_RECEIVE_ONCE:
      /* Drop the extra reference, since the send-once
         right is no longer alive.  */
      ports_port_deref (proxy);
      return 0;

    default:
      return KERN_INVALID_RIGHT;
    }
}
