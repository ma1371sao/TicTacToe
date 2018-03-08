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
#include <time.h>

#include "game.h"

int tictactoeAI(char board[ROWS][COLUMNS]) {
    int i, j;

    // simple strategy for now: find the first empty slot to move
    for (i = 0; i < ROWS; i++) {
        for (j = 0; j < COLUMNS; j++) {
            if (board[i][j] >= '1' && board[i][j] <= '9') {
                printf("AI move:%d\n", i * 3 + j + 1);
                return i * 3 + j + 1;
            } else {
                continue;
            }
        }
    }    
    return -1;
}

void clear_lines(int count) {
    while (count > 0) {
        printf("\033[1A");
        printf("\033[K");
        count--;
    }
}

position* convert_move(protocol* data) {
    position* p = malloc(sizeof(position));
    
    p->row = (int)((data->move - 1) / ROWS);
    p->col = (data->move - 1) % COLUMNS;

    return p;
}

position* check_move(int player, protocol* data, char board[ROWS][COLUMNS]) {
    position* pos = convert_move(data);
    
    while (data->move < 0 || data->move > 9 || board[pos->row][pos->col] != (data->move + '0')) {
        printf("==========Invalid move=======\n");
        printf("Player %d, enter a number: ", player);
        getchar();
        scanf("%d", &data->move);

        pos = convert_move(data);
    }             

    return pos;
}

int get_valid_board(int* boards_valid) {
    int i;
    for (i = 0; i < BACKLOG; i++) {
        if (boards_valid[i] == 1) {
            return i;
        }
    }
    return -1;
}

int tictactoe_server(int sockfd, int player, struct sockaddr_in from_address) {
    protocol* send_data = malloc(sizeof(protocol));
    protocol* recv_data = malloc(sizeof(protocol));
    position* pos;
    socklen_t from_length = sizeof(from_address);
    char boards[BACKLOG][ROWS][COLUMNS];
    int boards_valid[BACKLOG];
    time_t boards_time[BACKLOG];

    // init boards
    int i;
    for(i = 0; i < BACKLOG; i++) {
        initSharedState(boards[i]);
        boards_valid[i] = 1; // 1: avialable    0: unavailable
        boards_time[i] = time(NULL);
    } 

    while(1) {
        // recv message 
        recv_data = recv_protocol(sockfd, (struct sockaddr *)&from_address, &from_length);
        // check time out
        int j;
        for (j = 0; j < BACKLOG; j++) {
            int time_take = time(NULL) - boards_time[j];
            printf("time taken: %d (game number: %d)\n", time_take, j);
            if (time_take >= TIMEOUT) {
                initSharedState(boards[j]);
                boards_valid[j] = 1;
                boards_time[j] = time(NULL);
            }
        }

        // command? NEWGAME or MOVE
        if (recv_data->command == NEWGAME) {
            // find an available slot
            int game_number = get_valid_board(boards_valid);
            if (game_number == -1) { // all slots are full
                send_data->command = NEWGAME;
                send_data->move = -1;
                send_data->game_no = FULL;
                send_protocol(sockfd, send_data, (struct sockaddr *)&from_address, sizeof(from_address));
            } else { // get a game number
                // set the board as unavailable
                boards_valid[game_number] = 0;
                // record the time of getting board
                boards_time[game_number] = time(NULL);

                // AI make a move
                send_data->move = tictactoeAI(boards[game_number]);
                send_data->command = MOVE;
                send_data->game_no = game_number;
                // apply ai's move to the corresponding board
                pos = convert_move(send_data);
                boards[game_number][pos->row][pos->col] = 'X';
                // send the message to client
                send_protocol(sockfd, send_data, (struct sockaddr *)&from_address, sizeof(from_address));
            }
        } else if (recv_data->command == MOVE) {
            // extract game number from the message
            int game_number = recv_data->game_no;
            // check the game_number
            // game number should be "unavailable" since it is already taken
            // if the game number is "available", then the board was free because of timeout
            if (boards_valid[game_number] == 1) {
                printf("Board %d is free because of timeout.\n", game_number);
                send_data->command = NEWGAME;
                send_data->move = -1;
                send_data->game_no = FULL;
                send_protocol(sockfd, send_data, (struct sockaddr *)&from_address, sizeof(from_address));
                continue;
            }
            // record the time of move
            boards_time[game_number] = time(NULL);

            // apply client's move to the local board
            pos = convert_move(recv_data);
            if (recv_data->move < 0 || recv_data->move > 9 || boards[recv_data->game_no][pos->row][pos->col] != (recv_data->move + '0')) {
                printf("Bad move from client. Game number: %d\n", recv_data->game_no);
                continue;
            }
            boards[game_number][pos->row][pos->col] = 'O';

            // check if anyone wins after applying client's move
            if (checkwin(boards[game_number]) != -1) {
                // display winner
                print_winner(boards[game_number]);
                // game ends, re-init the board
                initSharedState(boards[game_number]);
                boards_valid[game_number] = 1;
                boards_time[game_number] = time(NULL);
                continue;
            }

            // AI make a move
            send_data->move = tictactoeAI(boards[game_number]);
            send_data->command = MOVE;
            send_data->game_no = game_number;
            // apply ai's move to the corresponding board
            pos = convert_move(send_data);
            boards[game_number][pos->row][pos->col] = 'X';
            // send the message to client
            send_protocol(sockfd, send_data, (struct sockaddr *)&from_address, sizeof(from_address));
            // check if anyone wins after applying AI's move
            if (checkwin(boards[game_number]) != -1) {
                // display winner
                print_winner(boards[game_number]);
                // game ends, re-init the board
                initSharedState(boards[game_number]);
                boards_valid[game_number] = 1;
                boards_time[game_number] = time(NULL);
                continue;
            }
        } else {
            printf("Invalid command from client: %d\n", recv_data->command);
            continue;
        }

    }

    free(send_data);
    free(recv_data);
    free(pos);

    return 0;
}

