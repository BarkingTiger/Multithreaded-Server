#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <pthread.h>
#include <semaphore.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <unistd.h>
#include "bind.h"
#include "queue.h"

#define BAD "HTTP/1.1 400 Bad Request\r\nContent-Length: 12\r\n\r\nBad Request\n"
#define NOT_IMPLEMENTED                                                                            \
    "HTTP/1.1 501 Not Implemented\r\nContent-Length: 16\r\n\r\nNot Implemented\n"
#define FORBIDDEN "HTTP/1.1 403 Forbidden\r\nContent-Length: 10\r\n\r\nForbidden\n"
#define NOT_FOUND "HTTP/1.1 404 Not Found\r\nContent-Length: 10\r\n\r\nNot Found\n"
#define OK        "HTTP/1.1 200 OK\r\nContent-Length: 3\r\n\r\nOK\n"
#define CREATED   "HTTP/1.1 201 Created\r\nContent-Length: 8\r\n\r\nCreated\n"
#define INTERNAL                                                                                   \
    "HTTP/1.1 500 Internal Server Error\r\nContent-Length: 22\r\n\r\nInternal Server Error\n"
#define OPTIONS "t:l:"

volatile int done = 0;

void sigterm_handler() {
    done = 1;
}

//HEAD request
void head(int socket, char *uri, int log, int id) {

    char response[100];

    //move pointer forward once
    uri += 1;

    int fd;

    //if file is a directory
    if ((fd = open(uri, O_DIRECTORY)) > 0) {
        write(socket, FORBIDDEN, strlen(FORBIDDEN));
        return;
    }

    fd = open(uri, O_RDONLY);

    //if file can't be accessed
    if (fd < 0 && errno == EACCES) {
        write(socket, FORBIDDEN, strlen(FORBIDDEN));
        return;
    }

    //if file doesn't exist
    if (fd < 0 && errno == ENOENT) {
        write(socket, NOT_FOUND, strlen(NOT_FOUND));
        snprintf(response, sizeof(response), "HEAD,/%s,404,%d\n", uri, id);
	flock(log, LOCK_EX);
        write(log, response, strlen(response));
	flock(log, LOCK_UN);
        return;
    }

    if (fd < 0) {
        write(socket, BAD, strlen(BAD));
        return;
    }

    struct stat path;
    fstat(fd, &path);

    uint64_t size = path.st_size;

    //write response
    snprintf(response, sizeof(response), "HTTP/1.1 200 OK\r\nContent-Length: %" PRIu64 "\r\n\r\n",
        size - 1);
    write(socket, response, strlen(response));

    close(fd);

    bzero(response, strlen(response));
    snprintf(response, sizeof(response), "HEAD,/%s,200,%d\n", uri, id);
    flock(log, LOCK_EX);
    write(log, response, strlen(response));
    flock(log, LOCK_UN);

    return;
}

//PUT request
void put(int socket, char *uri, int bytes_to_write, int log, int id) {

    //move point forward once
    uri += 1;

    char response[100];
    int created = 0;
    char buffer[2048];
    int fd;

    //if file is a directory
    if ((fd = open(uri, O_DIRECTORY)) > 0) {
        write(socket, FORBIDDEN, strlen(FORBIDDEN));
        return;
    }

    //open file
    fd = open(uri, O_WRONLY | O_TRUNC);

    //if file can't be accessed
    if (errno == EACCES) {
        write(socket, FORBIDDEN, strlen(FORBIDDEN));
        return;
    }

    //creates a new file if one doesn't exist
    if (fd < 0) {
        fd = open(uri, O_RDWR | O_CREAT | O_TRUNC, 0666);
        created = 1;
    }

    //if something happens
    if (fd < 0) {
        write(socket, BAD, strlen(BAD));
        return;
    }

    flock(socket, LOCK_EX);
    //write amount of bytes
    while (bytes_to_write > 0) {

        int read = recv(socket, buffer, 2048, 0);

        if (read <= 0) {
            return;
        }

        if (read > bytes_to_write) {
            read = bytes_to_write;
        }
        write(fd, buffer, read);
        bytes_to_write -= read;
    }
    flock(socket, LOCK_UN);

    //write to socket
    if (created) {
        write(socket, CREATED, strlen(CREATED));
        snprintf(response, sizeof(response), "PUT,/%s,201,%d\n", uri, id);
	flock(log, LOCK_EX);
        write(log, response, strlen(response));
	flock(log, LOCK_UN);
    } else {
        write(socket, OK, strlen(OK));
        snprintf(response, sizeof(response), "PUT,/%s,200,%d\n", uri, id);
	flock(log, LOCK_EX);
        write(log, response, strlen(response));
	flock(log, LOCK_UN);
    }

    close(fd);

    return;
}

