#include <stdio.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <netdb.h>
#include <pthread.h>
#include <semaphore.h>
#include "http-parser.h"
#include "queue.h"

#define BUFFER_MAX	1024

//------------------------CONF VARS-----------------------------//
int _queueSize;
int _threadNumber;
pthread_t* threads;

//--------------------MUTEX & SEMAPHORES-----------------------//
pthread_mutex_t m;
sem_t _queueEmptySpots;
sem_t _clientsInQueue;

//---------------------------QUEUE-----------------------------//
struct queue _clientQueue;

//-------------------------PARAMS STRUCT-----------------------//
typedef struct param {
    int id;
} param_t;

void parseConfig(char *filename);
void handle_sigchld(int sig);
int create_server_socket(char* port, int protocol);
void handle_client(int sock);

int sem_wait(sem_t *sem);
int sem_post(sem_t *sem);
int sem_init(sem_t *sem, int pshared, unsigned int value);


int set_blocking(int sock, int blocking) {
    int flags;
    /* Get flags for socket */
    if ((flags = fcntl(sock, F_GETFL)) == -1) {
        perror("fcntl get");
        exit(EXIT_FAILURE);
    }
    /* Only change flags if they're not what we want */
    if (blocking && (flags & O_NONBLOCK)) {
        if (fcntl(sock, F_SETFL, flags & ~O_NONBLOCK) == -1) {
            perror("fcntl set block");
            exit(EXIT_FAILURE);
        }
        return 0;
    }
    /* Only change flags if they're not what we want */
    if (!blocking && !(flags & O_NONBLOCK)) {
        if (fcntl(sock, F_SETFL, flags | O_NONBLOCK) == -1) {
            perror("fcntl set nonblock");
            exit(EXIT_FAILURE);
        }
        return 0;
    }
    return 0;
}

void my_sigchld_handler(int sig)
{
    printf("SIGNAL REAPER CALLED \n");
    pid_t p;
    int status;

    while ((p=waitpid(-1, &status, WNOHANG)) != -1)
    {
        if(p==0){
            break;
        }
        printf("REAPED CHILD PROCESS WITH ID: %i", p);
    }
}

void inthandler(int sig)
{
    _keepAccepting = 0;
}

void consumeClient(){
    printf("\nCONSUME CLIENT FUCNTION\n");
    while(_keepAccepting){
        printf("  sem_wait called\n");
        if (sem_wait(&_clientsInQueue) == -1){
            if (errno == EINTR) { /* this means we were interrupted by our signal handler */
                if (_keepAccepting == 0) { /* g_running is set to 0 by the SIGINT handler we made */
                    printf("  Terminating on consumeClient()\n");
                    break;
                }
            }
        }
        pthread_mutex_lock(&m);
        printf("  Getting client from queue\n");
        struct node c = popQueue(&_clientQueue);
        pthread_mutex_unlock(&m);
        sem_post(&_queueEmptySpots);
        printf("   Handling client: %i\n", c.client);
        printf("  Size of the queue is: %i", _clientQueue.size);
        handle_client(c.client);
    }
}


void createWorkerThreads(){
    printf("\nCREATING WORKER THREADS\n");
    threads = NULL;
    threads = (pthread_t*)malloc(_threadNumber * sizeof(pthread_t));
    for (int i = 0; i < _threadNumber; i++) {
        printf("  thread number %i created\n", i);
        pthread_create(&threads[i], NULL, consumeClient, NULL);
    }
}

void killThreads(){
    printf("\n\nKILLING THREADS\n\n");
    printf("Number of threads: %i\n", _threadNumber);
    for(int i = 0; i < _threadNumber; i++){
        printf("%i\n", i);
        printf("error code: %i\n", pthread_kill(threads[i], SIGINT));

    }

    printf("\n\nJOINNING THREADS\n\n");
    for(int i = 0; i < _threadNumber; i++){
        if(pthread_join(threads[i], NULL) != 0){
            perror("pthread_join");
        }
    }

    freeQueue(&_clientQueue);
    free(threads);
    printf("\n\nKILLING THREADS END\n\n");
    exit(EXIT_SUCCESS);
}

