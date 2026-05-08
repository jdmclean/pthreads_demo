#include <stdio.h>
#include <unistd.h>
#include <dirent.h>
#include <pthread.h>
#include <stdlib.h>
#include <libgen.h>
#include <string.h>
#include <arpa/inet.h>
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


typedef struct _queueItem
{
    char path[MAX_PATH];
    struct _queueItem *nextItem;
} QueueItem;

typedef struct _queue
{
    QueueItem *head;
    QueueItem *tail;
    int               numItems;
    pthread_mutex_t   queueLock;
} Queue;

Queue *initQueue(void) {
    Queue *theQueue = NULL;
    int rc;
    theQueue = calloc(1, sizeof(Queue));
    if (!theQueue) {
        perror("ERROR: Failed to initialize file path queue\n");
        return NULL;
    }
    rc = pthread_mutex_init(&(theQueue->queueLock), NULL);
    if (rc != 0) {
        free(theQueue);
        theQueue = NULL;
    }
    return theQueue;
}

int enQueue(Queue *theQueue, char *path) {
    QueueItem *newItem = NULL;
    QueueItem *currItem;

    if (!theQueue || !path) return -1;
    
    pthread_mutex_lock(&theQueue->queueLock);
    newItem = calloc(1, sizeof(QueueItem));
    if (newItem == NULL) {
        pthread_mutex_unlock(&theQueue->queueLock);
        return -1;
    }
    strncpy(newItem->path, path, MAX_PATH);
    if (theQueue->head == NULL) {
        theQueue->head = newItem;
        theQueue->tail = theQueue->head;
        theQueue->numItems = 1;
    }
    else {
        currItem = theQueue->tail;
        currItem->nextItem = newItem;
        theQueue->tail = newItem;
        theQueue->numItems++;
    }
    pthread_mutex_unlock(&theQueue->queueLock);
    return 0;
} 

int  deQueue(Queue *theQueue, char *path, int threadId)
{
    QueueItem *headItem = NULL;
    if (!theQueue || !theQueue->tail || !path) {
        printf("ERROR: queue not created,!\n");
        return -1;
    }
    TRACE("Entered deQueue(), thread %d, file %s, numItems = %d\n", 
          threadId, path, theQueue->numItems);
    if (theQueue->numItems == 0) {
        printf("Exiting, queue is empty!\n");
        return -1;
    }

    if (theQueue->head == NULL) {
        printf("ERROR!, attempted deQueue where theQueue->head is NULL!\n");
        return -1;
    }
    fflush(NULL);
    pthread_mutex_lock(&theQueue->queueLock);
    strncpy(path, theQueue->head->path, MAX_PATH);
    headItem = theQueue->head;
    theQueue->head = theQueue->head->nextItem;
    if (headItem) {
        free(headItem);
        theQueue->numItems--;
        TRACE("Thread %d deQueued %s, numItems = %d\n", 
               threadId, path, theQueue->numItems);
    } else {
        printf("ERROR: Thread %d failed to deQueue %s, numItems = %d\n", 
               threadId, path, theQueue->numItems);
        pthread_mutex_unlock(&theQueue->queueLock);
        return -1;
    }
    pthread_mutex_unlock(&theQueue->queueLock);
    return 0;
}

Queue *g_FilePathQueuePtr;

int createFilePathQueue(char *directory)
{
    char fullPath[MAX_PATH];
    DIR *dir;    
    struct dirent *dirEntry;
    g_FilePathQueuePtr = initQueue();
    if (g_FilePathQueuePtr == NULL) return -1;

    dir = opendir(directory);
    if (dir) {
        printf("Reading files from %s\n", directory);
        while ((dirEntry = readdir(dir)) != NULL) {
            if (dirEntry->d_type == DT_REG) {
                printf("Found file %s\n", dirEntry->d_name);
                memset(fullPath, 0, MAX_PATH);
                snprintf(fullPath, sizeof(fullPath), "%s/%s", 
                         directory, dirEntry->d_name);
                if (enQueue(g_FilePathQueuePtr, fullPath)) {
                    printf("Failed to add %s to file queue\n", 
                           fullPath);
                    return -1;
                }
                printf("Added %s to file send queue\n", fullPath);
            }
        }
        closedir(dir);
    } else {
        printf("ERROR: Failed to open directory %s\n", directory); 
        return -1;
    }
    return 0;
}

void destroyFilePathQueue()
{
    char currPath[MAX_PATH] = "\0";
    if (g_FilePathQueuePtr->numItems > 0) {
        printf("ERROR: file path queue should be empty!\n");
        while (g_FilePathQueuePtr->numItems > 0) {
            if (deQueue(g_FilePathQueuePtr, currPath, 0)) {
                printf("Removed %s from queue\n", currPath); 
            }
        } 
    }
    free(g_FilePathQueuePtr);
}

