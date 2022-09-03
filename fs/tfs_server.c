#include "operations.h"
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <pthread.h>

#define MAX_BUF 40
#define TAKEN 1
#define FREE 0
#define S 20

int handler(void* message);
void* reader();

int fcli[S];/*nºsessao*/
char* pipeCliName [S];/*path nessa sessao*/
int fserv;
char* pipeServName;

pthread_t threads[S];
pthread_mutex_t mutex;
pthread_mutex_t mutexHandle;
pthread_cond_t canRead, canHandle;

int main(int argc, char **argv) {
    ssize_t n;

    if (argc < 2) {
        printf("Please specify the pathname of the server's pipe.\n");
        return 1;
    }

    char *pipename = argv[1];
    printf("Starting TecnicoFS server with pipe called %s\n", pipename);

    unlink(pipename);
    errno = 0;

    if (mkfifo (pipename, 0777) < 0){
        return -1;}
    
    if ((fserv = open (pipename, O_RDONLY)) < 0) 
        return -1;

    pipeServName = pipename;

    n = tfs_init();
    if(n<0)
        return -1;

    n = pthread_cond_init(&canRead,0);
    if(n<0)
        return -1;

    pthread_mutex_init(&mutex,0);
    if(n<0)
        return -1;

    for(int i=0; i<S; i++){
        fcli[i] = -1;
    }

    for(int i=0; i<S; i++){
        n = pthread_create(&threads[i], NULL, reader, NULL);
        if(n<0)
            return -1;
    }

    while(true){}

    return 0;

}

int handler(void* message)
{
    
    char currOpcode='\0';
    int cli;
    ssize_t n;
    int id;
    ssize_t retVal;

    memcpy(&currOpcode,message,1);

    while(currOpcode == '\0'){
        pthread_cond_wait(&canHandle,&mutex);
    }

    if(currOpcode == TFS_OP_CODE_MOUNT)
    {
        char *cliPipe = malloc(MAX_BUF);

        for(int i = 0; i < S; i++){
            if (fcli[i] == -1){

                memcpy(cliPipe, message+sizeof(char), MAX_BUF);

                if ((cli = open (cliPipe, O_WRONLY)) < 0)
                    break;

                fcli[i] = cli;
                pipeCliName[i] = malloc(MAX_BUF);
                memcpy(pipeCliName[i], cliPipe, MAX_BUF);
                id = i;

                n = write (cli, &id, sizeof(int));
                if(n==-1)
                {
                    return -1;
                    break;
                }
                break;
            }
        }
        free(cliPipe);
    }
    else if(currOpcode == TFS_OP_CODE_UNMOUNT)
    {
        memcpy(&id,message+1,sizeof(int));
        cli = fcli[id];
        fcli[id] = -1;
        retVal = 0;
        n = write (cli, &retVal, sizeof(int));
        if (n <= 0){
            fcli[id] = -1;
            return -1;
        }
        close(cli);
    }
    else if(currOpcode == TFS_OP_CODE_OPEN)
    {
        char file [MAX_BUF];
        int flags;

        memcpy(&id,message+1,sizeof(int));
        memcpy(file,message+1+sizeof(int),MAX_BUF);
        memcpy(&flags,message + 1 + sizeof(int)+ MAX_BUF,sizeof(int));

        cli = fcli[id];
        retVal = tfs_open(file, flags);

        n = write (cli, &retVal, sizeof(int));
        if (n <= 0){
            fcli[id] = -1;
            return -1;
        }
    }
    else if(currOpcode == TFS_OP_CODE_CLOSE)
    {
        int fhandle;
        memcpy(&id,message+1,sizeof(int));
        cli = fcli[id];

        memcpy(&fhandle,message+1+sizeof(int),sizeof(int));
        retVal = tfs_close(fhandle);

        n = write(cli, &retVal, sizeof(int));
        if (n <= 0){
            fcli[id] = -1;
            return -1;
        }
    }
    else if(currOpcode == TFS_OP_CODE_WRITE)
    {
        int fhandle;
        size_t len;
        memcpy(&id,message+1,sizeof(int));
        cli = fcli[id];

        memcpy(&fhandle,message+1+sizeof(int),sizeof(int));
        memcpy(&len,message+1+sizeof(int)+sizeof(int),sizeof(size_t));

        char wrBuffer[len];
        memcpy(wrBuffer,message+1+sizeof(int)+sizeof(int)+ sizeof(size_t),len);
        retVal = tfs_write (fhandle, wrBuffer, len); 

        n = write (cli, &retVal, sizeof(int));
        if (n <= 0){
            fcli[id] = -1;
            return -1;
        }   
    }
    else if(currOpcode == TFS_OP_CODE_READ)
    {
        int fhandle;
        size_t len;
        memcpy(&id,message+1,sizeof(int));
        cli = fcli[id];

        int bytesread;
        memcpy(&fhandle,message+1+sizeof(int),sizeof(int));
        memcpy(&len,message+1+sizeof(int)+sizeof(int),sizeof(int));

        void* retBuff = malloc(sizeof(int)+sizeof(char)*len);
        char* rdBuffer = malloc(sizeof(char)*len);
        bytesread = (int)tfs_read(fhandle, rdBuffer, len);
        memcpy(retBuff, &bytesread, sizeof(int));
        memcpy(retBuff + sizeof(int), rdBuffer, sizeof(char)*len);

        if (bytesread != -1){
            n = write (cli, retBuff, sizeof(int) + len*sizeof(char));
            if (n <= 0){
                free(retBuff);
                free(rdBuffer);
                fcli[id] = -1;
                return -1;
            }
        }
        else if(bytesread == -1){
            n = write (cli, &bytesread, sizeof(int));
            if (n <= 0){
                free(retBuff);
                free(rdBuffer);
                fcli[id] = -1;
                return -1;
            }
        }


    }
    else if(currOpcode == TFS_OP_CODE_SHUTDOWN_AFTER_ALL_CLOSED)
    {
        memcpy(&id,message+1,sizeof(int));
        cli = fcli[id];

        retVal = tfs_destroy_after_all_closed();

        n = write (cli, &retVal, sizeof(int));
        if (n <= 0){
            fcli[id] = -1;
        }
        for(int i=0; i<S; i++){
            if (fcli[i] != -1 && i!=id){
                close (fcli[i]);
                unlink(pipeCliName[i]);
                free(pipeCliName[i]);
            }
        }
        unlink(pipeServName);
    }
    pthread_cond_signal(&canRead);
    pthread_mutex_unlock(&mutex);
    
    
    
    return 0;
}

