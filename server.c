#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <time.h>
#include <stdarg.h>
#include <signal.h>
#include <stddef.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/shm.h>
#include <sys/ipc.h>
#include <sys/msg.h>

#define POWER_THRESHOLD 5000
#define WARNING_THRESHOLD 4500
#define BACKLOG 10 /* Number of allowed connections */
#define BUFF_SIZE 8192
#define MAX_DEVICE 10
#define MAX_LOG_DEVICE 100
#define MAX_MESSAGE_LENGTH 1000

////////////////////
// Variables list //
////////////////////
int server_port;
pid_t connectMng, powerSupply, elePowerCtrl, powSupplyInfoAccess, logWrite;
// current number of connecting devices
int powerSupply_count = 0;
int listen_sock, conn_sock;
char recv_data[BUFF_SIZE];
int bytes_sent, bytes_received;
struct sockaddr_in server;
struct sockaddr_in client;
int sin_size;
char use_mode[][10] = {"off", "normal", "limited"};
key_t key_s = 8888, key_d = 1234, key_m = 5678; //system info, device storage, message queue
int shmid_s, shmid_d, msqid; //system info, device storage, message queue
FILE *log_server;

// power system struct
typedef struct {
    int current_power;
    int threshold_over;
    int supply_over;
    int reset; //not used
} powsys_t;
powsys_t *powsys;

// device struct
typedef struct {
    int pid;
    char name[50];
    int use_power[3];
    int priority;
    int mode;
} device_t;
device_t *devices;

/**
 * message struct
 * mtype = 1 -> logWrite_handle
 * mtype = 2 -> powSupplyInfoAccess_handle
 */
typedef struct {
    long mtype;
    char mtext[MAX_MESSAGE_LENGTH];
} msg_t;

int tprintf(const char *fmt, ...) {
    va_list args;
    struct tm *tstruct;
    time_t tsec = time(NULL);
    tstruct = localtime(&tsec);
    printf("pid %4d:  %02d:%02d:%02d| ", getpid(), tstruct->tm_hour, tstruct->tm_min, tstruct->tm_sec);
    va_start(args, fmt);
    return vprintf(fmt, args);
}

void sigHandleSIGINT() {
    msgctl(msqid, IPC_RMID, NULL);
    shmctl(shmid_s, IPC_RMID, NULL);
    shmctl(shmid_d, IPC_RMID, NULL);
    fclose(log_server);
    kill(0, SIGKILL);
    exit(0);
}

void powerSupply_handle(int conn_sock) {
    // check if this is first time client sent
    int is_first_message = 1;
    //////////////////////////////
    // Connect to shared memory //
    //////////////////////////////
    if ((powsys = (powsys_t *) shmat(shmid_s, (void *) 0, 0)) == (void *) -1) {
        tprintf("shmat() failed\n");
        exit(1);
    }

    while (1) {
        ///////////////////
        // listen on tcp //
        ///////////////////
        bytes_received = recv(conn_sock, recv_data, BUFF_SIZE - 1, 0);
        if (bytes_received <= 0) {
            // if DISCONNECT
            // send message to powSupplyInfoAccess
            msg_t new_msg;
            new_msg.mtype = 2;
            sprintf(new_msg.mtext, "d|%d|", getpid()); // d for DISS
            msgsnd(msqid, &new_msg, MAX_MESSAGE_LENGTH, 0);

            powerSupply_count--;
            // kill this process
            kill(getpid(), SIGKILL);
            break;
        } else {
            // if receive message from client
            recv_data[bytes_received] = '\0';
            if (is_first_message) {
                is_first_message = 0;
                // send device info to powSupplyInfoAccess
                msg_t new_msg;
                new_msg.mtype = 2;
                sprintf(new_msg.mtext, "n|%d|%s|", getpid(), recv_data); // n for NEW
                msgsnd(msqid, &new_msg, MAX_MESSAGE_LENGTH, 0);
            } else {
                // if not first time client send
                // send mode to powSupplyInfoAccess
                msg_t new_msg;
                new_msg.mtype = 2;
                sprintf(new_msg.mtext, "m|%d|%s|", getpid(), recv_data); // m for MODE
                msgsnd(msqid, &new_msg, MAX_MESSAGE_LENGTH, 0);
            }
        }
    } // endwhile
} // end function powerSupply_handle

