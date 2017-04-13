//
// Created by benjamin on 29/03/17.
//

#include "http-parser.h"

void stripQueryString(struct request *r){
    r->vars_entries = 0;

    char *queryString = strchr(r->rl.path, '?');
    char *path = (char*)&r->rl.path[0];

    if(queryString) {
        if(verbose_flag) printf("\nSTRIPPING THE QUERY STRING FROM PATH\n");
        //TERMINATE STRING AT THE ? AND COPY THIS TO THE R PATH VARIABLE.
        strcpy(r->queryString, ++queryString);
        if(verbose_flag) printf("  Got query string: %s\n", queryString);

        *--queryString = '\0';
        strcpy(r->rl.path, path);
        if(verbose_flag) printf("  Path without query string: %s\n", path);
        queryString = '?';
    }

}

//This will add the right path depending on the host
void sanitize_path(struct request *r){
    //DEFAULT HOST
    char host[255] = "www";

    stripQueryString(r);

    if(verbose_flag) printf("\nLOOKING FOR HOST'S PATH:\n");
    //Find the location for the host in the config file variables.
    for(int i = 0; i < r->header_entries; i++){

        if(strcmp(r->hlines[i].key, "Host:") == 0){
            if(verbose_flag) printf("  Found Host Key in Headers. Now searching for Key Hosts in config file\n");

            for(int j = 0; j < number_of_host_entries; j++){
                if(strstr(r->hlines[i].value, hosts[j].name)){
                    strcpy(host, hosts[j].value);
                    if(verbose_flag) printf("  the location for the host is: %s \n", host);
                    break;
                }
            }

            break;
        }
    }
    if(strcmp(r->rl.path, "/") == 0){
        strcpy(r->rl.path, strcat(host, "/index.html"));
    }
    else{
        strcpy(r->rl.path, strcat(host, r->rl.path));
    }
    if(verbose_flag) printf("  Final path of file is: %s \n", r->rl.path);
}

int isHeaderComplete(unsigned char buffer[BUFFER_MAX]){
    char *endOfHeader = strstr(buffer,"\r\n\r\n");
    if(endOfHeader){
        *endOfHeader = '\0';
        return 1;
    }
    return 0;
}

int isBodyComplete(unsigned char buffer[BUFFER_MAX]){
    char *endOfHeader = strstr(buffer,"\n\r\n");
    endOfHeader += 3;
    char *endOfBody = strstr(endOfHeader,"\r\n");
    if(endOfBody){
        *endOfBody = '\0';
        return 1;
    }
    return 0;
}

void parseRequestLine(char* currentLine, struct request *r){
    struct request_line parsing_request_line; //
    if (sscanf(currentLine, "%s %s %s",
               parsing_request_line.type, parsing_request_line.path, parsing_request_line.http_v) == 3) {
        r->first_line_read = 1;
        r->rl = parsing_request_line;
    }
    else{
        if(verbose_flag) printf("INVALID REQUEST LINE\n");
        strcpy(parsing_request_line.type, "GET");
        strcpy(parsing_request_line.path, "/invalid.html");
        strcpy(parsing_request_line.type, "HTTP/1.1");
        r->responseFlag = BAD_REQUEST;
    }
}

void extractRange(char *range, struct request *r ){
    if(verbose_flag) printf("Extracting range from: %s.\n", range);
    char *spointer = strchr(range, '=');
    spointer++;
    strcpy(range, spointer);
    spointer = strchr(range, '-');
    *spointer = '\0';
    r->from = atoi(range);
    *spointer++ = '-';
    if(spointer) {
        r->to = atoi(spointer);
        if(verbose_flag) printf("Found range from %i to %i\n", r->from, r->to);
    }
}

void parseHeaderLine(char* currentLine, struct request *r){
    struct header parsing_header;
    if (sscanf(currentLine, "%s %s", parsing_header.key, parsing_header.value) == 2) {
        r->hlines[r->header_index++] = parsing_header;

        if(strcmp(parsing_header.key, "Range:") == 0){
            r->byte_range = 1;
            extractRange(&parsing_header.value[0], r);
        }
    }
}

