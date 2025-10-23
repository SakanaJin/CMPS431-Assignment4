#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <string.h>
#include <unistd.h>

typedef enum {IN, OUT} EventType;
typedef enum {PENDING, DONE, CANCELED} Status;

typedef struct {
    char stock;     // 'A'..'F'
    int qty;
    EventType type;
    Status status;
    int id;         // event id for printing
} Event;

typedef struct {
    Event *ev;
    int *stocks;
    pthread_mutex_t *locks;
    int stock_index;
    int *result; // 1 = success, 0 = failed (not enough stock)
} ThreadArg;

#define NUM_STOCKS 6

// thread function: perform the event on the single stock
void *worker(void *arg) {
    ThreadArg *t = (ThreadArg *)arg;
    Event *ev = t->ev;
    int idx = t->stock_index;
    pthread_t tid = pthread_self();

    // Acquire lock for that stock
    pthread_mutex_lock(&t->locks[idx]);

    printf("[Thread %lu] Event %d: %s %d of stock %c (current=%d) -- acquiring lock\n",
           (unsigned long)tid, ev->id,
           (ev->type==IN) ? "IN" : "OUT",
           ev->qty, ev->stock, t->stocks[idx]);

    if (ev->type == IN) {
        // perform incoming
        t->stocks[idx] += ev->qty;
        printf("[Thread %lu] Event %d: IN completed. New %c = %d\n",
               (unsigned long)tid, ev->id, ev->stock, t->stocks[idx]);
        *(t->result) = 1;
    } else {
        // OUT
        if (t->stocks[idx] >= ev->qty) {
            t->stocks[idx] -= ev->qty;
            printf("[Thread %lu] Event %d: OUT completed. New %c = %d\n",
                   (unsigned long)tid, ev->id, ev->stock, t->stocks[idx]);
            *(t->result) = 1;
        } else {
            // Not enough stock -> release lock and terminate (fail)
            printf("[Thread %lu] Event %d: OUT cannot be done (need %d, have %d). Releasing lock and terminating.\n",
                   (unsigned long)tid, ev->id, ev->qty, t->stocks[idx]);
            *(t->result) = 0;
        }
    }

    pthread_mutex_unlock(&t->locks[idx]);
    // free thread-specific arg memory allocated by main
    free(t);
    return NULL;
}

int stock_index_from_char(char c) {
    if (c >= 'A' && c <= 'F') return c - 'A';
    return -1;
}

