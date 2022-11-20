#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <sys/wait.h>
#include <unistd.h>
#include <time.h>

#define EMPLOYEE_TIME_U 2
#define EMPLOYEE_TIME_L 1
#define STORAGE_MAX_THRESHOLD 5
#define STORAGE_MIN_THRESHOLD 1
#define STORAGE_EMPLOYEE_TIME 3
#define NUMBER_LOADING_EMPLOYEES 2
#define NUMBER_TRUCKS 3
#define TRUCKS_CAPACITY 3
#define TRUCKS_TIME 20
#define CEO_SALARY 3
#define HR_SALARY 2
#define TECHNICAL_EMPLOYEE_SALARY 1
#define STORAGE_EMPLOYEE_SALARY 2
#define LOADING_EMPLOYEE_SALARY 1
#define TRUCK_EMPLOYEE_SALARY 2
#define LABTOP_COST 3
#define LABTOP_PRICE 15
#define INCOME_THRESOLD 20
#define GAINS_CUT_THRESHOLD -20
#define GAINS_EMPLOY_THRESOLD 50
#define GAINS_END_SIMULATION_THRESOLD 100


// Struct to hold labtop inofrmation
typedef struct labtop {
    int laptop_number;
    int steps_done[10];
} labtop;

// Struct to hold line information
typedef struct line {

    // Array that hold the labtops in the line
    labtop labtops[10];

    // mutex & condition variable to stop and start manufacturing process
    pthread_mutex_t resume_lock;
    pthread_cond_t resume_cv;

    // start & free locks that synchronize the first 5 steps in manufacturing process
    pthread_mutex_t start_locks[5];
    pthread_mutex_t free_locks[5];

    // queue to let last 5 employee the ability to work on labtops
    labtop queue_last[5];
    
    // lock and condition variable to synchronize queue accessing
    pthread_mutex_t queue_lock;
    pthread_cond_t queue_cv;

    // to track the queue size
    int queue_size;

    // carton box
    int carton_box[10];
    int carton_size;
    pthread_mutex_t carton_box_lock;

} line;

// struct to hold truck infomrations
typedef struct truck {
    int number_of_cartons;
    int busy;
    pthread_mutex_t truck_lock;
    pthread_cond_t truck_cv;
} truck;

// Struct to hold employee thread arguments
typedef struct emp_attr{
    int index;
    int line;
} emp_attr;

// Struct to hold line thread arguments
typedef struct line_attr{
    int index;
} line_attr;

// Struct to hold storage employee thread arguments
typedef struct storage_emp_attr{
    int line_number;
} storage_emp_attr;

// Struct to hold truck thread arguments
typedef struct truck_attr{
    int truck_number;
} truck_attr;

// Global variables

// storage room variables
int storage_room_size = 0;
pthread_mutex_t storage_room_lock;
pthread_cond_t storage_room_cv;

// trucks variables
truck trucks[NUMBER_TRUCKS];

// array of lines to hold production lines information
line lines[100];
int active_lines = 10;
pthread_mutex_t active_lines_lock;

// simulation variables
int gains = 0;
int done_labtops = 0;
int stop_line[100];
pthread_mutex_t done_labtops_lock;

// to store threads ids
pthread_t line_threads[100];
pthread_t employee_threads[100][10];
pthread_t storage_employee_thread;
pthread_t loading_employee_threads[NUMBER_LOADING_EMPLOYEES];
pthread_t truck_threads[NUMBER_TRUCKS];
pthread_t manager_thread;

// Function to generate random number between in the period [lower_bound, upper_bound]
int generate_random(int lower_bound, int upper_bound){
    return rand() % upper_bound + lower_bound;
}

// Line thread
void *factory_line(void *args){

    // extract line index argument
    line_attr *line_args = (line_attr *) args;
    int index = line_args -> index;

    // variable to hold labtop information
    labtop labtop_node;
    
    // labtops start from index 1
    int labtop_number = 1;

    // keep generating labtops and send them to first employee in the line
    while (!stop_line[index]){
        
        // wait for signal from storage employee to tell if there is capcity in storage room
        pthread_mutex_lock(&storage_room_lock);
        pthread_cond_wait(&storage_room_cv, &storage_room_lock);
        pthread_mutex_unlock(&storage_room_lock);

        // assign labtop number
        labtop_node.laptop_number = labtop_number;

        // check if first employee is free
        pthread_mutex_lock(&lines[index].free_locks[0]);

        // give the labtop to first employee in the line
        lines[index].labtops[0] = labtop_node;

        // tell the employee to start working
        pthread_mutex_unlock(&lines[index].start_locks[0]);

        // Increment the labtop number to generate the next labtop
        labtop_number++;

    }
    pthread_exit(NULL);

}

