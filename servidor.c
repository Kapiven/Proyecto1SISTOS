#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <time.h>
#include "protocolo.h"

#define MAX_CLIENTS 100

typedef struct {
    char username[32];
    char ip[INET_ADDRSTRLEN];
    char status[16];
    int sockfd;
    int activo;
    time_t ultimo_mensaje;
} Cliente;

Cliente lista[MAX_CLIENTS];
int num_clientes = 0;
pthread_mutex_t mutex_lista = PTHREAD_MUTEX_INITIALIZER;

void send_packet(int sockfd, ChatPacket *pkt);
int find_client(const char *username);
void remove_client(int index);
void broadcast_to_all(ChatPacket *pkt, const char *except);
void *inactivity_checker(void *arg);

void *handle_client(void *arg) {
    int client_fd = *(int*)arg;
    char *ip = (char*)(arg + 1);
    free(arg);

    ChatPacket pkt;
    memset(&pkt, 0, sizeof(pkt));

    // Registro inicial
    if (recv(client_fd, &pkt, sizeof(pkt), MSG_WAITALL) <= 0) {
        close(client_fd);
        return NULL;
    }

    if (pkt.command != CMD_REGISTER) {
        close(client_fd);
        return NULL;
    }

    pthread_mutex_lock(&mutex_lista);

    if (find_client(pkt.sender) != -1) {
        pthread_mutex_unlock(&mutex_lista);

        ChatPacket err = {0};
        err.command = CMD_ERROR;
        strncpy(err.target, pkt.sender, 31);
        strncpy(err.payload, "Usuario ya existe", 956);
        send_packet(client_fd, &err);

        close(client_fd);
        return NULL;
    }

    // Agregar cliente
    strcpy(lista[num_clientes].username, pkt.sender);
    strcpy(lista[num_clientes].ip, ip);
    strcpy(lista[num_clientes].status, STATUS_ACTIVO);
    lista[num_clientes].sockfd = client_fd;
    lista[num_clientes].activo = 1;
    lista[num_clientes].ultimo_mensaje = time(NULL);
    num_clientes++;

    pthread_mutex_unlock(&mutex_lista);

    // OK
    ChatPacket ok = {0};
    ok.command = CMD_OK;
    strncpy(ok.target, pkt.sender, 31);
    strncpy(ok.payload, "Bienvenido", 956);
    send_packet(client_fd, &ok);

    // 🔁 Bucle principal
    while (recv(client_fd, &pkt, sizeof(pkt), MSG_WAITALL) > 0) {

        pthread_mutex_lock(&mutex_lista);

        int idx = find_client(pkt.sender);
        if (idx != -1) {
            lista[idx].ultimo_mensaje = time(NULL);

            // 🔥 Si estaba inactivo, vuelve a activo
            if (strcmp(lista[idx].status, STATUS_INACTIVO) == 0) {
                strcpy(lista[idx].status, STATUS_ACTIVO);
            }
        }

        pthread_mutex_unlock(&mutex_lista);

        ChatPacket response = {0};

        switch (pkt.command) {

            case CMD_BROADCAST: {
                ChatPacket msgpkt = {0};
                msgpkt.command = CMD_MSG;
                strncpy(msgpkt.sender, pkt.sender, 31);
                strncpy(msgpkt.target, "ALL", 31);
                strncpy(msgpkt.payload, pkt.payload, 956);

                broadcast_to_all(&msgpkt, NULL);
                break;
            }

            case CMD_DIRECT: {
                int tgt = find_client(pkt.target);

                if (tgt != -1) {
                    response.command = CMD_MSG;
                    strncpy(response.sender, pkt.sender, 31);
                    strncpy(response.target, pkt.target, 31);
                    strncpy(response.payload, pkt.payload, 956);

                    send_packet(lista[tgt].sockfd, &response);
                } else {
                    response.command = CMD_ERROR;
                    strncpy(response.target, pkt.sender, 31);
                    strncpy(response.payload, "Destinatario no conectado", 956);
                    send_packet(client_fd, &response);
                }
                break;
            }

            case CMD_LIST: {
                response.command = CMD_USER_LIST;
                strncpy(response.target, pkt.sender, 31);

                char list[957] = "";

                pthread_mutex_lock(&mutex_lista);
                for (int i = 0; i < num_clientes; i++) {
                    if (lista[i].activo) {
                        strcat(list, lista[i].username);
                        strcat(list, ",");
                        strcat(list, lista[i].status);
                        strcat(list, ";");
                    }
                }
                pthread_mutex_unlock(&mutex_lista);

                strncpy(response.payload, list, 956);
                send_packet(client_fd, &response);
                break;
            }

            case CMD_INFO: {
                int tgt = find_client(pkt.target);

                if (tgt != -1) {
                    response.command = CMD_USER_INFO;
                    strncpy(response.target, pkt.sender, 31);

                    char info[957];
                    snprintf(info, sizeof(info), "%s,%s", lista[tgt].ip, lista[tgt].status);

                    strncpy(response.payload, info, 956);
                    send_packet(client_fd, &response);
                } else {
                    response.command = CMD_ERROR;
                    strncpy(response.target, pkt.sender, 31);
                    strncpy(response.payload, "Usuario no conectado", 956);
                    send_packet(client_fd, &response);
                }
                break;
            }

            case CMD_STATUS: {
                pthread_mutex_lock(&mutex_lista);

                int idx = find_client(pkt.sender);
                if (idx != -1) {
                    strncpy(lista[idx].status, pkt.payload, 15);
                }

                pthread_mutex_unlock(&mutex_lista);

                response.command = CMD_OK;
                strncpy(response.target, pkt.sender, 31);
                strncpy(response.payload, pkt.payload, 956);
                send_packet(client_fd, &response);
                break;
            }

            case CMD_LOGOUT:
                goto logout;
        }
    }

logout:
    pthread_mutex_lock(&mutex_lista);

    int idx = find_client(pkt.sender);
    if (idx != -1) remove_client(idx);

    pthread_mutex_unlock(&mutex_lista);

    ChatPacket disc = {0};
    disc.command = CMD_DISCONNECTED;
    strncpy(disc.payload, pkt.sender, 31);

    broadcast_to_all(&disc, pkt.sender);

    close(client_fd);
    return NULL;
}

