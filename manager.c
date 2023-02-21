#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <pthread.h>
#include <sys/time.h>
#include <math.h>
#include "parking.h"
#include "hashtable.c"

typedef struct revenue_task
{
    item_t *car;
    struct revenue_task *next;
} revenue_task_t;
typedef struct bill_item
{
    item_t *car;
    float bill;
} bill_item_t;
// Hash table init
htab_t hash_plates;
htab_t hash_levels[LEVELS];
htab_t hash_revenue;
bill_item_t final_bill[100];
int size_of_bill = 0;
char *num_plate[100];

// Tracking numbers
float revenue = 0.00f;

// maybe wont use continue manager
int alarm_active = 0;

int parked_cars_total = 0;
int parked_cars_level[LEVELS];
sharedMemory_t shm;
pthread_mutex_t display_m = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t display_c = PTHREAD_COND_INITIALIZER;

pthread_mutexattr_t shared_mutex;
pthread_condattr_t shared_condition;

pthread_t *billing_thread;
int active_billing_threads;// Billing stuff
revenue_task_t *bill_operation;
revenue_task_t *final_bill_operation;
pthread_mutex_t revenue_mutex;
pthread_cond_t revenue_cond;
int num_revenue_task = 0;

void wait(int time)
{
    int xtime = time * TIME_SCALE;
    usleep(xtime);
}
bool access_shm_segment(sharedMemory_t *shm)
{

    shm->name = SHARE_NAME;
    // char *ptr;
    if ((shm->fd = shm_open(SHARE_NAME, O_RDWR, 0666)) < 0) //"O_CREAT |" will need to be removed only here for testing purposes- since there is no simulator yet i need to create the shared memory instance
    {
        perror("shm_open");
        shm->data = NULL;
        // exit(0);
        printf("error in manager init (1)\n");
        return false;
    }

    if ((shm->data = mmap(0, sizeof(sharedData_t), PROT_READ | PROT_WRITE, MAP_SHARED, shm->fd, 0)) == MAP_FAILED)
    {
        perror("mmap");
        printf("error in manager init (3)\n");
        return false;
    }
    return true;
}

void load_hashtables(void)
{
    htab_destroy(&hash_plates);
    for (int i = 0; i < 5; i++)
    {
        htab_destroy(&hash_levels[i]);
    }
    htab_destroy(&hash_revenue);

    size_t buckets = 30;
    FILE *fp = fopen("plates.txt", "r");

    if (fp == NULL)
    {
        printf("Failed to open File");
    }
    else
    {
        htab_init(&hash_plates, buckets);
        size_t len = 0;
        for (int i = 0; i < 100; i++)
        {
            len = 0;
            num_plate[i] = malloc(sizeof(char) * 7);
            getline(&num_plate[i], &len, fp);
            // printf("%s", num_plate[i]);
            num_plate[i][6] = '\0';
            htab_add(&hash_plates, num_plate[i], i);
        }
        fclose(fp);

        //-hashtable for revenue and hastable for level
        // buckets
        for (int i = 0; i < 5; i++)
        {
            if (!htab_init(&hash_levels[i], buckets))
            {
                printf("failed to initialise hash table\n");
            }
        }

        if (!htab_init(&hash_revenue, buckets))
        {
            printf("failed to initialise hash table\n");
        }
    }
}