void* reader(){
	ssize_t n, nr,nHandle;
	char opcode;
    int id;
	while(true){

        pthread_mutex_lock(&mutex);

        while(true){
            nr = read (fserv, &opcode, sizeof(char));
            if(nr<0)
                exit(1);
            
            if(nr>0)
                break;
        }

        while (nr == 0)
            pthread_cond_wait(&canRead,&mutex);
            

        if (opcode == TFS_OP_CODE_MOUNT){

        	void* message = malloc(sizeof(char)+MAX_BUF);
            memcpy(message, &opcode, sizeof(char));

        	n = read (fserv, message+sizeof(char), MAX_BUF);
            if (n <= 0){
                exit(1);
            }

            pthread_cond_signal(&canHandle);
            nHandle = handler(message);
        	if(nHandle==-1)
                exit(1);
            free(message);
        }      

        if(opcode == TFS_OP_CODE_UNMOUNT){
            void* message=malloc(sizeof(char)+sizeof(int));
            memcpy(message,&opcode,sizeof(char));
            n = read (fserv, message+sizeof(char), sizeof(int));
            if (n <= 0) 
                exit(1);
            
            pthread_cond_signal(&canHandle);
            
            nHandle = handler(message);
        	if(nHandle==-1)
                exit(1);
            free(message);
        }

        else if(opcode == TFS_OP_CODE_OPEN){
            void* message = malloc(sizeof(char)+sizeof(int)+MAX_BUF + sizeof(int));
            memcpy(message,&opcode,sizeof(char));
            
            n = read (fserv, message+sizeof(char), sizeof(int)+MAX_BUF+sizeof(int));
            if (n <= 0) 
                exit(1);

            pthread_cond_signal(&canHandle);
            if(handler(message)==-1)
                exit(1);
            free(message);
        }

        else if(opcode ==TFS_OP_CODE_CLOSE){
            void* message=malloc(sizeof(char)+sizeof(int)*2);
            memcpy(message,&opcode,sizeof(char));
            n = read (fserv, message+sizeof(char), sizeof(int)*2);
            if (n <= 0) 
                exit(1);
            
            pthread_cond_signal(&canHandle);
            nHandle = handler(message);
        	if(nHandle==-1)
                exit(1);
            free(message);
		}

		else if(opcode ==TFS_OP_CODE_WRITE){
            void* message = malloc(sizeof(char)+sizeof(int)*2+sizeof(size_t));
            memcpy(message,&opcode,sizeof(char));
            n = read (fserv, &id, sizeof(int));
            if (n <= 0) 
                exit(1);
            memcpy(message+sizeof(char),&id,sizeof(int));

			size_t len;

            n = read (fserv, message+sizeof(char)+sizeof(int), sizeof(int)+sizeof(size_t));
            if (n <= 0) 
                exit(1);

            memcpy(&len, message+sizeof(char)+sizeof(int)+sizeof(int),sizeof(size_t));

            void* message2 = malloc(sizeof(char)+sizeof(int)*2+sizeof(size_t)+len);

            memcpy(message2, message, sizeof(char)+sizeof(int)*2+sizeof(size_t));

            free(message);

            n = read (fserv, message2+sizeof(char)+sizeof(int)+sizeof(int)+sizeof(size_t), len);
            if (n <= 0) 
                exit(1);

            pthread_cond_signal(&canHandle);
            nHandle = handler(message2);
        	if(nHandle==-1)
                exit(1);
            free(message2);
        }

        else if(opcode ==TFS_OP_CODE_READ){
            void* message = malloc(sizeof(char)+sizeof(int)*2+sizeof(size_t));
            memcpy(message,&opcode,sizeof(char));
            n = read (fserv, message+sizeof(char), sizeof(int)*2+sizeof(size_t));
            if (n <= 0) 
                exit(1);
            
            pthread_cond_signal(&canHandle);
            nHandle = handler(message);
        	if(nHandle==-1)
                exit(1);
            free(message);
        }

        else if(opcode ==TFS_OP_CODE_SHUTDOWN_AFTER_ALL_CLOSED){
            void* message=malloc(sizeof(char)+sizeof(int));
            memcpy(message,&opcode,sizeof(char));
            n = read (fserv, message+sizeof(char), sizeof(int));
            if (n <= 0) 
                exit(1);
            
            pthread_cond_signal(&canHandle);
            nHandle = handler(message);
        	if(nHandle==-1)
                exit(1);
            free(message);
            exit(0);
        }

        opcode = '\0';
        
        
        pthread_mutex_unlock(&mutex);
	}
    exit(0);
}