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
#include <ctype.h>
#include <pthread.h>

#include "parking.h"
// Global Variables

typedef struct queue_node queue_node_t;
struct queue_node
{
    char plate[PLATE_SIZE];
    queue_node_t *next;
};

typedef struct car_level car_level_t;
struct car_level
{
    char plate[PLATE_SIZE];
    int level;
};
int alarm_active = 0;
sharedMemory_t shm;
int temperature_type;
char *num_plate[100];
int en_queue_num[ENTRANCES];
int platefile = 0;
int current_active_cars = 0;
pthread_mutexattr_t shared_mutex;
pthread_condattr_t shared_condition;
pthread_t *car_manager;
pthread_mutex_t sim_mutex;
pthread_cond_t sim_condition;

pthread_mutex_t rand_mutex;

queue_node_t *entrance_queue[ENTRANCES];

queue_node_t* insert_car(queue_node_t *head, char car[PLATE_SIZE])
{
    queue_node_t *front_of_line;

    queue_node_t *new = (queue_node_t *)malloc(sizeof(queue_node_t));
    memcpy(new->plate, car, PLATE_SIZE);
    new->next = NULL;

    queue_node_t *current = head;
    if (current == NULL)
    {
        front_of_line = new;
    }
    else{
        while (current->next != NULL)
        {
            current = current->next;
        }
        current->next = new;
        front_of_line = head;
    }
    return front_of_line;   
}

queue_node_t* remove_car(queue_node_t *head)
{
    queue_node_t *front_of_line = head;
    queue_node_t *next_in_line;

    if (front_of_line == NULL)
    {
        next_in_line = NULL;
        return next_in_line;
    }
    else{
        next_in_line = front_of_line->next;
        //free(front_of_line);
        return next_in_line;
    }
}
void print_queue(queue_node_t *head)
{
    queue_node_t *current = head;
    while (current != NULL)
    {
        printf("Current car is %s\n", current->plate);
        // Going over everyone else in the queue
        current = current->next;
    }
}

void destroy_queue(queue_node_t *head)
{
    queue_node_t *current = head;
    queue_node_t *next_in_line;

    while (current != NULL)
    {
        next_in_line = current->next;
        free(current);
        current = next_in_line;
    }
}

void wait(int time)
{
    int xtime = time * TIME_SCALE;
    usleep(xtime);
}
void innit_shm(sharedMemory_t* shm) {

    shm->name= SHARE_NAME;
    
    if((shm->fd= shm_open(SHARE_NAME, O_CREAT | O_RDWR, 0666))<0) 
    {
        perror("shm_open");
        shm->data=NULL;
        //exit(0);
        printf("error in manager init (1)\n");
    }
    if(ftruncate(shm->fd, sizeof(sharedData_t))<0)
    {
        perror("shm_open");
        printf("error in manager init (2)\n");
        shm->data=NULL;
    }
    if ((shm->data = mmap(0, sizeof(sharedData_t), PROT_READ | PROT_WRITE, MAP_SHARED, shm->fd, 0)) == MAP_FAILED) 
    {
        perror("mmap");
        printf("error in manager init (3)\n");
    }
    
}

void load_plates(void){
    FILE *fp = fopen("plates.txt", "r");

    if (fp == NULL)
    {
        printf("Failed to open File\n");   
    }
    else
    {
        size_t len = 0;
        for (int i = 0; i < 100; i++)
        {
            len = 0;
            num_plate[i] = malloc(sizeof(char) * 7);
            getline(&num_plate[i], &len, fp);
            num_plate[i][6] = '\0';
        }
        fclose(fp);
    }
}
void destroy_shm(sharedMemory_t* shm)
{
    munmap(shm, sizeof(sharedData_t));
    shm_unlink(shm->name);
    shm->fd = -1;
    shm->data = NULL;
}

