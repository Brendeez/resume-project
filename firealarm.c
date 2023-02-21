#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <pthread.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <unistd.h>
#include <fcntl.h>
#include "parking.h"

#define MEDIAN_WINDOW 5
#define TEMPCHANGE_WINDOW 30
#define SHM_KEY "PARKING"

/**
 * I rewrote the firealarm system, it felt like it was easier than going over it.
 */

sharedMemory_t shm;

/*To store current status of alarm*/
int8_t alarm_active = 0; 

typedef struct tempnode tempnode_t;
struct tempnode{
    int8_t temperature;
    tempnode_t *next;
};

/*Custom wait function*/
void wait(int16_t time)
{
    int16_t xtime = time * TIME_SCALE;
    usleep(xtime);
}


int getCount(struct tempnode* head)
{
    /*Initialize count*/
    int16_t count = 0; 
    /*Initialize current*/
    struct tempnode* current = head; 
    while (current != NULL) {
        count++;
        current = current->next;
    }
    return count;
}
/*Non recusive delete node function*/
void deleteEnd(struct tempnode** head){
    struct tempnode* temp = *head;
    struct tempnode* previous;
    
    
    /*else traverse to the last node*/
    while (temp->next != NULL) 
    {
        /*store previous link node as we need to change its next val*/
        previous = temp; 
        temp = temp->next; 
    }
    /*Curr assign 2nd last node's next to Null*/
    previous->next = NULL;
    /*delete the last node*/
    free(temp);
    /*2nd last now becomes the last node*/
}
int compare(const void *first, const void *second)
{
    return *((const int *)first) - *((const int *)second);
}
void tempmonitor(void){
    printf("Temperature Monitoring Started:\n");
    printf("\n");

    /*Initialise the read for the temp on each level*/
    tempnode_t *templist[LEVELS];
    tempnode_t *mediantemps[LEVELS];
    /*Calling and initialising linked list arrays before adding temperature
    Setting inital values in the list to NULL?*/
    for (int8_t i = 0; i < LEVELS; i++)
    {
        templist[i] = NULL;
        mediantemps[i]= NULL;
    }

    /*System will continue ti loop until active alarm's value is set to 1 / considered to be an emergency temperature*/
    while (alarm_active != 1)
    {
        wait(2);
        
        /*Looping through temp values adding them to linked list*/
        for (int8_t i = 0; i < LEVELS; i++)
        {
            /*Add newest temp to the begining of the linked list*/
            tempnode_t *newtemp = malloc(sizeof( struct tempnode));
            newtemp->temperature = shm.data->level[i].temperature;
            newtemp->next = templist[i];
            templist[i] = newtemp;
		
            
            //////////////////////////////////////////////////////--NEW NON RECURSIVE delete node
             int8_t count = getCount(templist[i]);
             if(count >MEDIAN_WINDOW){
                deleteEnd(&templist[i]);
             }
            //////////////////////////////////////////////////////
		    
            /*Count nodes*/
		    int8_t counttemp = 0;
            for (tempnode_t *t = templist[i]; t != NULL; t = t->next)
            {
                counttemp++;
            }
            //printf("%d counttemp\n", counttemp);
            
            /*Temperatures are only counted once we have 5 samples -> using a for loop to get every value and store*/
            if (counttemp == MEDIAN_WINDOW)
            {
                
                int *tempsort = malloc(sizeof(int) * MEDIAN_WINDOW);
                int16_t count = 0;

                for (tempnode_t *t = templist[i]; t != NULL; t = t->next)
                {
                    tempsort[count] = t->temperature;
                    count++;
                }
                qsort(tempsort, MEDIAN_WINDOW, sizeof(int), compare);
                /*Get median temperature*/
                int8_t mediantemp = tempsort[(MEDIAN_WINDOW -1) /2];
                /*printf("Median temp = %d\n", mediantemp);*/

                /*Add calculated value to linked list -> could alternatively use the malloc way
                newtemp->temperature = mediantemp;
                newtemp->next = mediantemps[i];
                mediantemps[i] = newtemp;*/

                /*Using malloc*/
                tempnode_t *newtempmed;
			    newtempmed = malloc(sizeof(struct tempnode));
			    newtempmed->temperature = mediantemp;
			    newtempmed->next = mediantemps[i];
			    mediantemps[i] = newtempmed;

                /*Delete nodes after 30th*/
                int8_t counter = getCount(mediantemps[i]);
                
                 if(counter > TEMPCHANGE_WINDOW){
                    deleteEnd(&mediantemps[i]);
                 }

                /*Count nodes*/
                int16_t median_count = 0;
                int16_t hightemps_count = 0;
                tempnode_t *oldesttemp;
                for (tempnode_t *t = mediantemps[i]; t != NULL; t = t->next)
                {
                    /*From value 58 and up -> is considered a dangerous enviroment / fire*/
                    if (t->temperature >= 58)
                    {
                        hightemps_count++;
                    }
                    else{;}

                    /*Store the oldest temperature for rate-of-rise detection*/
				    oldesttemp = t;
				    median_count++;
                }

                /*Start checking for emergency temperatures after 30 values have been read*/
                if (median_count == TEMPCHANGE_WINDOW)
                {
                    /*If 90% of the last 30 temperatures are >= 58 degrees,
				    this is considered a high temperature. Raise the alarm*/
                    if (hightemps_count >= TEMPCHANGE_WINDOW * 0.9)
                    {
                        printf("Triggering Fixed Temperature Fire Alarm\n");
                        alarm_active = 1;
                    }
                    else{;}

                    /*If the newest temp is >= 8 degrees higher than the oldest
				    temp (out of the last 30), this is a high rate-of-rise.
				    Raise the alarm*/
                    if (templist[i]->temperature - oldesttemp->temperature >= 8 && oldesttemp->temperature != 0)
                    {
                        printf("Triggering Rate of Rise Fire Alarm\n");
                        alarm_active = 1;
                    }
                    else{;}
                }
                else{;}
            }
        }
    }
    
    fprintf(stderr, "*** ALARM ACTIVE ***\n");

    /*Check for each level*/
    for (int8_t i = 0; i < LEVELS; i++)
    {
        int8_t status = 0;
        shm.data->level[i].alarm = 1;

        /*Open entrace boom gates on each level*/
        status = pthread_mutex_lock(&shm.data->exit[i].bg.mutex);
        if (status == 0)
        {
            if(shm.data->exit[i].bg.status != 'O'){
                shm.data->exit[i].bg.status = 'R';
            }
            else{
                
            }

            status = pthread_mutex_unlock(&shm.data->exit[i].bg.mutex);

            if(status == 0){
                status = pthread_cond_signal(&shm.data->exit[i].bg.condition);
                if(status != 0){
                    fprintf(stderr, "Error found in signal status: %d\n", status);
                }
                else{
                    printf("Exit boomgate set to 'R' on level: %d\n", i + 1);
                }
            }
            else{
                fprintf(stderr, "Error found in pthread mutex unlock: %d\n", status);
            }
        }
        else{
            fprintf(stderr, "Error found in pthread mutex lock: %d\n", status);
        }

        /*Open exit boom gates on each level*/
        status = pthread_mutex_lock(&shm.data->exit[i].bg.mutex);
        if (status == 0)
        {
            if(shm.data->exit[i].bg.status != 'O'){
                shm.data->exit[i].bg.status = 'R';
            }
            else{
                
            }

            status = pthread_mutex_unlock(&shm.data->exit[i].bg.mutex);

            if(status == 0){
                status = pthread_cond_signal(&shm.data->exit[i].bg.condition);
                if(status != 0){
                    fprintf(stderr, "Error found in signal status: %d\n", status);
                }
                else{;}
            }
            else{
                fprintf(stderr, "Error found in pthread mutex unlock: %d\n", status);
            }
        }
        else{
            fprintf(stderr, "Error found in pthread mutex lock: %d\n", status);
        }
    }
    
    /*Show evacuation method on an edless loop, unless error*/
        while(alarm_active == 1){
            int8_t status;
            char *evacmessage = "EVACUATE ";
            for(char *p = evacmessage; *p != '\0'; p++){
                for (int8_t i = 0; i < ENTRANCES; i++) {
                    status = pthread_mutex_lock(&shm.data->entrance[i].is.mutex);
                    if(status == 0){
                        shm.data->entrance[i].is.status = *p;
                        status = pthread_mutex_unlock(&shm.data->entrance[i].is.mutex);
                        if (status == 0){
                            status = pthread_cond_signal(&shm.data->entrance[i].is.condition);
                            if(status != 0){
                                fprintf(stderr, "Error found in signal status: %d\n", status);
                            }
                            else{;}
                        }
                        else{
                            fprintf(stderr, "Error found in pthread mutex unlock: %d\n", status);
                        }
                    }
                    else{
                        fprintf(stderr, "Error found in pthread mutex lock: %d\n", status);
                    }
                }
            }
        }
}

int main (void){
    shm.fd = shm_open(SHM_KEY, O_RDWR, 0);

    if(shm.fd > 0){
        shm.data = mmap(0, sizeof(sharedData_t), PROT_READ | PROT_WRITE, MAP_SHARED, shm.fd, 0);\

        if(shm.data != (sharedData_t *) MAP_FAILED){
            tempmonitor();
        }
        else{
            fprintf(stderr, "Error in mapping\n");
        }
    }
    else{
        fprintf(stderr, "Error in openning shared memory\n");
    }
    return 0;
}