void connectMng_handle() {
    ///////////////////////
    // Connect to client //
    ///////////////////////
    //Step 1: Construct a TCP socket to listen connection request
    if ((listen_sock = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
        tprintf("socket() failed\n");
        exit(1);
    }

    //Step 2: Bind address to socket
    bzero(&server, sizeof(server));
    server.sin_family = AF_INET;
    server.sin_port = htons(server_port);
    server.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(listen_sock, (struct sockaddr *) &server, sizeof(server)) == -1) {
        tprintf("bind() failed\n");
        exit(1);
    }

    //Step 3: Listen request from client
    if (listen(listen_sock, BACKLOG) == -1) {
        tprintf("listen() failed\n");
        exit(1);
    }

    //Step 4: Communicate with client
    while (1) {
        //accept request
        sin_size = sizeof(struct sockaddr_in);
        if ((conn_sock = accept(listen_sock, (struct sockaddr *) &client, &sin_size)) == -1) {
            tprintf("accept() failed\n");
            continue;
        }

        // if 11-th device connect to SERVER
        if (powerSupply_count == MAX_DEVICE) {
            char re = '9';
            if ((bytes_sent = send(conn_sock, &re, 1, 0)) <= 0)
                tprintf("send() failed\n");
            close(conn_sock);
            break;
        }

        // create new process powerSupply
        if ((powerSupply = fork()) < 0) {
            tprintf("powerSupply fork() failed\n");
            continue;
        }

        if (powerSupply == 0) {
            //in child
            close(listen_sock);
            powerSupply_handle(conn_sock);
            close(conn_sock);
        } else {
            //in parent
            close(conn_sock);
            powerSupply_count++;
            printf("\n-----------------------------\n");
            tprintf("A new device connected, connectMng forked new process powerSupply --- pid: %d.\n", powerSupply);
        }
    } //end communication

    close(listen_sock);
} //end function connectMng_handle