int init_tcp(char* path, char* port, int verbose, int threads, int queueSize) {

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = my_sigchld_handler;
    sigaction(SIGCHLD, &sa, NULL);

    struct sigaction sa2;
    memset(&sa2, 0, sizeof(sa2));
    sa2.sa_handler = inthandler;

    sigaction(SIGINT, &sa2, NULL);


    //------------------------------INIT MUTEX AND SEMAPHORES-------------------------//
    pthread_mutex_init(&m, NULL);
    sem_init(&_queueEmptySpots, 0, queueSize);
    sem_init(&_clientsInQueue, 0, 0);

    //-----------------SET UP GLOBAL THREAD NUMBER AND QUEUE SIZE---------------------//
    _queueSize = queueSize;
    _threadNumber = threads;

    //-------------------------------SETUP CLIENT QUEUE--------------------------------//
    _clientQueue = newQueue();

    //---------------------------CREATE WORKER THREADS--------------------------------//
    createWorkerThreads();

    //---------------------------SET UP OTHER VARIABLES-------------------------------//
	verbose_flag = verbose;
    _keepAccepting = 1;

	int sock = create_server_socket(port, SOCK_STREAM);
	parseConfig(path);

	while (_keepAccepting) {
		struct sockaddr_storage client_addr;
		socklen_t client_addr_len = sizeof(client_addr);

		int client = accept(sock, (struct sockaddr*)&client_addr, &client_addr_len);
		if (client == -1) {
            if (errno == EINTR) { /* this means we were interrupted by our signal handler */
                if (_keepAccepting == 0) { /* g_running is set to 0 by the SIGINT handler we made */
                    printf("\nBREAKING OUT OF MAIN LOOP\n");
                    break;
                }
            }

			perror("Connection Failed at function: accept");
			continue;
		}
		else{
            printf("\nGOT CLIENT\n");

            if(sem_wait(&_queueEmptySpots) == -1) {
                if (errno == EINTR) { /* this means we were interrupted by our signal handler */
                    if (_keepAccepting == 0) { /* g_running is set to 0 by the SIGINT handler we made */
                       break;
                    }
                }
            }
            printf(" Free space in queue available\n");
            pthread_mutex_lock(&m);
            printf("  Pushing client %i to the queue\n", client);
            pushQueue(&_clientQueue, client);
            printf("  Size of the queue is: %i", _clientQueue.size);
            //handle_client(client, client_addr, client_addr_len);
            pthread_mutex_unlock(&m);
            sem_post(&_clientsInQueue);

		}
	}

    //SEND PTHREAD KILL TO WORKERS
    killThreads();
	return 0;
}

void resetParsingHeader(struct request *r){
    r->is_header_ready = 0;
    r->is_body_ready = 0;
    r->content_length = 0;
    r->parsed_body = 0;
    r->responseFlag = 0;
    r->dynamicContent = NON_DYNAMIC;
    r->byte_range = 0;
    r->is_header_parsed = 0;
    r->is_body_parsed = 0;
    r->fragmented_line_waiting = 0;
}

void resetParsingHeaderFlags(struct request *r){
    r->first_line_read = 0;
    r->header_index = 0;
}

