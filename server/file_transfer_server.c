
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <signal.h>
#include "../include/client_server_common.h"

// Global value to enable trace mode 
int g_traceMode = 0;

// The logging macro
#define TRACE(...) \
    do { \
        if (g_traceMode) { \
            printf(__VA_ARGS__); \
        } \
    } while (0)

typedef struct _clientRequest 
{
    int requestSocketFd; 
    int requestId;
    struct _clientRequest *nextRequest;
} ClientRequest;

typedef struct {
    ClientRequest *head;
    int           numRequests;
    pthread_mutex_t requestMutex;
    pthread_cond_t requestCondition;
} ClientRequestQueue;

volatile sig_atomic_t g_runServer;
char g_saveDirectory[MAX_DIR];

ClientRequestQueue clientReqQueue = {NULL, 0,
                                     PTHREAD_MUTEX_INITIALIZER,
                                     PTHREAD_COND_INITIALIZER};

void *requestHandlerThread(void *thId)
{
    int  threadId = (int)(intptr_t)thId; // Note: using intptr_t type to pass variable in pointers place
    FILE *outputFile;
    char fileName[MAX_PATH];
    char directory[MAX_PATH];
    char fullPath[1024];       // TBD
    char recvBuf[BUF_SIZE];
    int  numBytesReceived = 0;
    size_t numBytesWritten = 0;
    size_t totalBytesWritten = 0;
    memset(recvBuf, 0, sizeof(recvBuf));
    memset(fullPath, 0, 1024); // TBD
    memset(directory, 0, MAX_PATH);
    strncpy(directory, g_saveDirectory, MAX_PATH);
    printf("requestHandlerThread %d starting up, saving files to: %s\n", 
           threadId, directory);
    while(g_runServer) {
        ClientRequest *currRequest;        
        pthread_mutex_lock(&clientReqQueue.requestMutex); // TBD, should this be after the wait?
        while (clientReqQueue.head == NULL && g_runServer) {
            /* Sleep, waiting on condition */
            pthread_cond_wait(&clientReqQueue.requestCondition,
                              &clientReqQueue.requestMutex);
        }
        currRequest = clientReqQueue.head;
        clientReqQueue.head = currRequest->nextRequest;
        pthread_mutex_unlock(&clientReqQueue.requestMutex);

        TRACE("Input queue condition met, file descriptor has data.\n");
        // First read of fd should be filename, retrieve, set and create file
        memset(fileName, 0, sizeof(fileName)); 
        numBytesReceived = recv(currRequest->requestSocketFd, 
                                fileName, 
                                sizeof(fileName), 0);
        if (numBytesReceived != MAX_PATH) {
            printf("ERROR: Failed to receive filename in threadId=%d\n",
                   threadId);
            close(currRequest->requestSocketFd);
            continue;
        }
        TRACE("Filename %s received, create local file\n.",
                fileName);
        TRACE("Saving data to %s\n.", fullPath);
        snprintf(fullPath, sizeof(fullPath), "%s/%s", directory, fileName);
        outputFile = fopen(fullPath, "w");
        if (outputFile == NULL) {
            // TBD, should, could retry here?
            printf("ERROR: Failed to open file %s\n", fileName);
            close(currRequest->requestSocketFd);
            continue;
        }

        /*
        * Now loop reading the socket until it is closed. 
        */
        TRACE("Looping, receiving data on file descriptor %d, for file %s, threadId=%d\n",
              currRequest->requestSocketFd, fileName, threadId);
        while (((numBytesReceived = recv(currRequest->requestSocketFd, recvBuf, sizeof(recvBuf), 0)) > 0) && g_runServer) {
            TRACE("Thread %d received %d bytes, for file %s\n", 
                   threadId, numBytesReceived, fileName);
            /* Write buffer to file */
            if (outputFile != NULL) {
                //numBytesWritten = fwrite(recvBuf, 1, BUF_SIZE, outputFile); 
                numBytesWritten = fwrite(recvBuf, 1, numBytesReceived, outputFile); 
                TRACE("Thread %d, numBytesWritten = %zu, fileName = %s\n", 
                        threadId, numBytesWritten, fileName);
                totalBytesWritten += numBytesWritten;
            }
            memset(recvBuf, 0, sizeof(recvBuf));
            numBytesReceived = 0;
        }
        if (numBytesReceived == 0) {
         printf("Client closed socket fd %d, threadId %d, fileName %s received\n",
                currRequest->requestSocketFd, threadId, fileName);
        } 
        else if (numBytesReceived == -1) {
            printf("ERROR: Call to recv() failed!, threadId = %d, fileName = %s\n", 
                   threadId, fileName);
        }
        close(currRequest->requestSocketFd);
        free(currRequest);
        fclose(outputFile);
    }
    printf("requestHandlerThread %d exiting\n", threadId);
    return NULL;
}

