all : main

main : main.c coroutine.c
	gcc -g -Wall -o $@ $^ -lpthread

clean :
	rm main
