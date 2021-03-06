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
#define MAX_SERVERS 100
#define MAX_CHAR 512
#define QUEUE_MSG_TYPE 10000

int readFile(char* file);
void print_SHM();
void systemManager();
void print_SHM2();
void insertFila(int id, int n_inst, int tMax);

void print_lista_tarefas();
void printServers();
void printServers2();
void logFile(char msg[MAX_CHAR]);
void stats();
void sigCleanup();


pid_t systemManagerPID;
pid_t mmPID;
pid_t monitorPID;
pid_t taskMPID;

enum serverState{
	RUNNING = 0,
	STOPPED = 1
};

enum cpuState{
	BUSY = 0,
	AVAILABLE = 1,
	ENDING = 2
};

typedef struct _s1{
	char name[MAX_CHAR];
	int capacidade1;
	int cpuState1;
	int capacidade2;
	int cpuState2;
	char nivelPerformance[MAX_CHAR];
	int timeNextTarefa1;
	int timeNextTarefa2;
	int state;
	int channel[2];
	int nTarefasRealizadas;
	int nOperacoesManutencao;
} edge_server;

typedef struct _s2{
	long mtype;
	char mesg_text[100];
} message;

int msgID;

char * task_pipe = "TASK_PIPE";

typedef struct{
	int currentTasksN;
	int slots; //numero de slots na fila interna do TaskManager
	int maxWait; //tempo maximo para que o processo Monitor eleve o nivel de performance dos Edge Servers
	int nEdgeServers;//numero de Edge Servers
	int highPerformanceFlag;//fica a 1 quando for para os servers ficarem no modo highperformance
	edge_server edgeList [MAX_SERVERS];
	int totalTarefasRealizadas;
	int tempoMedioDeEspera;
	int tarefasMedioDeEspera;
	int totalTarefasNaoRealizadas;
	int endingFlag;
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
	int test;
	data data;
	struct _s3 *next;
} tasks;

tasks *taskList;

typedef struct{
	int id;
	int n_instru;
	int tChegada;
} taskSimplificada;

struct tm* getTime() {
	time_t now = time(NULL);
	struct tm *tm_struct = localtime(&now);
	return tm_struct;
}

struct timeval timeout;

pthread_mutex_t mutexFila = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t scheduler_cs= PTHREAD_COND_INITIALIZER;
pthread_cond_t dispatcher_cs= PTHREAD_COND_INITIALIZER;
pthread_cond_t monitor_cs = PTHREAD_COND_INITIALIZER;

pthread_mutex_t mutexUnnamedPipe = PTHREAD_MUTEX_INITIALIZER;

FILE *r;

void logFile(char *msg) {
	printf("%s\n", msg);
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
					if(sh_mem->slots <= 0)
						return -2;
					break;
				case 1:
					sh_mem->maxWait = atoi(newLine);
					if(sh_mem->maxWait <= 0)
						return -2;
					break;
				case 2:
					sh_mem->nEdgeServers = atoi(newLine);
					if(sh_mem->nEdgeServers > MAX_SERVERS)
						return -3;
					break;		
			}
			i++;
		}
		else{
			return -2;
		}
	}
	for(i = 0; i < sh_mem->nEdgeServers; i++){
		if(fgets(line, sizeof(line), f)){
			char *newLine = strtok(line, "\n");
			char *value = strtok(newLine, ",");
			strcpy(sh_mem->edgeList[i].name, value);
			if(strlen(sh_mem->edgeList[i].name) <= 0)
				return -2;
			value = strtok(NULL, ",");
			sh_mem->edgeList[i].capacidade1 = atoi(value);
			if(sh_mem->edgeList[i].capacidade1 <= 0)
				return -2;
			value = strtok(NULL, ",");
			sh_mem->edgeList[i].capacidade2 = atoi(value);
			if(sh_mem->edgeList[i].capacidade2 <= 0)
				return -2;
			sh_mem->edgeList[i].state = STOPPED;
			sh_mem->edgeList[i].nTarefasRealizadas = 0;
			sh_mem->edgeList[i].nOperacoesManutencao = 0;
		}
		else{
			return -2;
		}
	}
	
	
	sh_mem->highPerformanceFlag = 0;
	sh_mem->endingFlag = 0;
	
	sem_post(mutex);
	fclose(f);
	
	return 0;
}