void *monitor_entrance(void *entry_level)
{
    int level = *((int *)entry_level);
    //printf("ENTRANCE MONITORED %d\n", level);
    while (alarm_active == 0)
    {
        wait(50);
        shm.data->entrance[level].bg.status = 'C';
        pthread_mutex_lock(&shm.data->entrance[level].lpr.mutex);
        //pthread_cond_wait(&shm.data->entrance[level].lpr.condition,&shm.data->entrance[level].lpr.mutex);
        item_t *current_car = htab_find(&hash_plates, shm.data->entrance[level].lpr.plate);
        //printf("Current car is %s\n", shm.data->entrance[level].lpr.plate);
        if (current_car != NULL)
        {
            //printf("Car is not random, allowed in\n");
            pthread_mutex_unlock(&shm.data->entrance[level].lpr.mutex);

            pthread_mutex_lock(&shm.data->entrance[level].is.mutex);
            if (parked_cars_total < (LEVELS * MAX_CAPACITY))
            {
                int assigned_level = (rand()%5);
                while (parked_cars_level[assigned_level] > MAX_CAPACITY - 1)
                {
                    if (assigned_level == (LEVELS - 1)){
                        assigned_level = 0;
                    }
                    else{
                        assigned_level++;
                    }
                }
                if (parked_cars_level[assigned_level] < MAX_CAPACITY)
                {
                        struct timeval start;
                        gettimeofday(&start, NULL);
                    // display in info sign which level car should go
                        char status = (assigned_level + 1) + '0';
                        //printf("Car is assigned to level %c\n",status);
                        // set screen with level value
                        shm.data->entrance[level].is.status = status;
                        // printf("status in shared memory is %c\n", shm.data->entrance[level].is.status);

                        // start timer for billing
                        
                        //printf("Adding car to billing tab\n");
                        htab_add_revenue(&hash_revenue, current_car->key, start);
                        //printf("Added car to billing tab\n");

                        // setting sign for car
                        //printf("unlocking entrance IS mutex\n");
                        pthread_mutex_unlock(&shm.data->entrance[level].is.mutex);
                        //printf("sending cond signal to IS\n");
                        pthread_cond_signal(&shm.data->entrance[level].is.condition);
                        //printf("sent cond signal to IS\n");
                        // starting boomgate operation
                        pthread_mutex_lock(&shm.data->entrance[level].bg.mutex);
                        shm.data->entrance[level].bg.status = 'R';
                        pthread_mutex_unlock(&shm.data->entrance[level].bg.mutex);
                    
                        pthread_cond_signal(&shm.data->entrance[level].bg.condition);
                    
                        //printf("Sent BG rising signal\n");
                        wait(30);

                        // leave bg open for 20ms
                    
                        pthread_mutex_lock(&shm.data->entrance[level].bg.mutex);
                        //printf("waiting for O signal\n");
                        //pthread_cond_wait(&shm.data->entrance[level].bg.condition, &shm.data->entrance[level].bg.mutex);
                        shm.data->entrance[level].bg.status = 'L';
                        pthread_mutex_unlock(&shm.data->entrance[level].bg.mutex);

                        pthread_cond_signal(&shm.data->entrance[level].bg.condition);
                        //printf("Boomgate done\n");
                }
            }
            else
            {
                    shm.data->entrance[level].is.status = 'F';
                    pthread_mutex_unlock(&shm.data->entrance[level].is.mutex);
                    pthread_cond_signal(&shm.data->entrance[level].is.condition);
            }
        }
        else
            {
                //printf("Car is random, not allowed in\n");
                pthread_mutex_unlock(&shm.data->entrance[level].lpr.mutex);
                // printf("This car is not in plates.txt!\n");
                //printf("locking IS mutex\n");
                pthread_mutex_lock(&shm.data->entrance[level].is.mutex);
                shm.data->entrance[level].is.status = 'X';
                //printf("IS set to X\n");
                pthread_mutex_unlock(&shm.data->entrance[level].is.mutex);
                //printf("sending cond signal to IS\n");
                pthread_cond_signal(&shm.data->entrance[level].is.condition);
                //printf("sent cond signal to IS\n");
            }
    // pthread_cond_wait(&shm.data->entrance[level].lpr.condition, &shm.data->entrance[level].lpr.mutex);
    }
    while(alarm_active == 1){
        shm.data->entrance[level].bg.status = 'C';
        //pthread_mutex_lock(&shm.data->entrance[level].lpr.mutex);
    }
    
    // delete this at end possibly
    pthread_exit(NULL);
}
void *monitor_level_lpr(void *level_lpr) {
    
    int level = *((int *)level_lpr);
    printf("Monitoring level LPR for level %d\n", level);
    int level_lpr_active = 0;
    while(level_lpr_active == 0){
        pthread_mutex_lock(&shm.data->level[level].lpr.mutex);
        pthread_cond_wait(&shm.data->level[level].lpr.condition, &shm.data->level[level].lpr.mutex);

        //printf("LEVEL %d HAS BEEN SIGNALED!\n", level);

        item_t *current_car = htab_find(&hash_plates, shm.data->level[level].lpr.plate);

            //if hash table of level doesn't contain car
        if (htab_find(&hash_levels[level], shm.data->level[level].lpr.plate) == NULL) {
            // htab_add_level(&h_lv, found_car->key, id);
            htab_add(&hash_levels[level], current_car->key, 0);
            parked_cars_total++;
            parked_cars_level[level]++;
            // htab_print(&h_lv);

             //printf("CAR HAS BEEN PARKED!\n");
            // htab_print(&h_billing);

            // unlock the mutex
            pthread_mutex_unlock(&shm.data->level[level].lpr.mutex);
        }
        // if hash table does contain car, remove the car form the table as it is leaving the level
        else {
            // delete the car in h_lv
            // htab_delete_level(&h_lv, found_car->key, id);
            htab_delete(&hash_levels[level], current_car->key);
            parked_cars_level[level]--;
            parked_cars_total--;
            // htab_print(&h_lv);
            // printf("CAR HAS BEEN EXITED!\n");
            // unlock the mutex
            memset(&shm.data->level[level].lpr.plate, 0, sizeof(PLATE_SIZE));
            pthread_mutex_unlock(&shm.data->level[level].lpr.mutex);
        }
        wait(10);
    }
    pthread_exit(NULL);
}
void *make_bill(void *arg){
    
    active_billing_threads++;
    item_t *car = arg;
    //printf("Making bill for car %s!\n", car->key);
    struct timeval current;
    gettimeofday(&current, NULL);
    //printf("Car %s: start time %ld, finish time %ld", car->key, car->time.tv_sec, current.tv_sec);
    float difference = ((float)(current.tv_sec - car->time.tv_sec) * 1000 + (float)(current.tv_usec - car->time.tv_usec) / 1000)*0.05;
    float rounded_diff = trunc(20 * difference + .05) / 20;
    bill_item_t *exiting_car;
    exiting_car = malloc(sizeof(bill_item_t));
    exiting_car->car = car;
    exiting_car->bill = rounded_diff;
    final_bill[size_of_bill] = *exiting_car;
    size_of_bill++;
    

    active_billing_threads--;
    pthread_exit(arg);
}
// Adding bill to the hash table <- this will get called in the monitor exit function