//GET request
void get(int socket, char *uri, int log, int id) {

    char response[100];
    char buffer[1024];
    int bytes = 0;
    int fd;

    //move pointer forward once
    uri += 1;

    if ((fd = open(uri, O_DIRECTORY)) > 0) {
        write(socket, FORBIDDEN, strlen(FORBIDDEN));
        return;
    }

    //open file
    fd = open(uri, O_RDWR);

    //if file can't be accessed
    if (fd < 0 && errno == EACCES) {
        write(socket, FORBIDDEN, strlen(FORBIDDEN));
        return;
    }

    //if file doesn't exist
    if (fd < 0 && errno == ENOENT) {
        write(socket, NOT_FOUND, strlen(NOT_FOUND));
        snprintf(response, sizeof(response), "GET,/%s,404,%d\n", uri, id);
	flock(log, LOCK_EX);
        write(log, response, strlen(response));
	flock(log, LOCK_UN);
        return;
    }

    if (fd < 0) {
        printf("FAILED\n");
        return;
    }

    struct stat path;
    fstat(fd, &path);

    uint64_t size = path.st_size;

    //write response
    snprintf(
        response, sizeof(response), "HTTP/1.1 200 OK\r\nContent-Length: %" PRIu64 "\r\n\r\n", size);
    write(socket, response, strlen(response));

    flock(fd, LOCK_SH);
    //read the file and write to client
    while ((bytes = read(fd, buffer, 1024)) > 0) {
        write(socket, buffer, bytes);
    }
    flock(fd, LOCK_UN);

    //close file
    close(fd);

    bzero(response, strlen(response));
    snprintf(response, sizeof(response), "GET,/%s,200,%d\n", uri, id);
    flock(log, LOCK_EX);
    write(log, response, strlen(response));
    flock(log, LOCK_UN);

    return;
}

//reads bytes until '\r\n\r\n' is found
void recv_all(int socket, char *buffer_ptr) {

    int bytes = 4096;
    int bytes_to_recv = bytes;

    while (bytes_to_recv > 0) {
        int ret = recv(socket, buffer_ptr, bytes_to_recv, 0);
        if (ret <= 0) {
            return;
        }
        if (strstr((buffer_ptr - (bytes - bytes_to_recv)), "\r\n\r\n")) {
            return;
        }
        bytes_to_recv -= ret;
        buffer_ptr += ret;
    }

    return;
}