void *simulate_rand_temp(void *arg)
{
    int level = (*(int *)arg);
    int cont = 1;
    int temperature_count = 0;
    volatile int normal_temp = 20;
    //printf("%d level fire sim\n", level);
    while (cont == 1)
    {
        if (temperature_type == 2)
        {
            if (temperature_count %30 == 0 && normal_temp < 85)
            {
                normal_temp++;
                shm.data->level[level].temperature= normal_temp;
            }
            wait(20);
        }
        else if (temperature_type == 3)
        {
            int temp3_count = 0;
            while (temperature_type == 3){
                if (temp3_count >= 900)
            {
                shm.data->level[level].temperature= (rand() % 12) + normal_temp;
            }
            else
            {
                shm.data->level[level].temperature= (rand() % 4) + normal_temp;
            }
            wait(2);
            temp3_count++;
            }
            
        }
        else
        {
            shm.data->level[level].temperature= (rand() % 6) + normal_temp;
        }
        
    }
    // Return something here
    pthread_exit(NULL);
}
int readkey(void)
{
    

        char input = getchar();
        if (input == 'X' || input == 'x')
        {
            printf("Closing simulator\n");
            destroy_shm(&shm);
            return 1;
        }
        else if (input == 'F'|| input == 'f')
        {
            printf("Triggered Fixed Temperature Simulation\n");
            temperature_type = 2;
        }
        else if (input == 'R' || input =='r')
        {
            printf("Triggered Rate-of-Rise Simulation\n");
            temperature_type = 3;
        }
        
        
    return 0;
}
void init_pthreads()
{
    pthread_mutexattr_init(&shared_mutex);
    pthread_condattr_init(&shared_condition);
    pthread_mutexattr_setpshared(&shared_mutex, PTHREAD_PROCESS_SHARED);
    pthread_condattr_setpshared(&shared_condition, PTHREAD_PROCESS_SHARED);

    pthread_mutex_init(&sim_mutex, &shared_mutex);
    pthread_cond_init(&sim_condition, &shared_condition);
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
        shm.data->entrance[i].bg.status = 'C';
        shm.data->exit[i].bg.status = 'C';
    }
    

}

