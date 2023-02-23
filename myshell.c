#include <stdio.h>
#include "parser.h"
#include<signal.h>
#include<sys/types.h>
#include<sys/wait.h>
#include<unistd.h>
#include<stdlib.h>
#include<fcntl.h>
#include<string.h>
#include <sys/stat.h>

typedef struct {
	pid_t pid; // pid del proceso
	int terminado; // 1 si terminado. 0 si no terminado
} TProceso;

typedef struct {
	char nombre[1024]; // nombre completo de la instruccion
	int numComandos; // numero de procesos conectados por pipes
	int numTerminados; // cantidad de procesos terminados
	TProceso *lista; // lista con los pids de todos los procesos y si estan terminados o no
} TInstruccion;

// VARIABLES GLOBALES. para que el manejador de señales pueda acceder a ellas
TInstruccion *lista; // lista con todas las instrucciones en background
int posicionfg; // posicion de la instruccion a la que se ha hecho fg. Si no hay ninguna vale 0

// MANEJADOR
void manejador(int sign) {
	int i;
	if(sign==SIGINT) {
		if(posicionfg!=0) {
			for(i=0; i<(lista+posicionfg-1)->numComandos; i++) {
				kill(((lista+posicionfg-1)->lista+i)->pid ,9);
			}
			posicionfg=0;
		}
		else {
			// volver a mostrar el prompt
		}
	}
}

// FUNCION CD
void cd(int argc, char *argv[]) {
	char buffer[100];
	if(argc>=3) {
		fprintf(stderr, "cd: demasiados argumentos\n");
	}
	if(argc==1) {
		if(chdir(getenv("HOME"))!=0) {
			fprintf(stderr, "Error\n");
		}
	}
	else {
		if(chdir(argv[1])!=0) {
			fprintf(stderr, "Error\n");
		}
	}
	printf("%s\n", getcwd(buffer, 100));
}

// FUNCION JOBS
void jobs(int argc, int *cantidad) {
	int i, j;
	if(argc>=2) {
		fprintf(stderr, "jobs: demasiados argumentos\n");
	}
	else {
		for(i=0; i<*cantidad; i++) {
			for(j=0; j<(lista+i)->numComandos; j++) {
				if(((lista+i)->lista+j)->terminado==0) { // no ha termiando
					if (waitpid(((lista+i)->lista+j)->pid, NULL, WNOHANG)!=0) { // ha acabado
						(((lista+i)->lista+j)->terminado)=1; // se marca como acabado
						(lista+i)->numTerminados++;
					}
				}
			}
			if((lista+i)->numComandos==(lista+i)->numTerminados) {
				if(i==*cantidad-1) {
					printf("[%d]+ Hecho			%s\n", i+1, (lista+i)->nombre);
					(lista+i)->numTerminados++; // para evitar que se vuelva a mostrar
				}
				else if(i==*cantidad-2) {
					printf("[%d]- Hecho			%s\n", i+1, (lista+i)->nombre);
					(lista+i)->numTerminados++;
				}
				else {
					printf("[%d]  Hecho			%s\n", i+1, (lista+i)->nombre);
					(lista+i)->numTerminados++;
				}
			}
			else if((lista+i)->numComandos>(lista+i)->numTerminados) {
				if(i==*cantidad-1) {
					printf("[%d]+ Ejecutando			%s\n", i+1, (lista+i)->nombre);
				}
				else if(i==*cantidad-2) {
					printf("[%d]- Ejecutando			%s\n", i+1, (lista+i)->nombre);
				}
				else {
					printf("[%d]  Ejecutando			%s\n", i+1, (lista+i)->nombre);
				}
			}
		}
		i=*cantidad-1;
		while(i>=0) { // se eliminan todos los terminados que no tengan ninguno sin terminar delante
			if((lista+i)->numComandos<=(lista+i)->numTerminados) {
				free((lista+i)->lista);
				(*cantidad)--;
				lista = realloc(lista, *cantidad*sizeof(TInstruccion));
				i--;
			}
			else {
				i=-1;
			}
		}
	}
}

// FUNCION FG
void fg(int argc, char **argv, int *cantidad) {
	int num;
	int i;
	if(*cantidad==0) {
		if(argc==1) {
			printf("fg: actual: no existe ese trabajo\n");
		}
		else {
			num = (int) strtol(argv[1], NULL, 10);
			printf("fg: %d: no existe ese trabajo\n", num);
		}
	}
	else {
		if(argc==1) { // se pasa a fg la ultima instruccion en bg
			num=*cantidad;
		}
		else { // se pasa a fg la intruccion en bg indicada por num
			num = (int) strtol(argv[1], NULL, 10);
		}
		if(num<1 || num>*cantidad) {
			printf("fg: %d: no existe ese trabajo\n", num);
			posicionfg=0;
		}
		else {
			printf("%s\n", (lista+num-1)->nombre);
			(lista+num-1)->numTerminados=(lista+num-1)->numComandos+1; // para que no lo muestre como hecho
			posicionfg=num; // por si llega la señal SIGINT
			for(i=0; i<(lista+num-1)->numComandos; i++) {
				waitpid(((lista+num-1)->lista+i)->pid, NULL, 0);
			}
		}
	}
}