void *monitor_exit(void *exit_lpr){
    int exit = *((int *)exit_lpr);
    while(alarm_active == 0){

        pthread_mutex_lock(&shm.data->exit[exit].lpr.mutex);
        // wait for the signal to start reading
        pthread_cond_wait(&shm.data->exit[exit].lpr.condition, &shm.data->exit[exit].lpr.mutex);
        item_t *current_car = htab_find(&hash_plates, shm.data->exit[exit].lpr.plate);
        if (current_car != NULL){
            current_car = htab_find(&hash_revenue, shm.data->exit[exit].lpr.plate);
            if (current_car != NULL){
                //printf("Car wants to leave! %s\n", current_car->key);
                pthread_mutex_unlock(&shm.data->exit[exit].lpr.mutex);
                pthread_create(billing_thread + active_billing_threads, NULL, make_bill, (void *)current_car);
                //printf("Added bill for car %s\n", current_car->key);
                pthread_mutex_lock(&shm.data->exit[exit].bg.mutex);
                shm.data->exit[exit].bg.status = 'R';
                pthread_mutex_unlock(&shm.data->exit[exit].bg.mutex);
                wait(10);
                pthread_cond_signal(&shm.data->exit[exit].bg.condition);
                
                pthread_mutex_lock(&shm.data->exit[exit].bg.mutex);
                        //printf("waiting for O signal\n");
                pthread_cond_wait(&shm.data->exit[exit].bg.condition, &shm.data->exit[exit].bg.mutex);
                shm.data->exit[exit].bg.status = 'L';
                pthread_mutex_unlock(&shm.data->exit[exit].bg.mutex);

                pthread_cond_signal(&shm.data->exit[exit].bg.condition);
            }
        }
    }
    pthread_mutex_lock(&shm.data->exit[exit].bg.mutex);
    shm.data->exit[exit].bg.status = 'O';
    pthread_mutex_unlock(&shm.data->exit[exit].bg.mutex);
    pthread_exit(NULL);
}

