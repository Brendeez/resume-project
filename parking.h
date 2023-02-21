#include <pthread.h>

#define SHARE_SIZE 2920
#define SHARE_NAME "PARKING"
#define LEVELS 5
#define ENTRANCES 5
#define EXITS 5
#define MAX_CAPACITY 20
#define PLATE_SIZE 6
#define TIME_SCALE 1000

typedef struct LPR
{
    pthread_mutex_t mutex;
    pthread_cond_t condition;
    char plate [PLATE_SIZE];
} LPR_t;

typedef struct boomgate
{
    pthread_mutex_t mutex;
    pthread_cond_t condition;
    char status;
} boomgate_t;

typedef struct InfoSign
{
    pthread_mutex_t mutex;
    pthread_cond_t condition;
    char status;
} InfoSign_t;

typedef struct entrance
{
    LPR_t lpr;
    boomgate_t bg;
    InfoSign_t is;
} entrance_t;

typedef struct exit
{
    LPR_t lpr;
    boomgate_t bg;
} exit_t;

typedef struct level
{
    LPR_t lpr;
    unsigned short temperature;
    volatile char alarm;
} level_t;


typedef struct sharedData{
    //parking_lot_t lot_1, lot_2, lot_3, lot_4, lot_5;
    //parking_lot_t lot[LEVELS];
    entrance_t entrance[ENTRANCES];
    exit_t exit[EXITS];
    level_t level[LEVELS];

} sharedData_t;

typedef struct sharedMemory{
    const char* name;
    int fd;
    sharedData_t* data;
} sharedMemory_t;