// factory employee thread
void *factory_employee(void *args){

    // extract employee arguemnts
    emp_attr *emp_args = (emp_attr*) args;
    int index = emp_args -> index;
    int line = emp_args -> line;

    // calculate employee processing time
    int time = generate_random(EMPLOYEE_TIME_L, EMPLOYEE_TIME_U);

    int i = 0,
        j = 0, 
        q = 0;

    // keep executing step assocciated with this employee
    while (!stop_line[line]){
        
        // wait for signal from storage employee to tell if there is capcity in storage room
        pthread_mutex_lock(&storage_room_lock);
        pthread_cond_wait(&storage_room_cv, &storage_room_lock);
        pthread_mutex_unlock(&storage_room_lock);

        // if the employee is one of the first 5
        // then the manufacturing must be done in order
        if(index < 5){

            // tell previous employee that this employee is free
            pthread_mutex_unlock(&lines[line].free_locks[index]);

            // wait for previous employee to send the labtop to start work
            pthread_mutex_lock(&lines[line].start_locks[index]);

            // manufacturing time
            sleep(time);
            
            printf("\nLine %d: Employee %d Labtop %d\n", line, index, lines[line].labtops[index].laptop_number);

            // set the step assigned to this employee as done for current labtop
            lines[line].labtops[index].steps_done[index] = 1;

        }

        // if the employee is one of the first 4
        // he will send the labtop to the next employee
        if(index < 4){

            // wait for the next employee to be free
            pthread_mutex_lock(&lines[line].free_locks[index + 1]);

            // give him the labtop
            lines[line].labtops[index + 1] = lines[line].labtops[index];
            lines[line].labtops[index].laptop_number = 0;

            // let next employee start his step
            pthread_mutex_unlock(&lines[line].start_locks[index + 1]);
        }

        // if employee is the 5th employee
        // then he will put the labtop in a queue
        // so any free employee from last 5 employees can work on it
        else if(index == 4){

            // wait for queue lock
            pthread_mutex_lock(&lines[line].queue_lock);

            // wait until the queue has capacity
            pthread_cond_wait(&lines[line].queue_cv, &lines[line].queue_lock);
            
            // add employee to the queue
            int size = lines[line].queue_size;
            lines[line].queue_last[size] = lines[line].labtops[index];

            // increase queue size
            lines[line].queue_size++;
            
            // unlock the queue lock
            pthread_mutex_unlock(&lines[line].queue_lock);

        }
        
        // if employee is is one of the last 5 employees
        // he will check if the queue has capacity and signal employee number 5
        // and will check if there is labtops in the queue and try to do his step
        else{

            // ask for queue lock
            pthread_mutex_lock(&lines[line].queue_lock);

            // check if the queue has capacity
            if(lines[line].queue_size < 4){
                // signal the 5th employee
                pthread_cond_signal(&lines[line].queue_cv);
            }

            // unlock the queue
            pthread_mutex_unlock(&lines[line].queue_lock);

            // ask for queue lock again
            // this step was done to prevent having two employees work on the same queue
            pthread_mutex_lock(&lines[line].queue_lock);

            // if the queue size is larger than 0
            if(lines[line].queue_size > 0){
                
                labtop chosen_labtop;
                chosen_labtop.laptop_number = -1;

                // check labtops in the queue
                for(i = 0; i < lines[line].queue_size; i++){

                    // check if there is a labtop with undone step corresponds to this employee
                    if(lines[line].queue_last[i].steps_done[index] == 0){
                        
                        // save the labtop so employee can work on it
                        chosen_labtop = lines[line].queue_last[i];

                        // do the undone step
                        sleep(time);

                        // mark the step as done in the labtop
                        lines[line].queue_last[i].steps_done[index] = 1;
                        lines[line].labtops[index] = lines[line].queue_last[i];
                        printf("\nLine %d: Employee %d Labtop %d\n", line, index, lines[line].queue_last[i].laptop_number);
                        break;

                    }
                }
                
                // check if any labtop has finished its manufacturing process
                for(i = 0; i < lines[line].queue_size; i++){

                    // check the sum of steps done
                    int sum = 0;
                    for(j = 0; j < 10; j++)
                        sum += lines[line].queue_last[i].steps_done[j];

                    // if the sum is 10, then all steps are done
                    if(sum == 10){
                        
                        // increment number of done labtops
                        pthread_mutex_lock(&done_labtops_lock);
                        done_labtops += 1;
                        pthread_mutex_unlock(&done_labtops_lock);

                        // print that the labtop is done
                        printf("\nLine %d: Labtop %d Done.\n", line, lines[line].queue_last[i].laptop_number);
                        
                        // ask for carton box queue lock
                        pthread_mutex_lock(&lines[line].carton_box_lock);

                        // add labtop to the carton box queue
                        int size = lines[line].carton_size;
                        lines[line].carton_box[size] = lines[line].queue_last[i].laptop_number;
                        lines[line].carton_size++;

                        pthread_mutex_unlock(&lines[line].carton_box_lock);

                        // wait storage employee to take the carton to continue manfucturing
                        if(lines[line].carton_size == 10){
                            pthread_mutex_lock(&lines[line].resume_lock);
                            pthread_cond_wait(&lines[line].resume_cv, &lines[line].resume_lock);

                            pthread_mutex_unlock(&lines[line].resume_lock);
                        }

                        // remove the done labtop from queue
                        for(q = i; q < 4; q++){
                            lines[line].queue_last[q] = lines[line].queue_last[q + 1];
                        }
                        lines[line].queue_size--;

                    }
                }
            }

            // unlock queue lock
            pthread_mutex_unlock(&lines[line].queue_lock);
            
        }
    }
    pthread_exit(NULL);
    
}