void powSupplyInfoAccess_handle() {
    // mtype = 2
    msg_t got_msg;

    //////////////////////////////
    // Connect to shared memory //
    //////////////////////////////
    if ((devices = (device_t *) shmat(shmid_d, (void *) 0, 0)) == (void *) -1) {
        tprintf("shmat() failed\n");
        exit(1);
    }

    if ((powsys = (powsys_t *) shmat(shmid_s, (void *) 0, 0)) == (void *) -1) {
        tprintf("shmat() failed\n");
        exit(1);
    }
    int sum_temp,i;
    ////////////////
    // check mail //
    ////////////////
    while (1) {
        // got mail!
        if (msgrcv(msqid, &got_msg, MAX_MESSAGE_LENGTH, 2, 0) <= 0) {
            tprintf("msgrcv() error");
            exit(1);
        }

        // header = 'n' => Create new device
        if (got_msg.mtext[0] == 'n') {
            int no;
            for (no = 0; no < MAX_DEVICE; no++) {
                if (devices[no].pid == 0)
                    break;
            }
            sscanf(got_msg.mtext, "%*c|%d|%[^|]|%d|%d|%d",
                   &devices[no].pid,
                   devices[no].name,
                   &devices[no].use_power[1],
                   &devices[no].use_power[2],
                   &devices[no].priority);
            devices[no].mode = 0;
            tprintf("--- Connected device info ---\n");
            tprintf("   name    : %s\n", devices[no].name);
            tprintf("   normal  : %dW\n", devices[no].use_power[1]);
            tprintf("   limit   : %dW\n", devices[no].use_power[2]);
            tprintf("   use mode: %s\n", use_mode[devices[no].mode]);
            tprintf("   priority: %d\n", devices[no].priority);
            tprintf("-----------------------------\n\n");
            tprintf("System power using: %dW\n\n", powsys->current_power);

            // send message to logWrite
            msg_t new_msg;
            new_msg.mtype = 1;
            sprintf(new_msg.mtext, "s|[%s] connected (Normal use: %dW, Linited use: %dW)|",
                    devices[no].name,
                    devices[no].use_power[1],
                    devices[no].use_power[2],
                    devices[no].priority);
            msgsnd(msqid, &new_msg, MAX_MESSAGE_LENGTH, 0);

            sprintf(new_msg.mtext, "s|Device [%s] set mode to [off] ~ using 0W|", devices[no].name);
            msgsnd(msqid, &new_msg, MAX_MESSAGE_LENGTH, 0);
        }

        // header = 'm' => Change the mode!
        if (got_msg.mtext[0] == 'm') {
            int no, temp_pid, temp_mode;

            sscanf(got_msg.mtext, "%*c|%d|%d|", &temp_pid, &temp_mode);

            for (no = 0; no < MAX_DEVICE; no++) {
                if (devices[no].pid == temp_pid)
                    break;
            }
            devices[no].mode = temp_mode;

            // send message to logWrite
            msg_t new_msg;
            new_msg.mtype = 1;
            char temp[MAX_MESSAGE_LENGTH];

            sprintf(temp, "Device [%s] change mode to [%s], comsume %dW",
                    devices[no].name,
                    use_mode[devices[no].mode],
                    devices[no].use_power[devices[no].mode]);
            tprintf("%s\n", temp);
            sprintf(new_msg.mtext, "s|%s", temp);
            msgsnd(msqid, &new_msg, MAX_MESSAGE_LENGTH, 0);

            sleep(1);
            // update cur_power
            sum_temp=0;
            for (i = 0; i < MAX_DEVICE; i++)
                sum_temp += devices[i].use_power[devices[i].mode];
            powsys->current_power = sum_temp;

            sprintf(temp, "System power using: %dW", powsys->current_power);
            tprintf("%s\n", temp);
            sprintf(new_msg.mtext, "s|%s", temp);
            msgsnd(msqid, &new_msg, MAX_MESSAGE_LENGTH, 0);

        }

        // header = 'd' => Disconnect
        if (got_msg.mtext[0] == 'd') {
            int no, temp_pid;
            sscanf(got_msg.mtext, "%*c|%d|", &temp_pid);

            // send message to logWrite
            msg_t new_msg;
            new_msg.mtype = 1;
            char temp[MAX_MESSAGE_LENGTH];

            sprintf(temp, "Device [%s] disconnected", devices[no].name);

            for (no = 0; no < MAX_DEVICE; no++) {
                if (devices[no].pid == temp_pid) {
                    tprintf("%s\n\n", temp);
                    devices[no].pid = 0;
                    strcpy(devices[no].name, "");
                    devices[no].use_power[0] = 0;
                    devices[no].use_power[1] = 0;
                    devices[no].use_power[2] = 0;
                    devices[no].priority = 0;
                    devices[no].mode = 0;
                    break;
                } else {
                    tprintf("Error! Device not found\n\n");
                }
            }
            sprintf(new_msg.mtext, "s|%s", temp);
            msgsnd(msqid, &new_msg, MAX_MESSAGE_LENGTH, 0);

            sum_temp=0;
            for (i = 0; i < MAX_DEVICE; i++)
                sum_temp += devices[i].use_power[devices[i].mode];
            powsys->current_power = sum_temp;

            sprintf(temp, "System power using: %dW", powsys->current_power);
            tprintf("%s\n", temp);
            sprintf(new_msg.mtext, "s|%s", temp);
            msgsnd(msqid, &new_msg, MAX_MESSAGE_LENGTH, 0);
        }

    } // endwhile
} //end function powSupplyInfoAccess_handle

// Todo: find device with max of used_power/priority
int find_device_with_max_priority_power() {
    int i;
    float maxValue = -100000;
    int maxNo = 0;
    for (i = 0; i < MAX_DEVICE; i++) {
        float power_priority = ( (float) devices[i].use_power[devices[i].mode] )/ ((float) devices[i].priority);
        if (power_priority > maxValue) {
            maxValue = power_priority;
            maxNo = i;
        }
    }
    return maxNo;
}


