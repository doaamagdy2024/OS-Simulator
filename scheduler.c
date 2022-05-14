#include "headers.h"
#include "priority_queue.h"
#include <string.h>

FILE* logFile, *perfFile;
ALGORITHM algorithm;
PriorityQueue readyQ;
Process* Process_Table;
Process* running = NULL;
int shmid;
int remainingtime;
int* shmRemainingtime;
int* current_process_id;
int* total_number_of_received_process;
int* total_number_of_processes;

bool process_generator_finished = false;

key_t key1;
int shmid1;

key_t key2;
int shmid2;

key_t key3;
int shmid3;

key_t key4;
int shmid4;

int msg_id;
MsgBuf msgbuf;

#if (WARNINGS == 1)
#warning "Scheduler: Read the following notes carefully!"
#warning "Systick callback the scheduler.updateInformation()"
#warning "1. Increase cummualtive running time for the running process"
#warning "2. Increase waiting time for the waited process"
#warning "3. Decrease the remaining time"
#warning "-----------------------------------------------------------------------------------------------------------------"
#warning "4. Need to fork process (Uncle) to trace the clocks and interrupt the scheduler (Parent) to do the callback"
#warning "5. We need the context switching to change the state, kill, print."
#warning "-----------------------------------------------------------------------------------------------------------------"
#warning "Note:"
#warning "1. We need to make the receiving operation with notification with no blocking."
#warning "2. Set a handler upon the termination."
#warning "-----------------------------------------------------------------------------------------------------------------"
#endif
/*
1. Signal from process_generator to scheduler to receive a new arrived process
2. When process_generator is finsied, check ppid in scheduler at the end of the handler
3. If ppid == 1 (systemd), then algorithm should now it have to finsih the exist process in readyQ only
4. If not and there is no processes in readyQ, then algorithm should know there are process but not arrived yet,
 so don't terminate.
*/

int parent(void);
int child(void);

void RR(int quantum);
void HPF(void);
void SRTN(void);

void updateInformation();

void handler_notify_scheduler_new_process_has_arrived(int signum);

