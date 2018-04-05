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
                printf("[INFO] AI move:%d\n", i * 3 + j + 1);
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
        printf("[ERROR] Invalid moven\n");
        printf("-------------Player %d, enter a number: --------------", player);
        getchar();
        scanf("%d", &data->move);

        pos = convert_move(data);
    }             

    return pos;
}

int get_valid_board(board_info *info[BACKLOG]) {
    int i;
    for (i = 0; i < BACKLOG; i++) {
        if (info[i]->valid == 1) {
            return i;
        }
    }
    return -1;
}

void init_board_info(board_info* info) {
    initSharedState(info->board);
    info->valid = 1;
    info->time = time(NULL); 
    info->is_draw = 0;
    info->sk_index = -1;
}

int tictactoe_server(int sockfd, int player, struct sockaddr_in from_address) {
    int clientSDList[MAXCLIENTS] = {0};
    fd_set socketFDS;
    int maxSD = sockfd;
    int connected_sd;

    protocol* send_data = malloc(sizeof(protocol));
    protocol* recv_data = malloc(sizeof(protocol));
    position* pos;
    socklen_t from_length = sizeof(from_address);
    board_info *boards[BACKLOG];
    int rc;

    // init boards
    for(int i = 0; i < BACKLOG; i++) {
        boards[i] = malloc(sizeof(board_info));
        init_board_info(boards[i]);
    } 


    while(1) {
        FD_ZERO(&socketFDS);
        // set the bit for the initial SD
        FD_SET(sockfd, &socketFDS);
        for (int i = 0; i < MAXCLIENTS; i++) {
            if (clientSDList[i] > 0) {
                FD_SET(clientSDList[i], &socketFDS);
                if (clientSDList[i] > maxSD)
                    maxSD = clientSDList[i];
            }
        }
        // block until something arrives
        rc = select(maxSD + 1, &socketFDS, NULL, NULL, NULL);
        // accept connect request
        if (FD_ISSET(sockfd, &socketFDS)) {
            connected_sd = accept(sockfd, (struct sockaddr*) &from_address, &from_length);
            for (int i = 0; i < MAXCLIENTS; i++) {
                if (clientSDList[i] == 0) {
                    clientSDList[i] = connected_sd;
                    break;
                }
            }
        }
        // read data
        for (int i = 0; i < MAXCLIENTS; i++) {
            if (FD_ISSET(clientSDList[i], &socketFDS)) {
                // recv message 
                rc = recv_protocol(clientSDList[i], recv_data);
                if (rc == 0) {
                    close(clientSDList[i]);
                    clientSDList[i] = 0;
                }
                else {
                    // check time out
                    for (int j = 0; j < BACKLOG; j++) {
                        int time_take = time(NULL) - boards[j]->time;
                        printf("[INFO] Time taken: %d (game number: %d, valid: %d)\n", time_take, j, boards[j]->valid);
                        if (time_take >= TIMEOUT && boards[j]->valid == 0) {
                            printf("[INFO] No response in game %d. Re-initializing the board\n", j);
                            printf("----------Before init ----------\n");
                            print_board(boards[j]->board);

                            printf("----------After init --------\n");
                            init_board_info(boards[j]);
                            print_board(boards[j]->board);
                        }
                    }
                    //if (rc == -1) continue;

                    // command? 
                    if (recv_data->command == NEWGAME) {
                        // find an available slot
                        int game_number = get_valid_board(boards);
                        if (game_number == -1) { // all slots are full
                            send_data->command = NEWGAME;
                            send_data->move = -1;
                            send_data->game_no = FULL;
                            send_protocol(clientSDList[i], send_data);
                        } else { // get a game number
                            // set the board as unavailable
                            boards[game_number]->valid = 0;
                            // record the time of getting board
                            boards[game_number]->time = time(NULL);
                            // record index of sock list
                            boards[game_number]-> sk_index = clientSDList[i];
                            // respond NEWGAME ACK
                            send_data->move = -1;
                            send_data->command = NEWGAME;
                            send_data->game_no = game_number;
                            send_protocol(clientSDList[i], send_data);
                        }
                    } else if (recv_data->command == MOVE) {
                        // extract game number from the message
                        int game_number = recv_data->game_no;
                        // check if the board is available and if the sk value matches to avoid mixing board for different clients
                        if (boards[game_number]->valid == 1 || clientSDList[i] != boards[game_number]->sk_index) {
                            printf("[ERROR] Board %d is free because of timeout.\n", game_number);
                            print_board(boards[game_number]->board);
                            send_data->command = NEWGAME;
                            send_data->move = -1;
                            send_data->game_no = FULL;
                            send_protocol(clientSDList[i], send_data);
                            continue;
                        }
                        // record the time of move
                        boards[game_number]->time = time(NULL);

                        // apply client's move to the local board
                        pos = convert_move(recv_data);

                        // check bad move
                        if (recv_data->move < 0 || recv_data->move > 9 || boards[recv_data->game_no]->board[pos->row][pos->col] != (recv_data->move + '0')) {
                            printf("[ERROR] Bad/Dup move from client. Game number: %d\n", recv_data->game_no);
                            print_board(boards[game_number]->board);
                            continue;
                        }
                        boards[game_number]->board[pos->row][pos->col] = 'X';

                        // check if anyone wins after applying client's move
                        if (checkwin(boards[game_number]->board) != -1) {
                            // server is loser
                            // display winner
                            print_winner(boards[game_number]->board);
                            // send ENDGAME command to client
                            send_data->command = ENDGAME;
                            send_data->move = -1;
                            send_data->game_no = game_number;
                            send_protocol(clientSDList[i], send_data);
                            continue;
                        }

                        // AI make a move
                        send_data->move = tictactoeAI(boards[game_number]->board);
                        send_data->command = MOVE;
                        send_data->game_no = game_number;
                        // apply ai's move to the corresponding board
                        pos = convert_move(send_data);
                        boards[game_number]->board[pos->row][pos->col] = 'O';
                        // send the message to client
                        send_protocol(clientSDList[i], send_data);
            
                        // check if anyone wins after applying AI's move
                        if (checkwin(boards[game_number]->board) != -1) {
                            if (checkwin(boards[game_number]->board) == 0) {
                                boards[game_number]->is_draw = 1;
                            }
                            // display winner
                            print_winner(boards[game_number]->board);
                            continue;
                        }
                    } else if (recv_data->command == ENDGAME) {
                        int game_number = recv_data->game_no;
                        int win = checkwin(boards[game_number]->board);
                        if (win == 2) {
                            // server is winner
                            // game ends, re-init the board
                            init_board_info(boards[game_number]);
                            // send ENDGAME ACK to client
                            send_data->command = ENDGAME;
                            send_data->move = -1;
                            send_data->game_no = game_number;
                            send_protocol(clientSDList[i], send_data);
                            continue;
                        } else if (win == 1) {
                            // server is loser
                            // receive the ENDGAME ACK from winner
                            // game ends, re-init the board
                            init_board_info(boards[game_number]);
                            continue;
                        } else if (win == 0) { // game draw
                            if (boards[game_number]->is_draw == 1) {
                                // if this case, this side is the one who first detect the game draw, which is similar to the "winner" in ENDGAME handshake
                                // game ends, re-init the board
                                init_board_info(boards[game_number]);
                                // send ENDGAME ACK to client
                                send_data->command = ENDGAME;
                                send_data->move = -1;
                                send_data->game_no = game_number;
                                send_protocol(clientSDList[i], send_data);
                                continue;
                            } else {
                                // if this case, this side is the one who last detect the game draw, which is similar to the "loser" in ENDGAME handshake
                                // receive the ENDGAME ACK from client
                                // game ends, re-init the board
                                init_board_info(boards[game_number]);
                                continue;
                            }
                        } 
                    } else {
                        printf("[ERROR] Invalid command from client: %d\n", recv_data->command);
                        continue;
                    }
                }
            }
        }  
    }

    free(send_data);
    free(recv_data);
    free(pos);
    for(int i = 0; i < BACKLOG; i++) {
        free(boards[i]);
    } 

    return 0;
}

