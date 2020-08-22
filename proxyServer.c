#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <netdb.h>
#include "threadpool.h"

/*======= declaration of define =======*/

#define BUF_SIZE 1024
#define USAGE_ERROR "proxyServer <port> <pool size> <max-number-of-request> <filter>\n"

#define BAD_REQUEST 400
#define ERROR_400 "400 Bad Request"
#define ERROR_MESSGE_400 "Bad Request."

#define FORBIDDEN 403
#define ERROR_403 "403 Forbidden"
#define ERROR_MESSGE_403 "Access denied."

#define NOT_FOUND 404
#define ERROR_404 "404 Not Found"
#define ERROR_MESSGE_404 "File not found."

#define INTERNAL_SERVER_ERROR 500
#define ERROR_500 "500 Internal Server Error"
#define ERROR_MESSGE_500 "Some server side error."

#define NOT_SUPPORTED 501
#define ERROR_501 "501 Not supported"
#define ERROR_MESSGE_501 "Method is not supported."

/*======= Global Variables =======*/

char **filter = NULL; // will onation the filter.txt file data

/*======= main struct data =======*/

struct serverData
{
    int port;
    int pool_size;
    int max_number_of_request;
    threadpool *tp;
};
typedef struct serverData serverData;

/*======= declaration of function =======*/

void readFilterFile(char *filter);
void freeFilter();
void checkArgvValidtion(int argc, char *argv[]);
void initializeDataServer(serverData **data, char *argv[]);
void checkIfStringIsValidNumber(char *str);
void sendUsageErrorMessage();
void sendErrorMessage(serverData *data, char *msg, int statusRelease, int fd, int *requests_fd);
char *createErrorMessage(char *Error_Num, char *Error_Message);
int responsetToServer(void *arg);
int checkInput(int fd, char *readBuf);
int sendRequest(int fd, char *host, int port, char *msg);

/*======= Main =======*/

int main(int argc, char *argv[])
{
    checkArgvValidtion(argc, argv); // check if the input in the argv and argc is valid

    serverData *data;
    initializeDataServer(&data, argv); // initialize the parameters
    readFilterFile(argv[4]);        // read the filter.txt file data into the filter

    // create socket for the server
    int sockfd;
    struct sockaddr_in serv_addr;

    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0)
    {
        sendErrorMessage(data, "ERROR, sockfd has failed", 0, 0, NULL);
    }
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    serv_addr.sin_port = htons(data->port);
    // create bind
    if (bind(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0)
    {
        sendErrorMessage(data, "ERROR, bind has failed", 1, sockfd, NULL);
    }
    // create listen
    if (listen(sockfd, 5) < 0)
    {
        sendErrorMessage(data, "ERROR, listen has failed", 1, sockfd, NULL);
    }
    // create arrays of fd
    int *requests_fd = (int *)malloc(sizeof(int) * data->max_number_of_request);
    if (requests_fd == NULL)
    {
        sendErrorMessage(data, "ERROR, requests_fd allocation has failed", 1, sockfd, NULL);
    }
    for (int i = 0; i < data->max_number_of_request; i++)
    { // send the threadpool to do is work
        requests_fd[i] = accept(sockfd, NULL, NULL);
        if (requests_fd[i] < 0)
        {
            sendErrorMessage(data, "ERROR, requests_fd[i] accept has failed", 2, sockfd, requests_fd);
        }
        dispatch(data->tp, responsetToServer, &requests_fd[i]);
    }

    // free all allocation memory
    destroy_threadpool(data->tp);
    free(requests_fd);
    free(data);
    freeFilter();
    close(sockfd);
    return 0;
}