// Storage employee thread
void *storage_employee(void *args){

    int i = 0,
        j = 0;

    while(1){
        
        pthread_mutex_lock(&active_lines_lock);
        // iterate through all lines cartons
        for(i = 0; i < active_lines; i++){
            // check if storage room exceeded max threshold
            pthread_mutex_lock(&storage_room_lock);

            if(storage_room_size >= STORAGE_MAX_THRESHOLD){
                printf("\nmax threshold of storage room reached: %d\n", storage_room_size);
                // wait for trucks to take some cartons
                // when the storage room size returns to the min threshold then continue manufacturing
                
                // release the storage room first to give chance for trucks
                pthread_mutex_unlock(&storage_room_lock);

                // check storage room size until it reaches the min thresold
                while(1){
                    pthread_mutex_lock(&storage_room_lock);
                    
                    if(storage_room_size <= STORAGE_MIN_THRESHOLD){
                        break;
                    }
                    
                    pthread_mutex_unlock(&storage_room_lock);
                }
            }

            pthread_mutex_unlock(&storage_room_lock);

            // if there is capacity in storage room
            // then the lines are signaled to manufactur labtops
            pthread_mutex_lock(&storage_room_lock);
            pthread_cond_broadcast(&storage_room_cv);
            pthread_mutex_unlock(&storage_room_lock);

            // ask for carton box queue lock
            pthread_mutex_lock(&lines[i].carton_box_lock);

            if(lines[i].carton_size == 10){

                // print carton with filled labtops
                printf("\nCarton filled with labtops ");
                for(j = 0; j < 10; j++){
                    printf("%d ", lines[i].carton_box[j]);
                }
                printf("from Line %d put in storage room\n", i);

                // empty the carton box
                lines[i].carton_size = 0;
                
                // signal line employees so they can resume manufacturing labtops
                pthread_mutex_lock(&lines[i].resume_lock);
                pthread_cond_signal(&lines[i].resume_cv);
                pthread_mutex_unlock(&lines[i].resume_lock);

                // ask for storage room lock
                pthread_mutex_lock(&storage_room_lock);

                // add labtops carton to storage room
                storage_room_size += 1;

                // release storage room lock
                pthread_mutex_unlock(&storage_room_lock);

                // processing time of storage employee
                sleep(STORAGE_EMPLOYEE_TIME);
            }

            // release carton box lock
            pthread_mutex_unlock(&lines[i].carton_box_lock);
        }
        pthread_mutex_unlock(&active_lines_lock);
        
    }
    pthread_exit(NULL);
    
}

// loading employee thread
void *loading_employee(void *args){

    int i = 0,
        foundFlag = 0;

    while(1){
        // ask for storage room lock
        pthread_mutex_lock(&storage_room_lock);
        
        // check if there is cartons in storage room
        if(storage_room_size > 0){
            // search for free truck
            foundFlag = 0;
            for(i = 0; i < NUMBER_TRUCKS; i++){

                // ask for truck lock
                pthread_mutex_lock(&trucks[i].truck_lock);

                // check if truck is free
                if(trucks[i].busy == 0){
                    foundFlag = 1;

                    // load truck with a carton from storage room
                    // remove the carton from storage room
                    storage_room_size -= 1;

                    // add carton from storage room to the truck
                    trucks[i].number_of_cartons += 1;

                    // check if truck i reached its capacity
                    if(trucks[i].number_of_cartons >= TRUCKS_CAPACITY){
                        // send signal to the truck to drive
                        pthread_cond_signal(&trucks[i].truck_cv);
                    }
                }
                pthread_mutex_unlock(&trucks[i].truck_lock);

                // break if free truck was found
                if(foundFlag)
                    break;
            }
        }
        pthread_mutex_unlock(&storage_room_lock);
    }
    pthread_exit(NULL);
}