void parseHeader(unsigned char buffer[BUFFER_MAX], struct request *r){

    if(verbose_flag) printf("\nPARSING HEADER\n");
    char *currentLine = (char *)&buffer[0];

    //READ LINE BY LINE
    while(currentLine) {
        char *nextLine = strstr(currentLine, "\r\n"); //Search for the string \r\n

        if (!r->is_header_ready && !nextLine) {
            strcpy(r->incompleteLine, currentLine);
            r->fragmented_line_waiting = 1;
            break;
        } else {
            if (nextLine) *nextLine = '\0';  // temporarily terminate the current line

            if(r->fragmented_line_waiting){
                strcat(r->incompleteLine,currentLine);
                if(!r->first_line_read){
                    parseRequestLine(r->incompleteLine, r);
                }
                else{
                    parseHeaderLine(r->incompleteLine, r);
                }
            }
            if (!r->first_line_read) {
                parseRequestLine(currentLine, r);
            }
            else {
                parseHeaderLine(currentLine, r);
            }

            if(verbose_flag) printf("  parsed the line: %s \n", currentLine);
            if (nextLine) *nextLine = '\r';  // then restore newline-char, just to be tidy
            currentLine = nextLine ? (nextLine + 2) : NULL;
        }
    }
    r->header_entries = r->header_index;
}

void getBodyContentLength(struct request *r){
    if(verbose_flag) printf("\nEXTRACTING CONTENT LENGTH\n");

    for(int i = 0; i < r->header_entries; i++){
        if(strcmp(r->hlines[i].key,"Content-Length:")==0){
            r->content_length = atoi(r->hlines[i].value);
            if(verbose_flag) printf("  Found content length Header of: %i\n", r->content_length);
            return;
        }
    }
    //INVALID REQUEST!
    if(verbose_flag) printf("No content length for POST method\n");
    strcpy(r->rl.type, "GET");
    strcpy(r->rl.path, "/invalid.html");
    strcpy(r->rl.type, "HTTP/1.1");
    r->responseFlag = BAD_REQUEST;
}

void parseBody(unsigned char buffer[BUFFER_MAX], struct request *r){
    if(verbose_flag) printf("\nPARSING REQUEST BODY\n");
    getBodyContentLength(r);

    char* bufferPointer = &buffer[0];
    if(!r->fragmented_line_waiting){
        bufferPointer += strlen(bufferPointer) + 4;
        if(verbose_flag) printf("  Skipped request header to: %s\n", bufferPointer);
    }
    else{
        bufferPointer = &buffer[0];
    }

    if(strlen(bufferPointer) >= r->content_length){
        strcpy(r->body, bufferPointer);
        if(verbose_flag) printf("  Found complete body: %s\n", bufferPointer);
        r->is_body_parsed = 1;
    }
    else if(strlen(bufferPointer) != r->content_length){
        strcpy(r->incompleteLine, bufferPointer);
        if(verbose_flag) printf("  Found fragmented body: %s\n", bufferPointer);
        r->fragmented_line_waiting = 1;
        return;
    }
    else if(r->fragmented_line_waiting = 1){
        if(strlen(r->incompleteLine) + strlen(bufferPointer) != r->content_length){
            strcat(r->incompleteLine, bufferPointer);
            if(verbose_flag) printf("  Added another portion to the fragmented body: %s\n", r->incompleteLine);
            r->fragmented_line_waiting = 1;
        }
        else{
            strcat(r->incompleteLine, bufferPointer);
            strcpy(r->body, r->incompleteLine);
            if(verbose_flag) printf("  Completed the fragmented body: %s\n", r->body);
            r->is_body_parsed = 1;
        }
    }
}

void getContentType(struct request *r, char *contentType){
    if(verbose_flag) printf("\nFIGURING CONTENT TYPE\n");
    char extension[64];
    char *c = strchr(r->rl.path,'.'); //Find the '.' of the file extension.
    if(c){
        strcpy(extension, ++c);
        if(verbose_flag) printf("  Found extension %s\n", extension);

        if(strcmp(extension, "php") == 0){
            if(verbose_flag) printf("  Found a dynamic extension. Setting type to text/html\n");
            strcpy(contentType,"text/html");
            r->dynamicContent = DYNAMIC;
            return;
        }

        for(int i = 0; i < number_of_media_entries; i++){
            if(strcmp(media[i].type, extension) == 0){
                strcpy(contentType, media[i].value);
                if(verbose_flag) printf("  Found content type for extension: %s\n", contentType);
                return;
            }
        }
    }

    if(verbose_flag) printf("  File has no extension. Setting content type to default: text/plain");
    strcpy(contentType,"text/plain");
}

