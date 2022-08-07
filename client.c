#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <time.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define BUFF_SIZE 8192

int main(int argc, char const *argv[]) {
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <Server IP> <Echo Port>\n", argv[0]);
        exit(1);
    }

    // Get info for device
    char name[50];
    int mode_2 = 0;
    int mode_3 = 0;
    int priority = 0;
    int use_mode;
    int check = 0;
    printf("Choose your device:"
           "\n1.   Light bulb"
           "\n2.   Electric Fan"
           "\n3.   PC"
           "\n4.   Washing Machine"
           "\n5.   Oven"
           "\n6.   Air Conditioner"
           "\n7.   Fridge"
           "\n8.   TV"
           "\n9.   Electromagnetic Stove"
           "\nYou choose: ");
    do {
        char device_choose = getchar();
        getchar();

        switch (device_choose) {
            case '1':
                check = 1;
                strcpy(name, "Light bulb");
                priority = 100;
                break;
            case '2':
                check = 1;
                strcpy(name, "Electric Fan");
                priority = 90;
                break;
            case '3':
                check = 1;
                strcpy(name, "PC");
                priority = 80;
                break;
            case '4':
                check = 1;
                strcpy(name, "Washing Machine");
                priority = 70;
                break;
            case '5':
                check = 1;
                strcpy(name, "Oven");
                priority = 60;
                break;
            case '6':
                check = 1;
                strcpy(name, "Air Conditioner");
                priority = 50;
                break;
            case '7':
                check = 1;
                strcpy(name, "Fridge");
                priority = 40;
                break;
            case '8':
                check = 1;
                strcpy(name, "TV");
                priority = 30;
                break;
            case '9':
                check = 1;
                strcpy(name, "Electromagnetic Stove");
                priority = 20;
                break;
            default:
                device_choose = '0';
        }
        if (device_choose == '0') {
            printf("DISCONNECTED\n");
            kill(0, SIGKILL);
        }
    } while (check = 0);

    while ((mode_2 <= mode_3) || (mode_2 >= 10000)) {
        printf("Normal power mode: ");
        scanf("%d", &mode_2);
        getchar();
        printf("Electric power saving mode: ");
        scanf("%d", &mode_3);
        getchar();
        if (mode_2 <= mode_3) {
            printf("The normal power mode must be larger than power saving mode\nInput again\n");
        }
        if (mode_2 >= 10000) {
            printf("Invalid power input\nInput again\n");
        }
    }

    // Step 0: Init variable
    int client_sock;
    char buff[BUFF_SIZE];
    struct sockaddr_in server_addr;
    int msg_len, bytes_sent, bytes_received;

    // Step 1: Construct socket
    client_sock = socket(AF_INET, SOCK_STREAM, 0);

    // Step 2: Specify server address
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(atoi(argv[2]));
    server_addr.sin_addr.s_addr = inet_addr(argv[1]);

    // Step 3: Request to connect server
    if (connect(client_sock, (struct sockaddr *) &server_addr, sizeof(struct sockaddr)) < 0) {
        perror("accept() failed\n");
        exit(1);
    }

    // Step 4: Communicate with server

    // First, send device info to server
    memset(buff, '\0', strlen(buff) + 1);
    sprintf(buff, "%s|%d|%d|%d", name, mode_2, mode_3, priority);
    msg_len = strlen(buff);
    if (msg_len == 0) {
        printf("No info on device\n");
        close(client_sock);
        exit(1);
    }
    bytes_sent = send(client_sock, buff, msg_len, 0);
    if (bytes_sent <= 0) {
        printf("Connection close\n");
        close(client_sock);
        exit(1);
    }

    // Then, wait for server response
    if (fork() == 0) {
        // Child: listen from server
        while (1) {
            bytes_received = recv(client_sock, buff, BUFF_SIZE - 1, 0);
            if (bytes_received <= 0) {
                // if DISCONNECT
                printf("\nDISCONNECT WITH SERVER.\n");
                break;
            } else {
                buff[bytes_received] = '\0';
            }

            int buff_i = atoi(buff);
            // if (buff_i = 9) => max device reached => quit

            if (buff_i == 9) {
                printf("Max devices reached. Can't connect to server\n");
            }

        }
    } else {
        // Parent: open menu for user
        do {
            sleep(1);
            printf(
                    "-------- MENU --------\n"
                    "0. Turn off the machine\n"
                    "1. Normal mode\n"
                    "2. Electric power saving mode\n"
                    "(Choose 0,1 or 2, others to disconnect): ");
            char menu = getchar();
            getchar();

            switch (menu) {
                case '0':
                    printf("TURN OFF\n\n");
                    break;
                case '1':
                    printf("NORMAL MODE TURN ON\n\n");
                    break;
                case '2':
                    printf("POWER SAVING MODE TURN ON\n\n");
                    break;
                default:
                    menu = '3';
                    printf("DISCONNECTED\n");
            }
            if (menu == '3')
                break;
            send(client_sock, &menu, 1, 0);
        } while (1);
    }

    // Step 5: Close socket
    close(client_sock);
    kill(0, SIGKILL);
    return 0;
}