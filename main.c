/*
* 01.06.2020
* Koray Kural
* 150170053
* Development environment: Ubuntu 18.04
* To compile: gcc main.c -lpthread -lrt -o main
* To run: ./main N ni nd ti td
* Example: ./main 150 4 2 2 4
*/

#include <stdio.h>       // printf()
#include <stdlib.h>      // exit()
#include <unistd.h>      // fork()
#include <sys/wait.h>    // wait(NULL)
#include <sys/shm.h>     // shmget()
#include <semaphore.h>   // sem_open()
#include <fcntl.h>       // O_CREAT // in sem_open()

void increaser_function(sem_t*, int*, int*, int*, int, int, int, int);
void decreaser_function(sem_t*, int*, int*, int*, int, int, int);


int main(int argc, char* argv[])
{
    // ----------------- CHECK ARGUMENT COUNT -----------------
    if(argc != 6)
    {
        printf("5 Arguments requiered, returning\n");
        return EXIT_FAILURE;
    }


    // ----------------- GET ARGUMENTS -----------------
    char *ptr;
    int threshold = strtol(argv[1], &ptr, 10);
    int ni = strtol(argv[2], &ptr, 10);
    int nd = strtol(argv[3], &ptr, 10);
    int ti = strtol(argv[4], &ptr, 10);
    int td = strtol(argv[5], &ptr, 10);


    // ----------------- CREATE SEMAPHORES -----------------
    sem_t * mutex = sem_open("money_sem", O_CREAT, 0644, 1);
    sem_unlink ("money_sem");


    // ----------------- CREATE SHARED MEMORIES -----------------
    // Shared memory for total money in the box
    int shm_money = shmget(ftok("main.c", 0), sizeof(int) , 0666|IPC_CREAT);
    int *money = (int*) shmat(shm_money,(void*)0,0);
    
    // Shared memory for increasers turns
    int shm_inc_turn = shmget(ftok("main.c", 1), sizeof(int) , 0666|IPC_CREAT);
    int *inc_turn = (int*) shmat(shm_inc_turn,(void*)0,0);

    // Shared memory for decreasers turns
    int shm_dec_turn = shmget(ftok("main.c", 2), sizeof(int) , 0666|IPC_CREAT);
    int *dec_turn = (int*) shmat(shm_dec_turn,(void*)0,0);

    // Shared memories for signal decreasers to start
    int shm_program_state = shmget(ftok("main.c", 3), sizeof(int), 0666|IPC_CREAT);
    int *state = (int*) shmat(shm_program_state,(void*)0,0);


    // ----------------- SET INITIAL VALUES -----------------
    *money = 0;
    *state = 2; // 2: increasers below threshold, 1: increasers, -1: decreasers, 0: terminate
    *inc_turn = 0;
    *dec_turn = 0;

    // Print initial money
    printf("Master Process: Current money is %d\n", *money);

    
    // ----------------- CREATE INCREMENTER PROCESSES -----------------
    for(int i = 0; i < ni; i++)
    {
        int pid = fork();
        int increment_by = i % 2 == 0 ? 10 : 15;

        // Fork error
        if(pid < 0)
        {
            printf("Fork failed, returning\n");
            return EXIT_FAILURE;
        }

        // Child process
        else if(pid == 0)
        {
            increaser_function(mutex, money, inc_turn, state, i, ni, ti, threshold);

            // Terminate child process
            exit(0);
            break;
        }

        // Main process does nothing here
        else continue;
    }


    // ----------------- CREATE DECREMENTER PROCESSES -----------------
    for(int i = 0; i < nd; i++)
    {
        int pid = fork();

        // Fork error
        if(pid < 0)
        {
            printf("Fork failed, returning\n");
            return EXIT_FAILURE;
        }

        // Child process
        else if(pid == 0)
        {
            decreaser_function(mutex, money, dec_turn, state, i, nd, td);
            
            // Terminate child process
            exit(0);
            break;
        }

        // Main process does nothing here
        else continue;
    }


    // ----------------- WAIT CHILDREN -----------------
    while (wait(NULL) > 0) ;


    // ----------------- TERMINATE PROGRAM -----------------
    printf("Master Process: Killing all children and terminating the program\n");

    shmctl(shm_money, IPC_RMID,NULL); 
    shmctl(shm_inc_turn, IPC_RMID,NULL); 
    shmctl(shm_dec_turn, IPC_RMID,NULL); 
    shmctl(shm_program_state, IPC_RMID,NULL); 
    sem_destroy(mutex);
    return EXIT_SUCCESS;
}