void *transferFileThread(void * thId)  
{
    int threadId = (int)(intptr_t)thId; // Note: using intptr_t type to pass variable in pointers place
    int sockFd;
    struct sockaddr_in server_addr;
    char buffer[BUF_SIZE];
    FILE *inputFp;
    char currPath[MAX_PATH];
    char fileName[MAX_PATH];
    ssize_t bytesSent = 0;
    int sentFileNameOneShot = 0;

    printf("transferFileThread %d starting up\n", threadId);
    if (g_FilePathQueuePtr == NULL) {
        printf("ERROR: Exiting transferFileThread(%d), g_FilePathQueuePtr not set\n",
               threadId);
        return NULL;
    }

    while (g_FilePathQueuePtr->numItems > 0) {
        memset(currPath, 0, MAX_PATH);
        TRACE("Dequeuing next file, threadId = %d\n", threadId); 
        if (!deQueue(g_FilePathQueuePtr, currPath, threadId)) {
            inputFp = fopen(currPath, "rb");
            if (inputFp == NULL) {
                printf("ERROR: fopen() call failed for file %s, threadId = %d\n", 
                       currPath, threadId);
                return NULL;
            }
            size_t bytes_read;
            while ((bytes_read = fread(buffer, 1, BUF_SIZE, inputFp)) > 0) {
                // We have read bytes from file, so we have something to send
                // so, just once for this file, create the socket and connect,
                // then send the filename in a dedicated packet.
                if (!sentFileNameOneShot) {
                    sentFileNameOneShot = 1;
                    // Create socket
                    sockFd = socket(AF_INET, SOCK_STREAM, 0);
                    if (sockFd < 0) {
                        printf("ERROR: Call to socket() failed! ThreadId = %d\n", threadId);
                        return NULL;
                    }
                    server_addr.sin_family = AF_INET;
                    server_addr.sin_port = htons(PORT);
                    server_addr.sin_addr.s_addr = inet_addr(SERVER_IP);
                    if (connect(sockFd, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1) {
                        printf("ERROR: Call to connect() failed! ThreadId = %d\n", threadId);
                        return NULL;
                    }

                    // Send filename
                    memset(fileName, 0, MAX_PATH);
                    snprintf(fileName, sizeof(fileName), "%s", basename(currPath));
                    bytesSent = send(sockFd, fileName, MAX_PATH, 0);
                    if (bytesSent != MAX_PATH) {
                        printf("ERROR: Failed to send filename for file:%s for %s, threadId=%d\n",
                               fileName, currPath, threadId);
                        perror("send(1) error is:");
                        if (sockFd > 2) close(sockFd);  // > stdin, stdout and stderr
                        if (fclose(inputFp)) {
                            printf("ERROR: Failed to close input file! threadId=%d\n", 
                                   threadId);
                        } 
                        return NULL;
                    }
                    printf("File name %s sent from threadId=%d\n", 
                           currPath, threadId);
                }
                if ((bytesSent = send(sockFd, buffer, bytes_read, 0)) == -1) {
                    printf("ERROR: send() call failed for file %s, threadId = %d\n",
                           currPath, threadId);
                    perror("send(2) error is:");
                    if (sockFd > 2) close(sockFd);  // > stdin, stdout and stderr
                    if (fclose(inputFp)) {
                        printf("ERROR: Failed to close input file! threadId=%d\n", 
                               threadId);
                    } 
                    return NULL;
                }
                TRACE("Sent %zu bytes to server from threadId = %d, fileName = %s\n", 
                      bytesSent, threadId, fileName);
                memset(buffer, 0, BUF_SIZE);
            }
            if (sockFd > 2) close(sockFd);  // > stdin, stdout and stderr
            sentFileNameOneShot = 0;
            printf("%s sent successfully, threadId=%d\n", currPath, threadId);
            if (fclose(inputFp)) {
                printf("ERROR: Failed to close input file! threadId=%d\n", threadId);
            } 
        } 
        else {
            if (sockFd > 2) close(sockFd);  // > stdin, stdout and stderr
            printf("ERROR: Call to deQueue() failed, numItems = %d, threadId = %d\n",
                   g_FilePathQueuePtr->numItems, threadId);     
        }
    }        
    sleep(2); // Give a little time for the tcp/ip stack to flush.
    printf("threadId %d exiting, no more files!\n", 
            threadId);
    return NULL;
}

void usage() {
    printf("file_transfer_client -d <directory> -t \n");
    printf("  where:\n");
    printf("   -d <dirctory> -- provide dir where files are to be read from. [Required]\n");
    printf("   -t            -- enable trace mode, gives  much more logging. [Optional]\n");
    printf("  eg:\n");
    printf("   file_transfer_client <my_dir> -t \n");
    exit(-1);
}

int main(int argc, char* argv[])
{
    int opt;
    char directory[MAX_DIR];

    memset(directory, 0, MAX_DIR);
    if (argc < 2) usage();
    while ((opt = getopt(argc, argv, "td:")) != -1) {
        switch (opt) {
            case 'd':
                strncpy(directory, optarg, 256); // Directory to load files from 
                break;
            case 't':
                g_traceMode = 1;
                break;
            default:
                usage();
        }
    }
    if (strnlen(directory, MAX_DIR) == 0) usage();

    printf("Transferring files from directory %s\n", directory);
    TRACE("Trace mode is enabled\n");

    /*
    *  Create list of files for reading
    */
    if (createFilePathQueue(directory)) {
        printf("ERROR: Failed to create file path queue\n");
        destroyFilePathQueue();
        return -1;
    }
   
    /*
    *  Create pool of threads to transfer files 
    *  Threads start accessing file queue immediately on creation
    */
    pthread_t threadPool[MAX_THREADS]; 
    for (int thId = 0; thId < MAX_THREADS; thId++) {
        if (pthread_create(&threadPool[thId], NULL, transferFileThread, (void*)(intptr_t)thId) != 0) {
            printf("ERROR: Failed to create thread, id = %d\n", thId);
            destroyFilePathQueue();
            return -1;
        }
    }
    
    /*
    * Wait for queue of files to be emptied.
    */
    while(g_FilePathQueuePtr->numItems > 0) {
        printf("Waiting on transfer completion, files remaining = %d\n",
               g_FilePathQueuePtr->numItems);
        sleep(3);
    }

    /*
    * Wait for all threads to complete
    */
    for (int i = 0; i < MAX_THREADS; i++) {
        if (pthread_join(threadPool[i], NULL) != 0) {
            printf("ERROR: Failed to join thread, id = %d\n", i);
            destroyFilePathQueue();
            return -1;
        }
    }
    TRACE("file_transfer_client finished transferring all files.\n");
    return 0;
}

