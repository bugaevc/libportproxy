#include <pthread.h>
#include <hurd/ihash.h>

extern struct hurd_ihash send_proxies[16];
extern pthread_mutex_t send_proxies_lock[16];
