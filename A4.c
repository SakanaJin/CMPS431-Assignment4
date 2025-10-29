#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <pthread.h>
#include <unistd.h>

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
    printf("[Worker: %d] has started\n", workerId);
    Task *task = NULL;
    while(true){
        pthread_barrier_wait(&start_barrier);
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
        // usleep(10);
    }
    free(arg);
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

// #include <stdio.h>
// #include <stdlib.h>
// #include <stdbool.h>
// #include <pthread.h>

// #define STOCKCOUNT 6
// #define MAXTHREADS 3

// typedef enum {IN, OUT} Operation;
// typedef enum {PENDING, FINISHED, CANCELLED} Status;

// int turns = 0;
// int cancelledcount = 0;
// int bumpcount = 0;
// bool progress = false;

// int activethreadcount = 0;
// pthread_t *thread_ids;

// typedef struct {
//     int Id;
//     char Stock;
//     int amount;
//     Status status;
//     Operation operation;
//     int completiontime;
// } Task;

// typedef struct{
//     Task *task;
//     pthread_mutex_t *lock;
//     int *stockamount;
//     int *result;
// } TaskArg;

// int taskcount = 0;
// int pendingcount = 0;
// Task *tasks = NULL;
// int *results = NULL;
// bool *hasactivethread = NULL;

// int stockIndex(char name){
//     if(name <= 'Z' && name >= 'A'){
//         name = name + ('a' - 'A');
//     }
//     return name - 'a';
// }

// void *Task_Thread(void *arg){
//     TaskArg *taskarg = (TaskArg *) arg;
//     Task *task = taskarg->task;
//     pthread_mutex_lock(taskarg->lock);
//     int oldamount = *(taskarg->stockamount);
//     if(task->operation == IN){
//         *(taskarg->stockamount) = *(taskarg->stockamount) + task->amount;
//         *(taskarg->result) = 1;
//         printf("[Task: %d] Added %d to stock %c {%d->%d}\n", task->Id, task->amount, task->Stock, oldamount, *(taskarg->stockamount));
//     }
//     else if(*(taskarg->stockamount) >= task->amount){
//         *(taskarg->stockamount) = *(taskarg->stockamount) - task->amount;
//         *(taskarg->result) = 1;
//         printf("[Task: %d] Removed %d from stock %c {%d->%d}\n", task->Id, task->amount, task->Stock, oldamount, *(taskarg->stockamount));
//     }
//     else{
//         printf("[Task: %d] Cannot remove %d from stock %c {%d}\n", task->Id, task->amount, task->Stock, *(taskarg->stockamount));
//     }
//     pthread_mutex_unlock(taskarg->lock);
//     free(taskarg);
//     return NULL;
// }

// void join_threads(){
//     for(int i = 0; i < taskcount; i++){
//         if(tasks[i].status != PENDING || !hasactivethread[i]) continue;
//         pthread_join(thread_ids[i], NULL);
//         hasactivethread[i] = false;
//         activethreadcount--;
//         turns++;
//         if(results[i] == 1){
//                 tasks[i].status = FINISHED;
//                 tasks[i].completiontime = turns;
//                 pendingcount--;
//                 progress = true;
//         }
//         else{
//             bumpcount++;
//         }
//     }
// }

// int main(){
//     //I decided for simplicity to just hard code the data into the program. It would not be hard to seperate this into a file.
//     int stocks[] = {11, 20, 5, 1, 8, 12}; //initial stock data, also where the tasks are going to edit
//     char task_stock_names[] = {'A', 'D', 'B', 'C', 'D', 'F', 'E', 'A', 'B', 'F', 'C', 'E', 'A', 'D', 'A'};
//     int task_amounts[] = {5, 3, 10, 3, 8, 10, 4, 3, 8, 5, 6, 5, 12, 5, 8};
//     Operation task_operations[] = {OUT, OUT, OUT, IN, IN, OUT, IN, IN, IN, OUT, IN, IN, OUT, IN, IN};
//     taskcount = sizeof(task_amounts)/sizeof(int);