int main(int argc, char * argv[])
{
    #if (DEBUGGING == 1)
    printf("Debugging mode is ON!\n");
    #endif

    //the remainging time of the current running process
    key_t key_id;
    key_id = ftok("key", 65);
    shmid = shmget(key_id, sizeof(int), IPC_CREAT | 0644);
    if (shmid == -1)
    {
        perror("Error in create");
        exit(-1);
    }
    
    //make running process --> shared between the parent(scheduler) and the child(systick)
    key_id = ftok("key", 67);
    int shmid_running = shmget(key_id, sizeof(running), IPC_CREAT | 0644);
    if (shmid_running == -1)
    {
        perror("Error in create");
        exit(-1);
    }

    key1 = ftok("key.txt" ,77);
    shmid1 = shmget(key1, 512 * 1024, IPC_CREAT | 0666); // We allocated 512 KB
    Process_Table = (Process*) shmat(shmid1, NULL, 0);

    key2 = ftok("key.txt" ,78);
    shmid2 = shmget(key2, sizeof(int), IPC_CREAT | 0666); // We allocated 8 Bytes
    total_number_of_received_process = (int*) shmat(shmid2, NULL, 0);

    key3 = ftok("key.txt" ,79);
    shmid3 = shmget(key3, sizeof(int), IPC_CREAT | 0666); // We allocated 8 Bytes
    current_process_id = (int*) shmat(shmid3, NULL, 0);

    key4 = ftok("key.txt" ,80);
    shmid4 = shmget(key4, sizeof(int), IPC_CREAT | 0666);
    


    int pid;

    pid = fork();

    if (pid == -1) /* I can't give birth for you! */
    {
        perror("Error in forking!");
    }
    else if (pid == 0) /* Hi, I am the child! */
    {
        child();
    }
    else /* Hi, I am the parent! */
    { 
        signal(SIGUSR1, handler_notify_scheduler_new_process_has_arrived);
        parent();   
    }
}
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
int parent(void)
{
    initClk();

    signal(SIGCHLD, ProcessTerminates);

    shmRemainingtime = (int*)shmat(shmid, (void *)0, 0);
    if (shmRemainingtime == -1)
    {
        perror("Error in attach in scheduler");
        exit(-1);
    }

    running = (Process*)shmat(shmid, (void *)0, 0);
    if (!running)
    {
        perror("Error in attach in scheduler");
        exit(-1);
    }
    

    total_number_of_processes = (int*) shmat(shmid4, NULL, 0);
    if (*total_number_of_processes == -1)
    {
        perror("Error in attach in scheduler");
        exit(-1);
    }


    /* Create a message buffer between process_generator and scheduler */
    key_t key = ftok("key.txt" ,66);
    msg_id = msgget( key, (IPC_CREAT | 0660) );

    if (msg_id == -1) {
        perror("Error in create!");
        exit(1);
    }
    #if (NOTIFICATION == 1)
    printf("Notification (Scheduler): Message Queue ID = %d\n", msg_id);
    #endif



    #if (DEBUGGING == 1) // To debug the communication between the scheduler module and the process_generator module
    for(;;)
    {
        // printf("Schedule: I am debugging!");
        // fflush(0);
        int receiveValue = msgrcv(msg_id, ADDRESS(msgbuf), sizeof(msgbuf) - sizeof(int), 7, !(IPC_NOWAIT));
        printf("DEBUGGING: { \nProcess ID: %d,\nProcessArrival Time: %d\n}\n", msgbuf.id, msgbuf.arrivalTime);
        fflush(0);
    }
    
    return 0;
    #endif

    *total_number_of_received_process = 0;
    *current_process_id = 0;

    #if (WARNINGS == 1)
    #warning "For now, I used a super loop, but We should change it to be a callback function, called when the scheduler is notified that there is an arrived process!"
    #endif
    printf("Algorithm is running!\n");
    while (!pq_isEmpty(&readyQ) || !process_generator_finished)
    {
        if (algorithm == HPF_ALGORITHM)
            pq_push(&readyQ, Process_Table + msgbuf.id, Process_Table[msgbuf.id].priority);
        else if (algorithm == SRTN_ALGORITHM) { /* WARNING: This needs change depends on the SRTN algorithm */
            #if (WARNINGS == 1)
            #warning "Scheduler: You should decide what will be the priority parameter in the priority queue in case of SRTN algorithm."
            #endif
            pq_push(&readyQ, Process_Table + msgbuf.id, Process_Table[msgbuf.id].remainingTime);
        }
        else if (algorithm == RR_ALGORITHM) {
            #if (WARNINGS == 1)
            #warning "Scheduler: You should decide what will be the priority parameter in the priority queue in case of RR algorithm."
            #endif
            pq_push(&readyQ, Process_Table + msgbuf.id, 0);
        }

        pq_pop(&readyQ);
    }
    printf("Algorithm is finished!\n");

    shmctl(shmid1, IPC_RMID, (struct shmid_ds *)0);
    shmctl(shmid2, IPC_RMID, (struct shmid_ds *)0);
    shmctl(shmid3, IPC_RMID, (struct shmid_ds *)0);
    
    #if (WARNINGS == 1)
    #warning "Scheduler: I think we should make the logging in periodic maner. I suggest to put it in updateInformation function which is calledback every clock."
    #endif
    /*
    logFile = fopen("Scheduler.log", "w");
    fprintf(logFile, "#At  time  x  process  y  state  arr  w  total  z  remain  y  wait  k\n");//should we ingnore this line ?
    */
    //TODO implement the scheduler :)
    //upon termination release the clock resources.
    

    fclose(logFile);
    destroyClk(true);
}

int child(void)
{
    key1 = ftok("key.txt" ,77);
    shmid1 = shmget(key1, 512 * 1024, IPC_CREAT | 0666); // We allocated 512 KB
    Process_Table = (Process*) shmat(shmid1, NULL, 0);

    key2 = ftok("key.txt" ,78);   
    shmid2 = shmget(key2, sizeof(int), IPC_CREAT | 0666); // We allocated 8 Bytes
    total_number_of_received_process = (int*) shmat(shmid2, NULL, 0);

    key3 = ftok("key.txt" ,79);
    shmid3 = shmget(key3, sizeof(int), IPC_CREAT | 0666); // We allocated 8 Bytes
    current_process_id = (int*) shmat(shmid3, NULL, 0);

    /* Super Loop to keep track the clock */
    int clk = 0;
    initClk();

    shmRemainingtime = (int*)shmat(shmid, (void *)0, 0);
    if (shmRemainingtime == -1)
    {
        perror("Error in attach in scheduler");
        exit(-1);
    }
    
    for(;;)
    {
        /* To detect the new cycle */
        if(getClk() != clk) {
            clk = getClk();
            
            updateInformation();

            #if (NOTIFICATION == 1)
            printf("Notification (Scheduler): Processes' Information have been updated successfully!\n");
            fflush(0);
            #endif
        }
    }
}