int tictactoe_client(int sockfd, int player, struct sockaddr_in from_address) {
    protocol* recv_data = malloc(sizeof(protocol));
    protocol* send_data = malloc(sizeof(protocol));
    position* pos;
    int game;
    int time_take, last_time;
    int rc;
    int is_draw;

    // init board
    char board[ROWS][COLUMNS];
    initSharedState(board);
    last_time = time(NULL);
    is_draw = 0;

    // send a NEWGAME request to server
    send_data->command = NEWGAME;
    send_data->move = -1; // does not matter, client does not make move at this time
    send_data->game_no = -1; // does not matter, the game number would be assigned by server
    send_protocol(sockfd, send_data);

    printf("[INFO] Sent a new game request.\n");

    // struct pollfd mypoll = { STDIN_FILENO, POLLIN|POLLPRI };

    // start receiving move from server
    while(1) {
        printf("[INFO] Waiting for server...\n");

        // receive message from server
        rc = recv_protocol(sockfd, recv_data);

        // check timeout
        time_take = time(NULL) - last_time;
        printf("[INFO] time taken:%d\n", time_take);
        if (time_take >= TIMEOUT) {
            printf("[INFO] Timeout, exit!\n");
            exit(1);
        }
        if (rc == -1) continue;


        if (recv_data->game_no == FULL) { // game slot full
            printf("[ERROR] No more game slot at the server side. Try again later!\n");
            exit(1);
        } else if (recv_data->command == ENDGAME) {
            int win = checkwin(board);
            if (win == 2) {
                // client is loser
                // receive ENDGAME ACK from server
                // game ends
                print_board(board);
                break;
            } else if (win == 1) {
                // client is winner
                // receive ENDGAME command from server
                // send ENDGAME ACK to server
                send_data->command = ENDGAME;
                send_data->move = -1;
                send_data->game_no = recv_data->game_no;
                send_protocol(sockfd, send_data);
                break;
            } else if (win == 0) {
                if (is_draw == 1) {
                    // client is the one who fist detect the game draw
                    // similar with "winner"
                    // send ENDGAME ACK
                    send_data->command = ENDGAME;
                    send_data->move = -1;
                    send_data->game_no = recv_data->game_no;
                    send_protocol(sockfd, send_data);
                    break;
                } else {
                    // client is the one who last detect the game draw
                    // similar with "loser"
                    // game ends
                    print_board(board);
                    break;
                }
            
            } else {
                printf("Invalid command from client: %d\n", recv_data->command);
                continue;
            }
        } else if (recv_data->command == MOVE || recv_data->command == NEWGAME) { 
            // game slot not full and command correct, start playing
            // get game number
            game = recv_data->game_no;
            printf("[INFO] Game Number: %d\n", game);

            // reset last_time
            last_time = time(NULL);

            if (recv_data->command == NEWGAME) {
                print_board(board);
            }

            if (recv_data->command == MOVE) {
                // apply move of server to the local board
                pos = convert_move(recv_data);

                // check bad move
                if (recv_data->move < 0 || recv_data->move > 9 || board[pos->row][pos->col] != (recv_data->move + '0')) {
                    printf("[ERROR] Bad move from server\n");
                    continue;
                }

                board[pos->row][pos->col] = 'O';
                // print the new move of server
                print_board(board);
                // check if anyone wins after applying server's move
                if (checkwin(board) != -1) {
                    // client is loser
                    // send ENDGAME command to server
                    send_data->command = ENDGAME;
                    send_data->move = -1;
                    send_data->game_no = game;
                    send_protocol(sockfd, send_data);
                    // waiting for ENDGAME ACK from server
                    continue;
                }
            }

            // read move of client 
            printf("-----------Player %d, enter a number within %ds: ------------\n", player, TIMEOUT);
            //scanf("%d", &send_data->move);
//            if (poll(&mypoll, 1, TIMEOUT * 1000)) {
                scanf("%d", &send_data->move); 
//            } else {
//                printf("Input timeout! You loss!\n");
//                close(sockfd);
//                exit(1);
//            }
            // check and apply move of client 
            pos = check_move(player, send_data, board);
            board[pos->row][pos->col] = 'X';
            // print the new move of client
            print_board(board);

            // check game draw
            if (checkwin(board) == 0) {
                is_draw = 1;
            }

            // send message to server
            send_data->command = MOVE;
            send_data->game_no = game;
            send_protocol(sockfd, send_data);

        } else {
            printf("[ERROR] Invalid command from server.\n");
            exit(1);
        }
    }

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

int send_protocol(int sockfd, protocol* data) {
    char sendBuffer[12];
    serialize_protocol(sendBuffer, data);
    int rc = write(sockfd, &sendBuffer, sizeof(sendBuffer));
    printf("[INFO] Send %d bytes message(Command: %d, Move: %d, Game Number: %d)\n", rc, data->command, data->move, data->game_no);
    return rc;
}

int recv_protocol(int sockfd, protocol* data) {
    char receiveBuffer[12];
    memset(receiveBuffer, 0, sizeof(receiveBuffer));
    int rc = read(sockfd, &receiveBuffer, sizeof(receiveBuffer));
    deserialize_protocol(receiveBuffer, data);
    printf("[INFO] Recv %d bytes message(Command: %d, Move: %d, Game Number: %d)\n", rc, data->command, data->move, data->game_no);
    return rc;
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