// this method read the request from the broswer
int responsetToServer(void *arg)
{
    int readBytes = 0;
    int size = 0;
    int fd = *((int *)arg);

    char *readBuf = (char *)malloc(sizeof(char));
    if (readBuf == NULL)
    {
        perror("Error, readBuf allocation has failed close the thread");
        pthread_exit(NULL);
    }
    memset(readBuf, 0, sizeof(char));

    char *temp = (char *)malloc(BUF_SIZE * sizeof(char));
    if (temp == NULL)
    {
        perror("Error, temp allocation has failed close the thread");
        pthread_exit(NULL);
    }
    memset(temp, 0, BUF_SIZE * sizeof(char));

    while ((readBytes = read(fd, temp, BUF_SIZE - 1)) > 0)
    { // read all the data into the readBuf
        size += readBytes;
        readBuf = (char *)realloc(readBuf, (sizeof(char) * size) + 1);
        if (readBuf == NULL)
        {
            perror("Error, readBuf allocation has failed close the thread");
            pthread_exit(NULL);
        }
        strcat(readBuf, temp);
        memset(temp, 0, BUF_SIZE * sizeof(char));
        if (strstr(readBuf, "\r\n\r\n"))
        {
            break;
        }
    }
    int status = checkInput(fd, readBuf);

    char *errorMsg;
    // get the status check if he one of the fail one and print him to the browser.
    if (status == BAD_REQUEST)
    {
        errorMsg = createErrorMessage(ERROR_400, ERROR_MESSGE_400);
        if (write(fd, errorMsg, strlen(errorMsg)) < 0)
        { // if the write has failed send to the INTERNAL_SERVER_ERROR
            perror("Error, responsetToServer write of BAD_REQUEST has failed");
            status = INTERNAL_SERVER_ERROR;
        }
        free(errorMsg);
    }
    else if (status == FORBIDDEN)
    {
        errorMsg = createErrorMessage(ERROR_403, ERROR_MESSGE_403);
        if (write(fd, errorMsg, strlen(errorMsg)) < 0)
        { // if the write has failed send to the INTERNAL_SERVER_ERROR
            perror("Error, responsetToServer write of FORBIDDEN has failed");
            status = INTERNAL_SERVER_ERROR;
        }
        free(errorMsg);
    }
    else if (status == NOT_FOUND)
    {
        errorMsg = createErrorMessage(ERROR_404, ERROR_MESSGE_404);
        if (write(fd, errorMsg, strlen(errorMsg)) < 0)
        { // if the write has failed send to the INTERNAL_SERVER_ERROR
            perror("Error, responsetToServer write of NOT_FOUND has failed");
            status = INTERNAL_SERVER_ERROR;
        }
        free(errorMsg);
    }
    else if (status == NOT_SUPPORTED)
    {
        errorMsg = createErrorMessage(ERROR_501, ERROR_MESSGE_501);
        if (write(fd, errorMsg, strlen(errorMsg)) < 0)
        { // if the write has failed send to the INTERNAL_SERVER_ERROR
            perror("Error, responsetToServer write of NOT_SUPPORTED has failed");
            status = INTERNAL_SERVER_ERROR;
        }
        free(errorMsg);
    }
    // if one of the status fail or the check input return INTERNAL_SERVER_ERROR
    if (status == INTERNAL_SERVER_ERROR)
    {
        errorMsg = createErrorMessage(ERROR_500, ERROR_MESSGE_500);
        if (write(fd, errorMsg, strlen(errorMsg)) < 0)
        { // if the INTERNAL_SERVER_ERROR failed then return perror free all and finish the work
            perror("Error, responsetToServer write of INTERNAL_SERVER_ERROR has failed");
        }
        free(errorMsg);
    }

    free(temp);
    free(readBuf);
    close(fd);
    return 0;
}

//  check the input we get and see if he his valid
int checkInput(int fd, char *input)
{
    if (strcmp(input, "") == 0)
    {
        return BAD_REQUEST;
    }
    char *method, *path, *protocol;
    char *inputCopy = (char *)malloc(sizeof(char) * (strlen(input) + 1));
    if (inputCopy == NULL)
    {
        perror("Error, inputCopy allocation has failed");
        return INTERNAL_SERVER_ERROR;
    }
    strncpy(inputCopy, input, strlen(input) + 1);

    method = strtok(inputCopy, " ");
    path = strtok(NULL, " ");
    protocol = strtok(NULL, "\r");
    // check the first line if 3 value are exsits
    if (method == NULL || path == NULL || protocol == NULL)
    {
        free(inputCopy);
        return BAD_REQUEST;
    }
    // check if the protocol is one of the HTTP 1.0 or 1.1
    if (strcmp("HTTP/1.0", protocol) != 0 && strcmp("HTTP/1.1", protocol) != 0)
    {
        free(inputCopy);
        return BAD_REQUEST;
    }
    // check if the method is GET
    if (strcmp(method, "GET") != 0)
    {
        free(inputCopy);
        return NOT_SUPPORTED;
    }
    strncpy(inputCopy, input, strlen(input) + 1);
    char *host = strtok(inputCopy, "\r\n");
    char *msg = (char *)malloc(sizeof(char) * (3 + strlen(host))); // create the msg: GET with the Host to send to the fd and connection close
    if (msg == NULL)
    {
        free(inputCopy);
        return INTERNAL_SERVER_ERROR;
    }
    strncpy(msg, host, strlen(host) + 1);
    while (host != NULL)
    { // search for the host in the input
        if (strstr(host, "Host: "))
        {
            break;
        }
        host = strtok(NULL, "\r\n");
    }
    if (host == NULL)
    { // if the host header does not exsit
        free(msg);
        free(inputCopy);
        return BAD_REQUEST;
    }
    host += 6;                // reamove the Host: before the host link
    host = strtok(host, ":"); // if :port exsit then remove it, else it will stay the same
    if (gethostbyname(host) == NULL)
    { // check if the host is valid
        free(msg);
        free(inputCopy);
        return NOT_FOUND;
    }

    int i = 0;
    while (filter[i] != NULL)
    { // check if the filter list contaion the host
        if (strcmp(host, filter[i]) == 0)
        {
            free(msg);
            free(inputCopy);
            return FORBIDDEN;
        }
        i++;
    }
    int port = 80; // if the port doesn't exsit then by defult it will be 80
    char *tempPort;
    if ((tempPort = strstr(host, ":")) != NULL)
    {
        tempPort += 1;         // remove the : before the port number
        port = atoi(tempPort); // change port to the number that he is
    }
    // cretate the full message and send if to connect between the proxy to server
    msg = (char *)realloc(msg, (1 + strlen("\r\nHost: ") + strlen(host) + strlen(msg) + strlen("\r\nConnection: close\r\n\r\n")) * sizeof(char));
    if (msg == NULL)
    {
        free(inputCopy);
        perror("Error, msg allocation has failed");
        return INTERNAL_SERVER_ERROR;
    }
    strcat(msg, "\r\nHost: ");
    strcat(msg, host);
    strcat(msg, "\r\nConnection: close\r\n\r\n");
    int status = sendRequest(fd, host, port, msg); // if the connetion between the proxsy and the server will fail it will return INTERNAL_SERVER_ERROR
    free(msg);
    free(inputCopy);
    return status;
}

