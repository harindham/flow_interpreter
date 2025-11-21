#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <assert.h>
#include <sys/time.h>

#define NUM_BUCKETS 5
#define NUM_KEYS 100000
int num_threads = 1;
int keys[NUM_KEYS];

typedef struct _bucket_entry {
  int key;
  int val;
  struct _bucket_entry *next;
} bucket_entry;

bucket_entry *table[NUM_BUCKETS];
pthread_spinlock_t table_spinlock;

void panic(char *msg) {
  printf("%s\n", msg);
  exit(1);
}

double now() {
  struct timeval tv;
  gettimeofday(&tv, 0);
  return tv.tv_sec + tv.tv_usec / 1000000.0;
}

void insert(int key, int val) {
  int i = key % NUM_BUCKETS;
  bucket_entry *e = (bucket_entry *) malloc(sizeof(bucket_entry));
  if (!e) panic("No memory to allocate bucket!");
  
  pthread_spin_lock(&table_spinlock);  // Spin instead of sleep
  e->next = table[i];
  e->key = key;
  e->val = val;
  table[i] = e;
  pthread_spin_unlock(&table_spinlock);
}

bucket_entry * retrieve(int key) {
  bucket_entry *b;
  pthread_spin_lock(&table_spinlock);
  for (b = table[key % NUM_BUCKETS]; b != NULL; b = b->next) {
    if (b->key == key) {
      pthread_spin_unlock(&table_spinlock);
      return b;
    }
  }
  pthread_spin_unlock(&table_spinlock);
  return NULL;
}

void * put_phase(void *arg) {
  long tid = (long) arg;
  int key = 0;
  for (key = tid ; key < NUM_KEYS; key += num_threads) {
    insert(keys[key], tid);
  }
  pthread_exit(NULL);
}

void * get_phase(void *arg) {
  long tid = (long) arg;
  int key = 0;
  long lost = 0;
  for (key = tid ; key < NUM_KEYS; key += num_threads) {
    if (retrieve(keys[key]) == NULL) lost++;
  }
  printf("[thread %ld] %ld keys lost!\n", tid, lost);
  pthread_exit((void *)lost);
}

int main(int argc, char **argv) {
  long i;
  pthread_t *threads;
  double start, end;

  if (argc != 2) {
    panic("usage: ./parallel_spin <num_threads>");
  }
  if ((num_threads = atoi(argv[1])) <= 0) {
    panic("must enter a valid number of threads to run");
  }

  pthread_spin_init(&table_spinlock, PTHREAD_PROCESS_PRIVATE);  // Initialize spinlock

  srandom(time(NULL));
  for (i = 0; i < NUM_KEYS; i++)
    keys[i] = random();

  threads = (pthread_t *) malloc(sizeof(pthread_t)*num_threads);
  if (!threads) {
    panic("out of memory allocating thread handles");
  }

  start = now();
  for (i = 0; i < num_threads; i++) {
    pthread_create(&threads[i], NULL, put_phase, (void *)i);
  }
  for (i = 0; i < num_threads; i++) {
    pthread_join(threads[i], NULL);
  }
  end = now();
  printf("[main] Inserted %d keys in %f seconds\n", NUM_KEYS, end - start);

  memset(threads, 0, sizeof(pthread_t)*num_threads);

  start = now();
  for (i = 0; i < num_threads; i++) {
    pthread_create(&threads[i], NULL, get_phase, (void *)i);
  }

  long total_lost = 0;
  long *lost_keys = (long *) malloc(sizeof(long) * num_threads);
  for (i = 0; i < num_threads; i++) {
    pthread_join(threads[i], (void **)&lost_keys[i]);
    total_lost += lost_keys[i];
  }
  end = now();

  printf("[main] Retrieved %ld/%d keys in %f seconds\n", NUM_KEYS - total_lost, NUM_KEYS, end - start);

  pthread_spin_destroy(&table_spinlock);
  free(threads);
  free(lost_keys);
  
  return 0;
}