void *monitor_alarms(void *arg){
    while (alarm_active == 0)
    {
        for (int i = 0; i < LEVELS; i++)
        {
            if (shm.data->level[i].alarm == 1)
            {
                alarm_active = 1;
            }
        }   
    }
    pthread_exit(arg);
    
}
void *display(void *arg)
{
    //printf("We are in the function.\n\n");
    int delay = 2000;
    pthread_mutex_lock(&display_m);
    while (delay > 0)
    {
        
        // will be duplicated 5 times for each level-> but for now just using one instance
        for (int i = 0; i < LEVELS; i++)
        {
            printf("Entrance %d: BoomGate: %c | LPR: %s | Sign: %c \n", (i + 1), shm.data->entrance[i].bg.status, shm.data->entrance[i].lpr.plate, shm.data->entrance[i].is.status);
        }
        printf("\n");

        for (int i = 0; i < LEVELS; i++)
        {
            printf("Exit %d: BoomGate: %c | LPR: %s \n", (i + 1), shm.data->exit[i].bg.status, shm.data->exit[i].lpr.plate);
        }
        printf("\n");

        for (int i = 0; i < LEVELS; i++)
        {
            printf("Level %d: Alarm: %d | Temp Sensor: %d | LPR: %s\n", (i + 1), shm.data->level[i].alarm, shm.data->level[i].temperature, shm.data->level[i].lpr.plate);
        }
        printf("\n");

        for (int i = 0; i < LEVELS; i++)
        {
            printf("Capacity of Level %d: %d/%d cars parked, %d spaces left.\n", (i + 1), parked_cars_level[i], MAX_CAPACITY, (MAX_CAPACITY - parked_cars_level[i]));
        }
        printf("\n");

        printf("Revenue generated: $%.2f\n", revenue); // need to add %f
        wait(50);
        system("clear");
        delay--;
    }
    pthread_mutex_unlock(&display_m);

    pthread_exit(arg);
}

void init_shm_mutex(void)
{
    pthread_cond_init(&display_c, NULL);
    pthread_mutex_init(&display_m, NULL);

    pthread_mutexattr_init(&shared_mutex);
    pthread_condattr_init(&shared_condition);
    pthread_mutexattr_setpshared(&shared_mutex, PTHREAD_PROCESS_SHARED);
    pthread_condattr_setpshared(&shared_condition, PTHREAD_PROCESS_SHARED);
    for (int i = 0; i < 5; i++)
    {
        pthread_mutex_init(&shm.data->entrance[i].lpr.mutex, &shared_mutex);
        pthread_mutex_init(&shm.data->exit[i].lpr.mutex, &shared_mutex);
        pthread_mutex_init(&shm.data->level[i].lpr.mutex, &shared_mutex);
        pthread_cond_init(&shm.data->entrance[i].lpr.condition, &shared_condition);
        pthread_cond_init(&shm.data->exit[i].lpr.condition, &shared_condition);
        pthread_cond_init(&shm.data->level[i].lpr.condition, &shared_condition);
        pthread_mutex_init(&shm.data->entrance[i].bg.mutex, &shared_mutex);
        pthread_mutex_init(&shm.data->exit[i].bg.mutex, &shared_mutex);
        pthread_cond_init(&shm.data->entrance[i].bg.condition, &shared_condition);
        pthread_cond_init(&shm.data->exit[i].bg.condition, &shared_condition);
        pthread_mutex_init(&shm.data->entrance[i].is.mutex, &shared_mutex);
        pthread_cond_init(&shm.data->entrance[i].is.condition, &shared_condition);
    }
    
}


