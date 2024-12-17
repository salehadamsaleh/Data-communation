#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdbool.h>
#include <sys/stat.h>
#include <time.h>

#define DATA_SIZE 255
#define MESSAGE_SIZE 512
#define MAX_CLIENTS 10
#define MAXUSER 10

struct User {
    socklen_t clilen;
    struct sockaddr_in cli_addr;
    int newsockfd;
    char username[16];
};

struct User users[MAXUSER];
int user_anz = 0;
struct sockaddr_in serv_addr;

void error(const char *msg) {
    perror(msg);
    exit(1);
}

unsigned long crc32(const unsigned char *data, unsigned int len) {
    unsigned long crc = 0;
    for (unsigned int i = 0; i < len; ++i) {
        crc += data[i];
    }
    return crc % 0xFFFFFFFF;
}

void create_log_directory() {
    struct stat st = {0};
    if (stat("logs", &st) == -1) {
        mkdir("logs", 0700);
    }
}

void write_log(const char *username, const char *message) {
    char filename[100];
    time_t t = time(NULL);
    struct tm *tm = localtime(&t);
    sprintf(filename, "logs/%d-%02d-%02d_%02d-%02d-%02d_%s.log",
            tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday,
            tm->tm_hour, tm->tm_min, tm->tm_sec, username);
    FILE *file = fopen(filename, "a");
    if (file) {
        fprintf(file, "%s\n", message);
        fclose(file);
    }
}

void *client_socket_reader(void *usernr) {
    int mynr = *(int *)usernr, n, i = 0;
    char buffer[DATA_SIZE], message_to_send[MESSAGE_SIZE], splitter[DATA_SIZE];

    while (1) {
        bzero(buffer, sizeof(buffer));
        n = read(users[mynr].newsockfd, buffer, sizeof(buffer) - 1);
        if (n <= 0) {
            printf("Client %s disconnected\n", users[mynr].username);
            user_anz--;
            close(users[mynr].newsockfd);
            free(usernr);
            return NULL;
        }

        unsigned long crc = crc32((unsigned char *)buffer, n);
        printf("Received CRC: %lu\n", crc);

        write_log(users[mynr].username, buffer);

        sscanf(buffer, "%s", splitter);

        if (strcmp(splitter, "list") == 0) {
            char userList[MESSAGE_SIZE] = "----Userlist----\n";
            for (i = 0; i < user_anz; i++) {
                strcat(userList, users[i].username);
                strcat(userList, "\n");
            }
            strcat(userList, "----------------\n");
            write(users[mynr].newsockfd, userList, strlen(userList));
        } else if (strcmp(splitter, "logout") == 0) {
            n = write(users[mynr].newsockfd, "You are disconnected\n", 22);
            user_anz--;
            free(usernr);
            return NULL;
        } else if (strcmp(splitter, "help") == 0) {
            char helpMessage[] = "----Help--------\nlist\nhelp\nlogout\n----------------\n";
            write(users[mynr].newsockfd, helpMessage, strlen(helpMessage));
        } else if (buffer[0] == '@') {
            // Handle private messages
            char recipient[16];
            sscanf(buffer, "@%15s", recipient);
            char *message_start = strchr(buffer, ' ');
            if (message_start) message_start++;
            else message_start = "";

            bool recipient_found = false;
            for (i = 0; i < user_anz; i++) {
                if (strcmp(users[i].username, recipient) == 0) {
                    snprintf(message_to_send, sizeof(message_to_send), "[Private] message from %s:  %s", users[mynr].username, message_start);
                    n = write(users[i].newsockfd, message_to_send, strlen(message_to_send));
                    if (n < 0) perror("ERROR writing to socket");
                    recipient_found = true;
                    break;
                }
            }

            if (!recipient_found) {
                char errorMsg[] = "Recipient not found\n";
                n = write(users[mynr].newsockfd, errorMsg, strlen(errorMsg));
                if (n < 0) perror("ERROR writing to socket");
            }
        } else {
            // Broadcast message to all users
            snprintf(message_to_send, sizeof(message_to_send), "%s: %s", users[mynr].username, buffer);
            for (i = 0; i < user_anz; i++) {
                if (i != mynr) {
                    n = write(users[i].newsockfd, message_to_send, strlen(message_to_send));
                    if (n < 0) perror("ERROR writing to socket");
                }
            }
        }
    }
}

int main(int argc, char *argv[]) {
    int sockfd, newsockfd, portno;
    socklen_t clilen;
    pthread_t pt;

    if (argc < 2) {
        fprintf(stderr, "ERROR, no port provided\n");
        exit(1);
    }

    create_log_directory();

    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) error("ERROR opening socket");

    bzero((char *)&serv_addr, sizeof(serv_addr));
    portno = atoi(argv[1]);
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = INADDR_ANY;
    serv_addr.sin_port = htons(portno);

    if (bind(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0)
        error("ERROR on binding");

    listen(sockfd, MAX_CLIENTS);
    clilen = sizeof(struct sockaddr_in);

    printf("The Server is started\n port number: %d\n", portno);

    while (1) {
        struct User newUser;
        newsockfd = accept(sockfd, (struct sockaddr *)&newUser.cli_addr, &clilen);
        if (newsockfd < 0) error("ERROR on accept");

        bzero(newUser.username, sizeof(newUser.username));
        read(newsockfd, newUser.username, sizeof(newUser.username) - 1);
        newUser.newsockfd = newsockfd;
        newUser.clilen = clilen;

        users[user_anz] = newUser;

        int *usernr = malloc(sizeof(int));
        *usernr = user_anz;
        if (pthread_create(&pt, NULL, client_socket_reader, usernr) != 0) {
            printf("Error creating thread for user %s\n", newUser.username);
            continue;
        }
        printf("%s has been created...\n", newUser.username);
        user_anz++;
    }

    close(sockfd);
    return 0;
}
