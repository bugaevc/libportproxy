#include "portproxy.h"
#include "private.h"

error_t
portproxy_copyout (void *p_existing,
                   mach_port_right_t required_type,
                   struct port_class *port_class,
                   struct port_bucket *bucket,
                   size_t size,
                   mach_port_t *right,
                   mach_msg_type_name_t *conversion,
                   void *p_created)
{
  error_t err;
  unsigned int key;
  struct portproxy *existing = p_existing;
  struct portproxy *created;

  assert_backtrace (size >= sizeof (struct portproxy));

  /* Set up the default values.  */
  *right = MACH_PORT_NULL;
  *conversion = 0;
  *(struct portproxy **) p_created = NULL;

  switch (required_type)
    {
    default:
      return KERN_INVALID_RIGHT;

    case MACH_PORT_RIGHT_SEND:
      if (existing)
        {
          switch (existing->type)
            {
            case PORTPROXY_TYPE_SEND:
              *right = existing->port;
              *conversion = MACH_MSG_TYPE_COPY_SEND;
              break;

            case PORTPROXY_TYPE_RECEIVE:
              *right = ports_get_right (existing);
              *conversion = MACH_MSG_TYPE_MAKE_SEND;
              break;

            default:
              return KERN_INVALID_RIGHT;
            }
          return 0;
        }

      err = ports_create_port (port_class, bucket,
                               size, &created);
      if (err)
        return err;

      created->type = PORTPROXY_TYPE_RECEIVE;
      pthread_rwlock_init (&created->lock, NULL);
      pthread_rwlock_wrlock (&created->lock);
      created->migrated = NULL;

      *(struct portproxy **) p_created = created;
      *right = ports_get_right (created);
      *conversion = MACH_MSG_TYPE_MAKE_SEND;
      return 0;

    case MACH_PORT_RIGHT_RECEIVE:
      if (existing && existing->type != PORTPROXY_TYPE_RECEIVE)
        return KERN_INVALID_RIGHT;

      /* Create a new send proxy.  */
      created = malloc (size);
      if (!created)
        return errno;

      if (existing)
        {
          /* Re-lock for writing.  */
          pthread_rwlock_unlock (&existing->lock);
          pthread_rwlock_wrlock (&existing->lock);

          /* Has somebody else claimed the receive right in the meantime?
             Note: we could chase the migrations here looking for the new
             receive right (if it's been migrated multiple times); but we
             consider concurrent claims of the same receive right to be
             just invalid, and return an error.  */
          if (existing->migrated)
            {
              free (created);
              return KERN_INVALID_RIGHT;
            }

          *right = ports_claim_right (existing);
        }
      else
        *right = mach_reply_port ();

      /* Give ourselves a send right, for created->port.  */
      err = mach_port_insert_right (mach_task_self (),
                                    *right, *right,
                                    MACH_MSG_TYPE_MAKE_SEND);
      assert_perror_backtrace (err);

      refcount_init (&created->refcount, 1);
      created->port = *right;
      created->clean_routine = port_class->clean_routine;
      created->type = PORTPROXY_TYPE_SEND;
      pthread_rwlock_init (&created->lock, NULL);
      pthread_rwlock_wrlock (&created->lock);
      created->migrated = NULL;

      key = *right % 16;

      pthread_mutex_lock (&send_proxies_lock[key]);
      err = hurd_ihash_add (&send_proxies[key], *right, created);
      pthread_mutex_unlock (&send_proxies_lock[key]);

      if (err)
        {
          /* Undo our changes.  */
          if (existing)
            {
              ports_reallocate_from_external (existing, *right);
              /* Leave existing read-locked.  */
              pthread_rwlock_unlock (&existing->lock);
              pthread_rwlock_rdlock (&existing->lock);
            }
          else
            mach_port_mod_refs (mach_task_self (), *right,
                                MACH_PORT_RIGHT_RECEIVE, -1);

          mach_port_deallocate (mach_task_self (), *right);
          *right = MACH_PORT_NULL;
          pthread_rwlock_unlock (&created->lock);
          pthread_rwlock_destroy (&created->lock);
          free (created);
          return err;
        }

      /* Nothing can go wrong anymore; commit to migration.  */
      if (existing)
        {
          portproxy_ref (created);
          existing->migrated = created;
        }

      *(struct portproxy **) p_created = created;
      /* (*right) initialized above */
      *conversion = MACH_MSG_TYPE_MOVE_RECEIVE;
      return 0;

    case MACH_PORT_RIGHT_SEND_ONCE:
      if (existing)
        {
          if (existing->type != PORTPROXY_TYPE_SEND_ONCE)
            return KERN_INVALID_RIGHT;

          /* Take the right.  */
          *right = existing->port;
          if (*right == MACH_PORT_NULL)
            return KERN_INVALID_RIGHT;  /* taken multiple times? */
          existing->port = MACH_PORT_NULL;

          *conversion = MACH_MSG_TYPE_MOVE_SEND_ONCE;
          return 0;
        }

      err = ports_create_port (port_class, bucket,
                               size, &created);
      if (err)
	return err;

      created->type = PORTPROXY_TYPE_RECEIVE_ONCE;
      pthread_rwlock_init (&created->lock, NULL);
      pthread_rwlock_wrlock (&created->lock);
      created->migrated = NULL;

      /* Extra reference for the send-once right being alive.  */
      ports_port_ref (created);

      *(struct portproxy **) p_created = created;
      *right = created->pi.port_right;
      *conversion = MACH_MSG_TYPE_MAKE_SEND_ONCE;
      return 0;
    }
}
