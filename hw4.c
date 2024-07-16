#define  _POSIX_C_SOURCE 200809L
#include<stdio.h>
#include<unistd.h>
#include<stdlib.h>
#include<string.h>
#include<fcntl.h>
#include<sys/stat.h>
#include<sys/types.h>
#include<sys/wait.h>
#include<sys/select.h>
#include<assert.h>

#include<sys/socket.h>
#include<arpa/inet.h>

#define exit(N) {fflush(stdout); fflush(stderr); _exit(N); }




static const char ok200_response_line[] = "HTTP/1.1 200 OK\r\n"; // top line of an OK response
static const char error400_response[] = "HTTP/1.1 400 Bad Request\r\n\r\n"; // Bad request
static const char error404_response[] = "HTTP/1.1 404 Not Found\r\n\r\n"; // Resource Not Found
//static char client_request[1024]; // A global buffer to store client requests
//static char response_header[1024]; // A global buffer to store the response header
static char response_body[1024]; // A global buffer to store the response body

static const char ping_request_header[] = "GET /ping HTTP/1.1\r\n";
static const char echo_request_header[] = "GET /echo HTTP/1.1\r\n"; 
static const char write_request_header[] = "POST /write HTTP/1.1\r\n";
static const char read_request_header[] = "GET /read HTTP/1.1\r\n"; 
static const char stats_request_header[] = "GET /stats HTTP/1.1\r\n";
static const char file_request_header[] = "GET /%s HTTP/1.1\r\n";

static const char content_len_response_line[] = "Content - Length : %lu\r\n"; 
static const char stats_body_response[] = "Requests: %d\nHeader bytes: %d\nBody bytes: %d\nErrors: %d\nError bytes: %d";


static char request[2048];
static char response[4096]; // response[2048] ; 
static char postText[2048] = "<empty>"; // postText[1024] ;
int postTextLenght = 0 ;

static int response_count = 0 ; //storing the number of requests made
static int header_bytes = 0; // Global variable to track header size
static int body_bytes = 0; // Global variable to track body size
static int error_count = 0 ; // Counts number of errors made
static int error_bytes = 0 ; //storing the size of all the errors made 

 
/*static int Open(const char *fname, int opts, int permissions)
{
    int fd = open(fname, opts, permissions);
    if (fd < 0) {
        perror("Error openning file.\n");
        exit(1);
    }
    return fd;
} */

static void Fstat(int fd, struct stat * buf)
{
    int ret = fstat(fd, buf);
    if (ret < 0) {
        perror("fstat");
        exit(1);
    }
} 


/*static off_t get_size(const char * filename)
{
    if (filename == NULL) return -1;

    struct stat buf;
    int fd = Open(filename, O_RDONLY, 0);
    Fstat(fd, &buf);
    return buf.st_size;
}
*/

///////////////
static int get_port(void)
{
    int fd = open("port.txt", O_RDONLY);
    if (fd < 0) {
        perror("Could not open port.txt");
        exit(1);
    }

    char buffer[32];
    int r = read(fd, buffer, sizeof(buffer));
    if (r < 0) {
        perror("Could not read port.txt");
        exit(1);
    }

    return atoi(buffer);
}






static int Socket(int namespace, int style, int protocol){
    int sockfd = socket(namespace,style, protocol); // sets up a socket
    if (sockfd < 0){ // error checking
        perror("socket");
        exit(1);  
    }
    return sockfd; 
}

static void Bind(int sockfd, struct sockaddr* server, socklen_t length){
    if (bind(sockfd, server, length) < 0){ // binds the server struct to the socket
        perror("bind"); // error checking
        exit(1); 
    }
}

static void Listen(int sockfd, int qlen){
    if (listen(sockfd, qlen) < 0){ // server is listening for clients
        perror("listen"); // error checking
        exit(1);
    }
}

static int Accept(int sockfd, struct sockaddr* addr, socklen_t * length_ptr){
    int newfd = accept(sockfd, addr, length_ptr); // accpets a client if avaliable otherwise blocks
    if (newfd < 0){ // error checking
        perror("accept"); 
        exit(1); 
    }
    return newfd; 
}

