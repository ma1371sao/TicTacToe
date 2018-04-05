#define BACKLOG 2 
#define ROWS 3
#define COLUMNS 3
#define LINES 20
#define TIMEOUT 15

#define NEWGAME 1
#define MOVE 0
#define ENDGAME 2

#define FULL -1

typedef struct protocol {
    int command;
    int move;
    int game_no;
} protocol;

typedef struct position {
    int row;
    int col;
} position;

typedef struct board_info {
    char board[ROWS][COLUMNS];
    int valid;  // 1: available    0: unavailable
    time_t time;

    // TODO: number of timeout, number of dup, from address, recent data
    int num_timeout;
    int num_dup;
    protocol *recent_data;
    struct sockaddr_in addr;

    int is_draw;  // set it to 1 if itself detect game draw
} board_info;

void init_board_info(board_info* info);

void serialize_protocol(char* buffer, protocol* data);

void deserialize_protocol(char* buffer, protocol* data);

int tictactoeAI(char board[ROWS][COLUMNS]);

position* check_move(int player, protocol* data, char board[ROWS][COLUMNS]);

int tictactoe_server(int sockfd, int player, struct sockaddr_in from_address);

int tictactoe_client(int sockfd, int player, struct sockaddr_in from_address);

int send_protocol(int sockfd, protocol* data, struct sockaddr *addr, socklen_t len); 

int recv_protocol(int sockfd, struct sockaddr *addr, socklen_t *len, protocol* data); 

void print_board(char board[ROWS][COLUMNS]);

int initSharedState(char board[ROWS][COLUMNS]);

int checkwin(char board[ROWS][COLUMNS]);

int winner(char mark);

void clear_lines(int count); 

position* convert_move(protocol* data);

int get_valid_board(board_info *info[BACKLOG]);

void print_winner(char board[ROWS][COLUMNS]);