// this function send message if some fucntion failed and release alloction memory
// statusRelease check if its time to release variables that was created later in the program
void sendErrorMessage(serverData *data, char *msg, int statusRelease, int fd, int *requests_fd)
{
    if (statusRelease > 0)
    {
        close(fd);
    }
    destroy_threadpool(data->tp);
    if (statusRelease > 1)
    {
        free(requests_fd);
    }
    free(data);
    freeFilter();
    perror(msg);
    exit(EXIT_FAILURE);
}

// print error message if one of the value invalid
void sendUsageErrorMessage()
{
    fprintf(stderr, USAGE_ERROR);
    exit(EXIT_FAILURE);
}

// this function check if the user insert valid input
void checkArgvValidtion(int argc, char *argv[])
{
    if (argc != 5)
    {
        sendUsageErrorMessage();
    }
    checkIfStringIsValidNumber(argv[1]);
    checkIfStringIsValidNumber(argv[2]);
    checkIfStringIsValidNumber(argv[3]);
    // check if the number is out of bounds
    if (atoi(argv[1]) < 1 || atoi(argv[1]) > 65535 || atoi(argv[3]) < 1)
    {
        sendUsageErrorMessage();
    }
}

// initialize all the data paramenters
void initializeDataServer(serverData **data, char *argv[])
{
    (*data) = (serverData *)malloc(sizeof(serverData));
    if ((*data) == NULL)
    {
        perror("Error, data allocation has failed");
        exit(EXIT_FAILURE);
    }
    // initialize the port, pool size and max number of request
    (*data)->port = atoi(argv[1]);
    (*data)->pool_size = atoi(argv[2]);
    (*data)->max_number_of_request = atoi(argv[3]);

    (*data)->tp = create_threadpool((*data)->pool_size);
    if ((*data)->tp == NULL)
    {
        free(data);
        sendUsageErrorMessage();
    }
}

// this function get string and check if he contain only number any char that is not 0 to 9 there will be  USAGE ERROR
void checkIfStringIsValidNumber(char *str)
{
    for (int i = 0; i < strlen(str); i++)
    {
        if (str[i] < '0' || str[i] > '9')
        {
            sendUsageErrorMessage();
        }
    }
}