// FUNCION UMASK
void masc(int argc, char **argv, char *mascara) {
	if(argc==1) {
		printf("%s\n", mascara);
	}
	else {
		if(strlen(argv[1])>4 || strstr(argv[1], "9")!=NULL) {
			fprintf(stderr, "umask: %s: numero octal fuera del intervalo\n", argv[1]);
		}
		else {
			if(strlen(argv[1])==1) {
				mascara[0]='0';
				mascara[1]='0';
				mascara[2]='0';
				strcpy(mascara+3, argv[1]);
			}
			else if(strlen(argv[1])==2) {
				mascara[0]='0';
				mascara[1]='0';
				strcpy(mascara+2, argv[1]);
			}
			else if(strlen(argv[1])==3) {
				mascara[0]='0';
				strcpy(mascara+1, argv[1]);
			}
			else {
				strcpy(mascara, argv[1]);
				mascara[0]='0';
			}
			umask((int) strtol(mascara, NULL, 8));
		}
	}
}

int main(void) {
	char buf[1024];
	tline * line;
	int i, j;
	
	// PIPES
	pid_t pid;
	pid_t *listapids;
	int tuberia1[2];
	int tuberia2[2];
	int tuberia1o2; // para alternar las tuberias
	
	// REDIRECCION. booleanos y descriptores de fichero
	int fichEntrada;
	int fdEntrada;
	int fichSalida;
	int fdSalida;
	int fichError;
	int fdError;
	
	// BACKGROUND
	int backg; // booleano
	int cantidad; // tamaño de la lista de instrucciones en background
	
	// UMASK. mascara actual y mascara de redireccion de ficheros
	char mascara[5]="0002";
	mode_t modo = S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH;
	
	cantidad=0;
	posicionfg=0;
	lista = (TInstruccion *) malloc(0*sizeof(TInstruccion));
	
	signal(SIGINT, manejador);
	umask(2); // iniciar mascara por defecto
	printf("msh> ");
	while(fgets(buf, 1024, stdin)) {
		line = tokenize(buf);
		// hay que comprobar si alguna instruccion en background ha terminado
		if(line->ncommands==0 || strcmp(line->commands[0].argv[0], "jobs")!=0) { 
			for(i=0; i<cantidad; i++) {
				for(j=0; j<(lista+i)->numComandos; j++) {
					if(((lista+i)->lista+j)->terminado==0) { // no estaba terminado
						if (waitpid(((lista+i)->lista+j)->pid, NULL, WNOHANG)!=0) { // ha acabado
							((lista+i)->lista+j)->terminado=1; // se marca como acabado
							(lista+i)->numTerminados++;
						}
					}
				}
			}
		}
		if(line==NULL) {
			continue;
		}
		if(line->redirect_input!=NULL) {
			fichEntrada=1;
		}
		else fichEntrada=0;
		if(line->redirect_output!=NULL) {
			fichSalida=1;
		}
		else fichSalida=0;
		if(line->redirect_error!=NULL) {
			fichError=1;
		}
		else fichError=0;
		if(line->background) {
			backg=1;
		}
		else backg=0;
		// UN SOLO COMANDO
		if(line->ncommands==1) {
			if(strcmp(line->commands[0].argv[0], "cd")==0) { // CD
				cd(line->commands[0].argc, line->commands[0].argv);
			}
			else if(strcmp(line->commands[0].argv[0], "jobs")==0) { // JOBS
				jobs(line->commands[0].argc, &cantidad); // como puntero para poder modificarlo
			}
			else if(strcmp(line->commands[0].argv[0], "fg")==0) { // FG
				fg(line->commands[0].argc, line->commands[0].argv, &cantidad); // como puntero para poder modificarlo
			}
			else if(strcmp(line->commands[0].argv[0], "umask")==0) { // UMASK
				masc(line->commands[0].argc, line->commands[0].argv, mascara);
			}
			else if(strcmp(line->commands[0].argv[0], "exit")==0) { // EXIT
				for(i=0; i<cantidad; i++) {
					for(j=0; j<(lista+i)->numComandos; j++) {
						if(((lista+i)->lista+j)->terminado==0) {
							kill(((lista+i)->lista+j)->pid, 9);
						}
					}
				}
				free(lista);
				exit(0);
			}
			else if(line->commands[0].filename==NULL) { // ningun mandato
				fprintf(stderr, "%s: No se encuentra el mandato\n", line->commands[0].argv[0]);
			}
			else { // cualquier otro mandato
				pid=fork();
				if(pid<0) { // error
					fprintf(stderr, "Error al crear el proceso");
				}
				else if(pid==0) { // hijo
					if(backg==0) {
						signal(SIGINT, SIG_DFL);
					}
					else {
						signal(SIGINT, SIG_IGN);
					}
					if(fichEntrada==1) {
						fdEntrada = open(line->redirect_input, O_RDONLY);
						if(fdEntrada==-1) {
							printf("%s. Error al acceder al fichero\n", line->redirect_input);
							exit(1);
						}
						else {
							dup2(fdEntrada, STDIN_FILENO);
							close(fdEntrada);
						}
					}
					if(fichSalida==1) {
						fdSalida = creat(line->redirect_output, modo);
						if(fdSalida==-1) {
							printf("%s. Error al acceder al fichero\n", line->redirect_output);
							exit(1);
						}
						else {
							dup2(fdSalida, STDOUT_FILENO);
							close(fdSalida);
						}
					}
					if(fichError==1) {
						fdError = creat(line->redirect_error, modo);
						if(fdError==-1) {
							printf("%s. Error al acceder al fichero\n", line->redirect_error);
							exit(1);
						}
						else {
							dup2(fdError, STDERR_FILENO);
							close(fdError);
						}
					}
					execvp(line->commands[0].filename, line->commands[0].argv);
					printf("%s: Error al ejecutar el mandato \n", line->commands[0].argv[0]);
					exit(1);
				}
				else { // padre
					if(backg==1) {
						cantidad++;
						lista = realloc(lista, cantidad*sizeof(TInstruccion));
						(lista+cantidad-1)->numComandos = 1;
						buf[strlen(buf)-1]='\0'; // eliminar salto linea
						strcpy((lista+cantidad-1)->nombre, buf);
						(lista+cantidad-1)->lista = (TProceso *) malloc((lista+cantidad-1)->numComandos*sizeof(TProceso));
						((lista+cantidad-1)->lista->pid) = pid;
						((lista+cantidad-1)->lista->terminado) = 0;
						(lista+cantidad-1)->numTerminados=0;
						printf("[%d] %d\n", cantidad, pid);
					}
					else {
						waitpid(pid, NULL, 0);
					}
				}
			}
		}
		// DOS O MAS COMANDOS
		else if(line->ncommands>=2) {
			listapids = (pid_t *) malloc(line->ncommands*sizeof(pid_t));
			pipe(tuberia1);
			pipe(tuberia2);
			tuberia1o2=1;
			for(i=0; i<line->ncommands; i++) {
				pid = fork();
				if(pid<0) { // error
					fprintf(stderr, "Error al crear el proceso\n");
				}
				else if(pid==0) { // hijo
					if(backg==0) {
						signal(SIGINT, SIG_DFL);
					}
					else {
						signal(SIGINT, SIG_IGN);
					}
					// si el comando en la posicion i es cd, jobs, fg, umask o exit, no se ejecutan
					if(strcmp(line->commands[i].argv[0], "cd")==0) {
						
					}
					else if(strcmp(line->commands[i].argv[0], "jobs")==0) {
						
					}
					else if(strcmp(line->commands[i].argv[0], "fg")==0) {
						
					}
					else if(strcmp(line->commands[i].argv[0], "umask")==0) {
						
					}
					else if(strcmp(line->commands[i].argv[0], "exit")==0) {
						
					}
					else if(line->commands[i].filename==NULL) { // no es ningun mandato
						printf("%s: No se encuentra el mandato\n", line->commands[i].argv[0]);
					}
					if(i==0) { // primer hijo, redirecciona entrada
						if(fichEntrada==1) {
							fdEntrada = open(line->redirect_input, O_RDONLY);
							if(fdEntrada==-1) {
								printf("%s. Error al acceder al fichero\n", line->redirect_input);
								exit(1);
							}
							else {
								dup2(fdEntrada, STDIN_FILENO);
								close(fdEntrada);
							}
						}
						close(tuberia2[0]);
						close(tuberia2[1]);
						close(tuberia1[0]);
						dup2(tuberia1[1], STDOUT_FILENO);
						close(tuberia1[1]);
					}
					else if(i==line->ncommands-1) { // ultimo hijo, redirecciona salida y error
						if(fichSalida==1) { // hay que redirigir la salida
							fdSalida = creat(line->redirect_output, modo);
							if(fdSalida==-1) {
								printf("%s. Error al acceder al fichero\n", line->redirect_output);
								exit(1);
							}
							else {
								dup2(fdSalida, STDOUT_FILENO);
								close(fdSalida);
							}
						}
						if(fichError==1) { // hay que redirigir errores
							fdError = creat(line->redirect_error, modo);
							if(fdError==-1) {
								printf("%s. Error al acceder al fichero\n", line->redirect_error);
								exit(1);
							}
							else {
								dup2(fdError, STDERR_FILENO);
								close(fdError);
							}
						}
						if(tuberia1o2==0) { // le toca leer del pipe 1
							close(tuberia1[1]);
							close(tuberia2[0]);
							close(tuberia2[1]);
							dup2(tuberia1[0], STDIN_FILENO);
							close(tuberia1[0]);
						}
						else { // le toca leer del pipe 2
							close(tuberia1[0]);
							close(tuberia2[1]);
							close(tuberia1[1]);
							dup2(tuberia2[0], STDIN_FILENO);
							close(tuberia2[0]);
						}
					}
					else { // hijo intermedio
						if(tuberia1o2==0) { // le toca leer del pipe 1 y escirbir en el pipe 2
							close(tuberia1[1]);
							close(tuberia2[0]);
							dup2(tuberia2[1], STDOUT_FILENO);
							close(tuberia2[1]);
							dup2(tuberia1[0], STDIN_FILENO);
							close(tuberia1[0]);
						}
						else { // le toca leer del pipe 2 y escribir en el pipe 1
							close(tuberia1[0]);
							close(tuberia2[1]);
							dup2(tuberia1[1], STDOUT_FILENO);
							close(tuberia1[1]);
							dup2(tuberia2[0], STDIN_FILENO);
							close(tuberia2[0]);
						}
					}
					execvp(line->commands[i].filename, line->commands[i].argv);
					printf("%s. No se encuentra el mandato\n", line->commands[1].filename);
					exit(1);
				}
				else { // padre
					*(listapids+i)=pid;
					if(tuberia1o2==1) { // hay que crear un nuevo pipe 2
						close(tuberia2[0]);
						close(tuberia2[1]);
						pipe(tuberia2);
						tuberia1o2 = 0;
					}
					else { // hay que crear un nuevo pipe 1
						close(tuberia1[0]);
						close(tuberia1[1]);
						pipe(tuberia1);
						tuberia1o2 = 1;
					}
				}
			}
			close(tuberia1[0]);
			close(tuberia1[1]);
			close(tuberia2[0]);
			close(tuberia2[1]);
			if(backg==1) {
				cantidad++;
				lista = realloc(lista, cantidad*sizeof(TInstruccion));
				(lista+cantidad-1)->numComandos = line->ncommands;
				buf[strlen(buf)-1]='\0'; // eliminar salto linea
				strcpy((lista+cantidad-1)->nombre, buf);
				(lista+cantidad-1)->lista = (TProceso *) malloc((lista+cantidad-1)->numComandos*sizeof(TProceso));
				for(i=0; i<line->ncommands; i++) {
					(((lista+cantidad-1)->lista+i)->pid) = *(listapids+i);
					(((lista+cantidad-1)->lista+i)->terminado) = 0;
				}
				(lista+cantidad-1)->numTerminados=0;
				printf("[%d] %d\n", cantidad, *listapids);
			}
			else {
				for(i=0; i<line->ncommands; i++) { // el padre espera a todos los hijos
					waitpid(*(listapids+i), NULL, 0);
				}
			}
			free(listapids);
		}
		// hay que mostrar las instrucciones en background terminadas
		if(line->ncommands==0 || strcmp(line->commands[0].argv[0], "jobs")!=0) {
			for(i=0; i<cantidad; i++) {
				if((lista+i)->numComandos==(lista+i)->numTerminados) {
					if(i==cantidad-1) {
						printf("[%d]+ Hecho			%s\n", i+1, (lista+i)->nombre);
						(lista+i)->numTerminados++; // para evitar que se vuelva a mostrar
					}
					else if(i==cantidad-2) {
						printf("[%d]- Hecho			%s\n", i+1, (lista+i)->nombre);
						(lista+i)->numTerminados++;
					}
					else {
						printf("[%d]  Hecho			%s\n", i+1, (lista+i)->nombre);
						(lista+i)->numTerminados++;
					}
				}
			}
			i=cantidad-1;
			while(i>=0) { // se eliminan todos los terminados que no tengan ninguno sin terminar delante
				if((lista+i)->numComandos<=(lista+i)->numTerminados) {
					free((lista+i)->lista);
					cantidad--;
					lista = realloc(lista, cantidad*sizeof(TInstruccion));
					i--;
				}
				else {
					i=-1;
				}
			}
		}
		printf("msh> ");
	}
}
