//Para compilar gcc -pthread projeto.c -o projeto
//Comando inicial: offload_simulator config_file.txt


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
#include <sys/msg.h>
#include <signal.h>
#include <time.h>
#include <pthread.h>

#define NCPU 2
int readFile(char* file);
void print_SHM();
void systemManager();
void print_SHM2();
void insertFila(int id, int n_inst, int tMax);
void cleanup();

pid_t systemManagerPID;

typedef struct _s1{
	char *name;
	int capacidade1;
	int capacidade2;
	char *nivelPerformance;
	int timeNextTarefa;
	struct _s1 *next;
} edge_server;

typedef struct _s2{
	char *mesg_text;
} message;

int msgID;

char * task_pipe = "TASK_PIPE";

typedef struct{
	int slots; //numero de slots na fila interna do TaskManager
	int maxWait; //tempo maximo para que o processo Monitor eleve o nivel de performance dos Edge Servers
	int nEdgeServers;//numero de Edge Servers
	edge_server* edgeList;
} shm_struct;

int shmid;
shm_struct *sh_mem;
sem_t *mutex;
sem_t *serverMutex;

typedef struct _s3{
	int id;
	int n_instrucoes;
	int tMax;
	int prio;
	struct _s3 *next;
} tasks;

typedef struct _s4{
	tasks* taskList;
} filaTasks;

struct tm* getTime() {
	time_t now = time(NULL);
	struct tm *tm_struct = localtime(&now);
	return tm_struct;
}

int filaID;
filaTasks *filaT;

pthread_mutex_t mutexFila = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t scheduler_cs= PTHREAD_COND_INITIALIZER;

void logFile(char msg[100]) {
	FILE *r = fopen("log.txt", "a");
	fprintf(r, "%d:%d:%d %s\n", getTime()->tm_hour, getTime()->tm_min, getTime()->tm_sec, msg);
	fclose(r);
}

int readFile(char* file){
	FILE *f = fopen(file, "r");
	if(f == NULL){
		return -1;
	}
	char line[100];
	int i = 0;
	sem_wait(mutex);
	
	
	while(i<3){
		if(fgets(line, sizeof(line), f)){
		char *newLine = strtok(line, "\n");
		
		switch(i){
			case 0:
				sh_mem->slots = atoi(newLine);
				break;
			case 1:
				sh_mem->maxWait = atoi(newLine);
				break;
			case 2:
				sh_mem->nEdgeServers = atoi(newLine);
				break;
					
		}
		i++;
		}
		else{
			return -2;
		}
	}
	i = 1;
	edge_server *e;
	if(fgets(line, sizeof(line), f)){
			char *newLine = strtok(line, "\n");
			char *value = strtok(newLine, ",");
			e = (edge_server*)malloc(sizeof(edge_server));
			e->name = (char*)malloc(strlen(value));
			sprintf(e->name, "%s", value);
			value = strtok(NULL, ",");
			e->capacidade1 = atoi(value);
			value = strtok(NULL, ",");
			e->capacidade2 = atoi(value);
			e->next = NULL;
		}
	else{
		return -2;
	}
	sh_mem->edgeList = e;
	while(i < sh_mem->nEdgeServers){
		if(fgets(line, sizeof(line), f)){
			char *newLine = strtok(line, "\n");
			char *value = strtok(newLine, ",");
			
			edge_server *e1 = (edge_server*)malloc(sizeof(edge_server));
			e1->name = (char*)malloc(strlen(value));
			sprintf(e1->name, "%s", value);
			value = strtok(NULL, ",");
			e1->capacidade1 = atoi(value);
			value = strtok(NULL, ",");
			e1->capacidade2 = atoi(value);
			e1->next = NULL;
			e->next = e1;
			e = e1;
		}
		else{
			return -2;
		}
		i++;
	} 
	
	sem_post(mutex);
	fclose(f);
	
	return 0;
}

void *cpu(void *t){
	int my_id = *((int *)t);

	printf("CPU is READY\n");
	
	pthread_exit(NULL);
}

void edgeServer(char *name, int capMin, int capMax){
	printf("%s STARTED\n", name);
	logFile(strcat(name, " READY"));
	
	int i;
	int id[NCPU];
	pthread_t threads[NCPU];
	
	for(i = 0; i < NCPU; i++){
		id[i] = i;
		pthread_create(&threads[i], NULL, cpu, &id[i]);
	}
	sem_post(serverMutex);
	for(i = 0; i < NCPU; i++){
		pthread_join(threads[i], NULL);
  	}
}

void removeTarefasImpo(){

}

void prioridades(){

}

void ordenar(){

}