//from the parent we will run each scheduler each clock cycle
void RR(int quantum)
{
    int pid, pr;
    int clk = getClk();
    //int timeToStop;
    int currentQuantum = quantum;

    while(total_number_of_processes)
    {
        if(running)
        {
            currentQuantum--;
            running->remainingTime = *shmRemainingtime;
            running->cumulativeRunningTime++;
            running->running_start_time = getClk();

            if(currentQuantum == 0)
            {
                running->state = WAITING;
                //update also the state in the process table
                Process_Table[running->id].state = WAITING;

                //send signal stop to this process and insert it back in the ready queue
                running->waiting_start_time = getClk();
                kill(running->pid, SIGTSTP);
                pq_push(&readyQ, running, 0);

                write_in_logfile_stopped();

                running = NULL;
            }
        }
        else{
            if(pq_peek(&readyQ))
            {
                running = pq_pop(&readyQ);
                *current_process_id = running->id;
                currentQuantum = quantum;
                if(running->state == READY)
                {
                    //meaning that it is the first time to be fun on the cpu
                    //inintialize the remaining time
                    *shmRemainingtime = running->burstTime;
                    pid = fork();
                    if(pid == -1) perror("Error in fork!!");
                    if(pid == 0)
                    {
                        pr = execl("./process.out", "process.out", (char*) NULL);
                        if(pr == -1)
                        {
                            perror("Error in the process fork!\n");
                            exit(0);
                        }
                    }
                    //put it in the Process
                    running->pid = pid;
                    running->state = RUNNING;
                    running->running_start_time = getClk();
                    Process_Table[running->id].pid = pid;

                    write_in_logfile_start();
                }
                else{
                    //wake it up
                    kill(running->pid, SIGCONT); //TO ASK
                    running->state = RUNNING;
                    running->running_start_time = getClk();
                    running->remainingTime = *shmRemainingtime;

                    write_in_logfile_resume();
                }
            }
            

        }

        while(clk == getClk());
        clk = getClk();
    }


}

/* Warning: Under development */
void HPF(void)
{
    int pid;
    int timeToStop;
    int pr;

    for(;;) // Super Loop
    {
        while(!pq_isEmpty(&readyQ)) {
            //checkProcessArrival();

            Process* p = pq_pop(&readyQ);

            //meaning that it is the first time to be fun on the cpu
            pid = fork();
            if(pid == -1) perror("Error in fork!!");
            if(pid == 0)
            {
                pr = execl("./process.out", "process.out", (char*) NULL);
                if(pr == -1)
                {
                    perror("Error in the process fork!\n");
                    exit(0);
                }
            }
            else
            {
                //put it in the Process
                Process_Table[p->id].pid = pid;
            }
            timeToStop = getClk() + Process_Table[p->id].executionTime;

            //Context_Switching_To_Run(p->id);
            /* Not Finished Yet */
        }
    }
}

void SRTN(void)
{
    int clk = -1;
    int peek;
    int pid, pr;
    Process* current = NULL;
    do
    {
        if(getClk() != clk)
        {
            clk = getClk();

            //check if arrived.
            if(pq_isEmpty(&readyQ))continue;
            if(current != NULL)
            {
                peek = pq_peek(&readyQ)->remainingTime;
                if(peek >= current->remainingTime) continue;

            //switch:
                //Context_Switching_To_Wait(current->id);
            }
            current = pq_pop(&readyQ);

            pid = fork();
            if(pid == -1) perror("Error in fork!!");
            if(pid == 0)
            {
                pr = execl("./process.out", "process.out", (char*) NULL);
                if(pr == -1)
                {
                    perror("Error in the process fork!\n");
                    exit(0);
                }
            }
            else
            {
                //put it in the Process
                Process_Table[current->id].pid = pid;
            }

            //Context_Switching_To_Start(current->id);

        }
        //when terminates --> set current to NULL.
    } while (1);

}

void updateInformation() {
    /* Update information for the currently running process */
    if (*total_number_of_received_process == 0)
    {
        printf("No received processes yet!\n");
        return;
    }

    // printf("DEBUGGING: { \nClock Now: %d,\nProcess ID: %d,\nArrival Time: %d\n}\n", getClk(), Process_Table[*total_number_of_received_process-1].pid, Process_Table[*total_number_of_received_process-1].arrivalTime);

    Process_Table[*current_process_id].cumulativeRunningTime += 1;
    Process_Table[*current_process_id].remainingTime = *shmRemainingtime;

    /* Update information for the waiting processes */
    for(int i = 0; i < *total_number_of_received_process; i++)
    {
        if (i == *current_process_id)
            continue;

        Process_Table[i].waitingTime += 1;
    }
}


