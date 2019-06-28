#include "types.h"
#include "user.h"

int main(void){
	int wtime;
	int rtime;
	int pid;
	for(int u = 0 ; u < 30 ; u++){
		int child = fork();

		if(child == 0){
		
			for(int i = 0 ; i < 50 ; i++){
				printf(1,"%d \n" , i);
			}
			pid = getPerformanceData(&wtime,&rtime);
			printf(1,"wtime: %d and rtime: %d child pid: %d \n", wtime, rtime , pid);
			exit();
		}else{
			wait();
			// pid = getPerformanceData(&wtime,&rtime);
			// printf(1,"wtime: %d and rtime: %d child pid: %d \n", wtime, rtime , pid);
			// printf(1, "parent: ");
			// getPerformanceData(&wtime,&rtime);
			// printf(1,"wtime: %d and rtime: %d \n", wtime, rtime);
		}
	}
	// exit();
}