void *scheduler(void *t){
	printf("Starting scheduler\n");
	while(1){
		pthread_mutex_lock(&mutexFila);
		pthread_cond_wait(&scheduler_cs, &mutexFila);
		printf("RECEBEU SINAL\n");
		
		removeTarefasImpo();
		prioridades();
		ordenar();
		pthread_mutex_unlock(&mutexFila);
		
	}
	pthread_exit(NULL);
}

void taskManager(){
	logFile("PROCESS TASK_MANAGER CREATED");
	int fd;
	char str1[100];
	sem_wait(mutex); 
	int totalServers = sh_mem->nEdgeServers;
	edge_server* serverList = sh_mem->edgeList;
	sem_post(mutex); 
	sem_unlink("SERVERMUTEX");
	serverMutex = sem_open("SERVERMUTEX", O_CREAT | O_EXCL, 0700, 1);
	
	for(int i = 0; i < totalServers; i++){
		
		
		sem_wait(serverMutex);
		
		if(fork() == 0){
			sem_wait(mutex); 
			char *name = serverList->name;
			int a = serverList->capacidade1;
			int b = serverList->capacidade2;
			printf("Edge Server %s\n", name);
			sem_post(mutex); 
			edgeServer(name, a, b);	
			
			
			exit(0);
		}
		
		sem_wait(mutex); 
		serverList = serverList->next;
		sem_post(mutex); 
	}
	
	sem_wait(serverMutex);//para apenas avancar quando os servers todos tiverem preparados
	printf("Servers ready\n");
	sem_post(serverMutex);
	//criacao da fila de tasks
	
	if((filaID = shmget(IPC_PRIVATE, sizeof(filaTasks), IPC_CREAT|0776)) < 0){
		logFile("ERROR CREATING SHARED MEMORY");
		perror("Erro ao criar a memoria partilhada2");
		cleanup();
		exit(1);
	}
	
	if((filaT = (filaTasks*) shmat(filaID, NULL, 0)) < 0){
		logFile("ERROR MAPING SHARED MEMORY");
		perror("Erro ao mapear a memoria partilhada2");
		cleanup();
		exit(1);
	}
	
	int id[1];//para ja apenas a dispatcher
  	pthread_t thread_id;
  	
	pthread_create(&thread_id, NULL, scheduler, NULL);
	
	
	pthread_mutex_lock(&mutexFila);
	tasks* t = (tasks*)malloc(sizeof(tasks));
	t->id = -1;// e utilizado -1 quando o espaco na lista nao esta ocupado
	t->next = NULL;
	filaT->taskList = t;
	for(int i = 1; i < sh_mem->slots; i++){
		tasks *t1 = (tasks*)malloc(sizeof(tasks));
		t1->id = -1;
		t1->next = NULL;
		t->next = t1;
		t = t1;
	}
	pthread_mutex_unlock(&mutexFila);

	
	
	while(1){
		fd = open(task_pipe, O_RDONLY);
		read(fd, str1, 100);
		printf("TaskManager read: %s\n", str1);
		if(strcmp(str1, "EXIT") == 0){
			logFile("EXIT COMMAND RECEIVED");
			logFile("SIMULATOR CLOSING");
			cleanup();
		}
		else if(strcmp(str1, "STATS") == 0){
			logFile("STATS COMMAND RECEIVED");
			printf("stats\n");
		}
		else{
			printf("task\n");
			int id = atoi(strtok(str1, ";"));
			int n_inst = atoi(strtok(NULL, ";"));
			int tMax = atoi(strtok(NULL, ";"));
			pthread_mutex_lock(&mutexFila);
			insertFila(id, n_inst, tMax);
			pthread_cond_signal(&scheduler_cs);
			pthread_mutex_unlock(&mutexFila);
			//print_SHM2();
		}
		close(fd);
	}
	pthread_join(thread_id, NULL);
	exit(0);
}

void insertFila(int id, int n_inst, int tMax){
	tasks* t = filaT->taskList;
	while(t != NULL){
		if(t->id == -1){
			
			t->id = id;
			t->n_instrucoes = n_inst;
			t->tMax = (int)time(NULL) + tMax;
			printf("TIME: %d\n", t->tMax);
			t->prio = 0;
			logFile("NEW TASK INSERTED");
			printf("Nova tarefa inserida\n");
			return;
		}
		t=t->next;
	}
	logFile("QUEUE FULL => TASK REFUSED");
	printf("Fila ocupada, tarefa eliminada\n");
}

void monitor(){
	logFile("PROCESS MONITOR CREATED");
	exit(0);
}

void maintenanceManager(){
	logFile("PROCESS MAINTENANCE MANAGER CREATED");
	exit(0);
}