void elePowerCtrl_handle() {
    //////////////////////////////
    // Connect to shared memory //
    //////////////////////////////
    if ((devices = (device_t *) shmat(shmid_d, (void *) 0, 0)) == (void *) -1) {
        tprintf("shmat() failed\n");
        exit(1);
    }

    if ((powsys = (powsys_t *) shmat(shmid_s, (void *) 0, 0)) == (void *) -1) {
        tprintf("shmat() failed\n");
        exit(1);
    }

    int i;
    int check_warn_threshold = 0;

    while (1) {
        // get total power using
        int sum_temp = 0;
        for (i = 0; i < MAX_DEVICE; i++)
            sum_temp += devices[i].use_power[devices[i].mode];
        powsys->current_power = sum_temp;

        // check threshold
        if (powsys->current_power >= POWER_THRESHOLD) {
            powsys->supply_over = 1;
            powsys->threshold_over = 1;
        } else if (powsys->current_power >= WARNING_THRESHOLD) {
            powsys->supply_over = 0;
            powsys->threshold_over = 1;
            powsys->reset = 0;
        } else {
            check_warn_threshold = 0;
            powsys->supply_over = 0;
            powsys->threshold_over = 0;
            powsys->reset = 0;
        }

        // WARN over threshold
        if (powsys->threshold_over && !check_warn_threshold) {
            check_warn_threshold = 1;

            // send message to logWrite
            msg_t new_msg;
            new_msg.mtype = 1;
            char temp[MAX_MESSAGE_LENGTH];
            sprintf(temp, "WARNING!!! Over threshold, power comsuming: %dW\n", powsys->current_power);
            tprintf("%s\n", temp);
            sprintf(new_msg.mtext, "s|%s", temp);
            msgsnd(msqid, &new_msg, MAX_MESSAGE_LENGTH, 0);
        }

        // overload
        if (powsys->supply_over) {
            // send message to logWrite
            msg_t new_msg;
            new_msg.mtype = 1;
            char temp[MAX_MESSAGE_LENGTH];

            sprintf(temp, "DANGER!!! System overload, power comsuming: %dW", powsys->current_power);
            tprintf("%s\n", temp);
            sprintf(new_msg.mtext, "s|%s", temp);
            msgsnd(msqid, &new_msg, MAX_MESSAGE_LENGTH, 0);

            // tprintf("Server reset in 10 seconds\n");

            int no;
            for(no = 0; no < MAX_DEVICE; no++) {
                if(devices[no].mode == 1) {
                    new_msg.mtype = 2;
                    sprintf(new_msg.mtext, "m|%d|2|", devices[no].pid);
                    msgsnd(msqid, &new_msg, MAX_MESSAGE_LENGTH, 0);
                }
            }

            pid_t my_child;
            if ((my_child = fork()) == 0) {
                // in child
                sleep(10);
                while (powsys->current_power >= POWER_THRESHOLD) {
                    int max_id = find_device_with_max_priority_power();
                    powsys->current_power -= devices[max_id].use_power[devices[max_id].mode];
                    devices[max_id].mode = 0;
                    new_msg.mtype = 2;
                    sprintf(new_msg.mtext, "m|%d|0|", devices[max_id].pid);
                    msgsnd(msqid, &new_msg, MAX_MESSAGE_LENGTH, 0);
                }

                kill(getpid(), SIGKILL);
            } else {
                //in parent
                while (1) {
                    sum_temp = 0;
                    for (i = 0; i < MAX_DEVICE; i++)
                        sum_temp += devices[i].use_power[devices[i].mode];
                    powsys->current_power = sum_temp;

                    if (powsys->current_power < POWER_THRESHOLD) {
                        powsys->supply_over = 0;
                        tprintf("OK, power now is %d\n", powsys->current_power);
                        kill(my_child, SIGKILL);
                        break;
                    }
                }
            }
        }
    } // endwhile
} //end function elePowerCtrl_handle

