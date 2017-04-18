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
#include <signal.h>
#include <linux/limits.h>

#include <sys/epoll.h>
#include <sys/types.h>
#include <fcntl.h>

#include "http-parser.h"
#include "queue.h"


#define BUFFER_MAX	1024

//----------------------EPOLL FILE DESCRIPTOR-------------------//
int epoll_fd;

//------------------------CONF VARS-----------------------------//
int _queueSize;
int _threadNumber;
#define MAX_EVENTS 10

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

//-------------------------CLIENT INFO STRUCT-----------------//
struct clientInfo{
    struct request r;
    int clientfd;
};

//------------------------THREAD INFO STRUCT------------------//
struct threadInfo{
    pthread_t* thread;
    int epollfd;
};

int nextThreadIndex;
struct threadInfo* threads;

void resetParsingHeader(struct request *r);
void parseConfig(char *filename);
void handle_sigchld(int sig);
int create_server_socket(char* port, int protocol);
void handle_client(int sock, struct clientInfo  *c_info, int epoll);

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

void consumeClient(void *args){
    printf("\nCONSUME CLIENT FUCNTION\n");

    struct threadInfo* thread =  (struct threadInfo*) args;
    struct epoll_event events[MAX_EVENTS];
    int nfds;

    while (_keepAccepting) {
        printf("  Epoll %i is waiting\n", thread->epollfd);
        nfds = epoll_wait(thread->epollfd, events, MAX_EVENTS, -1);
        printf("  Epoll %i got %i events\n", thread->epollfd, nfds);
        for(int i = 0; i < nfds; ++i){
            if(events[i].events & EPOLLIN && !(events[i].events & EPOLLOUT)) {
                events[i].data.fd;
                struct clientInfo *cInfo = (struct clientInfo *) events[i].data.ptr;
                printf("  Epoll %i got a request from %i\n", thread->epollfd, cInfo->clientfd);
                handle_client(cInfo->clientfd, (struct clientInfo *) events[i].data.ptr, thread->epollfd);
            }
            else {
                printf("I SEE! EPOLLET Switches between EPOLLIN and EPOLLOUT automatically!\n");
            }
        }
    }
}


void createWorkerThreads(){
    nextThreadIndex = 0;
    printf("\nCREATING WORKER THREADS\n");
    threads = NULL;
    threads = (struct threadInfo*)malloc(_threadNumber * sizeof(struct threadInfo));

    for (int i = 0; i < _threadNumber; i++) {
        printf("  thread number %i created\n", i);
        threads[i].epollfd = epoll_create1(0);
        if (threads[i].epollfd == -1) {
            perror("epoll_create1");
            exit(EXIT_FAILURE);
        }
        threads[i].thread = (pthread_t*) malloc(sizeof(pthread_t));
        if(!pthread_create(threads[i].thread, NULL, consumeClient, (void*) &threads[i] )){
            perror("pthread_create:");
        }
    }
}

void killThreads(){
    printf("\n\nKILLING THREADS\n\n");
    printf("Number of threads: %i\n", _threadNumber);
    for(int i = 0; i < _threadNumber; i++){
        printf("%i\n", i);
        printf("error code: %i\n", pthread_kill(threads[i].thread, SIGINT));

    }

    printf("\n\nJOINNING THREADS\n\n");
    for(int i = 0; i < _threadNumber; i++){
        if(pthread_join(threads[i].thread, NULL) != 0){
            perror("pthread_join");
        }
    }

    freeQueue(&_clientQueue);
    free(threads);
    printf("\n\nKILLING THREADS END\n\n");
    exit(EXIT_SUCCESS);
}