void submitToPool(int newSockFd)
{
    ClientRequest *newReq = malloc(sizeof(ClientRequest));
    if (newReq == NULL) {
        printf("ERROR: Failed to allocate request in submitToPool()!\n");
        return;
    }
    newReq->requestSocketFd = newSockFd; 
    newReq->nextRequest = NULL; 
    pthread_mutex_lock(&clientReqQueue.requestMutex);
    newReq->nextRequest = clientReqQueue.head;
    clientReqQueue.head = newReq;
    clientReqQueue.numRequests++;
    /*
    * Wake up a thread in the pool, which one exactly, is indeterminent.
    */ 
    pthread_cond_signal(&clientReqQueue.requestCondition);
    pthread_mutex_unlock(&clientReqQueue.requestMutex);
    printf("submitted file descritor %d to thread pool\n", newSockFd);
}


void handleSignal(int sig)
{
    const char msg[] = "Caught signal, setting g_runServer to zero\n";
    write(STDOUT_FILENO, msg, sizeof(msg) - 1);
    g_runServer = 0; // JMDB, remove
    if (sig == SIGINT || sig == SIGHUP) {
        g_runServer = 0;
    }
    _exit(0);
}


void usage() {
    printf("file_transfer_server -d <directory> -t \n");
    printf("  where:\n");
    printf("   -d <dirctory> -- provide dir where files are to be stored. [Required]\n");
    printf("   -t            -- enable trace mode, gives  much more logging. [Optional]\n");
    printf("  eg:\n");
    printf("   file_transfer_server <my_dir> -t \n");
    exit(-1);
}

int main(int argc, char *argv[])
{
    int opt;
    pthread_t threadPool[MAX_THREADS];
    struct sockaddr_in myAddress;
    int addrLen = sizeof(myAddress);
    int serverFd;
    int newSocketFd;
    struct sigaction sigAction;

    memset(g_saveDirectory, 0, MAX_DIR);
    if (argc < 2) usage();
    while ((opt = getopt(argc, argv, "d:t")) != -1) {
        switch (opt) {
            case 'd':
                strncpy(g_saveDirectory, optarg, 256); // Directory to store files 
                break;
            case 't':
                g_traceMode = 1;
                break;
            case '?':
                usage();
                return 1;
        }
    }
    if (strnlen(g_saveDirectory, MAX_DIR) == 0) usage();

    printf("Received files will be saved to the directory %s\n", g_saveDirectory);
    TRACE("Trace mode is enabled\n");

    /*
    * Configure signal handler to enable cleanup on shutdown.
    */
    memset(&sigAction, 0, sizeof(sigAction));
    sigAction.sa_handler = &handleSignal;
    sigAction.sa_flags = SA_RESTART; // restarts interrupted system calls
    sigemptyset(&sigAction.sa_mask);
    sigAction.sa_flags = 0;
    if ((sigaction(SIGINT, &sigAction, NULL)) == -1) {
        printf("ERROR: Failed to register signal handler for SIGINT\n");
    }
    if ((sigaction(SIGHUP, &sigAction, NULL)) == -1) {
        printf("ERROR: Failed to register signal handler for SIGHUP\n");
    }

    /*
    * Create thread pool of request handlers 
    */
    g_runServer = 1;
    for (int thId = 0; thId < MAX_THREADS; thId++) {
        pthread_create(&threadPool[thId], NULL, requestHandlerThread, (void *)(intptr_t)thId);
    }
    
    /*
    * Create socket and begin listening for incomming requests
    */
    serverFd = socket(AF_INET, SOCK_STREAM, 0);

    myAddress.sin_family = AF_INET;
    myAddress.sin_addr.s_addr = INADDR_ANY;
    myAddress.sin_port = htons(PORT);
    
    bind(serverFd, (struct sockaddr *)&myAddress, sizeof(myAddress));
    listen(serverFd, MAX_THREADS); // set backlog to max number of client threads 
    
    while (g_runServer) {
        newSocketFd = accept(serverFd, 
                           (struct sockaddr *)&myAddress, 
                           (socklen_t*)&addrLen);
        if (newSocketFd < 0) {
            printf("Call to accept() interrupted, of failed!\n");
            continue;
        }
        printf("New connection accepted, queuing newSocketFd = %d!\n", newSocketFd);
        /*
        * Submit the socket to the thread pool
        */
        submitToPool(newSocketFd);
    }
    close(serverFd);

    /*
    * Wait for all threads in pool to exit
    */
    for (int thId = 0; thId < MAX_THREADS; thId++) {
        printf("Calling pthread_join() now!\n");
        if (pthread_join(threadPool[thId], NULL) != 0) {
            printf("ERROR: Failed to join thread, id = %d\n", thId);
            return -1;
        }
    }
    printf("file_transfer_server exiting!\n");

    return 0; 

}
