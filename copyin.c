#include "portproxy.h"
#include "private.h"

error_t
portproxy_copyin (mach_port_t right,
                  mach_port_right_t type,
                  struct port_class *port_class,
                  struct port_bucket *bucket,
                  size_t size,
                  void *p_existing,
                  void *p_created)
{
  error_t err;
  unsigned int key = right % 16;
  struct portproxy *existing, *created;

  assert_backtrace (MACH_PORT_VALID (right));
  assert_backtrace (size >= sizeof (struct portproxy));

  /* Set up the default values.  */
  *(struct portproxy **) p_existing = NULL;
  *(struct portproxy **) p_created = NULL;

  switch (type)
    {
    default:
      return KERN_INVALID_RIGHT;

    case MACH_PORT_RIGHT_SEND:
      pthread_mutex_lock (&send_proxies_lock[key]);

      /* Is it a send right to one of our receive rights?  */
      existing = ports_lookup_port (bucket, right,
                                    port_class);
      if (existing)
        goto found_existing;

      /* Is it a send right we're already tracking?  */
      existing = hurd_ihash_find (&send_proxies[key], right);
      if (existing)
        {
          portproxy_ref (existing);
          goto found_existing;
        }

      /* Create a new send proxy.  */
      created = malloc (size);
      if (!created)
        {
          pthread_mutex_unlock (&send_proxies_lock[key]);
          return errno;
        }

      refcount_init (&created->refcount, 1);
      created->port = right;
      created->clean_routine = port_class->clean_routine;
      created->type = PORTPROXY_TYPE_SEND;
      pthread_rwlock_init (&created->lock, NULL);
      pthread_rwlock_wrlock (&created->lock);
      created->migrated = NULL;

      err = hurd_ihash_add (&send_proxies[key], right, created);
      pthread_mutex_unlock (&send_proxies_lock[key]);

      if (err)
        {
          pthread_rwlock_unlock (&created->lock);
          pthread_rwlock_destroy (&created->lock);
          free (created);
          return err;
        }

      *(struct portproxy **) p_created = created;
      return 0;

 found_existing:
      pthread_mutex_unlock (&send_proxies_lock[key]);

      /* We found an existing proxy, so we don't need
         another right reference.  */
      err = mach_port_deallocate (mach_task_self (), right);
      assert_perror_backtrace (err);

      pthread_rwlock_rdlock (&existing->lock);
      *(struct portproxy **) p_existing = portproxy_chase (existing);
      return 0;

    case MACH_PORT_RIGHT_RECEIVE:
      /* There's no ports_import_port_noinstall (), but we don't want
         to install the new port until we at least init its lock.  */
      err = ports_create_port_noinstall (port_class, bucket,
                                         size, &created);
      if (err)
        {
          pthread_mutex_unlock (&send_proxies_lock[key]);
          return err;
        }

      created->type = PORTPROXY_TYPE_RECEIVE;
      pthread_rwlock_init (&created->lock, NULL);
      pthread_rwlock_wrlock (&created->lock);
      created->migrated = NULL;

      pthread_mutex_lock (&send_proxies_lock[key]);
      /* Consumes the right and installs the port into its bucket.  */
      ports_reallocate_from_external (created, right);

      existing = hurd_ihash_find (&send_proxies[key], right);
      if (existing)
        {
          assert_backtrace (existing->type == PORTPROXY_TYPE_SEND);
          portproxy_ref (existing);
          hurd_ihash_locp_remove (&send_proxies[key], existing->locp);
        }

      /* Make sure to unlock the big lock
         before trying to lock existing.  */
      pthread_mutex_unlock (&send_proxies_lock[key]);

      if (existing)
        {
          pthread_rwlock_wrlock (&existing->lock);
          /* We have the receive right; nobody else can
             migrate the existing send right.  */
          assert_backtrace (existing->migrated == NULL);
          ports_port_ref (created);
          existing->migrated = created;
        }

      *(struct portproxy **) p_existing = existing;
      *(struct portproxy **) p_created = created;
      return 0;

    case MACH_PORT_RIGHT_SEND_ONCE:
      created = malloc (size);
      if (!created)
        return errno;

      refcount_init (&created->refcount, 1);
      created->port = right;
      created->clean_routine = port_class->clean_routine;
      created->type = PORTPROXY_TYPE_SEND_ONCE;
      pthread_rwlock_init (&created->lock, NULL);
      pthread_rwlock_wrlock (&created->lock);
      created->migrated = NULL;

      *(struct portproxy **) p_created = created;
      return 0;
    }
}