void increaser_function(sem_t* mutex, int* money, int* inc_turn, int* state, int i, int ni, int ti, int threshold)
{
    // Individual process turns
    int t = 0;

    // Increment money by this value
    int increment_by = i % 2 == 0 ? 15 : 10;

    // Work as long as program not terminated
    while(*state != 0) 
    {
        if(*state > 0) // Increasers should work
        {
            // Wait for other increasers to finish their turn
            while( !(*inc_turn < (t+1) * ni && *inc_turn >= t * ni) );

            // Enter to the critical section
            sem_wait(mutex);

            // If some other function changed state
            if(*state < 1)
            {
                sem_post(mutex);
                continue;
            }

            // Increment individual and shared turns
            t++;
            *inc_turn += 1;

            // Increment money
            *money += increment_by;

            // Check if last process of turn
            if( *inc_turn % ni == 0 )
            {
                printf("Increaser Process %d: Current money is %d, increaser processes finished their turn %d\n" , i, *money, *inc_turn / ni);

                // Check if next turn is decreasers
                if( *inc_turn % (ni * ti) == 0  && (*state == 1 || (*money >= threshold))) {
                    *state = -1;
                    sem_post(mutex);
                    continue;                            
                }
            }
            else
                printf("Increaser Process %d: Current money is %d\n", i, *money);

            // Leave critical section
            sem_post(mutex);
        }
    }
}


void decreaser_function(sem_t* mutex, int* money, int* dec_turn, int* state, int i, int nd, int td)
{
    // Individual process turns
    int t = 0;

    // Fibonacci variables
    int fibonacci = 1;
    int fibonacci_inc = 0;
    int fibonacci_id = 1;
    
    // Work as long as program not terminated
    while (*state != 0)
    {
        if(*state < 0) // Decreasers should work
        {
            // Wait for other decreasers to finish their turn
            while( !(*dec_turn < (t+1) * nd && *dec_turn >= t * nd) ) ;

            // Enter to the critical section
            sem_wait(mutex);

            // If some other function changed state
            if(*state != -1)
            {
                sem_post(mutex);
                continue;
            }

            // Check if odd-even matches
            if(i % 2 != *money % 2)
            {
                // If there are more processes avaiable, wait
                // Else, increment turns
                if( *dec_turn < t * nd + (nd / 2) )
                {
                    sem_post(mutex);
                    continue;
                }
                else
                {
                    // Increment turns
                    t++;
                    *dec_turn += 1;

                    if(*dec_turn % nd == 0)
                        printf("Decreaser Process %d: odd - even unmatch, decreaser processes finished their turn %d\n", i, *dec_turn / nd);

                    sem_post(mutex);
                    continue;
                }
            }

            // Increment turns
            t++;
            *dec_turn += 1;

            // Decrement money
            *money -= fibonacci;

            // Print values
            if(*money > 0)
                if(*dec_turn % nd == 0)
                    printf("Decreaser Process %d: Current money is %d (%dth fibonacci number for decreaser %d), decreaser processes finished their turn %d\n"
                        , i, *money, fibonacci_id, i, *dec_turn / nd);
                else
                    printf("Decreaser Process %d: Current money is %d (%dth fibonacci number for decreaser %d)\n", i, *money, fibonacci_id, i);
            else
            {
                printf("Decreaser Process %d: Current money is less than %d, signaling master to finish (%dth fibonacci number for decreaser %d)\n"
                    , i, fibonacci, fibonacci_id, i);
                *state = 0;
                sem_post(mutex);
                continue;
            }

            // Increment fibonacci
            int temp = fibonacci;
            fibonacci += fibonacci_inc;
            fibonacci_inc = temp;
            fibonacci_id++;

            // Check state
            if( *dec_turn % (nd * td) == 0 )
                *state = 1;
            
            // Leave critical section
            sem_post(mutex);
        }
    }
}