void *manage_billing_file(void* arg){
    if (!access("billing.txt", F_OK) == 0){
        FILE *fp = fopen("billing.txt", "w");
            if (fp == NULL){
                printf("Error creating file.\n ");
                pthread_exit(arg);
                }
            else{
                printf("Billing file created.\n ");
                }
        fclose(fp);
    }
        int current_appended = 0;
        while(size_of_bill >= 0)
        {
            if (size_of_bill > current_appended)
                    {
                    FILE *fpappend = fopen("billing.txt", "a");
                       // printf("Appending a bill!\n");
                    if (final_bill[current_appended].car->key != NULL){
                        //printf("%s, %f\n", final_bill[current_appended].car->key, final_bill[current_appended].bill);
                        fprintf(fpappend, "%s: $%.2f \n", final_bill[current_appended].car->key,final_bill[current_appended].bill);
                        revenue+= final_bill[current_appended].bill; 
                        htab_delete(&hash_revenue, final_bill[current_appended].car->key);
                        while(current_appended == size_of_bill)
                        {

                        }
                        current_appended++;
                        fclose(fpappend);
                    }
                    else
                    {
                         fclose(fpappend); 
                    }
                          
                }
                else{
                }
                    
                            
        }
    pthread_exit(arg);
}
           
int main(void)
{
    int entrance_num[ENTRANCES];
    int level_number[LEVELS];
    int exit_num[EXITS];
    access_shm_segment(&shm);
    init_shm_mutex();

    // Create hashtables
    load_hashtables();
    // print hashtables                      //delete
    // printf("plates\n");                     //delete
    // htab_print(&hash_plates);               //delete
    // printf("revnue\n");                     //delete
    // htab_print(&hash_revenue);              //delete
    // printf("levels\n");                     //delete
    // for(int i=0;i<5;i++){                   //delete
    //      htab_print(&hash_levels[i]);       //delete
    // }                                       //delete

    // Creating the threads
    pthread_t *entry_threads;
    entry_threads = malloc(sizeof(pthread_t) * ENTRANCES);
    pthread_t *level_threads;
    level_threads = malloc(sizeof(pthread_t) * LEVELS);
    pthread_t *exit_threads;
    exit_threads = malloc(sizeof(pthread_t) * EXITS);
    // Billing
    billing_thread = malloc(sizeof(pthread_t) * 100);
    pthread_t *alarm_thread;
    alarm_thread = malloc(sizeof(pthread_t));
    pthread_t *billing_file;
    billing_file = malloc(sizeof(pthread_t));
    pthread_create(billing_file, NULL, manage_billing_file, NULL);
    pthread_create(alarm_thread, NULL, monitor_alarms, NULL);

    for (int i = 0; i < ENTRANCES; i++)
    {
        entrance_num[i] = i;
        pthread_create(entry_threads + i, NULL, monitor_entrance, (void *)&entrance_num[i]);
    }
    for (int i = 0; i < LEVELS; i++)
    {
        level_number[i] = i;
        pthread_create(level_threads + i, NULL, monitor_level_lpr, (void *)&level_number[i]);
    }

    for (int i = 0; i < EXITS; i++)
    {
        exit_num[i] = i;
        
        pthread_create(exit_threads + i, NULL, monitor_exit, (void *)&exit_num[i]);
    }
    

    // pthread_t *level_lpr_threads;
    // level_lpr_threads = malloc(sizeof(pthread_t) * LEVELS);

    // pthread_t *exit_threads;
    // exit_threads = malloc(sizeof(pthread_t) * EXITS);

    ///* Create status display thread*/
    pthread_t *statusDisplay_thread;
    statusDisplay_thread = malloc(sizeof(pthread_t));
    pthread_cond_init(&display_c, &shared_condition);
    pthread_create(statusDisplay_thread, NULL, display, NULL);

    // pthread_t *temperature_threads;
    // temperature_threads = malloc(sizeof(pthread_t) * LEVELS);

    // pthread_t *entry_boomgate_threads;

    // pthread_t *exit_boomgate_threads;

    // free(entry_threads);
    // free(level_lpr_threads);
    // free(exit_threads);
    // free(statusDisplay_thread);
    // free(temperature_threads);
    wait(60000);
    return 0;
}