#include "tecnicofs_client_api.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include<stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>
#define MAX_SIZE_BUF 40

int sessionId;
char const *client_pipe_path_global;
int readClient;
int writeServer;

int tfs_mount(char const *client_pipe_path, char const *server_pipe_path) {
    char numberOp[MAX_SIZE_BUF];

    numberOp[0] = TFS_OP_CODE_MOUNT;

    unlink(client_pipe_path);

    if(mkfifo(client_pipe_path,0777)<0)
    {
        return -1;
    }
    writeServer = open(server_pipe_path,O_WRONLY);
    if(writeServer<0){
        return -1;
    }
     
    void* buf = malloc(MAX_SIZE_BUF+1);
    strcat(numberOp,client_pipe_path);

    if(write(writeServer,numberOp,strlen(numberOp))<=0){
        free(buf);
        return -1;
    }
    readClient = open(client_pipe_path,O_RDONLY);
    if((readClient<0)){
        free(buf);
        return -1;
    }
    client_pipe_path_global = client_pipe_path;
    if(read(readClient,buf,MAX_SIZE_BUF+1)<=0){
        free(buf);
        return -1;
    }
    int id = 0;
    memcpy(&id,buf,sizeof(int));
    sessionId = id;
    free(buf);
    return 0;
}

int tfs_unmount(){
    char numberOp = TFS_OP_CODE_UNMOUNT;
    void* buf = malloc(sizeof(char) + sizeof(int));
    memcpy(buf,&numberOp,sizeof(char));
    memcpy(buf+sizeof(char),&sessionId,sizeof(int));

    if(write(writeServer,buf,sizeof(char) + sizeof(int))<=0){
        free(buf);
        return -1;
    }
    if(read(readClient,buf,sizeof(int)) < 0){
        free(buf);
        return -1;
    }
    free(buf);
    close(readClient);
    close(writeServer);
    unlink(client_pipe_path_global);
    sessionId = -1;
    return 0;
}

int tfs_open(char const *name, int flags) {
    char numberOp = TFS_OP_CODE_OPEN;
    void* buf = malloc(sizeof(char)+sizeof(int)*2+40);
    unsigned long offset=0;
    memcpy(buf+offset,&numberOp,sizeof(char));
    offset += 1;
    memcpy(buf+offset,&sessionId,sizeof(int));
    offset += sizeof(int);
    memcpy(buf+offset,name,40);
    offset += 40;
    memcpy(buf+offset,&flags,sizeof(int));

    if(write(writeServer,buf,sizeof(char)+sizeof(int)*2+40)<=0){
        free(buf);
        return -1;
    }
    if(read(readClient,buf,sizeof(int)) <= 0){
        free(buf);
        return -1;
    }
    free(buf);
    return 0;
}

int tfs_close(int fhandle) {
    char numberOp = TFS_OP_CODE_CLOSE;
    void* buf = malloc(sizeof(char)+sizeof(int)*2);
    unsigned long offset=0;
    memcpy(buf+offset,&numberOp,sizeof(char));
    offset += sizeof(char);
    memcpy(buf+offset,&sessionId,sizeof(int));
    offset += sizeof(int);
    memcpy(buf+offset,&fhandle,sizeof(int));

    if(write(writeServer,buf,sizeof(char)+sizeof(int)*2)<=0){
        free(buf);
        return -1;
    }

    if(read(readClient,buf,sizeof(int)) < 0){
        free(buf);
        return -1;
    }

    free(buf);
    return 0;
}

ssize_t tfs_write(int fhandle, void const *buffer, size_t len) {
    char numberOp = TFS_OP_CODE_WRITE;
    void* buf = malloc(sizeof(char)+sizeof(int)*2 + sizeof(size_t) + sizeof(char)*len);
    unsigned long offset=0;
    memcpy(buf+offset,&numberOp,sizeof(char));
    offset += sizeof(char);
    memcpy(buf+offset,&sessionId,sizeof(int));
    offset += sizeof(int);
    memcpy(buf+offset,&fhandle,sizeof(int));
    offset += sizeof(int);
    memcpy(buf+offset,&len,sizeof(size_t));
    offset += sizeof(size_t);
    memcpy(buf+offset,buffer,sizeof(char)*len);

    if(write(writeServer,buf,sizeof(char)+sizeof(int)*2 + sizeof(size_t) + sizeof(char)*len)<=0){
        free(buf);
        return -1;
    }

    if(read(readClient,buf,sizeof(int)) <= 0){
        free(buf);
        return -1;
    }

    int n;
    memcpy(&n, buf, sizeof(int));

    free(buf);
    return n;
}

ssize_t tfs_read(int fhandle, void *buffer, size_t len) {
    char numberOp = TFS_OP_CODE_READ;
    void* buf = malloc(sizeof(char)+sizeof(int)*2 + sizeof(size_t));
    unsigned long offset=0;
    memcpy(buf+offset, &numberOp,sizeof(char));
    offset += sizeof(char);
    memcpy(buf+offset,&sessionId,sizeof(int));
    offset += sizeof(int);
    memcpy(buf+offset,&fhandle,sizeof(int));
    offset += sizeof(int);
    memcpy(buf+offset,&len,sizeof(size_t));
    if(write(writeServer,buf,sizeof(char)+sizeof(int)*2 + sizeof(size_t))<=0)
        return -1;
    
    int bytesRead;

    if(read(readClient, &bytesRead,sizeof(int)) <= 0){
        free(buf);
        return -1;
    }
    if(bytesRead == -1){
        free(buf);
        return -1;
    }
    char readCont[len];
    if(read(readClient,readCont,sizeof(char)*len) <= 0){
        free(buf);
        return -1;
    }
    memcpy(buffer,readCont,sizeof(char)*len);
    return bytesRead;
}

int tfs_shutdown_after_all_closed() {
    char numberOp = TFS_OP_CODE_SHUTDOWN_AFTER_ALL_CLOSED;
    void* buf = malloc(sizeof(char)+sizeof(int));
    unsigned long offset=0;
    memcpy(buf+offset,&numberOp,sizeof(char));
    offset += sizeof(char);
    memcpy(buf+offset,&sessionId,sizeof(int));
    if(write(writeServer,buf,sizeof(char)+sizeof(int))<=0){
        free(buf);
        return -1;
    }
    if(read(readClient,buf,sizeof(int)) <= 0){
        free(buf);
        return -1;
    }
    free(buf);
    unlink(client_pipe_path_global);
    return 0;
}