void handle_client(int sock) {

	unsigned char buffer[BUFFER_MAX];
	char client_hostname[NI_MAXHOST];
	char client_port[NI_MAXSERV];

	//You gotta receive until you see the double carriage return
	//After that you can check for content length and know if you are done or not.

    struct request parsing_request;
    resetParsingHeader(&parsing_request);
    resetParsingHeaderFlags(&parsing_request);

    int multipleRequests = 0;

	while (_keepAccepting) {
        if(!multipleRequests) {
            int bytes_read = recv(sock, buffer, BUFFER_MAX - 1, 0);

            if (bytes_read == -1) {
                if (errno == EINTR) { /* this means we were interrupted by our signal handler */
                    if (_keepAccepting == 0) { /* g_running is set to 0 by the SIGINT handler we made */
                        printf("  Terminating on handle_client()\n");
                        break;
                    }
                }
            }
            if (bytes_read == 0) {
                if (verbose_flag) printf("Peer disconnected\n");
                close(sock);
                break;
            }
            if (bytes_read < 0) {
                perror("recv");
                break;
            }
            buffer[bytes_read] = '\0';
        }
		if(verbose_flag) printf("RECEIVED:\n  %s\n", buffer);

        struct request r;

        if(isHeaderComplete(buffer) && !parsing_request.is_header_parsed){
            parsing_request.is_header_ready = 1;
            parseHeader(buffer, &parsing_request);
            parsing_request.is_header_parsed = 1;
        }
        else if(!parsing_request.is_header_parsed){
            //if the header isn't complete parse whatever you got and wait to receive more.
            parseHeader(buffer, &parsing_request);
            multipleRequests = 0;
            continue;
        }

        if(verbose_flag) printf("PARSED HEADER. REQUEST TYPE: %s\n", parsing_request.rl.type);

        if(parsing_request.rl.type[0] == 'P'){
            parseBody(buffer, &parsing_request);
            if(!parsing_request.is_body_parsed){
                continue;
            }
        }

        char *buffer_ptr = &buffer;
        int buffer_length = strlen(buffer_ptr);
        buffer_ptr += buffer_length+4;
        printf("buffer: %s\n", buffer_ptr);
        if( strstr(buffer_ptr, "\r\n\r\n")){
            printf("Found multiple requests\n");
            strcpy(buffer, buffer_ptr);
            multipleRequests = 1;
        } else{
            printf("Not multiple requests\n");
            multipleRequests = 0;
        }

        sanitize_path(&parsing_request);
        executeRequest(&parsing_request, sock);
        resetParsingHeader(&parsing_request);
        resetParsingHeaderFlags(&parsing_request);
	}
}

int create_server_socket(char* port, int protocol) {
	int sock;
	int ret;
	int optval = 1;
	struct addrinfo hints;
	struct addrinfo* addr_ptr;
	struct addrinfo* addr_list;

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = protocol;
	/* AI_PASSIVE for filtering out addresses on which we
	 * can't use for servers
	 *
	 * AI_ADDRCONFIG to filter out address types the system
	 * does not support
	 *
	 * AI_NUMERICSERV to indicate port parameter is a number
	 * and not a string
	 *
	 * */
	hints.ai_flags = AI_PASSIVE | AI_ADDRCONFIG | AI_NUMERICSERV;
	/*
	 *  On Linux binding to :: also binds to 0.0.0.0
	 *  Null is fine for TCP, but UDP needs both
	 *  See https://blog.powerdns.com/2012/10/08/on-binding-datagram-udp-sockets-to-the-any-addresses/
	 */
	ret = getaddrinfo(protocol == SOCK_DGRAM ? "::" : NULL, port, &hints, &addr_list);
	if (ret != 0) {
		fprintf(stderr, "Failed in getaddrinfo: %s\n", gai_strerror(ret));
		exit(EXIT_FAILURE);
	}
	
	for (addr_ptr = addr_list; addr_ptr != NULL; addr_ptr = addr_ptr->ai_next) {
		sock = socket(addr_ptr->ai_family, addr_ptr->ai_socktype, addr_ptr->ai_protocol);
		if (sock == -1) {
			perror("socket");
			continue;
		}

		// Allow us to quickly reuse the address if we shut down (avoiding timeout)
		ret = setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));
		if (ret == -1) {
			perror("setsockopt");
			close(sock);
			continue;
		}

		ret = bind(sock, addr_ptr->ai_addr, addr_ptr->ai_addrlen);
		if (ret == -1) {
			perror("bind");
			close(sock);
			continue;
		}
		break;
	}
	freeaddrinfo(addr_list);
	if (addr_ptr == NULL) {
		fprintf(stderr, "Failed to find a suitable address for binding\n");
		exit(EXIT_FAILURE);
	}

	if (protocol == SOCK_DGRAM) {
		return sock;
	}
	// Turn the socket into a listening socket if TCP
	ret = listen(sock, SOMAXCONN);
	if (ret == -1) {
		perror("listen");
		close(sock);
		exit(EXIT_FAILURE);
	}

	return sock;
}

