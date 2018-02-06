all: server client
server: player1_server.c
	gcc player1_server.c -o server
client: player2_client.c
	gcc player2_client.c -o client
