#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>
#include "protocolo.h"

int sockfd;
char mi_username[32];

void send_packet(ChatPacket *pkt) {
    pkt->payload_len = strlen(pkt->payload);
    send(sockfd, pkt, sizeof(ChatPacket), 0);
}

void *receive_thread(void *arg) {
    ChatPacket pkt;
    while (recv(sockfd, &pkt, sizeof(pkt), MSG_WAITALL) > 0) {
        switch (pkt.command) {
            case CMD_MSG:
                if (strcmp(pkt.target, "ALL") == 0)
                    printf("[BROADCAST] %s: %s\n", pkt.sender, pkt.payload);
                else
                    printf("[PRIVADO] %s: %s\n", pkt.sender, pkt.payload);
                break;
            case CMD_USER_LIST:
                printf("Usuarios conectados:\n%s\n", pkt.payload);
                break;
            case CMD_USER_INFO:
                printf("Info de %s: %s\n", pkt.target, pkt.payload);
                break;
            case CMD_OK:
                printf(" %s\n", pkt.payload);
                break;
            case CMD_ERROR:
                printf(" Error: %s\n", pkt.payload);
                break;
            case CMD_DISCONNECTED:
                printf("%s se desconectó\n", pkt.payload);
                break;
        }
    }
    return NULL;
}

int main(int argc, char *argv[]) {
    if (argc != 4) {
        printf("Uso: %s <username> <IP_servidor> <puerto>\n", argv[0]);
        return 1;
    }

    strcpy(mi_username, argv[1]);
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(atoi(argv[3]));
    inet_pton(AF_INET, argv[2], &addr.sin_addr);

    if (connect(sockfd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("Error conectando");
        return 1;
    }

    // Registro
    ChatPacket pkt = {0};
    pkt.command = CMD_REGISTER;
    strncpy(pkt.sender, mi_username, 31);
    strncpy(pkt.payload, mi_username, 31);
    send_packet(&pkt);

    pthread_t thread;
    pthread_create(&thread, NULL, receive_thread, NULL);

    printf("Conectado como %s. Escribe /help\n", mi_username);

    char buffer[1024];
    while (fgets(buffer, sizeof(buffer), stdin)) {
        buffer[strcspn(buffer, "\n")] = 0;
        if (strlen(buffer) == 0) continue;

        memset(&pkt, 0, sizeof(pkt));
        strncpy(pkt.sender, mi_username, 31);

        if (strcmp(buffer, "/exit") == 0) {
            pkt.command = CMD_LOGOUT;
            send_packet(&pkt);
            break;
        } else if (strcmp(buffer, "/help") == 0) {
            printf("/broadcast <msg>\n/msg <user> <msg>\n/status <ACTIVE|BUSY|INACTIVE>\n/list\n/info <user>\n/exit\n");
            continue;
        } else if (strncmp(buffer, "/broadcast ", 11) == 0) {
            pkt.command = CMD_BROADCAST;
            strncpy(pkt.payload, buffer + 11, 956);
        } else if (strncmp(buffer, "/msg ", 5) == 0) {
            char *user = strtok(buffer + 5, " ");
            char *msg = strtok(NULL, "");
            if (!user || !msg) continue;
            pkt.command = CMD_DIRECT;
            strncpy(pkt.target, user, 31);
            strncpy(pkt.payload, msg, 956);
        } else if (strncmp(buffer, "/status ", 8) == 0) {
            pkt.command = CMD_STATUS;
            strncpy(pkt.payload, buffer + 8, 15);
        } else if (strcmp(buffer, "/list") == 0) {
            pkt.command = CMD_LIST;
        } else if (strncmp(buffer, "/info ", 6) == 0) {
            pkt.command = CMD_INFO;
            strncpy(pkt.target, buffer + 6, 31);
        } else {
            printf("Comando desconocido. Usa /help\n");
            continue;
        }
        send_packet(&pkt);
    }

    close(sockfd);
    return 0;
}