int main() {
    // Initial stocks from the benchmark
    // A B C D E F
    int stocks[NUM_STOCKS] = {11, 20, 5, 1, 8, 12};

    // Tasks/events (from the prompt)
    // Stocks: A D B C D F E A B F C E A D A
    // Qty:    5 3 10 3 8 10 4 3 8 5 6 5 12 5 8
    // Types:  Out Out Out In In Out In In In Out In In Out In In
    char stock_seq[] = {'A','D','B','C','D','F','E','A','B','F','C','E','A','D','A'};
    int qty_seq[] =      { 5 , 3 ,10 , 3 , 8 ,10 , 4 , 3 , 8 , 5 , 6 , 5 ,12 , 5 , 8 };
    EventType typ_seq[] = {OUT,OUT,OUT,IN,IN,OUT,IN,IN,IN,OUT,IN,IN,OUT,IN,IN};

    int num_events = sizeof(qty_seq)/sizeof(qty_seq[0]);
    Event *events = malloc(sizeof(Event) * num_events);
    for (int i = 0; i < num_events; ++i) {
        events[i].stock = stock_seq[i];
        events[i].qty = qty_seq[i];
        events[i].type = typ_seq[i];
        events[i].status = PENDING;
        events[i].id = i+1;
    }

    // Initialize per-stock mutexes
    pthread_mutex_t locks[NUM_STOCKS];
    for (int i = 0; i < NUM_STOCKS; ++i) {
        pthread_mutex_init(&locks[i], NULL);
    }

    printf("Initial stocks:\n");
    for (int i = 0; i < NUM_STOCKS; ++i)
        printf("%c: %d  ", 'A'+i, stocks[i]);
    printf("\n\n");

    // Main loop: while there are pending events, attempt them
    int pending = num_events;
    while (1) {
        // If no pending, break
        pending = 0;
        for (int i = 0; i < num_events; ++i) if (events[i].status == PENDING) pending++;
        if (pending == 0) break;

        // We'll spawn a thread for each PENDING event this pass
        pthread_t *tids = malloc(sizeof(pthread_t) * num_events);
        int *results = calloc(num_events, sizeof(int)); // 0 default
        int threads_spawned = 0;

        for (int i = 0; i < num_events; ++i) {
            if (events[i].status != PENDING) continue;

            // Create ThreadArg on heap (freed in worker)
            ThreadArg *arg = malloc(sizeof(ThreadArg));
            arg->ev = &events[i];
            arg->stocks = stocks;
            arg->locks = locks;
            arg->stock_index = stock_index_from_char(events[i].stock);
            arg->result = &results[i];

            // spawn thread
            if (pthread_create(&tids[i], NULL, worker, arg) != 0) {
                perror("pthread_create");
                free(arg);
                results[i] = 0;
                continue;
            }
            threads_spawned++;
        }

        // Join all threads spawned in this pass
        for (int i = 0; i < num_events; ++i) {
            if (events[i].status != PENDING) continue;
            pthread_join(tids[i], NULL);
        }

        // Process results: mark successes as DONE; failures remain PENDING (for OUT)
        int progress = 0;
        for (int i = 0; i < num_events; ++i) {
            if (events[i].status != PENDING) continue;
            if (results[i] == 1) {
                events[i].status = DONE;
                progress = 1;
            } else {
                // results[i] == 0 => OUT couldn't be done
                // For IN events results[i] should never be 0
                // keep PENDING
            }
        }

        free(tids);
        free(results);

        // If no progress was made this entire pass, then none of the pending OUTs can be satisfied.
        // According to the assignment: cancel the remaining unfulfillable outgoing tasks and end.
        if (!progress) {
            // check if there are any pending IN (should be none that are blocked), but most likely only OUT remain
            int any_pending_out = 0;
            for (int i = 0; i < num_events; ++i) {
                if (events[i].status == PENDING && events[i].type == OUT)
                    any_pending_out = 1;
            }
            if (any_pending_out) {
                printf("\nNo progress made in this pass. Cancelling the remaining unfulfillable OUT tasks:\n");
                for (int i = 0; i < num_events; ++i) {
                    if (events[i].status == PENDING && events[i].type == OUT) {
                        events[i].status = CANCELED;
                        printf("Event %d: OUT %d of %c -- CANCELED\n", events[i].id, events[i].qty, events[i].stock);
                    }
                }
                break;
            }
        }

        // loop continues until pending == 0 or we cancelled remaining outs
    }

    // All threads done (or cancelled). Print final stocks.
    printf("\nFinal stocks after processing:\n");
    for (int i = 0; i < NUM_STOCKS; ++i)
        printf("%c: %d  ", 'A'+i, stocks[i]);
    printf("\n\n");

    // Report any events that were never done (canceled)
    for (int i = 0; i < num_events; ++i) {
        if (events[i].status == CANCELED) {
            printf("Event %d was canceled: %s %d of %c\n", events[i].id,
                   (events[i].type==IN) ? "IN" : "OUT", events[i].qty, events[i].stock);
        } else if (events[i].status == PENDING) {
            // theoretically shouldn't happen
            printf("Event %d remains pending (unexpected): %s %d of %c\n", events[i].id,
                   (events[i].type==IN) ? "IN" : "OUT", events[i].qty, events[i].stock);
        }
    }

    // cleanup
    for (int i = 0; i < NUM_STOCKS; ++i) pthread_mutex_destroy(&locks[i]);
    free(events);

    return 0;
}
