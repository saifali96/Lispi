CC=gcc

INC_DIR = include
SRC_DIR = src
BIN_DIR = bin
CFLAGS = -std=c99 -Wall -Werror -I./include -I.
LFLAGS = -ledit -lm

lispi:
	$(CC) $(CFLAGS) $(SRC_DIR)/lispi.c $(INC_DIR)/mpc.c -o $(BIN_DIR)/lispi $(LFLAGS)

clean:
	rm -f $(BIN_DIR)/lispi
default:
	lispi