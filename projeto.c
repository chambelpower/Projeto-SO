//Para compilar gcc -pthread projeto.c -o projeto
//Comando inicial: offload_simulator config_file.txt
//Rafael Amaral 2018286090
//Pedro Bonifácio 2018280935

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
void print_lista_tarefas();
void printServers();
void printServers2();
pid_t systemManagerPID;

enum serverState{
	RUNNING = 0,
	STOPPED = 1
};

enum cpuState{
	BUSY = 0,
	AVAILABLE = 1
};

typedef struct _s1{
	char *name;
	int capacidade1;
	int cpuState1;
	int capacidade2;
	int cpuState2;
	char *nivelPerformance;
	int timeNextTarefa;
	int state;
	int channel[2];
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
	int highPerformanceFlag;//fica a 1 quando for para os servers ficarem no modo highperformance
	edge_server* edgeList;
} shm_struct;

int shmid;
shm_struct *sh_mem;
sem_t *mutex;
sem_t *serverMutex;
sem_t *logSem;

typedef struct _s5{
	int id;
	int n_instrucoes;
	int tMax;
	int tChegada;
	int prio;
} data;

typedef struct _s3{
	data* data;
	struct _s3 *next;
} tasks;

tasks *taskList;

struct tm* getTime() {
	time_t now = time(NULL);
	struct tm *tm_struct = localtime(&now);
	return tm_struct;
}

struct timeval timeout;

pthread_mutex_t mutexFila = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t scheduler_cs= PTHREAD_COND_INITIALIZER;
pthread_cond_t dispatcher_cs= PTHREAD_COND_INITIALIZER;

pthread_mutex_t mutexUnnamedPipe = PTHREAD_MUTEX_INITIALIZER;

void logFile(char msg[100]) {
	sem_wait(logSem);
	FILE *r = fopen("log.txt", "a");
	fprintf(r, "%d:%d:%d %s\n", getTime()->tm_hour, getTime()->tm_min, getTime()->tm_sec, msg);
	fclose(r);
	sem_post(logSem);
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
			e->state = 5;
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
			e1->state = 5;
			e1->next = NULL;
			e->next = e1;
			e = e1;
		}
		else{
			return -2;
		}
		i++;
	} 
	sh_mem->highPerformanceFlag = 0;
	sem_post(mutex);
	fclose(f);
	
	return 0;
}

void *cpu1(void *t){
	printf("CPU1 is READY\n");
	int counter = *((int *)t);
	int i = 0;
	sem_wait(mutex);
	edge_server* e = sh_mem->edgeList;
	while(i < counter){
		e = e->next;
		i++;
	}	
	
	close(e->channel[1]);
	sem_post(mutex);
	fd_set read_set;
	
	tasks* t1;
	while(1){
		
		
		sem_wait(mutex);
	
		if(e->state == RUNNING){
		
			
			pthread_mutex_lock(&mutexUnnamedPipe);
		
			FD_ZERO(&read_set);
			
			FD_SET(e->channel[0], &read_set);
			
			if(select(e->channel[0]+1, &read_set, NULL, NULL, &timeout) > 0){
				read(e->channel[0], &t1, sizeof(tasks));
				printf("CPU1 RECEBEU TASK\n");
				sem_post(mutex);
			}
			else{
				sem_post(mutex);
			}
			pthread_mutex_unlock(&mutexUnnamedPipe);
			
		}
		else{
			sem_post(mutex);
		}
	}
	
	pthread_exit(NULL);
}

