#include "types.h"
#include "user.h"

int main(void){
	int wtime;
	int rtime;
	int child = fork();
	if(child == 0){
		
		for(int i = 0 ; i < 5000 ; i++){}
		printf(1, "child: ");
		getPerformanceData(&wtime,&rtime);
		printf(1,"wtime: %d and rtime: %d \n", wtime, rtime);
		exit();
	}else{
		wait();
		for(int i = 0 ; i < 20000 ; i++){}
		printf(1, "parent: ");
		getPerformanceData(&wtime,&rtime);
		printf(1,"wtime: %d and rtime: %d \n", wtime, rtime);
	}
	exit();
}

