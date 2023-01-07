CFLAGS=-std=c99
# ccflags-y := -std=gnu99

build: 
	gcc *.c -o main -g

run: build
	./main

ex: build
	./main examples/this.lox

ped: 
	gcc *.c -o main -Wall -Wextra -pedantic