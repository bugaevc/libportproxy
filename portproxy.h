#include <error.h>
#include <hurd/ports.h>
#include <refcount.h>

enum portproxy_type
{
  PORTPROXY_TYPE_SEND,
  PORTPROXY_TYPE_SEND_ONCE,
  PORTPROXY_TYPE_RECEIVE,
  PORTPROXY_TYPE_RECEIVE_ONCE,
};

struct portproxy
{
  union
  {
    struct port_info pi;
    struct
    {
      refcount_t refcount;
      mach_port_t port;
      hurd_ihash_locp_t locp;
      void (*clean_routine) (void *);
    };
  };
  enum portproxy_type type;
  pthread_rwlock_t lock;
  struct portproxy *migrated;
};

error_t
portproxy_copyin_request_port (void *proxy);

error_t
portproxy_copyin (mach_port_t right,
                  mach_port_right_t type,
                  struct port_class *port_class,
                  struct port_bucket *bucket,
                  size_t size,
                  void *existing,
                  void *created);

error_t
portproxy_copyout (void *existing,
                   mach_port_right_t required_type,
                   struct port_class *port_class,
                   struct port_bucket *bucket,
                   size_t size,
                   mach_port_t *right,
                   mach_msg_type_name_t *conversion,
                   void *created);

void
portproxy_clean (void *proxy);

static inline void
portproxy_ref (void *proxy)
{
  struct portproxy *p = proxy;

  switch (p->type)
    {
    case PORTPROXY_TYPE_RECEIVE:
    case PORTPROXY_TYPE_RECEIVE_ONCE:
      ports_port_ref (&p->pi);
      break;
    default:
      refcount_ref (&p->refcount);
      break;
    }
}

static inline void
portproxy_deref (void *proxy)
{
  struct portproxy *p = proxy;

  switch (p->type)
    {
    case PORTPROXY_TYPE_RECEIVE:
    case PORTPROXY_TYPE_RECEIVE_ONCE:
      ports_port_deref (&p->pi);
      break;
    default:
      if (refcount_deref (&p->refcount) == 0)
        {
          if (p->clean_routine)
            (*p->clean_routine) (proxy);
          else
            portproxy_clean (proxy);
        }
      break;
    }
}

static inline void
portproxy_rdlock (void *proxy)
{
  struct portproxy *p = proxy;

  pthread_rwlock_rdlock (&p->lock);
}

static inline void
portproxy_wrlock (void *proxy)
{
  struct portproxy *p = proxy;

  pthread_rwlock_wrlock (&p->lock);
}

static inline void
portproxy_unlock (void *proxy)
{
  struct portproxy *p = proxy;

  pthread_rwlock_unlock (&p->lock);
}

static inline mach_port_right_t
portproxy_conversion_to_type (mach_msg_type_name_t conversion)
{
  switch (conversion)
    {
    case MACH_MSG_TYPE_MOVE_RECEIVE:
      return MACH_PORT_RIGHT_RECEIVE;

    case MACH_MSG_TYPE_MOVE_SEND:
    case MACH_MSG_TYPE_COPY_SEND:
    case MACH_MSG_TYPE_MAKE_SEND:
      return MACH_PORT_RIGHT_SEND;

    case MACH_MSG_TYPE_MOVE_SEND_ONCE:
    case MACH_MSG_TYPE_MAKE_SEND_ONCE:
      return MACH_PORT_RIGHT_SEND_ONCE;

    default:
      return -1;
    }
}

static inline void *
portproxy_chase (void *proxy)
{
  struct portproxy *p = proxy;
  struct portproxy *next;

  while (p->migrated)
    {
      next = p->migrated;
      portproxy_ref (next);
      portproxy_unlock (p);
      portproxy_deref (p);
      portproxy_rdlock (next);
      p = next;
    }

  return p;
}
