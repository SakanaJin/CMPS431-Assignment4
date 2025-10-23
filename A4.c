#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <stdbool.h>

#define STOCKS 6
#define DELIMITERS ","
#define BUFFERSIZE 256

int stock_amounts[STOCKS];

pthread_mutex_t stock_locks[STOCKS];
pthread_cond_t stock_conds[STOCKS];
pthread_mutex_t global_lock = PTHREAD_MUTEX_INITIALIZER;
// pthread_cond_t global_cond = PTHREAD_COND_INITIALIZER;

struct task{
    int taskId;
    char stockName;
    int amount;
    bool isOutput;
    struct task *next;
};
int nextTaskId = 0;

struct task *taskqueuehead = NULL;
struct task *taskqueuetail = NULL;
int taskcount = 0;

int active_thread_count = 0;
// bool *finished_threads;
pthread_t *thread_ids;

struct task* create_task(char stockName, int amount, bool isOutput){
    struct task *newTask = malloc(sizeof(struct task));
    if(!newTask){perror("mallocation failed"); exit(1);}
    newTask->taskId = nextTaskId;
    nextTaskId++;
    newTask->stockName = stockName;
    newTask->amount = amount;
    newTask->isOutput = isOutput;
    newTask->next = NULL;
    return newTask;
}

void insert_task(char stockName, int amount, bool isOutput){
    struct task *newTask = create_task(stockName, amount, isOutput);
    if(taskqueuehead == NULL){
        taskqueuehead = newTask;
        taskqueuetail = newTask;
        return;
    }
    taskqueuetail->next = newTask;
    taskqueuetail = newTask;
    return;
}

// void remove_task(struct task* toRemove){
//     if(taskqueuehead == NULL){return;}
//     if(taskqueuehead == toRemove && toRemove->next != NULL){
//         taskqueuehead = toRemove->next;
//     }
//     struct task *curr = taskqueuehead;
//     while(curr->next != NULL && curr->next != toRemove){
//         curr = curr->next;
//     }
//     if(toRemove->next == NULL){
//         taskqueuetail = curr;
//     }
//     curr->next = toRemove->next;
//     free(toRemove);
// }

void remove_task(struct task* toRemove){
    if (taskqueuehead == NULL || toRemove == NULL) return;
    if (taskqueuehead == toRemove) {
        taskqueuehead = toRemove->next;
        if (taskqueuehead == NULL) {
            taskqueuetail = NULL;
        }
        free(toRemove);
        return;
    }
    struct task *curr = taskqueuehead;
    while (curr->next != NULL && curr->next != toRemove) {
        curr = curr->next;
    }
    if (curr->next == toRemove) {
        curr->next = toRemove->next;
        if (toRemove->next == NULL) {
            taskqueuetail = curr;
        }
        free(toRemove);
    }
}

// void print_list(){
//     struct task *curr = taskqueuehead;
//     if(curr == NULL){
//         printf("empty list\n");
//     }
//     while(curr != NULL){
//         printf("%d, %c, %d, %d\n", curr->taskId, curr->stockName, curr->amount, curr->isOutput);
//         curr = curr->next;
//     }
//     return;
// }

int char_to_stockIndex(char name){
    if(name <= 'Z' && name >= 'A'){
        name = name + ('a' - 'A');
    }
    return name - 'a';
}

void *add_stock_task(void *arg){
    struct task *currtask = (struct task*)arg;
    int stockIndex = char_to_stockIndex(currtask->stockName);
    pthread_mutex_t *taskLock = &stock_locks[stockIndex];
    pthread_cond_t *taskCond = &stock_conds[stockIndex];
    
    pthread_mutex_lock(taskLock);
    pthread_mutex_lock(&global_lock);
    int stock_amount = stock_amounts[stockIndex];
    stock_amounts[stockIndex] = stock_amount + currtask->amount;
    pthread_cond_signal(taskCond);
    
    printf("[Task: %d] Added %d to stock %c {%d -> %d}\n", currtask->taskId, currtask->amount, currtask->stockName, stock_amount, stock_amounts[stockIndex]);
    // finished_threads[currtask->taskId] = true;
    remove_task(currtask);
    // pthread_cond_signal(&global_cond);
    pthread_mutex_unlock(&global_lock);

    pthread_mutex_unlock(taskLock);

    return NULL;
}

