//Para compilar gcc mobile.c -o mobile
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/shm.h>
#include <semaphore.h>
#include <string.h>
#include <sys/ipc.h>
#include <sys/wait.h>
#include <stdlib.h>
#include <ctype.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <time.h>

struct tm* getTime() {
	time_t now = time(NULL);
	struct tm *tm_struct = localtime(&now);
	return tm_struct;
}

void log(char msg[100]) {
	FILE *r = fopen("log.txt", "a");
	fprintf(r, "%d:%d:%d %s\n", getTime()->tm_hour, getTime()->tm_min, getTime()->tm_sec, msg);
	fclose(r);
}

void writeNamedPipe(char *message){
	char * task_pipe = "TASK_PIPE";
	int fd;
	fd = open(task_pipe, O_WRONLY);
	if(fd != -1){
		write(fd, message, strlen(message)+1);
		printf("sent to named pipe: %s\n", message);
		close(fd);
	}
	else{
		log("NAMED PIPE ERROR");
		perror("Named pipe error");
		exit(0);
	}
}

void mobileNodes(char *var1,char *var2,char *var3,char *var4){
	int n_request = atoi(var1);
	int intervalo = atoi(var2);
	int mipr = atoi(var3);
	int tMax = atoi(var4);
	printf("n_request: %d\n", n_request);
	printf("intervalo: %d\n", intervalo);
	printf("mipr: %d\n", mipr);
	printf("tMax: %d\n", tMax);

	for(int i = 0; i < n_request; i++){
		char c[100];
		sprintf(c, "%d; %d; %d", i, mipr, tMax);
		writeNamedPipe(c);
		sleep(intervalo / 1000);
	}
	exit(0);
}

int checkNumber(char *var){
	int l = strlen(var);
	for(int i = 0; i<l; i++){
		if(!isdigit(var[i])){
			return 0;
		}
	}
	return 1;
}

int main(){
	char comando[100];
	while(1){
		fgets(comando, 100, stdin);
		if(strcmp(comando, "EXIT\n") == 0){
			writeNamedPipe("EXIT");
			exit(0);
		}
		else if(strcmp(comando, "STATS\n") == 0){
			writeNamedPipe("STATS");
		}
		else{
			char *token = strtok(comando, "\n");
			const char s[2] = " ";
			char *check_comando = strtok(token, s);

			if(strcmp(check_comando, "mobile_node") == 0){

				char *n_request = strtok(NULL, s);
				char *intervalo = strtok(NULL, s);
				char *mipr = strtok(NULL, s);
				char *tMax = strtok(NULL, s);

				if(checkNumber(n_request) && checkNumber(intervalo) && checkNumber(mipr) && checkNumber(tMax)){
					if(fork() == 0){
						mobileNodes(n_request, intervalo, mipr, tMax);
					}
				}
				else{
					//log(strcat("WRONG COMMAND => ", comando));
					printf("Comando invalido\n");
				}
			}

			else{
				//log(strcat("WRONG COMMAND => ", comando));
				printf("Comando invalido\n");
			}
		}
	}
	exit(0);
}