void *inactivity_checker(void *arg) {
    while (1) {
        sleep(10);
        time_t now = time(NULL);

        pthread_mutex_lock(&mutex_lista);

        for (int i = 0; i < num_clientes; i++) {
            if (lista[i].activo &&
                strcmp(lista[i].status, STATUS_INACTIVO) != 0 &&
                (now - lista[i].ultimo_mensaje > INACTIVITY_TIMEOUT)) {

                strcpy(lista[i].status, STATUS_INACTIVO);

                ChatPacket msg = {0};
                msg.command = CMD_MSG;
                strncpy(msg.sender, "SERVER", 31);
                strncpy(msg.target, lista[i].username, 31);
                strncpy(msg.payload, "Tu status cambió a INACTIVE", 956);

                send_packet(lista[i].sockfd, &msg);
            }
        }

        pthread_mutex_unlock(&mutex_lista);
    }
    return NULL;
}

int main(int argc, char *argv[]) {

    if (argc != 2) {
        printf("Uso: %s <puerto>\n", argv[0]);
        return 1;
    }

    int server_fd = socket(AF_INET, SOCK_STREAM, 0);

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(atoi(argv[1]));
    addr.sin_addr.s_addr = INADDR_ANY;

    bind(server_fd, (struct sockaddr*)&addr, sizeof(addr));
    listen(server_fd, 10);

    printf("Servidor escuchando en puerto %s...\n", argv[1]);

    pthread_t checker;
    pthread_create(&checker, NULL, inactivity_checker, NULL);
    pthread_detach(checker);

    while (1) {
        struct sockaddr_in client_addr;
        socklen_t len = sizeof(client_addr);

        int client_fd = accept(server_fd, (struct sockaddr*)&client_addr, &len);

        char ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &client_addr.sin_addr, ip, sizeof(ip));

        int *data = malloc(sizeof(int) + INET_ADDRSTRLEN);
        *data = client_fd;
        strcpy((char*)(data + 1), ip);

        pthread_t thread;
        pthread_create(&thread, NULL, handle_client, data);
        pthread_detach(thread);
    }

    return 0;
}

void send_packet(int sockfd, ChatPacket *pkt) {
    pkt->payload_len = strlen(pkt->payload);
    send(sockfd, pkt, sizeof(ChatPacket), 0);
}

int find_client(const char *username) {
    for (int i = 0; i < num_clientes; i++)
        if (lista[i].activo && strcmp(lista[i].username, username) == 0)
            return i;
    return -1;
}

void remove_client(int index) {
    lista[index].activo = 0;
    close(lista[index].sockfd);

    for (int i = index; i < num_clientes - 1; i++)
        lista[i] = lista[i + 1];

    num_clientes--;
}

void broadcast_to_all(ChatPacket *pkt, const char *except) {
    pthread_mutex_lock(&mutex_lista);

    for (int i = 0; i < num_clientes; i++) {
        if (lista[i].activo &&
            (except == NULL || strcmp(lista[i].username, except) != 0)) {

            ChatPacket copy = *pkt;

            if (pkt->command == CMD_MSG) {
                strncpy(copy.sender, pkt->sender, 31);
                strncpy(copy.target, "ALL", 31);
            }

            send_packet(lista[i].sockfd, &copy);
        }
    }

    pthread_mutex_unlock(&mutex_lista);
}
