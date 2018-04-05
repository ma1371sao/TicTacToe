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
    info->num_timeout = 0;
    info->num_dup = 0;

    info->is_draw = 0;
}

int tictactoe_server(int sockfd, int player, struct sockaddr_in from_address) {
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

    srand(time(NULL));

    while(1) {
        // recv message 
        rc = recv_protocol(sockfd, (struct sockaddr *)&from_address, &from_length, recv_data);
        // check time out
        for (int j = 0; j < BACKLOG; j++) {
            int time_take = time(NULL) - boards[j]->time;
            printf("time taken: %d (game number: %d, valid: %d)\n", time_take, j, boards[j]->valid);
            if (time_take >= TIMEOUT && boards[j]->valid == 0) {
                if (boards[j]->num_timeout >= 5) {
                    printf("No response, timeout\n");
                    init_board_info(boards[j]);
                    boards[j]->num_timeout = 0;
                    boards[j]->num_dup = 0;
                } else {
                    printf("Resend recent data, Command: %d, Move: %d, Game Num: %d \n", boards[j]->recent_data->command, boards[j]->recent_data->move, boards[j]->recent_data->game_no);
                    send_protocol(sockfd, boards[j]->recent_data, (struct sockaddr*)&(boards[j]->addr), sizeof(boards[j]->addr));
                    boards[j]->num_timeout += 1;
                    boards[j]->time = time(NULL);
                    printf("game %d num_timeout=%d\n", j, boards[j]->num_timeout);
                }
            }
        }
        if (rc == -1) continue;

        // command? 
        if (recv_data->command == NEWGAME) {
            // find an available slot
            int game_number = get_valid_board(boards);
            if (game_number == -1) { // all slots are full
                send_data->command = NEWGAME;
                send_data->move = -1;
                send_data->game_no = FULL;
                send_protocol(sockfd, send_data, (struct sockaddr *)&from_address, sizeof(from_address));
            } else { // get a game number
                // set the board as unavailable
                boards[game_number]->valid = 0;
                // record the time of getting board
                boards[game_number]->time = time(NULL);
                // init resend info
                boards[game_number]->num_timeout = 0;
                boards[game_number]->num_dup = 0;
                boards[game_number]->addr = from_address;
                boards[game_number]->recent_data = malloc(sizeof(protocol));
                // respond NEWGAME ACK
                send_data->move = -1;
                send_data->command = NEWGAME;
                send_data->game_no = game_number;
                send_protocol(sockfd, send_data, (struct sockaddr *)&from_address, sizeof(from_address));
                // record recent data
                memcpy(boards[game_number]->recent_data, send_data, sizeof(protocol));
                printf("Record recent Command:%d, Move:%d, Game number:%d\n", boards[game_number]->recent_data->command, boards[game_number]->recent_data->move, boards[game_number]->recent_data->game_no);
            }
        } else if (recv_data->command == MOVE) {
            // extract game number from the message
            int game_number = recv_data->game_no;
            // check the game_number
            // game number should be "unavailable" since it is already taken
            // if the game number is "available", then the board was free because of timeout
            // TODO: need to check from address to avoid mix board
            // e.g. one client may not have timeout at his side and still send move after his board is taken by another
            if (boards[game_number]->valid == 1) {
                printf("Board %d is free because of timeout.\n", game_number);
                send_data->command = NEWGAME;
                send_data->move = -1;
                send_data->game_no = FULL;
                send_protocol(sockfd, send_data, (struct sockaddr *)&from_address, sizeof(from_address));
                continue;
            }
            // record the time of move
            boards[game_number]->time = time(NULL);
            // reset num_timeout and num_dup
            boards[game_number]->num_timeout = 0;

            // apply client's move to the local board
            pos = convert_move(recv_data);
            // check dup/bad move
            if (recv_data->move < 0 || recv_data->move > 9 || boards[recv_data->game_no]->board[pos->row][pos->col] != (recv_data->move + '0')) {
                printf("Bad/Dup move from client. Game number: %d\n", recv_data->game_no);
                if (boards[game_number]->num_dup >= 5) {
                    printf("More than 5 dup, re-init game %d\n", game_number);
                    init_board_info(boards[game_number]);
                    boards[game_number]->num_timeout = 0;
                    boards[game_number]->num_dup = 0;
                } else {
                    printf("Resend recent data, Command: %d, Move: %d, Game Num: %d \n", boards[game_number]->recent_data->command, boards[game_number]->recent_data->move, boards[game_number]->recent_data->game_no);

                    send_protocol(sockfd, boards[game_number]->recent_data, (struct sockaddr*)&(boards[game_number]->addr), sizeof(boards[game_number]->addr));
                    boards[game_number]->num_dup += 1;
                }
                continue;
            }
            boards[game_number]->num_dup = 0;
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
                send_protocol(sockfd, send_data, (struct sockaddr *)&from_address, sizeof(from_address));

                memcpy(boards[game_number]->recent_data, send_data, sizeof(protocol));
                printf("Record recent Command:%d, Move:%d, Game number:%d\n", boards[game_number]->recent_data->command, boards[game_number]->recent_data->move, boards[game_number]->recent_data->game_no);

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
            send_protocol(sockfd, send_data, (struct sockaddr *)&from_address, sizeof(from_address));
            
            memcpy(boards[game_number]->recent_data, send_data, sizeof(protocol));

            // check if anyone wins after applying AI's move
            if (checkwin(boards[game_number]->board) != -1) {
                if (checkwin(boards[game_number]->board) == 0) {
                    boards[game_number]->is_draw = 1;
                }
                // server is winner
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
                send_protocol(sockfd, send_data, (struct sockaddr *)&from_address, sizeof(from_address));
                
                memcpy(boards[game_number]->recent_data, send_data, sizeof(protocol));

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
                    send_protocol(sockfd, send_data, (struct sockaddr *)&from_address, sizeof(from_address));
                
                    memcpy(boards[game_number]->recent_data, send_data, sizeof(protocol));
                    continue;
                } else {
                    // if this case, this side is the one who last detect the game draw, which is similar to the "loser" in ENDGAME handshake
                    // receive the ENDGAME ACK from client
                    // game ends, re-init the board
                    init_board_info(boards[game_number]);
                    continue;
                } 
            
            } else {
                printf("Invalid command from client: %d\n", recv_data->command);
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
    for(int i = 0; i < BACKLOG; i++) {
        free(boards[i]);
    } 

    return 0;
}

int tictactoe_client(int sockfd, int player, struct sockaddr_in from_address) {
    protocol* recv_data = malloc(sizeof(protocol));
    protocol* send_data = malloc(sizeof(protocol));
    protocol* recent_data = malloc(sizeof(protocol));
    position* pos;
    int game;
    int time_take, last_time, num_timeout, num_dup;
    int rc;
    int is_draw;
    int is_start;

    // init board
    char board[ROWS][COLUMNS];
    initSharedState(board);
    last_time = time(NULL);
    num_timeout = 0;
    num_dup = 0;
    is_draw = 0;
    is_start = 0;

    srand(time(NULL));

    // send a NEWGAME request to server
    send_data->command = NEWGAME;
    send_data->move = -1; // does not matter, client does not make move at this time
    send_data->game_no = -1; // does not matter, the game number would be assigned by server
    send_protocol(sockfd, send_data, (struct sockaddr *)&from_address, sizeof(from_address));
    memcpy(recent_data, send_data, sizeof(protocol));

    printf("Sent a new game request.\n");

//    struct pollfd mypoll = { STDIN_FILENO, POLLIN|POLLPRI };

    // start receiving move from server
    while (1) {
        printf("Waiting for server...\n");

        // receive message from server
        rc = recv_protocol(sockfd, NULL, NULL, recv_data);

        // check timeout
        time_take = time(NULL) - last_time;
        printf("time taken:%d\n", time_take);
        if (time_take >= TIMEOUT) {
            if (num_timeout >= 5) {
                printf("More than 5 timeout, exit!\n");
                exit(1);
            } else {
                printf("Timeout, Resend data, Command:%d, Move:%d, Game num:%d\n", recent_data->command, recent_data->move, recent_data->game_no);
                send_protocol(sockfd, recent_data, (struct sockaddr*)&from_address, sizeof(from_address));
                last_time = time(NULL);
                num_timeout += 1;
            }
        }
        if (rc == -1) continue;


        if (recv_data->game_no == FULL) { // game slot full
            printf("No more game slot at the server side. Try again later!\n");
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
                send_protocol(sockfd, send_data, (struct sockaddr *)&from_address, sizeof(from_address));
                memcpy(recent_data, send_data, sizeof(protocol));

                break;
            } else if (win == 0) {
                if (is_draw == 1) {
                    // client is the one who fist detect the game draw
                    // similar with "winner"
                    // send ENDGAME ACK
                    send_data->command = ENDGAME;
                    send_data->move = -1;
                    send_data->game_no = recv_data->game_no;
                    send_protocol(sockfd, send_data, (struct sockaddr *)&from_address, sizeof(from_address));
                    memcpy(recent_data, send_data, sizeof(protocol));

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
            printf("Game start! Game Number: %d\n", game);

            // reset last_time, num_timeout and num_dup
            last_time = time(NULL);
            num_timeout = 0;

            if (recv_data->command == NEWGAME) {
                print_board(board);
                if (is_start == 0) {
                    is_start = 1;
                } else {
                    printf("[ERROR] Received NEWGAME again from server. Ignore.\n");
                    send_protocol(sockfd, recent_data, (struct sockaddr*)&from_address, sizeof(from_address));

                    continue;
                }
            }

            if (recv_data->command == MOVE) {
                // apply move of server to the local board
                pos = convert_move(recv_data);

                // check dup/bad move
                if (recv_data->move < 0 || recv_data->move > 9 || board[pos->row][pos->col] != (recv_data->move + '0')) {
                    printf("Bad/Dup move from server\n");
                    if (num_dup >= 5) {
                        printf("More than 5 dup, exit\n");
                        exit(1);
                    } else {
                        send_protocol(sockfd, recent_data, (struct sockaddr*)&from_address, sizeof(from_address));
                        num_dup += 1;
                    }
                    continue;
                }
                num_dup = 0;

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
                    send_protocol(sockfd, send_data, (struct sockaddr *)&from_address, sizeof(from_address));
                    memcpy(recent_data, send_data, sizeof(protocol));

                    // waiting for ENDGAME ACK from server
                    continue;
                }
            }

            // read move of client 
            printf("Player %d, enter a number within %ds:\n", player, TIMEOUT);
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
            send_protocol(sockfd, send_data, (struct sockaddr *)&from_address, sizeof(from_address));
            memcpy(recent_data, send_data, sizeof(protocol));

        } else {
            printf("Invalid command from server: NEWGAME\n");
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

int send_protocol(int sockfd, protocol* data, struct sockaddr *addr, socklen_t len) {
    // uint32_t conv = htonl(data->move);
    char sendBuffer[12];
    serialize_protocol(sendBuffer, data);
    int rc = sendto(sockfd, &sendBuffer, sizeof(sendBuffer), 0, addr, len);
    printf("send_protocol: %d\n", rc);
    printf("Send message(Command: %d, Move: %d, Game Number: %d)\n", data->command, data->move, data->game_no);
    return rc;
}

int recv_protocol(int sockfd, struct sockaddr *addr, socklen_t *len, protocol* data) {
    // int rc, num; 
    char receiveBuffer[12];
    int rc = recvfrom(sockfd, &receiveBuffer, sizeof(receiveBuffer), 0, addr, len);
    printf("recv_protocol: %d\n", rc);

    deserialize_protocol(receiveBuffer, data);
    printf("Recv message(Command: %d, Move: %d, Game Number: %d)\n", data->command, data->move, data->game_no);

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