void *cpu2(void *t){
	
	printf("CPU2 is READY\n");
	int counter = *((int *)t);
	int i = 0;
	sem_wait(mutex);
	edge_server* e = sh_mem->edgeList;
	while(i < counter){
		e = e->next;
		i++;
	}	
	
	close(e->channel[1]);
	sem_post(mutex);
	fd_set read_set;
	tasks* t1;
	while(1){
		sem_wait(mutex);
		if(e->state == RUNNING && sh_mem->highPerformanceFlag == 1){
			
			pthread_mutex_lock(&mutexUnnamedPipe);
			FD_ZERO(&read_set);
			FD_SET(e->channel[0], &read_set);
			if(select(e->channel[0]+1, &read_set, NULL, NULL, &timeout) > 0){
				read(e->channel[0], &t1, sizeof(tasks));
				printf("CPU2 RECEBEU TASK\n");
				sem_post(mutex);
			}
			else{
				sem_post(mutex);
			}
			pthread_mutex_unlock(&mutexUnnamedPipe);
		}
		else{
			sem_post(mutex);
		}
	}
	
	pthread_exit(NULL);
}
void changeServerName(int counter, char *str){
	int i = 0;
	edge_server* serverList = sh_mem->edgeList;
	while(i < counter){
		serverList=serverList->next;
		i++;
	}
	strcat(serverList->name, str);
	sh_mem->highPerformanceFlag++;
	
	
}
void edgeServer(edge_server *e, int counter){
	int i;
	char var[100];
	pthread_t threads[NCPU];
	//printServers();
	sem_post(serverMutex);
	sem_wait(mutex);
	printf("%s STARTED\n", e->name);
	strcpy(var, e->name);
	logFile(strcat(var, " READY"));//nao pode ser assim, esta a adicionar READY a variavel na memoria partilhada
	

	printf("NAME: %s\n", e->name);
	
	e->state = RUNNING;
	printf("STATE: %d\n", e->state);
	
	changeServerName(counter, "DEBUG");
	sem_post(mutex);
	//printServers();
	pthread_create(&threads[0], NULL, cpu1, &counter);
	pthread_create(&threads[1], NULL, cpu2, &counter);
	
	
	printf("DEBUG5\n");
	
  	printServers();
	for(i = 0; i < NCPU; i++){
		pthread_join(threads[i], NULL);
  	}
  	
}

void removeTarefasImpo__Prio(){
	//print_lista_tarefas();
	tasks* t = taskList;
	int timeAtual = (int)time(NULL);
	while(t != NULL){
		if(t->data->id == -1){
			t->data->prio = 1000;
		}
		else{
			if(t->data->tMax <= timeAtual){
				t->data->id = -1;//-1 representa que o lugar na lista nao esta ocupado
				t->data->prio = 1000;
				printf("Task Removed: tMax->%d, timeAtual->%d\n", t->data->tMax, timeAtual);
				logFile("TASK REMOVED");
			}
			else{
				t->data->prio = t->data->tMax - timeAtual;//no maximo este valor pode ser 1, desempatado pela ordem de chegada
			}
		}
		t=t->next;
	}
	//print_lista_tarefas();
}



void ordenar(){
	tasks* current = taskList;
	tasks* index;
	tasks* temp = (tasks*)malloc(sizeof(tasks));
	while(current != NULL){
		index = current->next;
		
		while(index != NULL){
			if(current->data->prio > index->data->prio){
				temp->data = current->data;
				current->data = index->data;
				index->data = temp->data;
			}
			if(current->data->prio == index->data->prio){
				if(current->data->tChegada > index->data->tChegada){
					temp->data = current->data;
					current->data = index->data;
					index->data = temp->data;
				}
			}
			index = index->next;
		}
		current = current->next;
		
	}
}

void print_lista_tarefas(){

	tasks* current = taskList;
	printf("LISTA DE TAREFAS\n");
	while(current != NULL){
		printf("ID: %d, PRIO: %d\n", current->data->id, current->data->prio);
		current = current->next;
	}
	
}

void *scheduler(void *t){
	printf("Starting scheduler\n");
	pthread_mutex_lock(&mutexFila);
	//print_lista_tarefas();
	pthread_mutex_unlock(&mutexFila);
	while(1){
		pthread_mutex_lock(&mutexFila);
		pthread_cond_wait(&scheduler_cs, &mutexFila);
		printf("RECEBEU SINAL\n");
		//print_lista_tarefas();
		removeTarefasImpo__Prio();
		//printf("DEBUG1\n");
		//print_lista_tarefas();
		ordenar();
		//printf("DEBUG2\n");
		//print_lista_tarefas();
		pthread_mutex_unlock(&mutexFila);
		
	}
	pthread_exit(NULL);
}

int verificaExistirTarefas(){
	tasks* t = taskList;
	while(t != NULL){
		if(t->data->id != -1){
			printf("ENCONTROU TAREFA\n");
			return 1;
		}	
		t=t->next;
	}
	printf("NAO ENCONTROU TAREFA\n");
	return 0;
}