void buildResponseHeader(struct request *r, char *send_buffer){
    struct stat sb;
    int content_length;

    //-----------------------GET FILE INFO----------------------//
    char *path = (char*) &r->rl.path;
    if (stat(path, &sb) == -1) {
        perror("stat");
        //exit(EXIT_FAILURE);
    }

    //---------------------GET RESPONSE CODE--------------------//
    char responseCode[32];
    FILE* sendFile;

    if(!r->responseFlag){
        r->responseFlag = NO_ERROR;
    }
    if(r->responseFlag == NOT_IMPL){
        strcpy(responseCode,"501 Not Implemented");
        strcpy(r->rl.path, "www/notimplemented.html");
    }
    else if(r->responseFlag == BAD_REQUEST){
        strcpy(responseCode,"400 Bad Request");
        strcpy(r->rl.path, "www/badrequest.html");
    }
    else if ( ( sendFile = fopen( r->rl.path, "r" ) ) == NULL ) {
        perror("fopen");
        if(errno == EACCES){
            strcpy(responseCode,"403 Forbidden");
            strcpy(r->rl.path, "www/forbidden.html");
            printf("\n\n\nNOT ACCESS\n\n\n");
        }else if(errno == ENOENT || errno == ENOTDIR){
            strcpy(responseCode, "404 Not Found");
            strcpy(r->rl.path, "www/notfound.html");
            path = (char *) &r->rl.path;
            printf("\n\n\nNOT FOUND\n\n\n");
        }

    }
    else{
        strcpy(responseCode,"200 OK");
        fclose(sendFile);
    }

    //----------RE-READ THE FILE IN CASE PATH CHANGED---------//
    if (stat(path, &sb) == -1) {
        perror("stat");
    }

    content_length = sb.st_size;
    //------------------------SEE FOR BYTE RANGE-----------------------//
    if(strcmp(responseCode, "200 OK") == 0 && r->byte_range == 1){
        strcpy(responseCode, "206 Partial Content");
        if(r->to == 0){
            r->to = content_length;
            if(verbose_flag) printf("  The Range TO now is: %d\n", r->to);
        }
        content_length = r->to - r->from;
        content_length++;
    }

    //------------------------GET TIME--------------------------//
    char timeString[40];
    time_t rawtime;
    struct tm * timeinfo;
    time (&rawtime);
    timeinfo = localtime (&rawtime);
    strftime(timeString, sizeof(timeString), "%a, %d %b %Y %H:%M:%S %Z", timeinfo);

    //------------------------GET LAST MODIFIED TIME----------------//
    char lastModified[40];
    struct tm * mTime;

    mTime = localtime (&sb.st_mtime);
    strftime(lastModified, sizeof(lastModified), "%a, %d %b %Y %H:%M:%S %Z", mTime);

    //---------------------GET CONTENT TYPE---------------------//
    char contentType[200];
    char* contentTypePtr = (char*)&contentType;
    getContentType(r, contentTypePtr);

    if(r->dynamicContent == DYNAMIC){
        if (snprintf(send_buffer, MAX_LEN,
                     "HTTP/1.1 %s\r\n"
                             "Date: %s\r\n"
                             "Server: Sadam Husein\r\n"
                             "Last-Modified: %s\r\n"
                             , responseCode, timeString, contentType, lastModified) < 0) {
            if(verbose_flag) printf("sprintf Failed\n");
        }
        if(verbose_flag) if(verbose_flag) printf("  Response Header: \n%s\n", send_buffer);
        return;
    }

    //---------------------PRINT COMPLETED RESPONSE HEADER---------------------//

    if (snprintf(send_buffer, MAX_LEN,
                 "HTTP/1.1 %s\r\n"
                         "Date: %s\r\n"
                         "Server: Sadam Husein\r\n"
                         "Content-Type: %s\r\n"
                         "Content-Length: %ld\r\n"
                         "Last-Modified: %s\r\n"
                         "\r\n", responseCode, timeString, contentType, content_length, lastModified) < 0) {
        if(verbose_flag) printf("sprintf Failed\n");
    }

    if(verbose_flag) printf("  Response Header: \n%s\n", send_buffer);
}


