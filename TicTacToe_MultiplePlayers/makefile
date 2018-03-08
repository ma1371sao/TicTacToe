CC=gcc
CFLAGS = -g -Wall

all: tictactoeServer tictactoeClient 

tictactoeServer: tictactoeServer.o game.o
	$(CC) $(CFLAGS) tictactoeServer.o game.o -o tictactoeServer 

tictactoeClient: tictactoeClient.o game.o
	$(CC) $(CFLAGS) tictactoeClient.o game.o -o tictactoeClient 

game.o: game.h
	$(CC) $(CFLAGS) -c game.c -o game.o

tictactoeServer.o: game.h 
	$(CC) $(CFLAGS) -c tictactoeServer.c -o tictactoeServer.o

tictactoeClient.o: game.h 
	$(CC) $(CFLAGS) -c tictactoeClient.c -o tictactoeClient.o

.PHONY: clean
clean:
	rm -f tictactoeServer tictactoeClient *.o
