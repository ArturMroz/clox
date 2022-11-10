all: 
	gcc *.c -o main 
	./main

ped: 
	gcc *.c -o main -Wall -Wextra -pedantic