# pthread_demo
Client/Server pthreads demo code. Client sends files from client dir to server. Server saves file to it's own directory

## Get the code
git clone https://github.com/jdmclean/pthreads_demo.git

## Whats included

```
.
├── client
│   └── file_transfer_client.c
├── include
│   └── client_server_common.h
├── makefile
├── README.md
├── scripts
│   ├── create_test_files.sh
│   └── md5_check.sh
└── server
    └── file_transfer_server.c

```

## Build the code
make


## Architecture with some ASCII Art

```
+-----------------------------+                  +-----------------------------+
| file_transfer_client.c      |                  | file_transfer_server.c      |
|                             |                  |                             |
|                             |                  |                             |
| main()                      |                  | main()                      |
|                             |                  |                             |
| +-----------------------+   |                  |                             |
| | queue of file names   |   |                  |                             |
| |  -file1               |   |                  |                             |
| |  -file2               |   |                  |                             |
| |  -file3               |   |                  |                             |
| |  .                    |   |                  |                             |
| |  .                    |   |                  |                             |
| |  .                    |   |                  |                             |
| |  -fileN               |   |                  |                             |
| +-----------------------+   |                  |                             | 
|      !                      |                  |                             |
|      !                      |                  |                             |
|     \^/                     |                  |                             |
|      .                      |                  |                             |
| +-----------------------+   |                  |    +---------------------+  | 
| | thread pool           |   |                  |    | thread pool         |  |   
| |                       |   |                  |    |                     |  |        
| |  -thd1                +-------------------------->+   -thd1             |  |
| |  -thd2                |   |                  |    |   -thd2             |  |
| |  -thd3                |   |                  |    |   -thd3             |  |
| |                       |   |                  |    |                     |  |
| +-----------------------+   |                  |    +---------------------+  |
|      .                      |                  |          !                  |
|     /^\                     |                  |          !                  |
|      !                      |                  |          !                  |
|      !                      |                  |         \./                 |
|      !                      |                  |          .                  |
| +-----------------------+   |                  |    +---------------------+  |
| | disk                  |   |                  |    | disk                |  |
| |  -file1               +   |                  |    |   -file1            |  |
| |  -file2               |   |                  |    |   -file1            |  |
| |  -file3               |   |                  |    |   -file1            |  |
| |  .                    |   |                  |    |   .                 |  |
| |  .                    |   |                  |    |   .                 |  |
| |  .                    |   |                  |    |   .                 |  |
| |  -fileN               |   |                  |    |   -file1            |  |
| +-----------------------+   |                  |    +---------------------+  |
|                             |                  |                             |
+-----------------------------+                  +-----------------------------+
```

## To test the code

```
1.) Open three terminal windows.
2.) cd to the server directory. 
3.) Create a directory to receive the files. e.g.: receive_dir
4.) Run the server, e.g.:
        ./file_transfer_server -d receive_dir
5.) In the other terminal, cd to the client directory.
6.) Create a directory to hold the files for sending. e.g.: send_dir
7.) Create 10000 test files. e.g.:
    . ~/pthreads_demo/scripts/create_test_files.sh -d send_dir
8.) Run the client. e.g.:
    ./file_transfer_client -d send_dir
9.) Once complete run md5_check.sh to verify transfers were successful. e.g.:
    . ~/pthreads_demo/scripts/md5_check.sh ./send_dir ../server/receive_dir






    
    
