#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define SERVER_IP "127.0.0.1"
#define BUFFER_SIZE 1024
#define PORT 8080

void setup_socket(int *sockfd, struct sockaddr_in *server_addr);

int main(void)
{
    int                sockfd;
    struct sockaddr_in server_addr;
    char               buffer[BUFFER_SIZE];

    setup_socket(&sockfd, &server_addr);

    // Connect to the server
    if(connect(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
    {
        perror("connect failed");
        exit(EXIT_FAILURE);
    }

    printf("Connected to server. Type 'exit' to quit.\n");

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

void setup_socket(int *sockfd, struct sockaddr_in *server_addr)
{
    // Create the socket
    *sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if(*sockfd < 0)
    {
        perror("socket failed");
        exit(EXIT_FAILURE);
    }

    // Set up the server address structure
    server_addr->sin_family = AF_INET;
    server_addr->sin_port   = htons(PORT);
    if(inet_pton(AF_INET, SERVER_IP, &server_addr->sin_addr) <= 0)
    {
        perror("inet_pton failed");
        exit(EXIT_FAILURE);
    }
}