void logWrite_handle() {
    // mtype == 1
    msg_t got_msg;

    //////////////////////////////
    // Connect to shared memory //
    //////////////////////////////
    if ((devices = (device_t *) shmat(shmid_d, (void *) 0, 0)) == (void *) -1) {
        tprintf("shmat() failed\n");
        exit(1);
    }

    if ((powsys = (powsys_t *) shmat(shmid_s, (void *) 0, 0)) == (void *) -1) {
        tprintf("shmat() failed\n");
        exit(1);
    }

    ///////////////////////////
    // Create sever log file //
    ///////////////////////////
    char file_name[255];
    time_t t = time(NULL);
    struct tm *now = localtime(&t);
    strftime(file_name, sizeof(file_name), "./log/server_%Y-%m-%d_%H:%M:%S.txt", now);
    log_server = fopen(file_name, "w");
    tprintf("Log server started, file is %s\n", file_name);

    ///////////////////////////////
    // Listen to other processes //
    ///////////////////////////////
    while (1) {
        // got mail!
        if (msgrcv(msqid, &got_msg, MAX_MESSAGE_LENGTH, 1, 0) == -1) {
            tprintf("msgrcv() error");
            exit(1);
        }

        // header = 's' => Write log to server
        if (got_msg.mtext[0] == 's') {
            char buff[MAX_MESSAGE_LENGTH];
            //extract from message
            sscanf(got_msg.mtext, "%*2c%[^|]|", buff);
            // get time now
            char log_time[20];
            strftime(log_time, sizeof(log_time), "%Y/%m/%d_%H:%M:%S", now);
            // write log
            fprintf(log_server, "%s | %s\n", log_time, buff);
        }
    }
} //end function logWrite_handle

int main(int argc, char const *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage:  %s <Server Port>\n", argv[0]);
        exit(1);
    }
    server_port = atoi(argv[1]);
    printf("SERVER start, PID is %d.\n", getpid());

    ///////////////////////////////////////////
    // Create shared memory for power system //
    ///////////////////////////////////////////
    // 0644 is access privilege (RW-R--R--): the owner RW, the group R, others R
    if ((shmid_s = shmget(key_s, sizeof(powsys_t), 0644 | IPC_CREAT)) < 0) {
        tprintf("shmget() failed\n");
        exit(1);
    }
    if ((powsys = (powsys_t *) shmat(shmid_s, (void *) 0, 0)) == (void *) -1) {
        tprintf("shmat() failed\n");
        exit(1);
    }
    powsys->current_power = 0;
    powsys->threshold_over = 0;
    powsys->supply_over = 0;
    powsys->reset = 0;

    /////////////////////////////////////////////
    // Create shared memory for devices storage//
    /////////////////////////////////////////////
    if ((shmid_d = shmget(key_d, sizeof(device_t) * MAX_DEVICE, 0644 | IPC_CREAT)) < 0) {
        tprintf("shmget() failed\n");
        exit(1);
    }
    if ((devices = (device_t *) shmat(shmid_d, (void *) 0, 0)) == (void *) -1) {
        tprintf("shmat() failed\n");
        exit(1);
    }

    // Init data for shared memory
    int i;
    for (i = 0; i < MAX_DEVICE; i++) {
        devices[i].pid = 0;
        strcpy(devices[i].name, "");
        devices[i].use_power[0] = 0;
        devices[i].use_power[1] = 0;
        devices[i].use_power[2] = 0;
        devices[i].priority = 0;
        devices[i].mode = 0;
    }

    //////////////////////////////////
    // Create message queue for IPC //
    //////////////////////////////////
    if ((msqid = msgget(key_m, 0644 | IPC_CREAT)) < 0) {
        tprintf("msgget() failed\n");
        exit(1);
    }

    ///////////////////
    // Handle Ctrl-C //
    ///////////////////
    signal(SIGINT, sigHandleSIGINT);

    ///////////////////////////////////
    // start child process in SERVER //
    ///////////////////////////////////
    if ((connectMng = fork()) == 0) {
        connectMng_handle();
    } else if ((elePowerCtrl = fork()) == 0) {
        elePowerCtrl_handle();
    } else if ((powSupplyInfoAccess = fork()) == 0) {
        powSupplyInfoAccess_handle();
    } else if ((logWrite = fork()) == 0) {
        logWrite_handle();
    } else {
        tprintf("SERVER forked new process connectMng ------------------ pid: %d.\n", connectMng);
        tprintf("SERVER forked new process elePowerCtrl ---------------- pid: %d.\n", elePowerCtrl);
        tprintf("SERVER forked new process powSupplyInfoAccess --------- pid: %d.\n", powSupplyInfoAccess);
        tprintf("SERVER forked new process logWrite -------------------- pid: %d.\n", logWrite);
        // wait all below processes end then inform exit to cmd
        waitpid(connectMng, NULL, 0);
        waitpid(elePowerCtrl, NULL, 0);
        waitpid(powSupplyInfoAccess, NULL, 0);
        waitpid(logWrite, NULL, 0);
        tprintf("SERVER exited\n\n");
    }

    return 0;
}