ssize_t Recv(int socket, void * buffer, size_t size, int flags){
    ssize_t ret_size = recv(socket, buffer, size, flags); // recieves data from client 
    if (ret_size < 0){ // error checking
        perror("recv");
        exit(1); 
    }   
    return ret_size; 
}

ssize_t Send(int socket, const void *buffer, size_t size, int flags) {
    size_t total_sent = 0;
    char *char_buffer = (char *) buffer;

    while (total_sent < size) {
        ssize_t sent = send(socket, char_buffer + total_sent, size - total_sent, flags);

        if (sent < 0) {
            perror("send");
            exit(1);
        } else if (sent == 0) {
            break;
        }
        total_sent += sent;
    }
    return total_sent;
}

static void CallPing(int connfd){
    int content_length = 4; 
    int responseLength = sprintf(response, "%sContent-Length: %d\r\n\r\npong", ok200_response_line, content_length);
    response[responseLength] = '\0';
    Send(connfd,response, strlen(response), 0); 
    header_bytes += 38 ; 
    body_bytes += 4; 
}

static void CallEcho(int connfd) {
    char* start = strstr(request, "\r\n");
    *start = '\0'; 
    start += 2; // Move to the body of the request.
    char* end = strstr(start, "\r\n\r\n");
    if (end == NULL) {
        end = request + 1024;
    }
    *end = '\0';
    
    int length = strlen(start);
    char body[1024];
    memcpy(body,start,length);
    body[length] = '\0';
    int response_size = snprintf(response, sizeof(response), "%sContent-Length: %d\r\n\r\n%s", ok200_response_line, length, body);
    header_bytes +=  response_size - length ; 
    body_bytes += length;
    Send(connfd, response, response_size, 0);
    
}

static void CallRead(int connfd){
    int contentLength = strlen(postText);
    int response_size = sprintf(response, "%sContent-Length: %d\r\n\r\n%s", ok200_response_line, contentLength, postText); 
    int size = Send(connfd, response, response_size, 0); 
    body_bytes += contentLength ; 
    header_bytes += response_size - contentLength ; 

}

static void CallWrite(int connfd){

    char body[1024];
    int contentLength ; 
    char* parserLength = strstr(request, "Content-Length: "); 
    *parserLength = '\0'; 
    parserLength += 16; 
    contentLength = atoi(parserLength); 
    char* bodyText = strstr(parserLength, "\r\n\r\n"); 
    bodyText += 4; 
    *(bodyText + contentLength) = '\0' ; 
    memset(postText, '\0', sizeof(postText));
    if(contentLength > 1024){
        contentLength = 1024 ;
    }
    strncpy(postText, bodyText, contentLength); 
    postTextLenght = contentLength ;
    CallRead(connfd) ;
    
}


static void GetFile(int connfd){

    char header[1024] ;
    char body[1024];  

    char filename[128] ;
    sscanf(request, file_request_header, filename) ;


    int fd = open(filename, O_RDONLY, 0);
    if(fd < 0){
        Send(connfd, error404_response, strlen(error404_response), 0); 
        error_count++; 
        error_bytes += strlen(error404_response) ;
        response_count -- ;
        return; //file not found
    }
    
    struct stat fileInfo;
    Fstat(fd, &fileInfo);
    int fileSize = fileInfo.st_size; 


    
    int headerSize = sprintf(header, "%sContent-Length: %d\r\n\r\n", ok200_response_line, fileSize) ; 
    header_bytes += headerSize;
    Send(connfd, header, headerSize, 0);
   
   int bytesRead = 0 ;
   int bytesSent = 0 ;
   int totalSent = 0 ;

   while(totalSent < fileSize){
    bytesRead = read(fd, body, sizeof(body)) ;
    bytesSent = Send(connfd, body, bytesRead, 0);

    while(bytesSent != bytesRead){
        bytesSent += Send(connfd, body+bytesSent, bytesRead - bytesSent, 0); 
    }
    totalSent += bytesSent ; 
   }
    body_bytes += totalSent ;
    close(fd) ;
}

