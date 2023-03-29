#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>
#include <semaphore.h>
#include <sys/time.h>
#include <assert.h>

double GetTime() {
  struct timeval t;
  int rc = gettimeofday(&t, NULL);
  assert(rc == 0);
  return (double)t.tv_sec + (double)t.tv_usec/1e6;
}

void Spin(int howlong) {
  double t = GetTime();
  while ((GetTime() - t) < (double)howlong) {
    ;}
}

// *******************************************

#define Q_SIZE 5

typedef struct {
  int cid;
  int arrival_time;
  int service_time;
  int start_time;
} CustomerData;

typedef struct {
  CustomerData* customers[Q_SIZE];
  int head;
  int tail;
} CustomerQueue;

void init_queue(CustomerQueue* q) {
  q->head = 0;
  q->tail = 0;
}

int is_queue_empty(CustomerQueue* q) {
  return q->head == q->tail;
}

int is_queue_full(CustomerQueue* q) {
  return (q->tail + 1) % Q_SIZE == q->head;
}

void enqueue(CustomerQueue* q, CustomerData* customerData) {
  if (is_queue_full(q)) {
    printf("Queue is full\n");
    return;
  }
  q->customers[q->tail] = customerData;
  q->tail = (q->tail + 1) % Q_SIZE;
}

CustomerData* dequeue(CustomerQueue* q) {
  if (is_queue_empty(q)) {
    printf("Queue is empty\n");
    return NULL;
  }
  CustomerData* customer = q->customers[q->head];
  q->head = (q->head + 1) % Q_SIZE;
  return customer;
}

sem_t availableAssts;
sem_t customersWaiting;
sem_t mutex_lock;
CustomerQueue customerQueue;
CustomerQueue* qPtr = &customerQueue;
int serviceAvailableTime = 0;


void service() {
  while (1) {
    sem_wait(&customersWaiting);
    sem_wait(&mutex_lock);
    CustomerData* customer = dequeue(qPtr);
    customer->start_time = serviceAvailableTime;
    sem_post(&mutex_lock);
    printf("Time %*d: Customer %*d         starts\n", 2, customer->start_time, 2, customer->cid);
    Spin(customer->service_time);
    int finish_time = customer->start_time + customer->service_time;
    printf("Time %*d: Customer %*d                done\n", 2, finish_time, 2, customer->cid);
    serviceAvailableTime = finish_time;
    sem_post(&availableAssts);
  }
}

void enqueueCustomer(CustomerData* cust_data) { 
  sem_wait(&mutex_lock);
  if (is_queue_full(qPtr)) {
    printf("Time %*d: Customer %*d                     leaves\n", 2, cust_data->arrival_time, 2, cust_data->cid);
    sem_post(&mutex_lock);
    pthread_exit(NULL);
  }
  if (is_queue_empty(qPtr)) {
    serviceAvailableTime = cust_data->arrival_time;
  }
  enqueue(qPtr, cust_data);
  sem_post(&mutex_lock);

  sem_post(&customersWaiting);
  sem_wait(&availableAssts);
}

void createCustomer(void* custData) {
  CustomerData* cust_data = (CustomerData*) custData;
  usleep(cust_data->arrival_time * 1000000); // simulate arrvl time by sleeping x seconds
  printf("Time %*d: Customer %*d arrives\n", 2, cust_data->arrival_time, 2, cust_data->cid);
  enqueueCustomer(cust_data);
}

int main(void) {
  sem_init(&availableAssts, 0, 2);
  sem_init(&customersWaiting, 0, 0);
  sem_init(&mutex_lock, 0, 1);
  init_queue(qPtr);

  pthread_t* asst1 = malloc(sizeof(*asst1));
  pthread_t* asst2 = malloc(sizeof(*asst2));

  pthread_t* cust1 = malloc(sizeof(*cust1));
  CustomerData cust1Data;
  cust1Data.cid = 1;
  cust1Data.arrival_time = 3;
  cust1Data.service_time = 15;
  
  pthread_t* cust2 = malloc(sizeof(*cust2));
  CustomerData cust2Data;
  cust2Data.cid = 2;
  cust2Data.arrival_time = 7;
  cust2Data.service_time = 10;
  
  pthread_t* cust3 = malloc(sizeof(*cust3));
  CustomerData cust3Data;
  cust3Data.cid = 3;
  cust3Data.arrival_time = 8;
  cust3Data.service_time = 8;
  
  pthread_t* cust4 = malloc(sizeof(*cust4));
  CustomerData cust4Data;
  cust4Data.cid = 4;
  cust4Data.arrival_time = 9;
  cust4Data.service_time = 5;

  pthread_t* cust5 = malloc(sizeof(*cust5));
  CustomerData cust5Data;
  cust5Data.cid = 5;
  cust5Data.arrival_time = 11;
  cust5Data.service_time = 12;

  pthread_t* cust6 = malloc(sizeof(*cust6));
  CustomerData cust6Data;
  cust6Data.cid = 6;
  cust6Data.arrival_time = 12;
  cust6Data.service_time = 4;

  pthread_t* cust7 = malloc(sizeof(*cust7));
  CustomerData cust7Data;
  cust7Data.cid = 7;
  cust7Data.arrival_time = 14;
  cust7Data.service_time = 8;

  pthread_t* cust8 = malloc(sizeof(*cust8));
  CustomerData cust8Data;
  cust8Data.cid = 8;
  cust8Data.arrival_time = 16;
  cust8Data.service_time = 14;

  pthread_t* cust9 = malloc(sizeof(*cust9));
  CustomerData cust9Data;
  cust9Data.cid = 9;
  cust9Data.arrival_time = 19;
  cust9Data.service_time = 7;

  pthread_t* cust10 = malloc(sizeof(*cust10));
  CustomerData cust10Data;
  cust10Data.cid = 10;
  cust10Data.arrival_time = 22;
  cust10Data.service_time = 2;

  pthread_t* cust11 = malloc(sizeof(*cust11));
  CustomerData cust11Data;
  cust11Data.cid = 11;
  cust11Data.arrival_time = 34;
  cust11Data.service_time = 9;

  pthread_t* cust12 = malloc(sizeof(*cust12));
  CustomerData cust12Data;
  cust12Data.cid = 12;
  cust12Data.arrival_time = 39;
  cust12Data.service_time = 3;

  pthread_create(asst1, NULL, (void*)service, NULL);
  pthread_create(asst2, NULL, (void*)service, NULL);

  pthread_create(cust1, NULL, (void*)createCustomer, (void*)&cust1Data);
  pthread_create(cust2, NULL, (void*)createCustomer, (void*)&cust2Data);
  pthread_create(cust3, NULL, (void*)createCustomer, (void*)&cust3Data);
  pthread_create(cust4, NULL, (void*)createCustomer, (void*)&cust4Data);
  pthread_create(cust5, NULL, (void*)createCustomer, (void*)&cust5Data);
  pthread_create(cust6, NULL, (void*)createCustomer, (void*)&cust6Data);
  pthread_create(cust7, NULL, (void*)createCustomer, (void*)&cust7Data);
  pthread_create(cust8, NULL, (void*)createCustomer, (void*)&cust8Data);
  pthread_create(cust9, NULL, (void*)createCustomer, (void*)&cust9Data);
  pthread_create(cust10, NULL, (void*)createCustomer, (void*)&cust10Data);
  pthread_create(cust11, NULL, (void*)createCustomer, (void*)&cust11Data);
  pthread_create(cust12, NULL, (void*)createCustomer, (void*)&cust12Data);
  
  pthread_join(*asst1, NULL);
  pthread_join(*asst2, NULL);

  return 0;
}