// truck thread
void *truck_vehicle(void *args){

    // obtain truck arguments
    truck_attr *truck_args = (truck_attr *) args;
    int truck_number = truck_args -> truck_number;

    while(1){

        // waiting to be signaled from loading employee to tell if the truck is full
        pthread_mutex_lock(&trucks[truck_number].truck_lock);
        pthread_cond_wait(&trucks[truck_number].truck_cv, &trucks[truck_number].truck_lock);

        // mark the truck as busy
        trucks[truck_number].busy = 1;

        printf("\ntruck %d has moved..\n", truck_number);

        pthread_mutex_unlock(&trucks[truck_number].truck_lock);

        // processing time of the truck
        sleep(TRUCKS_TIME);

        // set the truck to free after the processing time
        pthread_mutex_lock(&trucks[truck_number].truck_lock);

        trucks[truck_number].busy = 0;
        trucks[truck_number].number_of_cartons = 0;

        pthread_mutex_unlock(&trucks[truck_number].truck_lock); 
    }
    pthread_exit(NULL);
}

// manager thread
void *manager(void *args){
    
    // declarations
    int i = 0,
        j = 0,
        thread_status = 0;

    while (1){
        // calculating gains each 5 seconds
        // assumed as the salary period
        sleep(10);

        pthread_mutex_lock(&done_labtops_lock);
        // gains = gains from done labtops - employees salaries
        gains += done_labtops * (LABTOP_PRICE - LABTOP_COST) - 
                active_lines * (10 * TECHNICAL_EMPLOYEE_SALARY) - 
                STORAGE_EMPLOYEE_SALARY - 
                NUMBER_LOADING_EMPLOYEES * LOADING_EMPLOYEE_SALARY -
                NUMBER_TRUCKS * TRUCK_EMPLOYEE_SALARY -
                CEO_SALARY - HR_SALARY;

        printf("\n******** Gains = %d **********\n", gains);
        done_labtops = 0;
        pthread_mutex_unlock(&done_labtops_lock);
        
        // check if simulation finished
        if(gains > GAINS_END_SIMULATION_THRESOLD || active_lines <= 5){
            printf("\nSimulation Finished, Gains: %d\n", gains);
            
            // kill all threads
            pthread_mutex_lock(&active_lines_lock);

            // lines and employees
            for(i = 0; i < active_lines; i++){
                pthread_cancel(line_threads[i]);
                for(j = 0; j < 10; j++){
                    pthread_cancel(employee_threads[i][j]);
                }
            }

            // storage employee
            pthread_cancel(storage_employee_thread);

            // loading employees
            for(i = 0; i < NUMBER_LOADING_EMPLOYEES; i++){
                pthread_cancel(loading_employee_threads[i]);
            }

            // trucks employees
            for(i = 0; i < NUMBER_TRUCKS; i++){
                pthread_cancel(truck_threads[i]);
            }

            pthread_mutex_unlock(&active_lines_lock);
            break;
        }

        // check if gains is less than the min threshold
        if(gains <= GAINS_CUT_THRESHOLD){
            pthread_mutex_lock(&active_lines_lock);
            active_lines -= 1;
            // suspend last line
            stop_line[active_lines] = 1;
            pthread_mutex_unlock(&active_lines_lock);

            // reset the gains to check the gains for next time
            gains = 0;
        }

        if(gains >= GAINS_EMPLOY_THRESOLD){
            pthread_mutex_lock(&active_lines_lock);
            gains = 0;
            active_lines += 1;

            // employ extra line
            // create arguments
            line_attr *args = (line_attr *) malloc(sizeof(line_attr));
            args -> index = active_lines - 1;
            
            // create the thread
            thread_status = pthread_create(&line_threads[active_lines - 1], NULL, factory_line, (void *) args);
            if (thread_status) {
                printf("ERROR; return code from pthread_create() is %d\n", thread_status);
                exit(-1);
            }

            // creating employees threads
            for(j = 0; j < 10; j++){
                
                // create arguments
                emp_attr *args = (emp_attr *) malloc(sizeof(emp_attr));
                args -> index = j;
                args -> line = active_lines - 1;
                
                // create the thread
                thread_status = pthread_create(&employee_threads[active_lines - 1][j], NULL, factory_employee, (void *) args);
                if (thread_status) {
                    printf("ERROR; return code from pthread_create() is %d\n", thread_status);
                    exit(-1);
                }
            }

            pthread_mutex_unlock(&active_lines_lock);

        }
    }
    pthread_exit(NULL);
    
}

