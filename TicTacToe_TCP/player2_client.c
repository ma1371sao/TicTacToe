/**********************************************************/
/* This program is a 'pass and play' version of tictactoe */
/* Two users, player 1 and player 2, pass the game back   */
/* and forth, on a single computer                        */
/**********************************************************/

/* include files go here */
#include <stdio.h>
#include <strings.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>

/* #define section, for now we will define the number of rows and columns */
#define ROWS  3
#define COLUMNS  3


/* C language requires that you predefine all the routines you are writing */
int checkwin(char board[ROWS][COLUMNS]);
void print_board(char board[ROWS][COLUMNS]);
int tictactoe();
int initSharedState(char board[ROWS][COLUMNS]);
int portNumber;
char serverIP[29];

int main(int argc, char *argv[])
{
  if (argc != 3) {
    printf("Invalid number of command line arguments\n");
    exit(1);
  }

  int rc;
  char board[ROWS][COLUMNS]; 
  strcpy(serverIP, argv[2]);
  portNumber = strtol(argv[1], NULL, 10);

  rc = initSharedState(board); // Initialize the 'game' board
  rc = tictactoe(board); // call the 'game' 
  return 0; 
}


int tictactoe(char board[ROWS][COLUMNS])
{
  /* this is the meat of the game, you'll look here for how to change it up */
  int sd;
  struct sockaddr_in server_address;
  int connected_sd;
  socklen_t fromLength;
  int status;

  sd = socket(AF_INET, SOCK_STREAM, 0);
  if (sd == -1) {
    perror("server: socket");
    exit(1);
  }

  server_address.sin_family = AF_INET;
  server_address.sin_port = htons(portNumber);
  server_address.sin_addr.s_addr = inet_addr(serverIP);

  if (connect(sd, (struct sockaddr *)&server_address, sizeof(struct sockaddr_in)) < 0) {
    close(sd);
    perror("error connecting stream socket");
    exit(1);
  }
  printf("Connected! Game Start!\n");

  int player = 2; // keep track of whose turn it is
  int i, choice;  // used for keeping track of choice user makes
  int row, column;
  char mark = 'O';      // either an 'x' or an 'o'
  int conv;
  /* loop, first print the board, then ask player 'n' to make a move */

  do {
    print_board(board);
    player = 1;
    status = read(sd, &conv, sizeof(conv));
    if (status <= 0) {
        printf("Player1 disconnected! Game Over!\n");
        close(sd);
        exit(0);
      }
    int player1_choice = ntohl(conv);
    row = (int)((player1_choice - 1) / ROWS); 
    column = (player1_choice - 1) % COLUMNS;
    board[row][column] = 'X';
    i = checkwin(board);
    if (i != -1) break;
    player = 2;
    print_board(board); // call function to print the board on the screen
    while (1) {
      printf("Player %d, enter a number:  ", player); // print out player so you can pass game
      scanf("%d", &choice); //using scanf to get the choice

      /******************************************************************/
      /** little math here. you know the squares are numbered 1-9, but  */
      /* the program is using 3 rows and 3 columns. We have to do some  */
      /* simple math to conver a 1-9 to the right row/column            */
      /******************************************************************/
      row = (int)((choice-1) / ROWS); 
      column = (choice-1) % COLUMNS;

      /* first check to see if the row/column chosen is has a digit in it, if it */
      /* square 8 has and '8' then it is a valid choice                          */

      if (choice < 10 && choice > 0 && board[row][column] == (choice+'0')) {
        board[row][column] = mark;
        int conv = htonl(choice);
        status = write(sd, &conv, sizeof(conv));
        break;
      }
      else {
	      printf("Invalid move\n");
	      getchar();
      }
    }
    /* after a move, check to see if someone won! (or if there is a draw */
    i = checkwin(board);
    
  } while (i ==  - 1); // -1 means no one won
    
  /* print out the board again */
  print_board(board);
    
  if (i == 1) // means a player won!! congratulate them
    printf("==>\aPlayer %d wins\n ", player);
  else
    printf("==>\aGame draw"); // ran out of squares, it is a draw
  close(sd);
  return 0;
}


int checkwin(char board[ROWS][COLUMNS])
{
  /************************************************************************/
  /* brute force check to see if someone won, or if there is a draw       */
  /* return a 0 if the game is 'over' and return -1 if game should go on  */
  /************************************************************************/
  if (board[0][0] == board[0][1] && board[0][1] == board[0][2] ) // row matches
    return 1;
        
  else if (board[1][0] == board[1][1] && board[1][1] == board[1][2] ) // row matches
    return 1;
        
  else if (board[2][0] == board[2][1] && board[2][1] == board[2][2] ) // row matches
    return 1;
        
  else if (board[0][0] == board[1][0] && board[1][0] == board[2][0] ) // column
    return 1;
        
  else if (board[0][1] == board[1][1] && board[1][1] == board[2][1] ) // column
    return 1;
        
  else if (board[0][2] == board[1][2] && board[1][2] == board[2][2] ) // column
    return 1;
        
  else if (board[0][0] == board[1][1] && board[1][1] == board[2][2] ) // diagonal
    return 1;
        
  else if (board[2][0] == board[1][1] && board[1][1] == board[0][2] ) // diagonal
    return 1;
        
  else if (board[0][0] != '1' && board[0][1] != '2' && board[0][2] != '3' &&
	   board[1][0] != '4' && board[1][1] != '5' && board[1][2] != '6' && 
	   board[2][0] != '7' && board[2][1] != '8' && board[2][2] != '9')

    return 0; // Return of 0 means game over
  else
    return  - 1; // return of -1 means keep playing
}


void print_board(char board[ROWS][COLUMNS])
{
  /*****************************************************************/
  /* brute force print out the board and all the squares/values    */
  /*****************************************************************/

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
  printf ("in sharedstate area\n");
  for (i = 0; i < 3; i++)
    for (j = 0; j < 3; j++) {
      board[i][j] = count + '0';
      count++;
    }
  return 0;
}