int tryDispatchTask(tasks* t){
	printf("TRY DISPATCH\n");
	sem_wait(mutex); 
	edge_server* serverList = sh_mem->edgeList;
	while(serverList != NULL){
		if(serverList->cpuState1 == AVAILABLE){
			if((int)(t->data->n_instrucoes/serverList->capacidade1) + (int)time(NULL) < t->data->tMax){
				//dispatch
				write(serverList->channel[1], &t, sizeof(tasks));
				//removetask
				t->data->id = -1;
				sem_post(mutex);
				return 1;
			}
		}
		if(sh_mem->highPerformanceFlag == 1){
			if(serverList->cpuState2 == AVAILABLE){
				if((int)(t->data->n_instrucoes/serverList->capacidade2) + (int)time(NULL) < t->data->tMax){
					//dispatch
					write(serverList->channel[1], &t, sizeof(tasks));
					//removetask
					t->data->id = -1;
					sem_post(mutex);
					return 1;
				}
			}
		}
		serverList = serverList->next;
	}
	sem_post(mutex);
	return -1;
}

int taskValida(tasks* t){
	sem_wait(mutex); 
	edge_server* serverList = sh_mem->edgeList;
	while(serverList != NULL){
		if((int)(t->data->n_instrucoes/serverList->capacidade1) + (int)time(NULL) < t->data->tMax){
			sem_post(mutex);
			return 1;
		}
		if((int)(t->data->n_instrucoes/serverList->capacidade2) + (int)time(NULL) < t->data->tMax){
			sem_post(mutex);
			return 1;
		}
		serverList = serverList->next;
	}
	t->data->id=-1;//removed
	sem_post(mutex);
	return -1;
}

int verificaCPU(){
	sem_wait(mutex);
	edge_server* serverList = sh_mem->edgeList;
	while(serverList != NULL){
		//printf("DEBUG0: %s\n", serverList->name);
		//printf("DEBUG1: %d\n", serverList->state);
		//printf("DEBUG2: %d\n", STOPPED);
		//printServers2();
		if(serverList->state == RUNNING){
			printf("ESTADO1: %d, ESTADO2: %d\n", serverList->cpuState1, serverList->cpuState2);
			if(serverList->cpuState1 == AVAILABLE){
				printf("ENCONTROU CPU1\n");
				return 1;
			}
			if(sh_mem->highPerformanceFlag == 1){
				if(serverList->cpuState2 == AVAILABLE){
					printf("ENCONTROU CPU2\n");
					return 2;
				}
			}	
		}
		serverList=serverList->next;
	}
	printf("FLAG: %d\n", sh_mem->highPerformanceFlag);
	sem_post(mutex);
	printf("NAO ENCONTROU CPU DISPONIVEL\n");
	
	return 0;
}

void printServers(){
	sem_wait(mutex);
	edge_server* serverList = sh_mem->edgeList;
	while(serverList != NULL){
		printf("%s, STATE: %d\n", serverList->name, serverList->state);
		serverList=serverList->next;
	}
	sem_post(mutex);
}

void printServers2(){
	
	edge_server* serverList = sh_mem->edgeList;
	while(serverList != NULL){
		printf("%s, STATE: %d\n", serverList->name, serverList->state);
		serverList=serverList->next;
	}
	
}

void *dispatcher(void *t){
	printf("Starting dispatcher\n");
	while(1){
		
		while(verificaExistirTarefas() == 0 && verificaCPU() == 0){
			sleep(2);
		}
		pthread_mutex_lock(&mutexFila);
		tasks* t = taskList;
		printf("DEBUG6\n");
		while(t != NULL){//esta sempre ordenado por prioridade por isso apenas temos que percorrer e encontrar uma que seja possivel fazer
			if(t->data->id != -1 && taskValida(t) == 1){
				if(tryDispatchTask(t) == 1){
					printf("TASK DISPATCHED\n");
					break;
				}
			}
			else{
				printf("TASK REMOVED\n");

			}
			t=t->next;
		}
		pthread_mutex_unlock(&mutexFila);
	}
}

