#include "types.h"
#include "user.h"

int main(void){
	int wtime[30];
	int rtime[30];
	int pid[30];
    int priority[30];
    int q;
    int wavg;
    int ravg;
    int q1;
    int wavg1;
    int ravg1;
    int q2;
    int wavg2;
    int ravg2;
    int q3;
    int wavg3;
    int ravg3;
	for(int u = 0 ; u < 30 ; u++){
        priority[u]=0;
		pid[u] = fork();

		if(pid[u] == 0){
            if(u%3==0){
                nice();
            }
            else if(u%3==1){
                nice();
                nice();
            }q = 0;
			for(int i = 0 ; i < 1000 ; i++){
				printf(1,"child <%d> prints for <%d> time \n" ,getpid(), i);
			}
			priority[u] = getPerformanceData(&wtime[u],&rtime[u]);
			exit();
		}
	}
    q = 0;
                q1 = 0;
                q2 = 0;
                q3 = 0;
                wavg = 0;
                wavg1 = 0;
                wavg2 = 0;
                wavg3 = 0;
                ravg = 0;
                ravg1 = 0;
                ravg2 = 0;
                ravg3 = 0;
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
    for(int u = 0 ; u < 30 ; u++){
            q++;
            wavg = wavg + wtime[u];
            ravg = ravg + rtime[u];
        
        if(priority[u]==1){
            q1++;
            wavg1 = wavg1 + wtime[u];
            ravg1 = ravg1 + rtime[u];
        }
        if(priority[u]==2){
            q2++;
            wavg2 = wavg2 + wtime[u];
            ravg2 = ravg2 + rtime[u];
        }
        if(priority[u]==3){
            q3++;
            wavg3 = wavg3 + wtime[u];
            ravg3 = ravg3 + rtime[u];
        }
        printf(1,"wtime: %d and rtime: %d child priority: %d \n", wtime[u], rtime[u] , priority[u]);
    }
    printf(1,"all average wtime: %d and average rtime: %d  \n", (wavg/q),
     (ravg/q));
     printf(1,"queue 1 RR average wtime: %d and average rtime: %d  \n", (wavg1/q1),
     (ravg1/q1));
     printf(1,"queue 2 FRR average wtime: %d and average rtime: %d  \n", (wavg2/q2),
     (ravg2/q2));
     printf(1,"queue 3 GRT average wtime: %d and average rtime: %d  \n", (wavg3/q3),
     (ravg3/q3));
	exit();
}