void randomplate(char *value){

    int var = (rand()%5)+1;
    if (var < 5){
        if (platefile < 100){
            strcpy(value, num_plate[platefile]);
            platefile++;
        }
        else{
            platefile = 0;
            strcpy(value, num_plate[platefile]);
            platefile++;
        }
        
    }
    else{

    // Refered from Stack Overflow for further knowledge 
    // https://stackoverflow.com/questions/440133/how-do-i-create-a-random-alpha-numeric-string-in-c
    static const char ucase[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    static const char ucase1[] = "1234567890";

    const size_t ucase_count = sizeof(ucase) - 1;
    const size_t ucase_count1 = sizeof(ucase1) - 1;

    int j;

    int time_sleep = 1 ; //in seconds  
    char random_plate[6];
    

        sleep (time_sleep);
        srand(time(NULL));           

        for(j = 0; j < 6; j++) {
            if(j<3){

                int random_index1 = (double)rand() / RAND_MAX * ucase_count1;
                random_plate[j] = ucase1[random_index1];
           
            }
            else {
                //random_plate[j] = 'A';

                int random_index = (double)rand() / RAND_MAX * ucase_count;
                random_plate[j] = ucase[random_index];
            }               

        }
            
        //printf("RANDOM plate is %s\n", random_plate);

        
        strcpy(value, random_plate);

    }
}
void trigger_entrance_lpr(int entry, queue_node_t plate){
    //printf("Triggering entrance LPR with car %s\n", plate.plate);
    pthread_mutex_lock(&shm.data->entrance[entry].lpr.mutex);
    memcpy(&shm.data->entrance[entry].lpr.plate, &plate, PLATE_SIZE);
    //printf("%s at entrance %d\n",shm.data->entrance[entry].lpr.plate, entry);
    pthread_mutex_unlock(&shm.data->entrance[entry].lpr.mutex);
    //printf("Unlocked entrance LPR mutex\n");
    pthread_cond_signal(&shm.data->entrance[entry].lpr.condition);
}
void boomgate_open(int entry){
    pthread_mutex_lock(&shm.data->entrance[entry].bg.mutex);
    pthread_cond_wait(&shm.data->entrance[entry].bg.condition, &shm.data->entrance[entry].bg.mutex);
    shm.data->entrance[entry].bg.status = 'O';
    //printf("Set BG to O\n");
    pthread_mutex_unlock(&shm.data->entrance[entry].bg.mutex);
    wait(10);
    pthread_cond_signal(&shm.data->entrance[entry].bg.condition);
    wait(20);
    //printf("start of close boomgate\n");
    
    pthread_mutex_lock(&shm.data->entrance[entry].bg.mutex);
    //printf("Mutex locked in boomgate close\n");
    pthread_cond_wait(&shm.data->entrance[entry].bg.condition,&shm.data->entrance[entry].bg.mutex);
    shm.data->entrance[entry].bg.status = 'C';
    pthread_mutex_unlock(&shm.data->entrance[entry].bg.mutex);
    //printf("End of close boomgate\n");
}

int getCount(queue_node_t *head){
    int count = 0;
    queue_node_t *current = head;
    while (current != NULL)
        {
            count++;
            current = current->next;
        }
    return count;
}
void *car_manager_function(void *arg)
{
    int exit = (rand()%5);
    current_active_cars++;
    car_level_t *car = arg;
    int level = (car->level - 1);
    //printf("Managing car %s to level %d\n", car->plate, car->level);
    wait(10);

    pthread_mutex_lock(&shm.data->level[level].lpr.mutex);
    memcpy(shm.data->level[level].lpr.plate, car->plate, PLATE_SIZE);
    pthread_mutex_unlock(&shm.data->level[level].lpr.mutex);

    
    
    pthread_cond_signal(&shm.data->level[level].lpr.condition);
    int parked = (rand()%10000)+400;
    //printf("Car %s is parked for %d milliseconds\n", car->plate,parked);
    wait(parked);

    pthread_mutex_lock(&shm.data->level[level].lpr.mutex);
    memcpy(shm.data->level[level].lpr.plate, car->plate, PLATE_SIZE);
    pthread_mutex_unlock(&shm.data->level[level].lpr.mutex);
    pthread_cond_signal(&shm.data->level[level].lpr.condition);
    //printf("Car %s has sent second level lpr signal\n",car->plate);
    
    pthread_mutex_lock(&shm.data->exit[exit].lpr.mutex);
    memcpy(shm.data->exit[exit].lpr.plate, car->plate, PLATE_SIZE);
    pthread_mutex_unlock(&shm.data->exit[exit].lpr.mutex);
    wait(10);
    pthread_cond_signal(&shm.data->exit[exit].lpr.condition);
    //printf("Car %s has sent plate to exit %d\n",car->plate, exit);

    pthread_mutex_lock(&shm.data->exit[exit].bg.mutex);
    pthread_cond_wait(&shm.data->exit[exit].bg.condition, &shm.data->exit[exit].bg.mutex);

    
    shm.data->exit[exit].bg.status = 'O';
    wait(20);
    pthread_cond_signal(&shm.data->exit[exit].bg.condition);
    //printf("Exit %d set to O\n",exit);
    pthread_cond_wait(&shm.data->exit[exit].bg.condition, &shm.data->exit[exit].bg.mutex);
    if (alarm_active == 0){
        shm.data->exit[exit].bg.status = 'C';
    }
    pthread_cond_signal(&shm.data->exit[exit].bg.condition);
    pthread_mutex_unlock(&shm.data->exit[exit].bg.mutex);


    free(car);
    current_active_cars--;
    pthread_exit(NULL);
}
//presenting queue to lpr at entrances
void *entry_handler(void *entry_level)
{
    int entry = *((int *)entry_level);
    int entrance_open = 1;
    while (entrance_open)
    {
        wait(50);
        int var = 0;
        var = getCount(entrance_queue[entry]);
        //printf("%d\n", var);
        if (var > 0){
            //printf("There are %d cars in queue for entrance %d\nCurrent car is: %s\n",en_queue_num[entry],entry, entrance_queue[entry]);
            trigger_entrance_lpr(entry,*entrance_queue[entry]);
            //printf("Locking entrance sign mutex\n");
            pthread_mutex_lock(&shm.data->entrance[entry].is.mutex);
            //printf("Waiting for condition signal in sign\n");
            pthread_cond_wait(&shm.data->entrance[entry].is.condition, &shm.data->entrance[entry].is.mutex);
            //printf("Finished waiting for condition signal in sign\n");
            // Read sign
            char sign_val = shm.data->entrance[entry].is.status;
            pthread_mutex_unlock(&shm.data->entrance[entry].is.mutex);
            //printf("sign val : %c\n", sign_val);
            if (!isdigit(sign_val))
            {
                //printf("Car not accepted! Queue size before removal: %d     ", getCount(entrance_queue[entry]));
                entrance_queue[entry] = remove_car(entrance_queue[entry]);
                //printf("Queue size after Removal: %d\n", getCount(entrance_queue[entry]));
            }
            else{
                int var = sign_val - '0';
                //printf("car allowed to enter to level %d\n", var);

                car_level_t *car = malloc(sizeof(car_level_t));
                strcpy(car->plate,entrance_queue[entry]->plate);

                car->level = var;
                //printf("Managing car %s to level %d\n", car->plate, car->level);
                boomgate_open(entry);
                pthread_create(car_manager + current_active_cars, NULL, car_manager_function, (void *)car);
                entrance_queue[entry] = remove_car(entrance_queue[entry]);
            }
        }
    }
    pthread_exit(NULL);

}



void *populate(){
    //pthread_mutex_init(&rand_mutex, NULL);
    int populate = 1;
    while (populate){
        //pthread_mutex_lock(&rand_mutex);
        int delay = (rand()%100)+1;
        wait(delay);

        char *plate;
        plate=malloc(sizeof(char)*6);
        randomplate(plate);
        int entrance_no = (rand()%5);
        entrance_queue[entrance_no] = insert_car(entrance_queue[entrance_no], plate);
        en_queue_num[entrance_no]++;
        //pthread_mutex_unlock(&rand_mutex);
        //printf("Printing Thread, car added in thread %d\n", entrance_no);
        //print_queue(entrance_queue[0]);
    }

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
    printf("Fire detected, evacuate!\n");
    for (int i = 0; i < LEVELS; i++)
        {
            wait(10);
            shm.data->exit[i].bg.status = 'O';
        }
    pthread_exit(arg);
    
}
void trigger_exit_lpr(int exit_num, char plate[PLATE_SIZE])
{
    // Same stuff as entry
    int exit_lpr_num =exit_num;
    pthread_mutex_lock(&shm.data->exit[exit_lpr_num].lpr.mutex);
    memcpy(&shm.data->exit[exit_lpr_num].lpr.plate, &plate, PLATE_SIZE);
    //printf("%s at entrance %d\n",shm.data->entrance[entry].lpr.plate, entry);
    pthread_mutex_unlock(&shm.data->exit[exit_lpr_num].lpr.mutex);
    //pthread_cond_signal(&shm.data->entrance[entry].lpr.condition);

}
void wait_for_exit_boomgate(int exit_num)
{
    int open = 0;
    while (open == 0)
    {
        if (shm.data->exit[exit_num].bg.status == 'O')
        {
            open = 1;
        }
        else
        {
            wait(5);
        }   
    }
}
int main (void)
{
    int entrance_num[LEVELS];
    int entrance_num_temp[LEVELS];
    int end = 0;

    //printf("BEFORE\n");
    innit_shm(&shm);
    load_plates();
    //threads
    
    pthread_t *entrance_queue_sim;
    
    
    pthread_t *temperature_thread;
    pthread_t *populate_queues;
    car_manager = malloc(sizeof(pthread_t) * 100);
    init_pthreads();
    pthread_t *alarm_thread;
    alarm_thread = malloc(sizeof(pthread_t));
    pthread_create(alarm_thread, NULL, monitor_alarms, NULL);
    temperature_thread = malloc(sizeof(pthread_t) * LEVELS);
    
    for (int i = 0; i < LEVELS; i++){
        entrance_num_temp[i] = i;
        pthread_create(temperature_thread, NULL, simulate_rand_temp, (void *)&entrance_num_temp[i]);
    }
    
    entrance_queue_sim = malloc(sizeof(pthread_t) * LEVELS);
    populate_queues = malloc(sizeof(pthread_t));
    pthread_create(populate_queues, NULL, populate,NULL);
    for (int i = 0; i < ENTRANCES; i++){
        entrance_num[i] = i;
        pthread_create(entrance_queue_sim, NULL, entry_handler, (void *)&entrance_num[i]); //queue of new cars
    }

    





    printf("To stop simulation, press 'X' and enter key. \n");
    printf("To test fire alarm system, press 'F' for a fixed temperature, or 'R' for a rate-of-rise test. \n");
    //printf("DURING\n");
    while (end == 0){
        end = readkey();
        
    }
    if (end == 1){
        printf("Simulator closed.\n");
        return 0;
    }  
}