#include "types.h"
#include "user.h"

int main(void){
	// int wtime[10];
	// int rtime[10];
	int pid[10];
	for(int u = 0 ; u < 10 ; u++){
		pid[u] = fork();

		if(pid[u] == 0){
			for(int i = 0 ; i < 1000 ; i++){
				// printf(1,"child <%d> prints for <%d> time \n" ,getpid(), i);
			}
			exit();
		}
	}
    wait();
    wait();
    wait();
    wait();
    wait();
    wait();
    wait();
    wait();
    wait();
    wait();
	exit();
}