void parse(int connection, int log) {

    char buffer[4096];
    char request[40];
    char method[9];
    char uri[20];
    char http[9];

    memset(buffer, 0, 4096);
    memset(request, 0, 40);
    memset(method, 0, 9);
    memset(uri, 0, 20);
    memset(http, 0, 9);
    long int bytes = 0;
    int id = 0;

    recv_all(connection, buffer);

    //if all 2048 bytes are read and '\r\n\r\n' isn't in it
    if ((strstr(buffer, "\r\n\r\n")) == NULL) {
        write(connection, BAD, strlen(BAD));
        close(connection);
        return;
    }

    if (strlen(buffer) > 2052) {
        write(connection, BAD, strlen(BAD));
        close(connection);
        return;
    }

    int space = 0;

    //scan for request line
    for (unsigned long i = 0; i < strlen(buffer) - 1; i += 1) {
        if (buffer[i] == ' ') {
            space += 1;
        }
        if (buffer[i] == '\r' && buffer[i + 1] == '\n') {
            strncpy(request, buffer, i + 1);
            break;
        }
    }

    //if there are more than 2 spaces in the request line
    if (space != 2) {
        write(connection, BAD, strlen(BAD));
        close(connection);
        return;
    }

    //checking if request actually has stuff
    if (strlen(request) < 5) {
        write(connection, BAD, strlen(BAD));
        close(connection);
        return;
    }

    const char *end_of_method = strchr(request, ' ');
    const char *start_of_uri = end_of_method + 1;
    const char *end_of_uri = strchr(start_of_uri, ' ');
    const char *start_of_http = end_of_uri + 1;
    const char *end_of_http = strchr(request, '\r');

    strncpy(method, request, end_of_method - request);
    strncpy(uri, start_of_uri, end_of_uri - start_of_uri);
    strncpy(http, start_of_http, end_of_http - start_of_http);

    method[strlen(method)] = 0;
    uri[strlen(uri)] = 0;
    http[strlen(http)] = 0;

    int format = 0;

    //check if method legnth is greater than 8
    if (strlen(method) > 8) {
        write(connection, BAD, strlen(BAD));
        close(connection);
        return;
    }

    //checks if method is only in the alphabet
    for (unsigned long i = 0; method[i] != 0; i += 1) {
        if (!isalpha(method[i])) {
            format = 1;
            break;
        }
    }

    if (format) {
        write(connection, BAD, strlen(BAD));
        close(connection);
        return;
    }

    //checks if the uri is greater than 19
    if (strlen(uri) > 19) {
        write(connection, BAD, strlen(BAD));
        close(connection);
        return;
    }

    //check if uri starts with "/"
    if (uri[0] != '/') {
        write(connection, BAD, strlen(BAD));
        close(connection);
        return;
    }

    //check if uri doesn't contain letters, number, ".", or "-"
    for (unsigned long i = 1; i < strlen(uri); i += 1) {
        if (!isalnum(uri[i]) && uri[i] != '.' && uri[i] != '-') {
            format = 1;
            break;
        }
    }

    //if uri is wrong
    if (format) {
        write(connection, BAD, strlen(BAD));
        close(connection);
        return;
    }

    //checks for "HTTP/1.1"
    if (strcmp(http, "HTTP/1.1") != 0) {
        write(connection, BAD, strlen(BAD));
        close(connection);
        return;
    }

    //store header fields
    char *end = strstr(buffer, "\r\n") + 2;

    //parse header fields
    char *start = strstr(buffer, "\r\n") + 2;

    while ((end = strstr(end, "\r\n")) != NULL) {

        //if '\r\n\r\n' is reached
        if (strlen(end) == 2) {
            break;
        }

        char temp[end - start];
        strncpy(temp, start, end - start);
        temp[sizeof(temp)] = 0;

        char *split = strchr(temp, ':');

        //if header field doesnt't follow format
        if (split == NULL) {
            format = 1;
            break;
        }

        if (split[1] != ' ') {
            format = 1;
            break;
        }

        char key[20];
        bzero(key, 20);
        strncpy(key, temp, split - temp);
        key[strlen(key)] = '\0';

        if (strchr(key, ' ') != NULL) {
            format = 1;
            break;
        }

        //check for content length
        if (strcmp(key, "Content-Length") == 0) {
            split += 1;
            bytes = strtoul(split, NULL, 10);
            //break;
        }

        //check for request id
        if (strcmp(key, "Request-Id") == 0) {
            split += 1;
            id = atoi(split);
            //break;
        }

        //move to next header field
        start = end + 2;
        end += 2;
    }

    //if one of the header fields is wrong
    if (format) {
        write(connection, BAD, strlen(BAD));
        close(connection);
        return;
    }

    //check for positive number
    if (bytes < 0) {
        write(connection, BAD, strlen(BAD));
        close(connection);
        return;
    }

    //check what method to use
    if (strcmp(method, "GET") == 0) {
        get(connection, uri, log, id);
    } else if (strcmp(method, "PUT") == 0) {
        put(connection, uri, bytes, log, id);
    } else if (strcmp(method, "HEAD") == 0) {
        head(connection, uri, log, id);
    } else {
        write(connection, NOT_IMPLEMENTED, strlen(NOT_IMPLEMENTED));
    }
    close(connection);
    return;
}

queue_t *q;
int fd;

void *startThread() {
    void *r;
    while (1) {
        queue_pop(q, &r);
        parse((int) r, fd);
    }
}

int main(int argc, char **argv) {

    int socket, connection, opt, threads;

    //create socket
    if ((socket = create_listen_socket(strtol(argv[argc - 1], NULL, 10))) < 0) {
        err(1, "%s", "bind");
    }

    opt = 0;
    fd = 2;
    threads = 4;

    //command line options
    while ((opt = getopt(argc, argv, OPTIONS)) != -1) {
        switch (opt) {
        case 't': threads = atoi(optarg); break;
        case 'l': fd = open(optarg, O_RDWR | O_CREAT | O_TRUNC, 0666); break;
        default: break;
        }
    }

    //sigterm handler
    signal(SIGTERM, sigterm_handler);

    //thread pool
    pthread_t tp[threads];

    //initialize queue
    q = queue_new(threads);

    //create threads
    for (int i = 0; i < threads; i += 1) {
        if (pthread_create(&tp[i], NULL, &startThread, NULL) != 0) {
            perror("error making threads");
        }
    }

    //run loop until SIGTERM
    while (!done) {

        connection = accept(socket, NULL, NULL);
        if (connection < 0) {
            continue;
        }

        queue_push(q, (void *) connection);

    }

    //cleanup
    queue_delete(&q);
    close(fd);

    return 0;
}
