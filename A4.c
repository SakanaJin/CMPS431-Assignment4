#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <pthread.h>

#define STOCKCOUNT 6
#define MAXTHREADS 3

typedef enum {IN, OUT} Operation;
typedef enum {PENDING, FINISHED, CANCELLED} Status;

typedef struct Task{
    int Id;
    char Stock;
    int amount;
    Status status;
    Operation operation;
    int completiontime;
    struct Task *next;
    struct Task *prev;
    bool bumped;
} Task;

Task *taskhead = NULL;
Task *tasktail = NULL;
Task *completehead = NULL;

int turns = 0;
int cancelledcount = 0;
int bumpcount = 0;

int activethreadcount = 0;
pthread_t thread_ids[MAXTHREADS];
pthread_mutex_t global = PTHREAD_MUTEX_INITIALIZER;
pthread_barrier_t start_barrier;
pthread_mutex_t locks[STOCKCOUNT];

int stocks[] = {11, 20, 5, 1, 8, 12};

int stockIndex(char name){
    if(name <= 'Z' && name >= 'A'){
        name = name + ('a' - 'A');
    }
    return name - 'a';
}

Task *create_task(char stock, int amount, Operation operation){
    static int idgen = 0;
    Task *new = malloc(sizeof(Task));
    new->Id = idgen++;
    new->Stock = stock;
    new->amount = amount;
    new->status = PENDING;
    new->operation = operation;
    new->completiontime = -1;
    new->next = NULL;
    new->prev = NULL;
    new->bumped = false;
    return new;
}

void push_task(Task *task){
    if(taskhead == NULL){
        taskhead = tasktail = task;
        return;
    }
    tasktail->next = task;
    task->prev = tasktail;
    tasktail = task;
    return;
}

void insert_complete(Task *task){
    if(completehead == NULL){
        completehead = task;
        return;
    }
    task->next = completehead;
    completehead->prev = task;
    completehead = task;
    return;
}

void remove_task(Task *task){
    if(task->prev){
        task->prev->next = task->next;
    }
    else{
        taskhead = task->next;
    }
    if(task->next){
        task->next->prev = task->prev;
    }
    else{
        tasktail = task->prev;
    }
    task->next = NULL;
    task->prev = NULL;
    return;
}

void free_list(){
    if(completehead == NULL){
        return;
    }
    Task *curr = completehead;
    while(curr != NULL && curr->next != NULL){
        curr = curr->next;
        free(curr->prev);
    }
    free(curr);
    completehead = NULL;
    return;
}

void print_list(){
    Task *curr = taskhead;
    while(curr != NULL){
        printf("[Task: %d]\n", curr->Id);
        curr = curr->next;
    }
    return;
}

void *worker_thread(void *arg){
    int workerId = *(int*)arg;
    free(arg);
    printf("[Worker: %d] has started\n", workerId);
    Task *task = NULL;
    while(true){
        pthread_barrier_wait(&start_barrier);//only here so worker one doesn't finish all the tasks before the other workers can init
        pthread_mutex_lock(&global);
        task = taskhead;
        if(task == NULL){
            pthread_mutex_unlock(&global);
            break;
        }
        remove_task(task);
        turns++;
        int stockindex = stockIndex(task->Stock);
        pthread_mutex_unlock(&global);
        pthread_mutex_lock(&locks[stockindex]);
        int oldamount = stocks[stockindex];
        if(task->operation == IN){
            stocks[stockindex] = stocks[stockindex] + task->amount;
            task->status = FINISHED;
            task->completiontime = turns;
            pthread_mutex_lock(&global);
            printf("[Worker: %d] Task #%d: Added %d to stock %c {%d->%d}\n", workerId, task->Id, task->amount, task->Stock, oldamount, stocks[stockindex]);
            insert_complete(task);
            pthread_mutex_unlock(&global);
        }
        else if(oldamount >= task->amount){
            stocks[stockindex] = stocks[stockindex] - task->amount;
            task->status = FINISHED;
            task->completiontime = turns;
            pthread_mutex_lock(&global);
            printf("[Worker: %d] Task #%d: Removed %d from stock %c {%d->%d}\n", workerId, task->Id, task->amount, task->Stock, oldamount, stocks[stockindex]);
            insert_complete(task);
            pthread_mutex_unlock(&global);
        }
        else if(task->bumped){
            task->status = CANCELLED;
            pthread_mutex_lock(&global);
            printf("[Worker: %d] Cancelled Task #%d: Remove %d from %c {%d}\n", workerId, task->Id, task->amount, task->Stock, oldamount);
            cancelledcount++;
            insert_complete(task);
            pthread_mutex_unlock(&global);
        }
        else{
            task->bumped = true;
            pthread_mutex_lock(&global);
            printf("[Worker: %d] Task #%d: Unable to remove %d from %c {%d}\n", workerId, task->Id, task->amount, task->Stock, oldamount);
            bumpcount++;
            push_task(task);
            pthread_mutex_unlock(&global);
        }
        pthread_mutex_unlock(&locks[stockindex]);
    }
    return NULL;
}