void taskManager(){
	logFile("PROCESS TASK_MANAGER CREATED");
	printf("PROCESS TASK_MANAGER CREATED\n");
	int fd;
	char str1[100];
	
	timeout.tv_sec = 0;
	timeout.tv_usec = 10;
	sem_wait(mutex); 
	int totalServers = sh_mem->nEdgeServers;
	edge_server* serverList = sh_mem->edgeList;
	sem_unlink("SERVERMUTEX");
	serverMutex = sem_open("SERVERMUTEX", O_CREAT | O_EXCL, 0700, 1);
	
	for(int i = 0; i < totalServers; i++){
		printf("here1\n");
		sem_wait(serverMutex);
		printf("here2\n");
		pipe(serverList->channel);
		if(fork() == 0){
			
			printf("Edge Server %s\n", serverList->name);
			edgeServer(serverList, i);	
			exit(0);
		}
		serverList = serverList->next;
		
	}
	
	sem_post(mutex); 
	
	sem_wait(serverMutex);//para apenas avancar quando os servers todos tiverem preparados
	printf("Servers ready\n");
	sem_post(serverMutex);
	//criacao da fila de tasks
	sleep(3);
	printServers();
	
	pthread_mutex_lock(&mutexFila);
	tasks* t = (tasks*)malloc(sizeof(tasks));
	t->data = (data*)malloc(sizeof(data));
	t->data->id = -1;// e utilizado -1 quando o espaco na lista nao esta ocupado
	t->data->prio = 1000;
	t->next = NULL;
	taskList = t;
	for(int i = 1; i < sh_mem->slots; i++){
		tasks *t1 = (tasks*)malloc(sizeof(tasks));
		t1->data = (data*)malloc(sizeof(data));
		t1->data->id = -1;
		t1->data->prio = 1000;
		t1->next = NULL;
		t->next = t1;
		t = t1;
	}
	//print_lista_tarefas();
	pthread_mutex_unlock(&mutexFila);

	
  	pthread_t thread_id[2];
  	
	pthread_create(&thread_id[0], NULL, scheduler, NULL);
	pthread_create(&thread_id[1], NULL, dispatcher, NULL);
	
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
			//print_taskList();
		}
		close(fd);
	}
	pthread_join(thread_id[0], NULL);
	pthread_join(thread_id[1], NULL);
	exit(0);
}

void insertFila(int id, int n_inst, int tMax){
	tasks* t = taskList;
	while(t != NULL){
		if(t->data->id == -1){
			
			t->data->id = id;
			t->data->n_instrucoes = n_inst;
			t->data->tMax = (int)time(NULL) + tMax;
			t->data->tChegada = (int)time(NULL);
			//printf("TIME: %d\n", t->tMax);
			t->data->prio = 0;
			logFile("NEW TASK INSERTED");
			printf("NEW TASK INSERTED\n");
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

void print_taskList(){
	pthread_mutex_lock(&mutexFila);
	printf("SHM2:\n");
	tasks* t = taskList;
	while(t != NULL){
		printf("ID: %d\n", t->data->id);
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
	//sem_close(logSem); //da segfault??
	//sem_unlink("LOGSEM");
	pthread_mutex_destroy(&mutexFila);
	pthread_cond_destroy(&scheduler_cs);
	logFile("SIMULATOR CLOSING");
	kill(0, SIGTERM);
	exit(0);
	}
}

void cleanup(){//falta dar free a memoria da fila de tasks
	printf("CLEANUP\n");
	msgctl(msgID, IPC_RMID, 0);
	
	shmctl(shmid, IPC_RMID, 0);
	
	sem_close(mutex);
	sem_unlink("MUTEX");
	sem_close(serverMutex);
	sem_unlink("SERVERMUTEX");
	//sem_close(logSem);
	//sem_unlink("LOGSEM");
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
	
	
	//int fd;
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
	sem_unlink("LOGSEM");
	logSem = sem_open("LOGSEM", O_CREAT | O_EXCL, 0700, 1);
	printf("Insira o comando com o nome do ficheiro de config\n");
	char comando[100];
	while(1){
		fgets(comando, 100, stdin);
		//char *token = strtok(comando, "\n");
		//const char s[2] = " ";
		//char *check_comando = strtok(token, s);
		//if(strcmp(check_comando, "offload_simulator") == 0){
			
			//char *filename = strtok(NULL, s);
			char *filename = "config_file.txt";//
			systemManager(filename);
		//}
	}
	cleanup();	
	
}