// main thread
int main(int argc, char const *argv[]){

    // declarations
    int i = 0,
        j = 0,
        thread_status = 0;

    // create seed for rand() function
    srand(time(0));

    // initialize locks and condition variables
    for(i = 0; i < 10; i++){
        for(j = 0; j < 5; j++){
            pthread_mutex_init(&lines[i].start_locks[j], NULL);
            pthread_mutex_init(&lines[i].free_locks[j], NULL);

            // initialize to be locked
            pthread_mutex_lock(&lines[i].start_locks[j]);
            pthread_mutex_lock(&lines[i].free_locks[j]);
        }
        pthread_mutex_init(&lines[i].queue_lock, NULL);
        pthread_mutex_init(&lines[i].resume_lock, NULL);
        pthread_cond_init(&lines[i].queue_cv, NULL);
        pthread_cond_init(&lines[i].resume_cv, NULL);
        pthread_mutex_init(&lines[i].carton_box_lock, NULL);
    }
    pthread_mutex_init(&storage_room_lock, NULL);
    pthread_cond_init(&storage_room_cv, NULL);
    for(i = 0; i < NUMBER_TRUCKS; i++){
        pthread_mutex_init(&trucks[i].truck_lock, NULL);
        pthread_cond_init(&trucks[i].truck_cv, NULL);
    }
    pthread_mutex_init(&done_labtops_lock, NULL);
    pthread_mutex_init(&active_lines_lock, NULL);



    // creating lines threads
    for(i = 0; i < 10; i++){

        // create arguments
        line_attr *args = (line_attr *) malloc(sizeof(line_attr));
        args -> index = i;
        
        // create the thread
        thread_status = pthread_create(&line_threads[i], NULL, factory_line, (void *) args);
        if (thread_status) {
            printf("ERROR; return code from pthread_create() is %d\n", thread_status);
            exit(-1);
        }

        // creating employees threads
        for(j = 0; j < 10; j++){
            
            // create arguments
            emp_attr *args = (emp_attr *) malloc(sizeof(emp_attr));
            args -> index = j;
            args -> line = i;
            
            // create the thread
            thread_status = pthread_create(&employee_threads[i][j], NULL, factory_employee, (void *) args);
            if (thread_status) {
                printf("ERROR; return code from pthread_create() is %d\n", thread_status);
                exit(-1);
            }
        }
    }

    // creating storage employee thread
    thread_status = pthread_create(&storage_employee_thread, NULL, storage_employee, NULL);
    if (thread_status) {
        printf("ERROR; return code from pthread_create() is %d\n", thread_status);
        exit(-1);
    }

    // creating loading employees threads
    for(i = 0; i < NUMBER_LOADING_EMPLOYEES; i++){
        thread_status = pthread_create(&loading_employee_threads[i], NULL, loading_employee, NULL);
        if (thread_status) {
            printf("ERROR; return code from pthread_create() is %d\n", thread_status);
            exit(-1);
        }
    }

    // creating trucks threads
    for(i = 0; i < NUMBER_TRUCKS; i++){
        
        // create arguments
        truck_attr *args = (truck_attr *) malloc(sizeof(truck_attr));
        args -> truck_number = i;

        // creating the thread
        thread_status = pthread_create(&truck_threads[i], NULL, truck_vehicle, (void *)args);
        if (thread_status) {
            printf("ERROR; return code from pthread_create() is %d\n", thread_status);
            exit(-1);
        }
    }

    // creating manager thread
    thread_status = pthread_create(&manager_thread, NULL, manager, NULL);
    if (thread_status) {
        printf("ERROR; return code from pthread_create() is %d\n", thread_status);
        exit(-1);
    }

    // joining threads
    for(i = 0; i < 10; i++){
        pthread_join(line_threads[i], NULL);
    }
    for(i = 0; i < 10; i++){
        for(j = 0; j < 10; j++){
            pthread_join(employee_threads[i][j], NULL);
        }
    }
    pthread_join(storage_employee_thread, NULL);
    for(i = 0; i < NUMBER_LOADING_EMPLOYEES; i++){
        pthread_join(loading_employee_threads[i], NULL);
    }
    for(i = 0; i < NUMBER_TRUCKS; i++){
        pthread_join(truck_threads[i], NULL);
    }

    return 0;
}