//TODO: THIS COULD BE RENAMED TO SEND STRING
void sendResponseHeader(char *send_buffer, int sock){
    int bytes_sent;
    char *send_buffer_ptr = send_buffer;
    //--------------------LOOP UNTIL ALL BYTES ARE SENT-------------------//
    do {
        bytes_sent = send(sock, send_buffer_ptr, strlen(send_buffer), 0);
        if(bytes_sent == -1) {
            if (errno == EINTR) { /* this means we were interrupted by our signal handler */
                if (_keepAccepting == 0) { /* g_running is set to 0 by the SIGINT handler we made */
                    printf("  Terminating on sendResponseHeader()\n");
                    exit(EXIT_SUCCESS);
                }
            }
        }
        if(verbose_flag) printf("  Sent %i bytes of %i\n", bytes_sent, strlen(send_buffer));
        send_buffer_ptr += bytes_sent;
    }while(bytes_sent < strlen(send_buffer));
}

void getCGIResource(struct request *r, FILE** sendFile, int sock){
    printf("\nRUNNING CGI FOR DYNAMIC CONTENT\n");

    pid_t pid = 0;
    int pipefd[2];
    char line[256];
    int status;
    struct stat sb;


    pipe(pipefd);
    if ((pid = fork()) != 0) { // if we're the parent
        close(pipefd[1]);
        waitpid(pid, NULL, 0); // wait for program to exit

        int content_length = 0;

        //-----------------------GET FILE INFO----------------------//
        FILE* output = fdopen(pipefd[0], "r");

        fstat(pipefd[0], &sb);

        char* cgi_buffer = malloc(sizeof(char)*MAX_LEN);
        char* cgi_buffer_start = cgi_buffer;
        int cgi_string_size = 0;

        while ( !feof(output) ) {
            if ( ( cgi_buffer = (char *)realloc( cgi_buffer_start, ( sizeof(cgi_buffer_start) + BUFFER_MAX ) * sizeof( char ) ) ) == NULL ) {
                close( output );
                exit(EXIT_FAILURE);
            }

            int bytes_read = fread(cgi_buffer, sizeof(char), BUFFER_MAX, output);
            cgi_string_size += bytes_read;
            cgi_buffer += bytes_read;

            if ( bytes_read < 0 ) {
                perror(read);
            }
        }

        *cgi_buffer = '\0';
        cgi_string_size -= 42; //CGI ADDS A HEADER. we need to subtract the header's length

        printf("%s\n", cgi_buffer_start);
        printf("Size : %i\n", cgi_string_size);


        char contentHeader[100];
        snprintf(contentHeader, sizeof(contentHeader),
                 "Content-Length: %ld\r\n", cgi_string_size);

        sendResponseHeader(&contentHeader, sock);
        sendResponseHeader(cgi_buffer_start, sock);
        free(cgi_buffer);
        return;
    }

    close(pipefd[0]);
    printf("  Setting up dup2 to redirect output\n");
    dup2(pipefd[1], STDOUT_FILENO); // pipefd[1] is the parent.
    dup2(pipefd[1], STDERR_FILENO);


    char cwd[1024];
    if (getcwd(cwd, sizeof(cwd)) == NULL) {
        perror("    getcwd() error\n");
        return;
    }
    strcat(cwd, "/");
    strcat(cwd, r->rl.path);
    setenv("SERVER_SOFTWARE", "Sadam Husein", 1);
    setenv("QUERY_STRING", r->queryString, 1);
    setenv("PATH_TRANSLATED", cwd, 1);

    char* args[] = { "/usr/bin/php-cgi7.0", (char*)0 };

    if(execv(args[0], args)){
        perror("execv");
        exit( EXIT_FAILURE );
    }
}

