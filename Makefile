main:mem_hook.c main.cpp
	g++ -g -Wno-deprecated-declarations -std=c++11 -I./ -rdynamic -g mem_hook.c  main.cpp -o main -pthread
