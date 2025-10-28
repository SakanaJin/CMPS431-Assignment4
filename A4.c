//write a version that has a real thread pool of three threads and the threads loop over and select tasks to do (needs queuelock)
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <pthread.h>

#define STOCKCOUNT 6
#define MAXTHREADS 3

typedef enum {IN, OUT} Operation;
typedef enum {PENDING, FINISHED, CANCELLED} Status;

int turns = 0;
int cancelledcount = 0;
int bumpcount = 0;
bool progress = false;

int activethreadcount = 0;
pthread_t *thread_ids;

typedef struct {
    int Id;
    char Stock;
    int amount;
    Status status;
    Operation operation;
    int completiontime;
} Task;

typedef struct{
    Task *task;
    pthread_mutex_t *lock;
    int *stockamount;
    int *result;
} TaskArg;

int taskcount = 0;
int pendingcount = 0;
Task *tasks = NULL;
int *results = NULL;
bool *hasactivethread = NULL;

int stockIndex(char name){
    if(name <= 'Z' && name >= 'A'){
        name = name + ('a' - 'A');
    }
    return name - 'a';
}

void *Task_Thread(void *arg){
    TaskArg *taskarg = (TaskArg *) arg;
    Task *task = taskarg->task;
    pthread_mutex_lock(taskarg->lock);
    int oldamount = *(taskarg->stockamount);
    if(task->operation == IN){
        *(taskarg->stockamount) = *(taskarg->stockamount) + task->amount;
        *(taskarg->result) = 1;
        printf("[Task: %d] Added %d to stock %c {%d->%d}\n", task->Id, task->amount, task->Stock, oldamount, *(taskarg->stockamount));
    }
    else if(*(taskarg->stockamount) >= task->amount){
        *(taskarg->stockamount) = *(taskarg->stockamount) - task->amount;
        *(taskarg->result) = 1;
        printf("[Task: %d] Removed %d from stock %c {%d->%d}\n", task->Id, task->amount, task->Stock, oldamount, *(taskarg->stockamount));
    }
    else{
        printf("[Task: %d] Cannot remove %d from stock %c {%d}\n", task->Id, task->amount, task->Stock, *(taskarg->stockamount));
    }
    pthread_mutex_unlock(taskarg->lock);
    free(taskarg);
    return NULL;
}

void join_threads(){
    for(int i = 0; i < taskcount; i++){
        if(tasks[i].status != PENDING || !hasactivethread[i]) continue;
        pthread_join(thread_ids[i], NULL);
        hasactivethread[i] = false;
        activethreadcount--;
        turns++;
        if(results[i] == 1){
                tasks[i].status = FINISHED;
                tasks[i].completiontime = turns;
                pendingcount--;
                progress = true;
        }
        else{
            bumpcount++;
        }
    }
}

int main(){
    //I decided for simplicity to just hard code the data into the program. It would not be hard to seperate this into a file.
    int stocks[] = {11, 20, 5, 1, 8, 12}; //initial stock data, also where the tasks are going to edit
    char task_stock_names[] = {'A', 'D', 'B', 'C', 'D', 'F', 'E', 'A', 'B', 'F', 'C', 'E', 'A', 'D', 'A'};
    int task_amounts[] = {5, 3, 10, 3, 8, 10, 4, 3, 8, 5, 6, 5, 12, 5, 8};
    Operation task_operations[] = {OUT, OUT, OUT, IN, IN, OUT, IN, IN, IN, OUT, IN, IN, OUT, IN, IN};
    taskcount = sizeof(task_amounts)/sizeof(int);

    tasks = malloc(sizeof(Task) * taskcount);
    results = calloc(taskcount, sizeof(int));
    thread_ids = malloc(sizeof(pthread_t) * taskcount);
    hasactivethread = calloc(taskcount, sizeof(bool));
    for(int i = 0; i < taskcount; i++){
        hasactivethread[i] = false;
        tasks[i].Id = i;
        tasks[i].Stock = task_stock_names[i];
        tasks[i].amount = task_amounts[i];
        tasks[i].status = PENDING;
        tasks[i].operation = task_operations[i];
        tasks[i].completiontime = -1;
    }

    pthread_mutex_t locks[STOCKCOUNT];
    for(int i = 0; i < STOCKCOUNT; i++){
        pthread_mutex_init(&locks[i], NULL);
    }

    pendingcount = taskcount;
    while(pendingcount != 0){
        progress = false;
        for(int i = 0; i < taskcount; i++){
            if(tasks[i].status != PENDING) continue;
            if(activethreadcount == MAXTHREADS){
                join_threads();
            }
            TaskArg *arg = malloc(sizeof(TaskArg));
            int stockindex = stockIndex(tasks[i].Stock);
            arg->task = &tasks[i];
            arg->lock = &locks[stockindex];
            arg->stockamount = &stocks[stockindex];
            arg->result = &results[i];
            if(pthread_create(&thread_ids[i], NULL, Task_Thread, (void *) arg) != 0){
                perror("Thread Creation");
                free(arg);
                continue;
            }
            hasactivethread[i] = true;
            activethreadcount++;
        }
        join_threads();
        if(!progress){
            for(int i = 0; i < taskcount; i++){
                if(tasks[i].status == PENDING && tasks[i].operation == OUT){
                    tasks[i].status = CANCELLED;
                    cancelledcount++;
                    printf("[Task: %d] Cancelled taking %d out of stock %c {%d}\n", tasks[i].Id, tasks[i].amount, tasks[i].Stock, stocks[stockIndex(tasks[i].Stock)]);
                }
            }
            break;
        }
    }
    turns = turns % MAXTHREADS == 0 ? turns / MAXTHREADS : (turns / MAXTHREADS) + 1;
    int sum = 0;
    for(int i = 0; i < taskcount; i++){
        if(tasks[i].status == CANCELLED) continue;
        tasks[i].completiontime = tasks[i].completiontime % MAXTHREADS == 0 ? tasks[i].completiontime / MAXTHREADS : (tasks[i].completiontime / MAXTHREADS) + 1;
        sum = sum + tasks[i].completiontime;
    }
    float avgcompletiontime = (float)sum / taskcount;
    printf("\n==============\n");
    for(int i = 0; i < STOCKCOUNT; i++){
        pthread_mutex_destroy(&locks[i]);
        printf("|| %c || %-3d ||\n==============\n", (char)(i + 'A'), stocks[i]);
    }
    printf("\nTotal Time: %d\nAverage Completion Time: %f\nTotal \"Bumps\": %d\nTotal Cancelled: %d\n", turns, avgcompletiontime, bumpcount, cancelledcount);
    if(cancelledcount != 0){
        printf("\nCancelled Tasks:\n");
        for(int i = 0; i < taskcount; i++){
            if(tasks[i].status == CANCELLED){
                printf("[Task: %d] Could not remove %d from stock %c\n", tasks[i].Id, tasks[i].amount, tasks[i].Stock);
            }
        }
    }
    free(tasks);
    free(thread_ids);
    free(results);
    free(hasactivethread);
    return 0;
}