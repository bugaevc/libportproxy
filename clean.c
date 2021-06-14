#include "portproxy.h"
#include "private.h"

void
portproxy_clean (void *proxy)
{
  error_t err;
  struct portproxy *p = proxy;
  struct portproxy *migrated;
  unsigned int key;

  pthread_rwlock_destroy (&p->lock);
  migrated = p->migrated;

  switch (p->type)
    {
    case PORTPROXY_TYPE_SEND:
      key = p->port % 16;

      pthread_mutex_lock (&send_proxies_lock[key]);
      hurd_ihash_locp_remove (&send_proxies[key], p->locp);
      pthread_mutex_unlock (&send_proxies_lock[key]);

      /* fallthrough */

    case PORTPROXY_TYPE_SEND_ONCE:
      /* Deallocate the port, if it has not been taken out.  */
      if (p->port != MACH_PORT_NULL)
        {
          err = mach_port_deallocate (mach_task_self (), p->port);
          assert_perror_backtrace (err);
        }

      free (proxy);
      break;

    default:
      break;
    }

  /* Tail recurse.  */
  if (migrated)
    portproxy_deref (migrated);
}
