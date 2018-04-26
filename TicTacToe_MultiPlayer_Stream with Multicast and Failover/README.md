# CSE 5462 Project
TicTacToe supporting multiple player with Multicast and Failover

## Usage
To compile

> $ make

To start server(player 1, first move)

> $ ./tictactoeServer [port number]

To start client(player 2)
> $ ./tictactoeClient 

Multicast IP and port are hardcoded:
> MC_PORT 1818
> MC_GROUP "239.0.0.1"
