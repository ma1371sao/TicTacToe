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
    
    struct sockaddr_in server_address, from_address;

    memset(&from_address, 0, sizeof(from_address));
    memset(&server_address, 0, sizeof(server_address));

    pid_t pid = fork();

    if (pid != 0) {  // parent process: listen to multicast and response
        printf("[Parent] Listen to MC\n");

        // make a udp socket
        int sd = socket(AF_INET, SOCK_DGRAM, 0);
        if (sd == -1) {
            perror("[MC] server: socket");
            exit(1);
        }

		server_address.sin_family = AF_INET;
		server_address.sin_port = htons(MC_PORT);
		server_address.sin_addr.s_addr = INADDR_ANY;

        struct ip_mreq mreq;
		mreq.imr_multiaddr.s_addr = inet_addr(MC_GROUP);
		mreq.imr_interface.s_addr = htonl(INADDR_ANY);
        if(setsockopt(sd, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq)) < 0) {  
			perror("[MC] setsockopt");  
			return -1;  
		}

        // bind to the port
        if (bind(sd, (struct sockaddr*)&server_address, sizeof(server_address)) < 0) {
            perror("[MC] server: bind");
            close(sd);
            exit(1);
        }

        socklen_t fromLength = sizeof(from_address);

        char buf;
        char sendBuf[4];
        uint32_t num = htonl(portNumber);
        memcpy(sendBuf, &num, 4);
        int rc;
        while (1){ // return port for incoming UDP
			rc = recvfrom(sd, &buf, sizeof(buf), 0, (struct sockaddr*)&from_address, &fromLength);
			printf("recvfrom %d bytes\n", rc);
            printf("receive from client: %c\n", buf);
			rc = sendto(sd, &sendBuf, sizeof(sendBuf), 0, (struct sockaddr*)&from_address, fromLength);
            printf("sendto %d bytes\n", rc);
        }

		close(sd);
    } else {  // child process: game
        printf("[Child] TicTacToe game\n");

        // make a tcp socket
        int sd = socket(AF_INET, SOCK_STREAM, 0);
        if (sd == -1) {
            perror("[TCP] server: socket");
            exit(1);
        }

        // network and timeout set up
        //struct timeval tv;
        //tv.tv_sec = TIMEOUT / 3;
        //tv.tv_usec = 0;
        //setsockopt(sd, SOL_SOCKET, SO_RCVTIMEO, (char*)&tv, sizeof(struct timeval));

        server_address.sin_family = AF_INET;
        server_address.sin_port = htons(portNumber);
        server_address.sin_addr.s_addr = INADDR_ANY;

        // bind to the port
        if (bind(sd, (struct sockaddr *)&server_address, sizeof(server_address)) < 0) {
            perror("[TCP] server: bind");
            close(sd);
            exit(1);
        }

        if (listen(sd, 5) == -1) {
            perror("[TCP] server: listen");
            close(sd);
            exit(1);
        }

        printf("server: waiting for connection...\n");

        // start game
        tictactoe_server(sd, 2, from_address);

        // cleanup
        close(sd);
    }

    return 0;
}

