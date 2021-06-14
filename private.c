#include <pthread.h>
#include <hurd/ihash.h>

#include "portproxy.h"
#include "private.h"

__attribute__ ((visibility("hidden")))
struct hurd_ihash send_proxies[16] =
{
  HURD_IHASH_INITIALIZER (offsetof (struct portproxy, locp)),  /* [0] */
  HURD_IHASH_INITIALIZER (offsetof (struct portproxy, locp)),  /* [1] */
  HURD_IHASH_INITIALIZER (offsetof (struct portproxy, locp)),  /* [2] */
  HURD_IHASH_INITIALIZER (offsetof (struct portproxy, locp)),  /* [3] */
  HURD_IHASH_INITIALIZER (offsetof (struct portproxy, locp)),  /* [4] */
  HURD_IHASH_INITIALIZER (offsetof (struct portproxy, locp)),  /* [5] */
  HURD_IHASH_INITIALIZER (offsetof (struct portproxy, locp)),  /* [6] */
  HURD_IHASH_INITIALIZER (offsetof (struct portproxy, locp)),  /* [7] */
  HURD_IHASH_INITIALIZER (offsetof (struct portproxy, locp)),  /* [8] */
  HURD_IHASH_INITIALIZER (offsetof (struct portproxy, locp)),  /* [9] */
  HURD_IHASH_INITIALIZER (offsetof (struct portproxy, locp)),  /* [a] */
  HURD_IHASH_INITIALIZER (offsetof (struct portproxy, locp)),  /* [b] */
  HURD_IHASH_INITIALIZER (offsetof (struct portproxy, locp)),  /* [c] */
  HURD_IHASH_INITIALIZER (offsetof (struct portproxy, locp)),  /* [d] */
  HURD_IHASH_INITIALIZER (offsetof (struct portproxy, locp)),  /* [e] */
  HURD_IHASH_INITIALIZER (offsetof (struct portproxy, locp)),  /* [f] */
};

__attribute__ ((visibility("hidden")))
pthread_mutex_t send_proxies_lock[16] =
{
  PTHREAD_MUTEX_INITIALIZER,  /* [0] */
  PTHREAD_MUTEX_INITIALIZER,  /* [1] */
  PTHREAD_MUTEX_INITIALIZER,  /* [2] */
  PTHREAD_MUTEX_INITIALIZER,  /* [3] */
  PTHREAD_MUTEX_INITIALIZER,  /* [4] */
  PTHREAD_MUTEX_INITIALIZER,  /* [5] */
  PTHREAD_MUTEX_INITIALIZER,  /* [6] */
  PTHREAD_MUTEX_INITIALIZER,  /* [7] */
  PTHREAD_MUTEX_INITIALIZER,  /* [8] */
  PTHREAD_MUTEX_INITIALIZER,  /* [9] */
  PTHREAD_MUTEX_INITIALIZER,  /* [a] */
  PTHREAD_MUTEX_INITIALIZER,  /* [b] */
  PTHREAD_MUTEX_INITIALIZER,  /* [c] */
  PTHREAD_MUTEX_INITIALIZER,  /* [d] */
  PTHREAD_MUTEX_INITIALIZER,  /* [e] */
  PTHREAD_MUTEX_INITIALIZER,  /* [f] */
};