int main(){
    char task_stock_names[] = {'A', 'D', 'B', 'C', 'D', 'F', 'E', 'A', 'B', 'F', 'C', 'E', 'A', 'D', 'A'};
    int task_amounts[] = {5, 3, 10, 3, 8, 10, 4, 3, 8, 5, 6, 5, 12, 5, 8};
    Operation task_operations[] = {OUT, OUT, OUT, IN, IN, OUT, IN, IN, IN, OUT, IN, IN, OUT, IN, IN};
    int taskcount = sizeof(task_amounts)/sizeof(int);
    
    for(int i = 0; i < taskcount; i++){
        push_task(create_task(task_stock_names[i], task_amounts[i], task_operations[i]));
    }
    
    for(int i = 0; i < STOCKCOUNT; i++){
        pthread_mutex_init(&locks[i], NULL);
    }
    pthread_barrier_init(&start_barrier, NULL, MAXTHREADS);
    
    for(int i = 0; i < MAXTHREADS; i++){
        int *workerId = malloc(sizeof(int));
        *workerId = i;
        pthread_create(&thread_ids[i], NULL, worker_thread, (void*)workerId);
    }
    
    for(int i = 0; i < MAXTHREADS; i++){
        pthread_join(thread_ids[i], NULL);
    }

    turns = turns % MAXTHREADS == 0 ? turns / MAXTHREADS : (turns / MAXTHREADS) + 1;
    int sum = 0;
    Task *task = completehead;
    while(task != NULL){
        if(task->status == CANCELLED){task=task->next; continue;}
        task->completiontime = task->completiontime % MAXTHREADS == 0 ? task->completiontime / MAXTHREADS : (task->completiontime / MAXTHREADS) + 1;
        sum = sum + task->completiontime;
        task = task->next;
    }
    float avgcompletiontime = (float)sum / taskcount;
    pthread_mutex_destroy(&global);
    pthread_barrier_destroy(&start_barrier);
    printf("\n==============\n");
    for(int i = 0; i < STOCKCOUNT; i++){
        pthread_mutex_destroy(&locks[i]);
        printf("|| %c || %-3d ||\n==============\n", (char)(i + 'A'), stocks[i]);
    }
    printf("\nTotal Time: %d\nAverage Completion Time: %f\nTotal \"Bumps\": %d\nTotal Cancelled: %d\n", turns, avgcompletiontime, bumpcount, cancelledcount);
    if(cancelledcount != 0){
        task = completehead;
        printf("\nCancelled Tasks:\n");
        while(task != NULL){
            if(task->status == CANCELLED){
                printf("[Task: %d] Could not remove %d from stock %c\n", task->Id, task->amount, task->Stock);
            }
            task = task->next;
        }
    }
    free_list();
    return 0;
}