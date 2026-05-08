CC = gcc
CFLAGS = -g -O0 -Wall -Wextra -pthread
LDFLAGS = -pthread

# Executables
TARGET1 = ./client/file_transfer_client
TARGET2 = ./server/file_transfer_server

# Default rule to build the program
all: $(TARGET1) $(TARGET2)

$(TARGET1): ./client/file_transfer_client.o
	$(CC) $(LDFLAGS) -o $(TARGET1) ./client/file_transfer_client.o

$(TARGET2): ./server/file_transfer_server.o
	$(CC) $(LDFLAGS) -o $(TARGET2) ./server/file_transfer_server.o

client/main.o: ./client/main.c
	$(CC) $(CFLAGS) -c file_transfer_client.c

server/main.o: ./server/main.c
	$(CC) $(CFLAGS) -c file_transfer_server.c

# Clean up build files
clean:
	rm -f $(TARGET1) ./client/file_transfer_client.o
	rm -f $(TARGET2) ./server/file_transfer_server.o