static void CallStats(int connfd){
    char statBody[1028] ;
    int contentSize = sprintf(statBody, stats_body_response, response_count, header_bytes, body_bytes, error_count, error_bytes);
    //statBody[contentSize] = '\0'; 
    int statSize = sprintf(response, "%sContent-Length: %d\r\n\r\nRequests: %d\nHeader bytes: %d\nBody bytes: %d\nErrors: %d\nError bytes: %d", ok200_response_line, contentSize, response_count, header_bytes, body_bytes, error_count, error_bytes);
    response[statSize] = '\0'; 
    Send(connfd, response, statSize, 0); 
    body_bytes += contentSize ;
    header_bytes += statSize - contentSize ; 
}




static void Handle_Client(int connfd)
{
    
    int length = Recv(connfd, request, sizeof(request) -1, 0);
    if(length == 0){ //empty request
        return; 
    }
    request[length] = '\0' ; 
    if(length == 0){
        return; // there was no request
    }

    if(!strncmp(request, ping_request_header, strlen(ping_request_header))){
        CallPing(connfd);
        response_count++ ;
        return;

    }else if(!strncmp(request, echo_request_header, strlen(echo_request_header))){
        CallEcho(connfd);
        response_count++; 
       
        return; 
    }else if(!strncmp(request, write_request_header, strlen(write_request_header))){
        CallWrite(connfd);
        response_count++; 
        
        return;

    }else if(!strncmp(request, read_request_header, strlen(read_request_header))){
        CallRead(connfd);
        response_count++;
        
        return; 
    }else if( !strncmp(request, stats_request_header, strlen(stats_request_header))){
        CallStats(connfd);
        response_count++; 
        
        return;
    }else if(!strncmp(request, "GET /", 5)){
        GetFile(connfd);
        response_count++ ; 
        
        return; 
    }

    Send(connfd, error400_response, strlen(error400_response),0); //Bad request
    error_count++; 
    error_bytes += strlen(error400_response) ; 
    
    return; 

}

int client_sockets[10] ;
int client_count = 0;



// Main starts here
int main(int argc, char** argv)
{   
    response_count = 0 ; //storing the number of requests made
    header_bytes = 0; // Global variable to track header size
    body_bytes = 0; // Global variable to track body size
    error_count = 0 ; // Counts number of errors made
    error_bytes = 0 ; //storing the size of all the errors made 


    //Getting a port
    int port = get_port();

    printf("Using port %d\n", port);
    printf("PID: %d\n", getpid());

    // Setting up the socker
    int sockfd = Socket(AF_INET, SOCK_STREAM,0);

    // Make server available on port
    static struct sockaddr_in server;
    server.sin_family = AF_INET;
    server.sin_port = htons(port);
    inet_pton(AF_INET, "127.0.0.1", &(server.sin_addr)); 

    // making the socket readilly available for the server
    int optval = 1; 
    setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));   

    //Binding the server to a socket and making the server listen for clients
    Bind(sockfd, (struct sockaddr *) &server, sizeof(server));
    Listen(sockfd, 10); 

    //Creating a client object
    struct sockaddr_in client;
    socklen_t csize = sizeof (client);

    fd_set ready_set, read_fds;
    FD_ZERO(&read_fds);
    FD_SET(sockfd, &read_fds); 
    int max_fd = sockfd; 

    // Process client requests
    while (1) {
        ready_set = read_fds ; 
        int activity = select(max_fd+1, &ready_set, NULL, NULL, NULL);
        if(activity < 0){
            perror("select") ;
            exit(1); 
        }
        if(FD_ISSET(sockfd, &ready_set)){
            int connfd = Accept(sockfd, (struct sockaddr*) &client, &csize); // Accepts when a client connects
           // Handle_Client(connfd); // handles the clients request
            FD_SET(connfd, &read_fds); 
            if(connfd > max_fd){
                max_fd = connfd ;
            }
            //close(connfd);
        }
        for(int fd = 0; fd <= max_fd; fd++){
            if(FD_ISSET(fd, &ready_set)){
                if(fd == sockfd){
                    continue; 
                }
                Handle_Client(fd) ;
                close(fd);
                FD_CLR(fd, &read_fds) ;
            }
        }

    }

    close(sockfd);

    return 0;
}