//write_in_logfile
void write_in_logfile_start()
{
    fprintf(logFile, "At  time  %i  process  %i  started  arr  %i  total  %i  remain  %i  wait  %i\n", 
        running->running_start_time,
        running->id,
        running->arrivalTime,
        running->burstTime - running->remainingTime,    //to make sure ?!
        running->remainingTime,
        running->waitingTime   //we are sure that this variable --> no 2 processes will write on it at the same time as the update info func update it for only the wainting (not running) processes
    );
}

void write_in_logfile_resume()
{
    fprintf(logFile, "At  time  %d  process  %i  resumed  arr  %d  total  %d  remain  %d  wait  %d\n", 
        running->running_start_time,
        running->id,
        running->arrivalTime,
        running->burstTime - running->remainingTime,    //to make sure ?!
        running->remainingTime,
        running->waitingTime 
    );
}

void write_in_logfile_stopped()
{
    fprintf(logFile, "At  time  %i  process  %i  stopped  arr  %i  total  %i  remain  %i  wait  %i\n", 
        running->waiting_start_time,
        running->id,
        running->arrivalTime,
        running->burstTime - running->remainingTime,    //to make sure ?!
        running->remainingTime,
        running->waitingTime   //we are sure that this variable --> no 2 processes will write on it at the same time as the update info func update it for only the wainting (not running) processes
    );
}

void write_in_logfile_finished()
{
    int clk = getClk();
    fprintf(logFile, "At  time  %i  process  %i  finished  arr  %i  total  %i  remain  %i  wait  %i  TA  %i  WTA  %d\n", 
        clk,
        running->id,
        running->arrivalTime,
        running->burstTime - running->remainingTime,    //to make sure ?!
        running->remainingTime,
        running->waitingTime ,  //we are sure that this variable --> no 2 processes will write on it at the same time as the update info func update it for only the wainting (not running) processes

        clk - running->arrivalTime,  //finish - arrival
        (float)(clk - running->arrivalTime) / running->burstTime  //to ask (float)
    );
}


//handler_notify_scheduler_I_terminated
void ProcessTerminates(int signum)
{
    //TODO
    //implement what the scheduler should do when it gets notifies that a process is finished
    write_in_logfile_finished();
    //scheduler should delete its data from the process table
    free(Process_Table + running->id);
    //call the function Terminate_Process
    running = NULL;
    total_number_of_processes--;
    //to ask
    //should we check on the total number of processes and if it equals 0 then terminate the scheduler 

    signal(SIGCHLD, ProcessTerminates);
}


void handler_notify_scheduler_new_process_has_arrived(int signum)
{
    printf("Scehduler: I received!\n");
    fflush(0);
    int receiveValue = msgrcv(msg_id, ADDRESS(msgbuf), sizeof(msgbuf) - sizeof(int), 7, !(IPC_NOWAIT));
    #if (NOTIFICATION == 1)
    printf("Notification (Scheduler): { \nProcess ID: %d,\nProcessArrival Time: %d\n}\n", msgbuf.id, msgbuf.arrivalTime);
    #endif

    *total_number_of_received_process += 1;

    Process_Table[msgbuf.id].id = msgbuf.id;
    Process_Table[msgbuf.id].waitingTime = msgbuf.waitingTime;
    Process_Table[msgbuf.id].remainingTime = msgbuf.remainingTime;
    Process_Table[msgbuf.id].executionTime = msgbuf.executionTime;
    Process_Table[msgbuf.id].priority = msgbuf.priority;
    Process_Table[msgbuf.id].cumulativeRunningTime = msgbuf.cumulativeRunningTime;
    Process_Table[msgbuf.id].waiting_start_time = msgbuf.waiting_start_time;
    Process_Table[msgbuf.id].running_start_time = msgbuf.running_start_time;
    Process_Table[msgbuf.id].arrivalTime = msgbuf.arrivalTime;
    Process_Table[msgbuf.id].state = msgbuf.state;

    /* Parent is systemd, which means the process_generator is died! */
    if (getppid() == 1) {
        printf("My father is died!\n");
        fflush(0);
        process_generator_finished = true;
    }

    signal(SIGUSR1, handler_notify_scheduler_new_process_has_arrived);
}