void print_SHM2(){
	pthread_mutex_lock(&mutexFila);
	printf("SHM2:\n");
	tasks* t = filaT->taskList;
	while(t != NULL){
		printf("ID: %d\n", t->id);
		t=t->next;
	}
	pthread_mutex_unlock(&mutexFila);
}

void print_SHM(){
	sem_wait(mutex);
	printf("SHM:\n");
	printf("Slots: %d\n", sh_mem->slots);
	printf("MaxWait: %d\n", sh_mem->maxWait);
	printf("N de EdgeServers: %d\n", sh_mem->nEdgeServers);
	edge_server* serverList = sh_mem->edgeList;
	while(serverList != NULL){
		printf("%s: %d, %d\n", serverList->name, serverList->capacidade1, serverList->capacidade2);
		serverList = serverList->next;
	}
	sem_post(mutex);
}

void sigCleanup(){
	if((int)systemManagerPID == (int)getpid()){
	printf("CLEANUP\n");
	logFile("SIGNAL SIGINT RECEIVED");
	msgctl(msgID, IPC_RMID, 0);
	
	shmctl(shmid, IPC_RMID, 0);
	
	sem_close(mutex);
	sem_unlink("MUTEX");
	sem_close(serverMutex);
	sem_unlink("SERVERMUTEX");
	pthread_mutex_destroy(&mutexFila);
	pthread_cond_destroy(&scheduler_cs);
	logFile("SIMULATOR CLOSING");
	kill(0, SIGTERM);
	exit(0);
	}
}

void cleanup(){
	printf("CLEANUP\n");
	msgctl(msgID, IPC_RMID, 0);
	
	shmctl(shmid, IPC_RMID, 0);
	
	sem_close(mutex);
	sem_unlink("MUTEX");
	sem_close(serverMutex);
	sem_unlink("SERVERMUTEX");
	pthread_mutex_destroy(&mutexFila);
	pthread_cond_destroy(&scheduler_cs);
	kill(0, SIGTERM);
	exit(0);
}

void stats(){
	
	if((int)systemManagerPID == (int)getpid()){
		logFile("SIGNAL SIGTSTP RECEIVED");
		printf("Stats: TODO\n");
	}
}

void systemManager(char *filename){
	systemManagerPID = getpid();
	
	signal(SIGINT, sigCleanup);
	signal(SIGTSTP, stats);
	
	FILE *f = fopen("log.txt", "w");
	fclose(f);
	
	logFile("OFFLOAD SIMULATOR STARTING");
	
	if((shmid = shmget(IPC_PRIVATE, sizeof(shm_struct), IPC_CREAT|0776)) < 0){
		logFile("ERROR CREATING SHARED MEMORY");
		perror("Erro ao criar a memoria partilhada");
		cleanup();
		exit(1);
	}
	
	if((sh_mem = (shm_struct*) shmat(shmid, NULL, 0)) < 0){
		logFile("ERROR MAPING SHARED MEMORY");
		perror("Erro ao mapear a memoria partilhada");
		cleanup();
		exit(1);
	}
	

	sem_unlink("MUTEX");
	mutex = sem_open("MUTEX", O_CREAT | O_EXCL, 0700, 1);
	//aqui a variavel systemManagerPID fica a 0 por alguma razao
	systemManagerPID = getpid();
	int a = readFile(filename);
	if(a == -1){
		logFile("ERROR READING CONFIG FILE");
		perror("Erro ao ler o ficheiro de configuracoes");
		cleanup();
		exit(1);
	}
	if(a == -2){
		logFile("INVALID CONFIG FILE");
		perror("Ficheiro de configuracoes invalido");
		cleanup();
		exit(1);
	}

	print_SHM();
	
	
	int fd;
	if(mkfifo(task_pipe, 0666) == -1){
		printf("Named pipe already exists or an error ocurred\n");
	}
	else{
		printf("Named pipe created\n");
	}
	
	
	
	msgID = msgget(IPC_PRIVATE, IPC_CREAT|0777);
	if(msgID < 0){
		perror("Creating message queue");
		cleanup();
		exit(0);
	}
	
	
	if(fork() == 0){
		taskManager();
		exit(0);
	}
	if(fork() == 0){
		monitor();
		exit(0);
	}
	if(fork() == 0){
		maintenanceManager();
		exit(0);
	}
	
	for(int i = 0; i < 3; i++){
		wait(NULL);
	}
}





int main(){
	printf("Insira o comando com o nome do ficheiro de config\n");
	char comando[100];
	while(1){
		fgets(comando, 100, stdin);
		char *token = strtok(comando, "\n");
		const char s[2] = " ";
		char *check_comando = strtok(token, s);
		if(strcmp(check_comando, "offload_simulator") == 0){
			char *filename = strtok(NULL, s);
			systemManager(filename);
		}
	}
	cleanup();	
	
}
