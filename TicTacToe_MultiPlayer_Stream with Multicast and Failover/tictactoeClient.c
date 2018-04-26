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
 * 
 */
int main(int argc, char *argv[]) {
    
    /* if (argc < 3) { */
    /*     printf("Error\n"); */
    /*     exit(1); */ 
    /* } */
    int maybeContinue = 0;
    char board[ROWS][COLUMNS];

    int portNumber;
    //char serverIP[29];
    
    struct sockaddr_in server_address_udp, server_address_tcp, from_address;

    memset(&server_address_udp, 0, sizeof(server_address_udp));
    memset(&server_address_tcp, 0, sizeof(server_address_tcp));
    //strcpy(serverIP, argv[1]);
    //portNumber = strtol(argv[2], NULL, 10);

    ////////////////// send to MC ///////////////////////////
    int mc_sd = socket(AF_INET, SOCK_DGRAM, 0);

    server_address_udp.sin_family = AF_INET;
	server_address_udp.sin_addr.s_addr = inet_addr(MC_GROUP);
	server_address_udp.sin_port = htons(MC_PORT);

    socklen_t fromLength = sizeof(from_address);

    char sendBuf = '1';
    char recvBuf[4];
mc:
    sendto(mc_sd, &sendBuf, sizeof(sendBuf), 0, (struct sockaddr*)&server_address_udp, sizeof(server_address_udp));
    int rc;

    while ((rc = recvfrom(mc_sd, &recvBuf, sizeof(recvBuf), 0, (struct sockaddr*)&from_address, &(fromLength))) > 0) {
        int num;
        memcpy(&num, recvBuf, 4);
        portNumber = ntohl(num);
        printf("Port number is %d\n", portNumber);
        printf("IP: %d\n", from_address.sin_addr.s_addr);
        
        //////////////////// connet to game ///////////////////////////////////

        // make a socket
        int tcp_sd = socket(AF_INET, SOCK_STREAM, 0);
        if (tcp_sd == -1) {
            perror("client: socket");
            exit(1);
        }

        // set up timeout
        //struct timeval tv;
        //tv.tv_sec = TIMEOUT / 3;
        //tv.tv_usec = 0;
        //setsockopt(sd, SOL_SOCKET, SO_RCVTIMEO, (char*)&tv, sizeof(struct timeval));
        
        memset(&server_address_tcp, 0, sizeof(server_address_tcp));
        server_address_tcp.sin_family = AF_INET;
        server_address_tcp.sin_port = htons(portNumber);
        // server_address_tcp.sin_addr.s_addr = inet_addr(serverIP);
        server_address_tcp.sin_addr.s_addr = from_address.sin_addr.s_addr;

        if (connect(tcp_sd, (struct sockaddr*)&server_address_tcp, sizeof(struct sockaddr_in)) < 0) {
            close(tcp_sd);
            exit(1);
        }

        printf("Connected! Game Start!\n");

        // start game
        int flag;
        if (maybeContinue == 0) {
            initSharedState(board);
            flag = tictactoe_client(tcp_sd, 1, server_address_tcp, 0, board);
        } else if (maybeContinue == 1) {
            flag = tictactoe_client(tcp_sd, 1, server_address_tcp, 1, board);
        }
        
        // if this server is full of games, continue to connect to next server
        if (flag == GAME_FULL) {
            close(tcp_sd);
            continue;
        }
        if (flag == OK) {
            close(tcp_sd);
            break;
        }
        if (flag == CRASH) {
            maybeContinue = 1;
            goto mc;
        }
    }
    
    return 0;
}

