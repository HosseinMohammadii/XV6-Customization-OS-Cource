#include "types.h"
#include "user.h"

int main(void){
	int wtime;
	int rtime;
	for(int u = 0 ; u < 30 ; u++){
		int child = fork();

		if(child == 0){
		
			for(int i = 0 ; i < 500 ; i++){}
			printf(1, "child: ");
			getPerformanceData(&wtime,&rtime);
			printf(1,"wtime: %d and rtime: %d getpid: %d \n", wtime, rtime , getpid());
			exit();
		}else{
			wait();
			// printf(1, "parent: ");
			// getPerformanceData(&wtime,&rtime);
			// printf(1,"wtime: %d and rtime: %d \n", wtime, rtime);
		}
	}
	exit();
}

