#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define BUFFER_SIZE 1024
#define BASE 10

void setup_socket(int *sockfd, struct sockaddr_in *server_addr, const char *ip, uint16_t port);

int main(const int argc, const char *argv[])
{
    int                sockfd;
    const char        *server_ip = argv[1];
    char              *endptr;
    uint16_t           port;
    struct sockaddr_in server_addr;
    char               buffer[BUFFER_SIZE];
    long               temp_port;

    if(argc != 3)
    {
        fprintf(stderr, "Usage: %s <server_ip> <port>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    temp_port = strtol(argv[2], &endptr, BASE);
    if(*endptr != '\0' || temp_port <= 0 || temp_port > UINT16_MAX)
    {
        fprintf(stderr, "Invalid port number.\n");
        exit(EXIT_FAILURE);
    }
    port = (uint16_t)temp_port;

    setup_socket(&sockfd, &server_addr, server_ip, port);

    // Connect to the server
    if(connect(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
    {
        perror("connect failed");
        exit(EXIT_FAILURE);
    }

    printf("Connected to server %s:%d. Type 'exit' to quit.\n", server_ip, port);

    while(1)
    {
        printf("> ");
        fgets(buffer, BUFFER_SIZE, stdin);

        // Send the command to the server
        write(sockfd, buffer, strlen(buffer));

        // Check for exit command
        if(strcmp(buffer, "exit\n") == 0)
        {
            break;
        }

        // Read the server's response
        memset(buffer, 0, BUFFER_SIZE);
        read(sockfd, buffer, BUFFER_SIZE - 1);
        printf("%s", buffer);
    }

    // Close the socket
    close(sockfd);
    printf("Disconnected from server.\n");

    return 0;
}

void setup_socket(int *sockfd, struct sockaddr_in *server_addr, const char *ip, uint16_t port)
{
    // Create the socket
    *sockfd = socket(AF_INET, SOCK_STREAM, 0);  // NOLINT(android-cloexec-socket)
    if(*sockfd < 0)
    {
        perror("socket failed");
        exit(EXIT_FAILURE);
    }

    // Set up the server address structure
    server_addr->sin_family = AF_INET;
    server_addr->sin_port   = htons(port);
    if(inet_pton(AF_INET, ip, &server_addr->sin_addr) <= 0)
    {
        perror("inet_pton failed");
        exit(EXIT_FAILURE);
    }
}