//     tasks = malloc(sizeof(Task) * taskcount);
//     results = calloc(taskcount, sizeof(int));
//     thread_ids = malloc(sizeof(pthread_t) * taskcount);
//     hasactivethread = calloc(taskcount, sizeof(bool));
//     for(int i = 0; i < taskcount; i++){
//         hasactivethread[i] = false;
//         tasks[i].Id = i;
//         tasks[i].Stock = task_stock_names[i];
//         tasks[i].amount = task_amounts[i];
//         tasks[i].status = PENDING;
//         tasks[i].operation = task_operations[i];
//         tasks[i].completiontime = -1;
//     }

//     pthread_mutex_t locks[STOCKCOUNT];
//     for(int i = 0; i < STOCKCOUNT; i++){
//         pthread_mutex_init(&locks[i], NULL);
//     }

//     pendingcount = taskcount;
//     while(pendingcount != 0){
//         progress = false;
//         for(int i = 0; i < taskcount; i++){
//             if(tasks[i].status != PENDING) continue;
//             if(activethreadcount == MAXTHREADS){
//                 join_threads();
//             }
//             TaskArg *arg = malloc(sizeof(TaskArg));
//             int stockindex = stockIndex(tasks[i].Stock);
//             arg->task = &tasks[i];
//             arg->lock = &locks[stockindex];
//             arg->stockamount = &stocks[stockindex];
//             arg->result = &results[i];
//             if(pthread_create(&thread_ids[i], NULL, Task_Thread, (void *) arg) != 0){
//                 perror("Thread Creation");
//                 free(arg);
//                 continue;
//             }
//             hasactivethread[i] = true;
//             activethreadcount++;
//         }
//         join_threads();
//         if(!progress){
//             for(int i = 0; i < taskcount; i++){
//                 if(tasks[i].status == PENDING && tasks[i].operation == OUT){
//                     tasks[i].status = CANCELLED;
//                     cancelledcount++;
//                     printf("[Task: %d] Cancelled taking %d out of stock %c {%d}\n", tasks[i].Id, tasks[i].amount, tasks[i].Stock, stocks[stockIndex(tasks[i].Stock)]);
//                 }
//             }
//             break;
//         }
//     }
//     turns = turns % MAXTHREADS == 0 ? turns / MAXTHREADS : (turns / MAXTHREADS) + 1;
//     int sum = 0;
//     for(int i = 0; i < taskcount; i++){
//         if(tasks[i].status == CANCELLED) continue;
//         tasks[i].completiontime = tasks[i].completiontime % MAXTHREADS == 0 ? tasks[i].completiontime / MAXTHREADS : (tasks[i].completiontime / MAXTHREADS) + 1;
//         sum = sum + tasks[i].completiontime;
//     }
//     float avgcompletiontime = (float)sum / taskcount;
//     printf("\n==============\n");
//     for(int i = 0; i < STOCKCOUNT; i++){
//         pthread_mutex_destroy(&locks[i]);
//         printf("|| %c || %-3d ||\n==============\n", (char)(i + 'A'), stocks[i]);
//     }
//     printf("\nTotal Time: %d\nAverage Completion Time: %f\nTotal \"Bumps\": %d\nTotal Cancelled: %d\n", turns, avgcompletiontime, bumpcount, cancelledcount);
//     if(cancelledcount != 0){
//         printf("\nCancelled Tasks:\n");
//         for(int i = 0; i < taskcount; i++){
//             if(tasks[i].status == CANCELLED){
//                 printf("[Task: %d] Could not remove %d from stock %c\n", tasks[i].Id, tasks[i].amount, tasks[i].Stock);
//             }
//         }
//     }
//     free(tasks);
//     free(thread_ids);
//     free(results);
//     free(hasactivethread);
//     return 0;
// }