int tictactoe_client(int sockfd, int player, struct sockaddr_in from_address) {
    protocol* recv_data = malloc(sizeof(protocol));
    protocol* send_data = malloc(sizeof(protocol));
    position* pos;
    int game;

    // init board
    char board[ROWS][COLUMNS];
    initSharedState(board);

    // send a NEWGAME request to server
    send_data->command = NEWGAME;
    send_data->move = -1; // does not matter, client does not make move at this time
    send_data->game_no = -1; // does not matter, the game number would be assigned by server
    send_protocol(sockfd, send_data, (struct sockaddr *)&from_address, sizeof(from_address));
    printf("Sent a new game request.\n");

    struct pollfd mypoll = { STDIN_FILENO, POLLIN|POLLPRI };

    // start receiving move from server
    do {
        printf("Waiting for server...\n");

        // receive message from server
        recv_data = recv_protocol(sockfd, NULL, NULL);

        if (recv_data->game_no == FULL) { // game slot full
            printf("No more game slot at the server side. Try again later!\n");
            exit(1);
        } else if (recv_data->command == MOVE){ // game slot not full and command correct, start playing
            // get game number
            game = recv_data->game_no;
            printf("Game start! Game Number: %d\n", game);

            // apply move of server to the local board
            pos = convert_move(recv_data);
            board[pos->row][pos->col] = 'X';
            // print the new move of server
            print_board(board);
            // check if anyone wins after applying server's move
            if (checkwin(board) != -1) break;

            // read move of client 
            printf("Player %d, enter a number within %ds:\n", player, TIMEOUT);
            //scanf("%d", &send_data->move);
            if (poll(&mypoll, 1, TIMEOUT * 1000)) {
                scanf("%d", &send_data->move); 
            } else {
                printf("Input timeout! You loss!\n");
                close(sockfd);
                exit(1);
            }
            // check and apply move of client 
            pos = check_move(player, send_data, board);
            board[pos->row][pos->col] = 'O';
            // print the new move of client
            print_board(board);

            // send message to server
            send_data->command = MOVE;
            send_data->game_no = game;
            send_protocol(sockfd, send_data, (struct sockaddr *)&from_address, sizeof(from_address));
        } else {
            printf("Invalid command from server: NEWGAME\n");
            exit(1);
        }
    } while(checkwin(board) == -1);

    // determine which one wins
    print_winner(board);

    free(send_data);
    free(recv_data);
    free(pos);

    return 0;
}

void print_winner(char board[ROWS][COLUMNS]) {
    // determine which one wins
    if (checkwin(board) == 0) {
        printf("==>\aGame draw");
    } else {
        printf("==>\aPlayer %d wins\n", checkwin(board));
    }
}

void serialize_protocol(char* buffer, protocol* data) {
    uint32_t num1 = htonl(data->command);
    uint32_t num2 = htonl(data->move);
    uint32_t num3 = htonl(data->game_no);

    memcpy(buffer, &num1, 4);
    memcpy(buffer+4, &num2, 4);
    memcpy(buffer+8, &num3, 4);
}

