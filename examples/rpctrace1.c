#include <stdatomic.h>
#include <stdio.h>
#include <hurd.h>
#include <hurd/ports.h>
#include <hurd/fd.h>
#include "portproxy.h"

struct port_class *traced_class;
struct port_bucket *traced_bucket;

static atomic_int next_id = 1;

struct traced_proxy
{
  struct portproxy portproxy;
  struct traced_proxy *peer;
  int id;
};

static void
traced_clean (void *arg)
{
  struct traced_proxy *proxy = arg;

  if (proxy->peer)
    {
      if (proxy->portproxy.type == MACH_PORT_RIGHT_SEND)
        portproxy_ref (proxy->peer);

      portproxy_wrlock (proxy->peer);
      proxy->peer->peer = NULL;
      portproxy_unlock (proxy->peer);

      portproxy_deref (proxy->peer);
    }

  proxy->peer = NULL;

  portproxy_clean (proxy);
}

static error_t
traced_copyin (mach_port_t right,
               mach_port_right_t type,
               struct traced_proxy **proxy)
{
  error_t err;
  struct traced_proxy *existing, *created;

  err = portproxy_copyin (right, type,
                          traced_class, traced_bucket,
                          sizeof (struct traced_proxy),
                          &existing,
                          &created);
  if (err)
    {
      *proxy = NULL;
      return err;
    }

  if (created)
    {
      if (existing)
        {
          /* Migrate our data over and release existing.  */
          if (existing->peer)
            {
              created->peer = existing->peer;
              created->peer->peer = created;
              existing->peer = NULL;
              if (existing->portproxy.type == PORTPROXY_TYPE_SEND)
                {
                  assert_backtrace (created->portproxy.type == PORTPROXY_TYPE_RECEIVE);
                  portproxy_deref (existing);
                }
            }
          else
            created->peer = NULL;
          created->id = existing->id;
          portproxy_unlock (existing);
          portproxy_deref (existing);
        }
      else
        {
          created->peer = NULL;
          created->id = next_id++;
        }

      *proxy = created;
    }
  else
    *proxy = existing;

  return 0;
}

static error_t
traced_copyout (struct traced_proxy **proxy,
                mach_port_right_t required_type,
                mach_port_t *right,
                mach_msg_type_name_t *conversion)
{
  error_t err;
  struct traced_proxy *existing, *created;

  existing = *proxy;

  err = portproxy_copyout (existing,
                           required_type,
                           traced_class, traced_bucket,
                           sizeof (struct traced_proxy),
                           right, conversion,
                           &created);

  if (err)
    return err;

  if (created)
    {
      if (existing)
        {
          /* Migrate our data over and release existing.  */
          if (existing->peer)
            {
              created->peer = existing->peer;
              created->peer->peer = created;
              existing->peer = NULL;
              if (existing->portproxy.type == PORTPROXY_TYPE_RECEIVE)
                {
                  assert_backtrace (created->portproxy.type == PORTPROXY_TYPE_SEND);
                  portproxy_ref (created);
                }
            }
          created->id = existing->id;
          portproxy_unlock (existing);
          portproxy_deref (existing);
        }
      else
        {
          created->peer = NULL;
          created->id = next_id++;
        }

      *proxy = created;
    }

  return 0;
}

static error_t
traced_copyout_peer (struct traced_proxy *proxy,
                     mach_port_right_t required_type,
                     mach_port_t *right,
                     mach_msg_type_name_t *conversion)
{
  error_t err;
  struct traced_proxy *peer = proxy->peer;

  if (peer)
    {
      portproxy_ref (peer);
      portproxy_rdlock (peer);
    }

  err = traced_copyout (&peer, required_type,
                        right, conversion);

  if (!err && peer && !proxy->peer)
    {
      /* We have the write lock.  */
      peer->peer = proxy;
      peer->id = proxy->id;
      if (proxy->portproxy.type == PORTPROXY_TYPE_RECEIVE
       || proxy->portproxy.type == PORTPROXY_TYPE_RECEIVE_ONCE)
        portproxy_ref (peer);
      else
        portproxy_ref (proxy);
    }

  /* FIXME: don't we need a write lock for this?  */
  if (!err)
    proxy->peer = peer;

  portproxy_unlock (peer);
  portproxy_deref (peer);

  portproxy_unlock (proxy);
  portproxy_deref (proxy);

  return err;
}