// this function return one of the error message 400 403 404 500 501
char *createErrorMessage(char *Error_Num, char *Error_Message)
{
    char *msg1 = "HTTP/1.0 ";
    char *msg2 = "\r\nServer: webserver/1.0\r\nContent-Type: text/html\r\nContent-Length: ";
    char *msg3 = "\r\nConnection: close\r\n\r\n";
    int lenOfFirstPart = strlen(msg1) + strlen(Error_Num) + strlen(msg2) + 3 + strlen(msg3);
    char *msg4 = "<HTML><HEAD><TITLE>";
    char *msg5 = "</TITLE></HEAD>\r\n<BODY></H4>";
    char *msg6 = "<H4>\r\n";
    char *msg7 = "\r\n</BODY></HTML>\r\n";
    int contentLength = strlen(msg4) + strlen(Error_Num) + strlen(msg5) + strlen(Error_Num) + strlen(msg6) + strlen(Error_Message) + strlen(msg7);

    char *tempContentLength = (char *)malloc(4 * sizeof(char));
    if (tempContentLength == NULL)
    {
        perror("Error, tempContentLength allocation has failed");
        return NULL;
    }
    sprintf(tempContentLength, "%d", contentLength);

    // insert all the msg into the errorMessage
    char *errorMessage = (char *)malloc(sizeof(char) * (contentLength + lenOfFirstPart + 1));
    strncpy(errorMessage, msg1, strlen(msg1) + 1);
    strcat(errorMessage, Error_Num);
    strcat(errorMessage, msg2);
    strcat(errorMessage, tempContentLength);
    strcat(errorMessage, msg3);
    strcat(errorMessage, msg4);
    strcat(errorMessage, Error_Num);
    strcat(errorMessage, msg5);
    strcat(errorMessage, Error_Num);
    strcat(errorMessage, msg6);
    strcat(errorMessage, Error_Message);
    strcat(errorMessage, msg7);
    free(tempContentLength);
    return errorMessage;
}
// read the filter file and insert each line into the filter variable
void readFilterFile(char *filterFile)
{
    FILE *fp;
    char *newLine = NULL;
    size_t size = 0;
    fp = fopen(filterFile, "r");
    if (fp == NULL)
    { // open file location and read for the file
        fprintf(stderr, USAGE_ERROR);
        exit(EXIT_FAILURE);
    }
    int i = 0;
    while (getline(&newLine, &size, fp) != -1)
    {                                                                // get line into the next line and save it in the filter
        filter = (char **)realloc(filter, (i + 1) * sizeof(char *)); // increase filter size for each line
        if (filter == NULL)
        { // checl allocation
            perror("Error, filter allocation has failed");
            exit(EXIT_FAILURE);
        }
        filter[i] = (char *)malloc(sizeof(char) * (strlen(newLine) + 1));
        if (filter[i] == NULL)
        { // checl allocation
            perror("Error, filter[i] allocation has failed");
            exit(EXIT_FAILURE);
        }
        strncpy(filter[i], newLine, strlen(newLine) + 1);
        filter[i] = strtok(filter[i], "\r\n"); // remove \r\n from every line we get from the filter.txt file
        i++;
    }
    i++;
    filter = (char **)realloc(filter, i * sizeof(char *)); // increase filter size for each line
    if (filter == NULL)
    { // checl allocation
        perror("Error, filter allocation has failed");
        exit(EXIT_FAILURE);
    }
    filter[i - 1] = NULL; // add NULL at the end of the filter
    free(newLine);
    fclose(fp);
}

// free the global variable filter
void freeFilter()
{
    int i = 0;
    while (filter[i] != NULL)
    {
        free(filter[i]);
        i++;
    }
    free(filter);
}

// this function connect betweem the proxy to the server
int sendRequest(int fd, char *host, int port, char *msg)
{
    // create soket
    int sockfd;
    struct sockaddr_in serv_addr;
    struct hostent *server;

    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0)
    {
        perror("Error, sockfd from sendRequest has failed");
        close(sockfd);
        return INTERNAL_SERVER_ERROR;
    }

    server = gethostbyname(host);

    serv_addr.sin_family = AF_INET;
    bcopy((char *)server->h_addr_list[0], (char *)&serv_addr.sin_addr.s_addr, server->h_length);
    serv_addr.sin_port = htons(port);

    if (connect(sockfd, (const struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0)
    {
        perror("Error, connect from sendRequest has failed");
        close(sockfd);
        return INTERNAL_SERVER_ERROR;
    }

    if (write(sockfd, msg, strlen(msg)) < 0)
    {
        perror("Error, write from sendRequest has failed");
        close(sockfd);
        return INTERNAL_SERVER_ERROR;
    }

    int sentAndGetRequest = 0;
    char buffer[BUF_SIZE];
    while (1)
    {
        sentAndGetRequest = read(sockfd, buffer, BUF_SIZE - 1);
        if (sentAndGetRequest > 0)
        { // geting the data from the server
            if (write(fd, buffer, sentAndGetRequest) < 0)
            {
                perror("Error, write from sendRequest has failed");
                close(sockfd);
                return INTERNAL_SERVER_ERROR;
            }
            memset(buffer, 0, BUF_SIZE * sizeof(char));
        }
        else if (sentAndGetRequest == 0)
        { // if we read all the data then exit
            break;
        }
        else
        { // if the read has fail
            perror("Error, read from sendRequest has failed");
            close(sockfd);
            return INTERNAL_SERVER_ERROR;
        }
    }
    close(sockfd);
    return 0;
}