void deserialize_protocol(char* buffer, protocol* data) {
    int num1, num2, num3;

    memcpy(&num1, buffer, 4);
    memcpy(&num2, buffer+4, 4);
    memcpy(&num3, buffer+8, 4);

    data->command = ntohl(num1);
    data->move = ntohl(num2);
    data->game_no = ntohl(num3);
}

void send_protocol(int sockfd, protocol* data, struct sockaddr *addr, socklen_t len) {
    // uint32_t conv = htonl(data->move);
    char sendBuffer[12];
    serialize_protocol(sendBuffer, data);
    int rc = sendto(sockfd, &sendBuffer, sizeof(sendBuffer), 0, addr, len);
    printf("send_protocol: %d\n", rc);
    printf("Send message(Command: %d, Move: %d, Game Number: %d)\n", data->command, data->move, data->game_no);
}

protocol* recv_protocol(int sockfd, struct sockaddr *addr, socklen_t *len) {
    // int rc, num; 
    char receiveBuffer[12];
    protocol* data;
    int rc = recvfrom(sockfd, &receiveBuffer, sizeof(receiveBuffer), 0, addr, len);
    if (rc <= 0) {
        printf("The other side crashed!\n");
        exit(1);
    }
    printf("recv_protocol: %d\n", rc);
    data = malloc(sizeof(protocol));

    deserialize_protocol(receiveBuffer, data);
    printf("Recv message(Command: %d, Move: %d, Game Number: %d)\n", data->command, data->move, data->game_no);

    return data;
}

void print_board(char board[ROWS][COLUMNS])
{
  printf("\n\n\n\tCurrent TicTacToe Game\n\n");

  printf("Player 1 (X)  -  Player 2 (O)\n\n\n");


  printf("     |     |     \n");
  printf("  %c  |  %c  |  %c \n", board[0][0], board[0][1], board[0][2]);

  printf("_____|_____|_____\n");
  printf("     |     |     \n");

  printf("  %c  |  %c  |  %c \n", board[1][0], board[1][1], board[1][2]);

  printf("_____|_____|_____\n");
  printf("     |     |     \n");

  printf("  %c  |  %c  |  %c \n", board[2][0], board[2][1], board[2][2]);

  printf("     |     |     \n\n");
}

int initSharedState(char board[ROWS][COLUMNS]){    
  /* this just initializing the shared state aka the board */
  int i, j, count = 1;
  //printf ("in sharedstate area\n");
  for (i=0;i<3;i++)
    for (j=0;j<3;j++){
      board[i][j] = count + '0';
      //printf("board[%d][%d]=%c\n", i, j, board[i][j]);
      count++;
    }

  return 0;

}

int checkwin(char board[ROWS][COLUMNS])
{
  /************************************************************************/
  /* brute force check to see if someone won, or if there is a draw       */
  /* return a 0 if the game is 'over' and return -1 if game should go on  */
  /************************************************************************/
  if (board[0][0] == board[0][1] && board[0][1] == board[0][2] ) // row matches
    return winner(board[0][0]);
        
  else if (board[1][0] == board[1][1] && board[1][1] == board[1][2] ) // row matches
    return winner(board[1][0]);
        
  else if (board[2][0] == board[2][1] && board[2][1] == board[2][2] ) // row matches
    return winner(board[2][0]);
        
  else if (board[0][0] == board[1][0] && board[1][0] == board[2][0] ) // column
    return winner(board[0][0]);
        
  else if (board[0][1] == board[1][1] && board[1][1] == board[2][1] ) // column
    return winner(board[0][1]);
        
  else if (board[0][2] == board[1][2] && board[1][2] == board[2][2] ) // column
    return winner(board[0][2]);
        
  else if (board[0][0] == board[1][1] && board[1][1] == board[2][2] ) // diagonal
    return winner(board[0][0]);
        
  else if (board[2][0] == board[1][1] && board[1][1] == board[0][2] ) // diagonal
    return winner(board[2][0]);
        
  else if (board[0][0] != '1' && board[0][1] != '2' && board[0][2] != '3' &&
	   board[1][0] != '4' && board[1][1] != '5' && board[1][2] != '6' && 
	   board[2][0] != '7' && board[2][1] != '8' && board[2][2] != '9')

    return 0; // Return of 0 means game over
  else
    return  -1; // return of -1 means keep playing
}

int winner(char mark) {
    if (mark == 'X') {
        return 1;
    } else if (mark == 'O') {
        return 2;
    }
    return -1;
}

