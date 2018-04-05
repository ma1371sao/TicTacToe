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
 * client [server-ip] [port]  
 */
int main(int argc, char *argv[]) {
    if (argc < 3) {
        printf("Error\n");
        exit(1); 
    }

    int portNumber;
    char serverIP[29];
    int sd;
    struct sockaddr_in server_address;

    memset(&server_address, 0, sizeof(server_address));
    
    strcpy(serverIP, argv[1]);
    portNumber = strtol(argv[2], NULL, 10);

    // make a socket
    sd = socket(AF_INET, SOCK_STREAM, 0);
    if (sd == -1) {
        perror("client: socket");
        exit(1);
    }

    // set up timeout
    struct timeval tv;
    tv.tv_sec = TIMEOUT / 3;
    tv.tv_usec = 0;
    //setsockopt(sd, SOL_SOCKET, SO_RCVTIMEO, (char*)&tv, sizeof(struct timeval));

    server_address.sin_family = AF_INET;
    server_address.sin_port = htons(portNumber);
    server_address.sin_addr.s_addr = inet_addr(serverIP);

    if (connect(sd, (struct sockaddr*)&server_address, sizeof(struct sockaddr_in)) < 0) {
        close(sd);
        exit(1);
    }

    printf("Connected! Game Start!\n");

    // start game
    tictactoe_client(sd, 1, server_address);
    
    // cleanup
    close(sd);
    return 0;
}