int init_tcp(char* path, char* port, int verbose, int threadNo, int queueSize) {

    //----------------------------SETUP SIGNAL HANDLERS------------------------------//
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
    _threadNumber = threadNo;

    //---------------------------SET UP OTHER VARIABLES-------------------------------//
    verbose_flag = verbose;
    _keepAccepting = 1;

    //---------------------------CREATE WORKER THREADS--------------------------------//
    createWorkerThreads();



	int sock = create_server_socket(port, SOCK_STREAM);
    printf("Setting server port as a non-blocking call\n");
    set_blocking(sock, 0);
	parseConfig(path);

    //----------------------------CREATE EPOLL FOR MAIN THREAD-------------------------//
    struct epoll_event ev, events[MAX_EVENTS];
    int nfds, epollfd;

    epollfd = epoll_create1(0);
    if (epollfd == -1) {
        perror("epoll_create1");
        exit(EXIT_FAILURE);
    }

    ev.events = EPOLLIN;
    ev.data.fd = sock;
    if (epoll_ctl(epollfd, EPOLL_CTL_ADD, sock, &ev) == -1) {
        perror("epoll_ctl: listen_sock");
        exit(EXIT_FAILURE);
    }

    //--------------------------LOOP LOOKING FOR CLIENTS--------------------------------//

    int counter = 0;
    for (;;) {
        printf("\nMAIN EPOLL IS WAITING\n");
        nfds = epoll_wait(epollfd, events, MAX_EVENTS, -1);
        if (nfds == -1) {
            perror("epoll_wait");
            exit(EXIT_FAILURE);
        }

        struct sockaddr_storage client_addr;
        socklen_t client_addr_len = sizeof(client_addr);
        int conn_sock = accept(sock, (struct sockaddr *) &client_addr, &client_addr_len);
        printf("\n\n\nCLIENT COUNTER: %i\n\n\n", ++counter);

        if (conn_sock == -1) {

            if (errno == EINTR) { /* this means we were interrupted by our signal handler */
                if (_keepAccepting == 0) { /* g_running is set to 0 by the SIGINT handler we made */
                    printf("\nBREAKING OUT OF MAIN LOOP\n");
                    break;
                }
            }

            perror("accept");
            continue;
        }

        //-------------------SET CLIENT TO NON BLOCKING----------------//
        set_blocking(conn_sock, 0);

        //----------------GETTING THE NEXT THREAD INDEX----------------//
        int poller = nextThreadIndex%_threadNumber;
        nextThreadIndex = poller + 1;

        threads[poller].epollfd;
        ev.events = EPOLLIN | EPOLLET;
        ev.data.fd = conn_sock;

        //-------------CREATE AND INITIALIZE CLIENT INFO---------------//
        struct clientInfo* cInfo = malloc(sizeof(struct clientInfo));
        resetParsingHeader(&cInfo->r);
        resetParsingHeaderFlags(&cInfo->r);
        cInfo->clientfd = conn_sock;

        //------------------ATTACH CLIENT INFO TO EVENT---------------//
        ev.data.ptr = (void*)cInfo;

        if (epoll_ctl(threads[poller].epollfd, EPOLL_CTL_ADD, conn_sock,
                      &ev) == -1) {
            perror("epoll_ctl: conn_sock");
            exit(EXIT_FAILURE);
        }
        printf("  Epoll with id: %i got the client: %i\n", threads[poller].epollfd, conn_sock);
    }
    killThreads();
	return 0;
}

void resetParsingHeader(struct request *r){

    memset(r->incompleteLine, 0, sizeof(r->incompleteLine));
    memset(r->hlines, 0, sizeof(r->hlines));
    memset(r->vars, 0, sizeof(r->vars));
    memset(r->contenttype, 0, sizeof(r->contenttype));
    memset(r->body, 0, sizeof(r->body));
    memset(r->queryString, 0, sizeof(r->queryString));

    memset(r->rl.type, 0, sizeof(r->rl.type));
    memset(r->rl.path, 0, sizeof(r->rl.path));
    memset(r->rl.http_v, 0, sizeof(r->rl.http_v));

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


void exhaustReceivingData(int sock, char *buffer, int epoll){

    while (_keepAccepting) {
        //--------------------------CALL RECV AND FILL BUFFER-------------------------------//
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
            epoll_ctl(epoll, EPOLL_CTL_DEL, sock, NULL);
            close(sock);
            break;
        }
        if (bytes_read < 0) {
            if ((errno == EAGAIN || EWOULDBLOCK)) { /* this means we were interrupted by our signal handler */
                //printf("\nEAGAIN || EWOULDBLOCK\n");
                break;
            }
            perror("recv");
            break;
        }

        //-------------------------END THE RECEIVED INFO STRING-----------------------------------//
        buffer[bytes_read] = '\0';
    }
    if(verbose_flag) printf("RECEIVED:\n  \n%s\n", buffer);
}


