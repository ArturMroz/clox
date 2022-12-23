CFLAGS=-std=c99
# ccflags-y := -std=gnu99

all: 
	gcc *.c -o main 
	./main

ped: 
	gcc *.c -o main -Wall -Wextra -pedantic