void sendResponse(char *send_buffer, int sock, struct request *r){

    if(verbose_flag) printf("\nSENDING RESPONSE\n");
    FILE* sendFile;
    if(r->dynamicContent == DYNAMIC){
        getCGIResource(r, sendFile, sock);
        return;
    }
    else {
    //---------------------------OPEN FILE------------------------------//
        if ((sendFile = fopen(r->rl.path, "r")) == NULL) {
            perror("fopen");
        }
    }
    //---------------------------SENDING FILE---------------------------//
    int total_bytes_sent = 0;
    int contentLength;
    if(r->byte_range){contentLength = r->to - r->from; contentLength++;}

    while( !feof(sendFile) )
    {
        int numread;

        //--------------READ FROM FILE DEPENDING OF BYTE RANGE ON/OFF--------------//
        if(r->byte_range == 1){
            numread = fread(send_buffer, sizeof(char), contentLength>MAX_LEN?MAX_LEN:contentLength, sendFile);
            contentLength -= numread;
        }
        else {
            numread = fread(send_buffer, sizeof(char), MAX_LEN, sendFile);
        }

        if( numread < 1 ) break; // EOF or error

        char *send_buffer_ptr = send_buffer;
        //---------------LOOP UNTIL ALL BYTES ARE SENT-----------------//
        do
        {
            int numsent = send(sock, send_buffer_ptr, numread, 0);
            if(numsent == -1) {
                if (errno == EINTR) { /* this means we were interrupted by our signal handler */
                    if (_keepAccepting == 0) { /* g_running is set to 0 by the SIGINT handler we made */
                        printf("  Terminating on sendResponse()\n");
                        exit(EXIT_SUCCESS);
                    }
                }
            }
            send_buffer_ptr += numsent;
            numread -= numsent; //numread becomes the number of bytes left to send.
            total_bytes_sent += numsent;
        }
        while( numread > 0 );
    }
    if(verbose_flag) printf("  Total Bytes Sent: %i\n", total_bytes_sent);
    fclose(sendFile);
}

void executeGet(struct request *r, int sock){
    if(verbose_flag) printf("\nEVALUATING GET REQUEST\n");
    char send_buffer[MAX_LEN];
    buildResponseHeader(r, (char *)send_buffer);
    //---------------------SEND HEADER---------------------------------//
    //char *send_buffer_ptr = send_buffer;
    sendResponseHeader((char *)send_buffer,sock);
    //---------------------SEND RESPONSE BODY/FILE--------------------------//
    sendResponse((char *)send_buffer, sock, r);
}

void executeHead(struct request *r, int sock){
    if(verbose_flag) printf("\nEVALUATING HEAD REQUEST\n");
    char send_buffer[MAX_LEN];
    //---------------------SEND HEADER---------------------------------//
    buildResponseHeader(r, (char *)send_buffer);
    sendResponseHeader((char *)send_buffer,sock);
}

void executePost(struct request *r, int sock){
    if(verbose_flag) printf("\nEVALUATING POST REQUEST\n");
    char send_buffer[MAX_LEN];
    buildResponseHeader(r, (char *)send_buffer);
    //---------------------SEND HEADER---------------------------------//
    //char *send_buffer_ptr = send_buffer;
    sendResponseHeader((char *)send_buffer,sock);
    //---------------------SEND RESPONSE BODY/FILE--------------------------//
    sendResponse((char *)send_buffer, sock, r);
}

void executeRequest(struct request *r, int sock){
    if(strcmp(r->rl.type, "GET") == 0){
        executeGet(r, sock);
    }
    else if(strcmp(r->rl.type, "HEAD") == 0){
        executeHead(r, sock);
    }
    else if(strcmp(r->rl.type, "POST") == 0){
        strcpy(r->queryString, r->body);
        executePost(r, sock);
    }
    else{
        r->responseFlag = NOT_IMPL;
        strcpy(r->rl.type, "GET");
        strcpy(r->rl.path, "/notimplemented.html");
        strcpy(r->rl.http_v, "HTTP/1.1");
        executeGet(r, sock);
    }
}

