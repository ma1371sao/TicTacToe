#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <poll.h>

#include "game.h"

/*
 * server [port]
 */
int main(int argc, char *argv[]) {
    // check command line arguments
    if (argc != 2) {
        printf("Invalid number of command line arguments\n");
        exit(1);
    }

    int portNumber = strtol(argv[1], NULL, 10);
    int sd;
    struct sockaddr_in server_address, from_address;

    memset(&from_address, 0, sizeof(from_address));
    memset(&server_address, 0, sizeof(server_address));

    // make a socket
    sd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sd == -1) {
        perror("server: socket");
        exit(1);
    }

    // network and timeout set up
//    struct timeval tv;
//    tv.tv_sec = TIMEOUT;
//    tv.tv_usec = 0;
//    setsockopt(sd, SOL_SOCKET, SO_RCVTIMEO, (char*)&tv, sizeof(struct timeval));

    server_address.sin_family = AF_INET;
    server_address.sin_port = htons(portNumber);
    server_address.sin_addr.s_addr = INADDR_ANY;

    // bind to the port
    if (bind(sd, (struct sockaddr *)&server_address, sizeof(server_address)) < 0) {
        perror("server: bind");
        close(sd);
        exit(1);
    }

    printf("server: waiting for connection...\n");

    // start game
    tictactoe_server(sd, 1, from_address);

    // cleanup
    close(sd);
    return 0;
}