int countSeparateRequests(char* buffer){
    int counter = 0;
    char* bufferPtr = strstr(buffer, "\r\n\r\n");
    while(bufferPtr){
        ++counter;
        bufferPtr += 4;
        if(bufferPtr) {
            bufferPtr = strstr(bufferPtr, "\r\n\r\n");
        }
    }
    return counter;
}


void separateRequests(char* buffer, char** separatedRequests, char* incompleteRequest, int counter){
    printf("counter:  %i \n", counter);
    char* bufferStart = buffer;
    int bufferLength = strlen(buffer);
    printf("Buffer: %i, BufferStart: %i, BufferLen:  %i \n", buffer, bufferStart, bufferLength);
    char* bufferPtr;
    for(int i = 0; i < counter; ++i){
        bufferPtr = strstr(buffer, "\r\n\r\n");
        *bufferPtr = '\0';
        separatedRequests[i] = malloc(sizeof(char)*BUFFER_MAX);
        strcpy(separatedRequests[i], buffer);
        buffer = bufferPtr + 4;
    }

    printf("Buffer: %i, BufferStart: %i, BufferLen:  %i \n", buffer, bufferStart, bufferLength);
    printf("Buffer-BufferStart = %i\n", buffer-bufferStart);

    if(buffer-bufferStart < bufferLength-1){
        strcpy(incompleteRequest, buffer);
    }
}

void test(char *asdf){
    asdf = 10;
}

void handle_client(int sock, struct clientInfo  *c_info, int epoll) {

    printf("\nHANDLING CLIENT %i\n", sock);

	unsigned char buffer[BUFFER_MAX];
    char *buffer_copy;
    char** separatedRequests;
    //TODO: FREE THIS ONE
    char incompleteRequest[BUFFER_MAX];

    exhaustReceivingData(sock, &buffer[0], epoll);
    int requestNo = countSeparateRequests(&buffer[0]);
    separatedRequests = malloc(sizeof(char*) * requestNo);
    separateRequests(&buffer[0], separatedRequests, &incompleteRequest[0], requestNo);

    //--------------------IF THERE IS NO FRAGMENTED LINE RESET REQUEST VALUES-------------------//
    if(!c_info->r.fragmented_line_waiting){
        struct request r;
        c_info->r = r;
        resetParsingHeader(&c_info->r);
        resetParsingHeaderFlags(&c_info->r);
    }
    else{
        printf("INCOMPLETE LINE: %s\n", c_info->r.incompleteLine);
    }


    //---------------------------------BEGIN PARSING--------------------------------------------//
    for(int i = 0; i < requestNo; ++i){

        c_info->r.is_header_ready = 1;
        parseHeader(separatedRequests[i], &c_info->r);
        c_info->r.is_header_parsed = 1;


        //--------------------------IF POST REQUEST THEN PARSE BODY-----------------------------//
        if(c_info->r.rl.type[0] == 'P'){
            parseBody(separatedRequests[i] + 4, &c_info->r);
            c_info->r.is_body_parsed = 1;
        }

        sanitize_path(&c_info->r);
        executeRequest(&c_info->r, sock);
        resetParsingHeader(&c_info->r);
        resetParsingHeaderFlags(&c_info->r);
    }

    if(strlen(incompleteRequest)>1){
        printf("  Header is not complete!\n");
        c_info->r.is_header_ready = 0;
        parseHeader(incompleteRequest, &c_info->r);
        c_info->r.fragmented_line_waiting = 1;
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