static void
foo (mach_msg_header_t *inp)
{
  error_t err = 0;
  void *ptr;
  mach_msg_type_name_t local_bits, remote_bits;
  struct traced_proxy *local_port, *remote_port;

  local_bits = MACH_MSGH_BITS_LOCAL (inp->msgh_bits);
  remote_bits = MACH_MSGH_BITS_REMOTE (inp->msgh_bits);

  if (local_bits == MACH_MSG_TYPE_PROTECTED_PAYLOAD)
    {
      local_port = ports_lookup_payload (traced_bucket,
                                         inp->msgh_protected_payload,
                                         traced_class);
      local_bits = local_port->portproxy.type == PORTPROXY_TYPE_RECEIVE
                     ? MACH_MSG_TYPE_MOVE_SEND
                     : MACH_MSG_TYPE_MOVE_SEND_ONCE;
    }
  else
    local_port = ports_lookup_port (traced_bucket,
                                    inp->msgh_local_port,
                                    traced_class);

  err = portproxy_copyin_request_port (local_port);
  assert_perror_backtrace (err);

  printf ("local_port %p %d\n", local_port, local_port->id);

  err = traced_copyout_peer (local_port,
                             portproxy_conversion_to_type (local_bits),
                             &inp->msgh_local_port, &local_bits);
  assert_perror_backtrace (err);

  if (MACH_PORT_VALID (inp->msgh_remote_port))
    {
      err = traced_copyin (inp->msgh_remote_port,
                           portproxy_conversion_to_type (remote_bits),
                           &remote_port);
      assert_perror_backtrace (err);

      printf ("remote_port %p %d\n", remote_port, remote_port->id);

      err = traced_copyout_peer (remote_port,
                                 portproxy_conversion_to_type (remote_bits),
                                 &inp->msgh_remote_port, &remote_bits);
      assert_perror_backtrace (err);
    }

  inp->msgh_bits = MACH_MSGH_BITS (remote_bits, local_bits);

  ptr = inp + 1;

  while ((unsigned char *) ptr < (unsigned char *) inp + inp->msgh_size)
    {
       mach_msg_type_t *const type = ptr;
       mach_msg_type_name_t name;
       mach_port_right_t port_type;
       mach_port_t *right;
       struct traced_proxy *proxy;

       name = type->msgt_name;
       right = (mach_port_t *) (type + 1);
       ptr = right + 1;

       if (!MACH_MSG_TYPE_PORT_ANY_RIGHT (name))
         continue;

       if (!MACH_PORT_VALID (*right))
         {
           printf ("invalid\n");
           continue;
         }

       port_type = portproxy_conversion_to_type (name);

       err = traced_copyin (*right, port_type, &proxy);
       assert_perror_backtrace (err);
       printf ("port %p %d\n", proxy, proxy->id);

       err = traced_copyout_peer (proxy, port_type,
                                  right, &name);
       assert_perror_backtrace (err);
       type->msgt_name = name;
    }
}


static void *
send_something (void *unused)
{
  error_t err;
  mach_port_t port;
  struct traced_proxy *proxy = NULL;
  mach_msg_type_name_t conversion;
  int fd;
  FILE *f;

  err = HURD_DPORT_USE (1, traced_copyin (port, MACH_PORT_RIGHT_SEND,
                                          &proxy));
  assert_perror_backtrace (err);

  assert_backtrace (!proxy->peer);
  err = traced_copyout_peer (proxy, MACH_PORT_RIGHT_SEND,
                             &port, &conversion);
  assert_perror_backtrace (err);

  assert_backtrace (conversion == MACH_MSG_TYPE_MAKE_SEND);

  err = mach_port_insert_right (mach_task_self (),
                                port, port, conversion);
  assert_perror_backtrace (err);

  fd = openport (port, O_WRITE);
  f = fdopen (fd, "w");
  fprintf (f, "Hello!\n");
}

int
main ()
{
  error_t err;
  pthread_t thread;
  struct
  {
    mach_msg_header_t header;
    unsigned char body[1024];
  } msg;
  mach_msg_type_name_t local_bits, remote_bits;
  mach_port_t tmp;

  traced_bucket = ports_create_bucket ();
  traced_class = ports_create_class (&traced_clean, NULL);

  pthread_create (&thread, NULL, send_something, NULL);

  while (1)
    {
      err = mach_msg (&msg.header, MACH_RCV_MSG, 0, sizeof msg,
                      traced_bucket->portset, 0, MACH_PORT_NULL);
      assert_perror_backtrace (err);

      printf ("\nrecv'd message of size %d, id %d\n",
              msg.header.msgh_size, msg.header.msgh_id);
      foo ((mach_msg_header_t *) &msg);

      local_bits = MACH_MSGH_BITS_LOCAL (msg.header.msgh_bits);
      remote_bits = MACH_MSGH_BITS_REMOTE (msg.header.msgh_bits);

      msg.header.msgh_bits = MACH_MSGH_BITS (local_bits, remote_bits)
        | MACH_MSGH_BITS_COMPLEX;
      tmp = msg.header.msgh_local_port;
      msg.header.msgh_local_port = msg.header.msgh_remote_port;
      msg.header.msgh_remote_port = tmp;

      err = mach_msg (&msg.header, MACH_SEND_MSG, msg.header.msgh_size,
                      0, MACH_PORT_NULL, 0, MACH_PORT_NULL);
      assert_perror_backtrace (err);
    }
}