void *remove_stock_task(void *arg){
    struct task *currtask = (struct task*)arg;
    int stockIndex = char_to_stockIndex(currtask->stockName);
    
    pthread_mutex_t *taskLock = &stock_locks[stockIndex];
    pthread_cond_t *taskCond = &stock_conds[stockIndex];
    
    pthread_mutex_lock(taskLock);
    
    pthread_mutex_lock(&global_lock);
    printf("[Task: %d] Attempting to remove %d from stock %c\n", currtask->taskId, currtask->amount, currtask->stockName);
    pthread_mutex_unlock(&global_lock);
    
    int difference = stock_amounts[stockIndex] - currtask->amount;
    while(difference < 0){

        pthread_mutex_lock(&global_lock);
        int i = 0;
        bool stillPossible = false;
        struct task *curr = taskqueuehead;
        while(curr != NULL){
            if(curr->stockName == currtask->stockName && !curr->isOutput){
                stillPossible = true;
                break;
            }
            curr = curr->next;
        }
        if(!stillPossible){
            printf("[Task: %d] Unable to remove %d from stock %c, terminating\n", currtask->taskId, currtask->amount, currtask->stockName);
            // finished_threads[currtask->taskId] = true;
            remove_task(currtask);
            // pthread_cond_signal(&global_cond);
            pthread_mutex_unlock(&global_lock);
            pthread_mutex_unlock(taskLock);
            return NULL;
        }
        printf("[Task: %d] Unable to remove %d from stock %c, waiting...\n", currtask->taskId, currtask->amount, currtask->stockName);
        pthread_mutex_unlock(&global_lock);

        pthread_cond_wait(taskCond, taskLock);
        difference = stock_amounts[stockIndex] - currtask->amount;
    }
    stock_amounts[stockIndex] = difference;

    pthread_mutex_lock(&global_lock);
    printf("[Task: %d] Removed %d from stock %c {%d -> %d}\n", currtask->taskId, currtask->amount, currtask->stockName, stock_amounts[stockIndex] + currtask->amount, stock_amounts[stockIndex]);
    // finished_threads[currtask->taskId] = true;
    remove_task(currtask);
    // pthread_cond_signal(&global_cond);
    pthread_mutex_unlock(&global_lock);

    pthread_mutex_unlock(taskLock);

    return NULL;
}

// void join_finished_threads(){
//     for(int i = 0; i < taskcount; i++){
//         if(finished_threads[i]){
//             finished_threads[i] = false;

//             pthread_mutex_unlock(&global_lock);
//             pthread_join(thread_ids[i], NULL);
//             pthread_mutex_lock(&global_lock);

//             active_thread_count--;
//             // pthread_cond_signal(&global_cond);
//         }
//     }
//     return;
// }

int main(){
    for(int i = 0; i < STOCKS; i++){
        pthread_mutex_init(&stock_locks[i], NULL);
        pthread_cond_init(&stock_conds[i], NULL);
    }
    char buffer[BUFFERSIZE];
    FILE *fptr = fopen("initial_stocks.csv", "r");
    if(fptr == NULL){fprintf(stderr, "Error opening initial stocks file\n"); exit(1);}
    while(fgets(buffer, BUFFERSIZE, fptr)){
        buffer[strcspn(buffer, "\r\n")] = 0;
        char *token = strtok(buffer, DELIMITERS);
        if(!token){continue;}
        int stockIndex = char_to_stockIndex((char)*token);
        token = strtok(NULL, DELIMITERS);
        if(!token){continue;}
        int amount = atoi(token);
        stock_amounts[stockIndex] = amount;
    }
    fclose(fptr);
    fptr = fopen("stock_tasks.csv", "r");
    if(fptr == NULL){fprintf(stderr, "Error opening stock tasks file\n"); exit(1);}
    while(fgets(buffer, BUFFERSIZE, fptr)){
        buffer[strcspn(buffer, "\r\n")] = 0;
        char *token = strtok(buffer, DELIMITERS);
        if(!token){continue;}
        char stockName = (char)*token;
        token = strtok(NULL, DELIMITERS);
        if(!token){continue;}
        int amount = atoi(token);
        token = strtok(NULL, DELIMITERS);
        if(!token){continue;}
        bool isOutput;
        if(atoi(token)){
            isOutput = true;
        }
        else{
            isOutput = false;
        }
        insert_task(stockName, amount, isOutput);
        taskcount++;
    }
    fclose(fptr);
    // print_list();
    thread_ids = malloc(taskcount * sizeof(pthread_t));
    if(thread_ids == NULL){perror("Failed to allocate thread_ids"); exit(1);}
    // finished_threads = calloc(taskcount, sizeof(bool));
    // if(finished_threads == NULL){perror("Failed to allocate finsihed threads"); exit(1);}
    struct task *curr = taskqueuehead;
    pthread_mutex_lock(&global_lock);
    while(curr != NULL){
        void *func = curr->isOutput ? remove_stock_task : add_stock_task;
        if(pthread_create(&thread_ids[curr->taskId], NULL, func, (void*)curr) != 0){
            perror("Error creating thread");
            pthread_mutex_unlock(&global_lock);
            break;
        }
        active_thread_count++;
        curr = curr->next;
    }
    pthread_mutex_unlock(&global_lock);

    for(int i = 0; i < taskcount; i++){
        if(pthread_join(thread_ids[i], NULL) != 0){
            perror("pthread_join");
        }
    }
    printf("==============\n");
    for(int i = 0; i < STOCKS; i++){
        printf("|| %c || %-3d ||\n==============\n", (char)(i + 'A'), stock_amounts[i]);
    }
    for(int i = 0; i < STOCKS; i++){
        pthread_mutex_destroy(&stock_locks[i]);
        pthread_cond_destroy(&stock_conds[i]);
    }
    pthread_mutex_destroy(&global_lock);
    // pthread_cond_destory(&global_cond);
    free(thread_ids);
    // free(finished_threads);
    return 0;
}