void doTask(int n_instrucoes, int capacidade){
	while(n_instrucoes > 0){
		n_instrucoes -= capacidade;
		sleep(1);
	}
}

void *cpu1(void *t){
	fd_set read_set;
	char var1[MAX_CHAR*2];
	int counter = *((int *)t);
	
	sem_wait(mutex);
	close(sh_mem->edgeList[counter].channel[1]);
	sprintf(var1,  "%s CPU1 is READY", sh_mem->edgeList[counter].name);
	logFile(var1);
	sem_post(mutex);
	
	while(1){
		sem_wait(mutex);
		
		if(sh_mem->endingFlag == 1){
			sh_mem->edgeList[counter].cpuState1 = ENDING;
			sem_post(mutex);
			break;
		}
		sh_mem->edgeList[counter].cpuState1 = AVAILABLE;
		if(sh_mem->edgeList[counter].state == RUNNING){
			
			pthread_mutex_lock(&mutexUnnamedPipe);
			FD_ZERO(&read_set);
			FD_SET(sh_mem->edgeList[counter].channel[0], &read_set);

			if(select(sh_mem->edgeList[counter].channel[0]+1, &read_set, NULL, NULL, &timeout) > 0){
				
				taskSimplificada ts;
				read(sh_mem->edgeList[counter].channel[0], &ts, sizeof(taskSimplificada));
				sprintf(var1, "%s CPU1 RECEBEU TASK %d", sh_mem->edgeList[counter].name, ts.id);
				logFile(var1);
				sh_mem->edgeList[counter].cpuState1 = BUSY;
				sh_mem->edgeList[counter].timeNextTarefa1 = (int)(ts.n_instru/sh_mem->edgeList[counter].capacidade1);
				
				int instru = ts.n_instru;
				int cap = sh_mem->edgeList[counter].capacidade1;
				int timeAtual = (int)time(NULL);
				sh_mem->tempoMedioDeEspera += timeAtual - ts.tChegada;
				sh_mem->tarefasMedioDeEspera++;
				
				pthread_mutex_unlock(&mutexUnnamedPipe);
				sem_post(mutex);

				doTask(instru, cap);

				sem_wait(mutex);
				sprintf(var1, "%s CPU1 COMPLETOU TASK %d", sh_mem->edgeList[counter].name, ts.id);
				logFile(var1);
				sh_mem->totalTarefasRealizadas++;
				sh_mem->edgeList[counter].nTarefasRealizadas++;
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
	fd_set read_set;
	char var1[MAX_CHAR*2];
	int counter = *((int *)t);
	
	sem_wait(mutex);
	close(sh_mem->edgeList[counter].channel[1]);
	sprintf(var1,  "%s CPU2 is READY", sh_mem->edgeList[counter].name);
	logFile(var1);
	sem_post(mutex);
	
	while(1){
		sem_wait(mutex);
		
		if(sh_mem->endingFlag == 1){
			sh_mem->edgeList[counter].cpuState2 = ENDING;
			sem_post(mutex);
			break;
		}
		sh_mem->edgeList[counter].cpuState2 = AVAILABLE;
		if(sh_mem->edgeList[counter].state == RUNNING && sh_mem->highPerformanceFlag == 1){

			pthread_mutex_lock(&mutexUnnamedPipe);
			FD_ZERO(&read_set);
			FD_SET(sh_mem->edgeList[counter].channel[0], &read_set);
			
			if(select(sh_mem->edgeList[counter].channel[0]+1, &read_set, NULL, NULL, &timeout) > 0){
				
				taskSimplificada ts;
				read(sh_mem->edgeList[counter].channel[0], &ts, sizeof(taskSimplificada));
				sprintf(var1, "%s CPU2 RECEBEU TASK %d", sh_mem->edgeList[counter].name, ts.id);
				logFile(var1);
				sh_mem->edgeList[counter].cpuState2 = BUSY;
				sh_mem->edgeList[counter].timeNextTarefa2 = (int)(ts.n_instru/sh_mem->edgeList[counter].capacidade2);
				
				int instru = ts.n_instru;
				int cap = sh_mem->edgeList[counter].capacidade2;
				int timeAtual = (int)time(NULL);
				sh_mem->tempoMedioDeEspera += timeAtual - ts.tChegada;
				sh_mem->tarefasMedioDeEspera++;
				
				pthread_mutex_unlock(&mutexUnnamedPipe);
				sem_post(mutex);
				
				doTask(instru, cap);
				
				sem_wait(mutex);
				sprintf(var1, "%s CPU2 COMPLETOU TASK %d", sh_mem->edgeList[counter].name, ts.id);
				logFile(var1);
				sh_mem->totalTarefasRealizadas++;
				sh_mem->edgeList[counter].nTarefasRealizadas++;
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

void edgeServer(int counter){
	int i;
	char var[MAX_CHAR*2];
	pthread_t threads[NCPU];

	sem_post(serverMutex);

	pthread_create(&threads[0], NULL, cpu1, &counter);
	pthread_create(&threads[1], NULL, cpu2, &counter);
	
	sem_wait(mutex);
	sprintf(var, "%s IS READY", sh_mem->edgeList[counter].name);
	logFile(var);
	sh_mem->edgeList[counter].state = RUNNING;
	message msg;
	msg.mtype = QUEUE_MSG_TYPE;
	strcpy(msg.mesg_text, "alive");
	msgsnd(msgID, &msg, sizeof(message), 0); //informa o mm que esta vivo
	sem_post(mutex);
	
	while(1){
		msg.mtype = counter;
		msgrcv(msgID, &msg, sizeof(message), counter+1, 0);//aviso que tem que entrar em modo STOPPED
		
		sem_wait(mutex);
		sh_mem->edgeList[counter].state = STOPPED;
		sh_mem->edgeList[counter].nOperacoesManutencao++;
		sem_post(mutex);

		while(1){
			sem_wait(mutex);
			if(sh_mem->edgeList[counter].cpuState1 == AVAILABLE && sh_mem->edgeList[counter].cpuState2 == AVAILABLE)		
				break;
			sem_post(mutex);
		}

		msg.mtype = QUEUE_MSG_TYPE;
		strcpy(msg.mesg_text, "ready");
		msgsnd(msgID, &msg, sizeof(message), 0);
		
		sprintf(var, "%s HAS STOPPED", sh_mem->edgeList[counter].name);
		sem_post(mutex);
		logFile(var);
		
		msgrcv(msgID, &msg, sizeof(message), counter+1, 0);//aviso que pode voltar ao modo RUNNING
		
		sem_wait(mutex);
		sh_mem->edgeList[counter].state = RUNNING;
		sprintf(var, "%s IS BACK TO RUNNING", sh_mem->edgeList[counter].name);
		sem_post(mutex);
		logFile(var);
	}
	
	for(i = 0; i < NCPU; i++){
		pthread_join(threads[i], NULL);
  	}
}

void removeTarefasImpo__Prio(){
	char var[MAX_CHAR*2];
	tasks* t = taskList;
	int timeAtual = (int)time(NULL);
	while(t != NULL){
		if(t->data.id == -1){
			t->data.prio = 1000;
		}
		else{
			if(t->data.tMax <= timeAtual){
				sprintf(var, "TASK %d REMOVED", t->data.id);
				logFile(var);

				t->data.id = -1;//-1 representa que o lugar na lista nao esta ocupado
				t->data.prio = 1000;

				sem_wait(mutex);
				sh_mem->currentTasksN--;
				sh_mem->totalTarefasNaoRealizadas++;
				sem_post(mutex);
			}
			else{
				t->data.prio = t->data.tMax - timeAtual;//no maximo este valor pode ser 1, desempatado pela ordem de chegada
			}
		}
		t=t->next;
	}
}

void ordenar(){
	tasks* current = taskList;
	tasks* index;
	tasks* temp = (tasks*)malloc(sizeof(tasks));
	while(current != NULL){
		index = current->next;
		while(index != NULL){
			if(current->data.prio > index->data.prio){
				temp->data = current->data;
				current->data = index->data;
				index->data = temp->data;
			}
			if(current->data.prio == index->data.prio){
				if(current->data.tChegada > index->data.tChegada){
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

void *scheduler(void *t){
	logFile("STARTING SCHEDULER");
	while(1){
		pthread_mutex_lock(&mutexFila);
		pthread_cond_wait(&scheduler_cs, &mutexFila);

		removeTarefasImpo__Prio();
		ordenar();

		pthread_mutex_unlock(&mutexFila);
	}
	pthread_exit(NULL);
}

int verificaExistirTarefas(){
	pthread_mutex_lock(&mutexFila);
	tasks* t = taskList;
	while(t != NULL){
		if(t->data.id != -1){
			pthread_mutex_unlock(&mutexFila);
			return 1;
		}
		t=t->next;
	}
	pthread_mutex_unlock(&mutexFila);
	return 0;
}

int tryDispatchTask(tasks* t){
	sem_wait(mutex); 
	for(int i=0; i < sh_mem->nEdgeServers; i++){
		if(sh_mem->edgeList[i].state == RUNNING){
			if(sh_mem->edgeList[i].cpuState1 == AVAILABLE){
				if((int)(t->data.n_instrucoes/sh_mem->edgeList[i].capacidade1) + (int)time(NULL) < t->data.tMax){
				
					taskSimplificada ts;
					ts.id = t->data.id;
					ts.n_instru = t->data.n_instrucoes;
					ts.tChegada = t->data.tChegada;
					write(sh_mem->edgeList[i].channel[1], &ts, sizeof(taskSimplificada));
				
					t->data.id = -1;
					sh_mem->currentTasksN--;
					sem_post(mutex);
					return 1;
			}
		}
			if(sh_mem->highPerformanceFlag == 1){
				if(sh_mem->edgeList[i].cpuState2 == AVAILABLE){
					if((int)(t->data.n_instrucoes/sh_mem->edgeList[i].capacidade2) + (int)time(NULL) < t->data.tMax){

						taskSimplificada ts;
						ts.id = t->data.id;
						ts.n_instru = t->data.n_instrucoes;
						ts.tChegada = t->data.tChegada;
						write(sh_mem->edgeList[i].channel[1], &ts, sizeof(taskSimplificada));
				
						t->data.id = -1;
						sh_mem->currentTasksN--;
						sem_post(mutex);
						return 1;
					}
				}
			}
		}
	}
	sem_post(mutex);
	return -1;
}

int taskValida(tasks* t){
	sem_wait(mutex); 
	for(int i=0; i < sh_mem->nEdgeServers; i++){
		if((int)(t->data.n_instrucoes/sh_mem->edgeList[i].capacidade1) + (int)time(NULL) < t->data.tMax){
			sem_post(mutex);
			return 1;
		}
		if((int)(t->data.n_instrucoes/sh_mem->edgeList[i].capacidade2) + (int)time(NULL) < t->data.tMax){
			sem_post(mutex);
			return 1;
		}
	}

	t->data.id=-1;
	sh_mem->currentTasksN--;
	sh_mem->totalTarefasNaoRealizadas++;
	sem_post(mutex);
	return -1;
}

int verificaCPU(){
	sem_wait(mutex);
	for(int i=0; i < sh_mem->nEdgeServers; i++){
		if(sh_mem->edgeList[i].state == RUNNING){
			if(sh_mem->edgeList[i].cpuState1 == AVAILABLE){
				sem_post(mutex);
				return 1;
			}
			if(sh_mem->highPerformanceFlag == 1){
				if(sh_mem->edgeList[i].cpuState2 == AVAILABLE){
					sem_post(mutex);
					return 2;
				}
			}	
		}
	}
	sem_post(mutex);
	return 0;
}

void *dispatcher(void *t){
	char var[MAX_CHAR*2];
	int id;
	logFile("STARTING DISPATCHER");
	while(1){
		
		while(verificaExistirTarefas() == 0 && verificaCPU() == 0);
		pthread_mutex_lock(&mutexFila);
		tasks* t = taskList;
		while(t != NULL){//esta sempre ordenado por prioridade por isso apenas temos que percorrer e encontrar uma que seja possivel fazer
			if(t->data.id != -1){
				id = t->data.id;
				if(taskValida(t) == 1){
					if(tryDispatchTask(t) == 1)
						break;
					}
				else{
					sprintf(var, "TASK %d REMOVED1", id);
					logFile(var);
				}
			}
			t=t->next;
		}
		pthread_mutex_unlock(&mutexFila);
	}
	pthread_exit(NULL);
}

void taskManager(){
	logFile("PROCESS TASK_MANAGER CREATED");

	int fd;
	char str1[MAX_CHAR];
	
	timeout.tv_sec = 0;
	timeout.tv_usec = 10;

	sem_unlink("SERVERMUTEX");
	serverMutex = sem_open("SERVERMUTEX", O_CREAT | O_EXCL, 0700, 1);
	
	sem_wait(mutex);
	
	for(int i = 0; i < sh_mem->nEdgeServers; i++){
		sem_wait(serverMutex);
		pipe(sh_mem->edgeList[i].channel);
		if(fork() == 0){
			edgeServer(i);	
			exit(0);
		}
	}

	sem_post(mutex); 
	
	sem_wait(serverMutex);//para apenas avancar quando os servers todos tiverem preparados
	sem_post(serverMutex);

	//criacao da fila de tasks
	pthread_mutex_lock(&mutexFila);
	tasks* t = (tasks*)malloc(sizeof(tasks));
	t->data.id = -1;// e utilizado -1 quando o espaco na lista nao esta ocupado
	t->data.prio = 1000;
	t->next = NULL;
	taskList = t;
	for(int i = 1; i < sh_mem->slots; i++){
		tasks *t1 = (tasks*)malloc(sizeof(tasks));
		t1->data.id = -1;
		t1->data.prio = 1000;
		t1->next = NULL;
		t->next = t1;
		t = t1;
	}
	pthread_mutex_unlock(&mutexFila);

  	pthread_t thread_id[2];
  	
	pthread_create(&thread_id[0], NULL, scheduler, NULL);
	pthread_create(&thread_id[1], NULL, dispatcher, NULL);
	
	while(1){
		fd = open(task_pipe, O_RDONLY);	
		read(fd, str1, MAX_CHAR);
		if(strcmp(str1, "EXIT") == 0){
			logFile("EXIT COMMAND RECEIVED");
			kill(systemManagerPID, SIGINT);
		}
		else if(strcmp(str1, "STATS") == 0){
			logFile("STATS COMMAND RECEIVED");
			sem_wait(mutex);
		
			printf("TOTAL TAREFAS: %d\n", sh_mem->totalTarefasRealizadas);
		
			if(sh_mem->tarefasMedioDeEspera == 0)
				printf("TEMPO1 MEDIO DE ESPERA: 0 sec\n");
			else	
				printf("TEMPO2 MEDIO DE ESPERA: %d sec\n", sh_mem->tempoMedioDeEspera / sh_mem->tarefasMedioDeEspera);
		
			for(int i=0; i < sh_mem->nEdgeServers; i++){
				printf("%s -> %d TAREFAS, %d OPERACOES DE MANUTENCAO\n", sh_mem->edgeList[i].name, sh_mem->edgeList[i].nTarefasRealizadas, sh_mem->edgeList[i].nOperacoesManutencao);
			}
		
			printf("TOTAL DE TAREFAS NAO EXECUTADAS: %d\n", sh_mem->totalTarefasNaoRealizadas);
			sleep(5);//para dar tempo para ler
			
			sem_post(mutex);
		}
		else{
			int id = atoi(strtok(str1, ";"));
			int n_inst = atoi(strtok(NULL, ";"));
			int tMax = atoi(strtok(NULL, ";"));
			pthread_mutex_lock(&mutexFila);
			insertFila(id, n_inst, tMax);
			pthread_cond_signal(&scheduler_cs);
			pthread_mutex_unlock(&mutexFila);
			sem_wait(mutex);
			if(sh_mem->endingFlag == 1){
				close(fd);
				sem_post(mutex);
				break;
			}
			sem_post(mutex);
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
		if(t->data.id == -1){
			t->data.id = id;
			t->data.n_instrucoes = n_inst;
			t->data.tMax = (int)time(NULL) + tMax;
			t->data.tChegada = (int)time(NULL);
			t->data.prio = 0;
			
			sem_wait(mutex);
			sh_mem->currentTasksN++;
			sem_post(mutex);

			logFile("NEW TASK INSERTED");
			return;
		}
		t=t->next;
	}
	logFile("QUEUE FULL => TASK REFUSED");
	sem_wait(mutex);
	sh_mem->totalTarefasNaoRealizadas++;
	sem_wait(mutex);
}

int tempoEspera(){
	for(int i = 0; i < sh_mem->nEdgeServers; i++){
		if(sh_mem->edgeList[i].state == RUNNING){
			if(sh_mem->edgeList[i].timeNextTarefa1 <= sh_mem->maxWait)
				return 0;
			if(sh_mem->edgeList[i].timeNextTarefa2 <= sh_mem->maxWait)
				return 0;	
		}
	}
	return 1;
}

void monitor(){
	float var;
	logFile("PROCESS MONITOR CREATED");
	while(1){
		while(1){
			sem_wait(mutex);
			var = sh_mem->currentTasksN / sh_mem->slots;
			if(var > 0.8 && tempoEspera() == 1){
				sh_mem->highPerformanceFlag = 1; 
				logFile("HIGHPERFORMANCE FLAG ATIVATED");
				sem_post(mutex);
				break;
			}
			sem_post(mutex);
		}
		while(1){
			sem_wait(mutex);
			var = sh_mem->currentTasksN / sh_mem->slots;
			if(var <= 0.2){
				sh_mem->highPerformanceFlag = 0;
				logFile("HIGHPERFORMANCE FLAG DEATIVATED");
				sem_post(mutex);
				break;
			}
			sem_post(mutex);
		}
	}
	exit(0);
}

void maintenanceManager(){
	srandom(getpid());
	int randomStop;
	int randomIntervalo;
	int min = 1;
	int max = 5;
	message msg;
	
	logFile("PROCESS MAINTENANCE MANAGER CREATED");
	
	sem_wait(mutex);
	int totalServers = sh_mem-> nEdgeServers;
	sem_post(mutex);
	
	for(int i = 0; i < totalServers; i++){//esperar pela mensagem de alive dos servers
		msgrcv(msgID, &msg, sizeof(message), QUEUE_MSG_TYPE, 0);
	}
	
	logFile("MAINTENANCE MANAGER HAS BEGUN STOPPING SERVERS");
	while(1){
		for(int i = 0; i < totalServers; i++){
			sem_wait(mutex);
			if(sh_mem->endingFlag == 1){
				sem_post(mutex);
				exit(0);
			}
			sem_post(mutex);
			
			strcpy(msg.mesg_text, "PrepareStop");
			msg.mtype = i+1;
			msgsnd(msgID, &msg, sizeof(message), 0);//mensagem para o servidor com o aviso de que vai haver uma intervencao
			randomStop = (random() % (max + 1 - min)) + min;

			msg.mtype = QUEUE_MSG_TYPE;
			msgrcv(msgID, &msg, sizeof(message), QUEUE_MSG_TYPE, 0);//mensagem do servidor a dizer que esta preparado para entrar em modo stopped
			sleep(randomStop);

			strcpy(msg.mesg_text, "StartRunning");
			msg.mtype = i+1;
			msgsnd(msgID, &msg, sizeof(message), 0);//informa o servidor que pode voltar a correr
			
			randomIntervalo = (random() % (max + 1 - min)) + min;
			sleep(randomIntervalo);
		}
	}
	exit(0);
}

int cpuEnding(){
	for(int i = 0; i < sh_mem->nEdgeServers; i++){
		if(sh_mem->edgeList[i].cpuState1 != ENDING)
			return -1;
		if(sh_mem->edgeList[i].cpuState2 != ENDING)
			return -1;	
	}
	return 0;
}

void checkTasks(){
	pthread_mutex_lock(&mutexFila);
	tasks* t;
	char var[MAX_CHAR*2];
	while(taskList != NULL){
		if(taskList->data.id != -1){
			sprintf(var, "TASK %d NOT EXECUTED", t->data.id);
			logFile(var);
			sem_wait(mutex);
			sh_mem->totalTarefasNaoRealizadas++;
			sem_post(mutex);
		}
		t=taskList;
		taskList = taskList->next;
		free(t);
	}
	pthread_mutex_unlock(&mutexFila);
}




void sigCleanup(){
	if((int)systemManagerPID == (int)getpid()){
		logFile("SIGNAL SIGINT RECEIVED");
		
		sem_wait(mutex);
		sh_mem->endingFlag = 1;
		sem_post(mutex);
		logFile("SIMULATOR WAITING FOR LAST TASKS TO FINISH");
		while(1){
			sem_wait(mutex);
			if(cpuEnding() == 0){
				sem_post(mutex);
				break;
			}
			sem_post(mutex);
		}
		checkTasks();
		
		stats();
		
		for(int i = 0; i < sh_mem->nEdgeServers; i++){
			close(sh_mem->edgeList[i].channel[1]);
			close(sh_mem->edgeList[i].channel[0]);
		}
		
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



void stats(){
	if((int)systemManagerPID == (int)getpid()){
		logFile("SIGNAL SIGTSTP RECEIVED");

		sem_wait(mutex);
		
		printf("TOTAL TAREFAS: %d\n", sh_mem->totalTarefasRealizadas);
		
		if(sh_mem->tarefasMedioDeEspera == 0)
			printf("TEMPO1 MEDIO DE ESPERA: 0 sec\n");
		else	
			printf("TEMPO2 MEDIO DE ESPERA: %d sec\n", sh_mem->tempoMedioDeEspera / sh_mem->tarefasMedioDeEspera);
		
		for(int i=0; i < sh_mem->nEdgeServers; i++){
			printf("%s -> %d TAREFAS, %d OPERACOES DE MANUTENCAO\n", sh_mem->edgeList[i].name, sh_mem->edgeList[i].nTarefasRealizadas, sh_mem->edgeList[i].nOperacoesManutencao);
		}
		
		printf("TOTAL DE TAREFAS NAO EXECUTADAS: %d\n", sh_mem->totalTarefasNaoRealizadas);
		sleep(5);//para dar tempo para ler
		
		sem_post(mutex);
		
	}
}

void systemManager(char *filename){
	systemManagerPID = getpid();
	
	signal(SIGINT, sigCleanup);
	signal(SIGTSTP, stats);
	
	r = fopen("log.txt", "w");
	fclose(r);

	r = fopen("log.txt", "a");
	
	
	
	logFile("OFFLOAD SIMULATOR STARTING");
	
	if((shmid = shmget(IPC_PRIVATE, sizeof(shm_struct), IPC_CREAT|0776)) < 0){
		logFile("ERROR CREATING SHARED MEMORY");
		perror("Erro ao criar a memoria partilhada");
		sigCleanup();
		exit(1);
	}
	
	if((sh_mem = (shm_struct*) shmat(shmid, NULL, 0)) < 0){
		logFile("ERROR MAPING SHARED MEMORY");
		perror("Erro ao mapear a memoria partilhada");
		sigCleanup();
		exit(1);
	}
	
	
	
	sem_unlink("MUTEX");
	mutex = sem_open("MUTEX", O_CREAT | O_EXCL, 0700, 1);
	
	sem_wait(mutex);

	sh_mem->currentTasksN = 0;
	sh_mem->totalTarefasRealizadas = 0;
	sh_mem->tempoMedioDeEspera = 0;
	sh_mem->tarefasMedioDeEspera = 0;
	sh_mem->totalTarefasNaoRealizadas = 0;
	
	sem_post(mutex);

	systemManagerPID = getpid();

	int a = readFile(filename);
	if(a == -1){
		logFile("ERROR READING CONFIG FILE");
		perror("Erro ao ler o ficheiro de configuracoes");
		sigCleanup();
		exit(1);
	}
	if(a == -2){
		logFile("INVALID CONFIG FILE");
		perror("Ficheiro de configuracoes invalido");
		sigCleanup();
		exit(1);
	}
	if(a == -3){
		logFile("INVALID NUMBER OF SERVERS");
		perror("Numero de servers ultrapassa o maximo estabelecido");
		sigCleanup();
		exit(1);
	}

	if(mkfifo(task_pipe, 0666) == -1){
		printf("Named pipe already exists or an error ocurred\n");
	}
	else{
		printf("Named pipe created\n");
	}
	
	msgID = msgget(IPC_PRIVATE, IPC_CREAT|0777);
	if(msgID < 0){
		perror("Creating message queue");
		sigCleanup();
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
		char *token = strtok(comando, "\n");
		const char s[2] = " ";
		char *check_comando = strtok(token, s);
		if(strcmp(check_comando, "offload_simulator") == 0){
			
			char *filename = strtok(NULL, s);
			//char *filename = "config_file.txt";
			systemManager(filename);
		}
	}	
}
