#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>

#define PORT 8080
#define BUFFER_SIZE 1024
#define MAX_ARGS 10

static volatile sig_atomic_t running = 1;    // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)

void setup_socket(int *sockfd);
void handle_client(int client_socket);
void execute_command(char *args[], char *output, size_t output_size);
int  validate_input(const char *command);
void tokenize(char buffer[BUFFER_SIZE], char *args[MAX_ARGS]);
void handle_builtin(char *args[], int client_socket);
int  is_builtin(char *args[]);
void free_args(char *args[]);
void sig_handler(int sig);

int main(void)
{
    int                server_fd;
    struct sockaddr_in client_addr;
    socklen_t          client_addr_len = sizeof(client_addr);

    // Move struct sigaction declaration to avoid mixed declarations and code
    struct sigaction sa = {0};
#if defined(__clang__)
    #pragma clang diagnostic push
    #pragma clang diagnostic ignored "-Wdisabled-macro-expansion"
#endif
    sa.sa_handler = sig_handler;
#if defined(__clang__)
    #pragma clang diagnostic pop
#endif
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;

    // Set up signal handler
    if(sigaction(SIGINT, &sa, NULL) == -1)
    {
        perror("sigaction");
        exit(EXIT_FAILURE);
    }

    // Set up the server socket
    setup_socket(&server_fd);

    printf("Server listening on port %d...\n", PORT);

    while(running)
    {
        int client_socket = accept(server_fd, (struct sockaddr *)&client_addr, &client_addr_len);
        if(client_socket < 0)
        {
            perror("accept failed");
            continue;
        }

        printf("Client connected\n");

        // Handle client requests
        handle_client(client_socket);

        // Close the client socket
        close(client_socket);
        printf("Client disconnected\n");
    }

    close(server_fd);
    return 0;
}

void setup_socket(int *sockfd)
{
    struct sockaddr_in server_addr;

    // Create the socket
    *sockfd = socket(AF_INET, SOCK_CLOEXEC, 0);
    if(*sockfd < 0)
    {
        perror("socket failed");
        exit(EXIT_FAILURE);
    }

    // Set up the server address structure
    server_addr.sin_family      = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port        = htons(PORT);

    // Bind the socket to the address
    if(bind(*sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
    {
        perror("bind failed");
        exit(EXIT_FAILURE);
    }

    // Listen for incoming connections
    if(listen(*sockfd, SOMAXCONN) < 0)
    {
        perror("listen failed");
        exit(EXIT_FAILURE);
    }
}

void handle_client(int client_socket)
{
    char buffer[BUFFER_SIZE];

    while(running)
    {
        char  output[BUFFER_SIZE];
        char *args[MAX_ARGS];

        // Read the command from the client
        const ssize_t bytes_read = read(client_socket, buffer, BUFFER_SIZE - 1);
        if(bytes_read <= 0)
        {
            break;    // Client disconnected or error occurred
        }

        buffer[bytes_read] = '\0';    // Null-terminate the command

        // Tokenize the command
        tokenize(buffer, args);

        if(is_builtin(args))
        {
            handle_builtin(args, client_socket);
            free_args(args);
            continue;
        }

        // Execute the command
        execute_command(args, output, BUFFER_SIZE);

        // Send the output back to the client
        write(client_socket, output, strlen(output));

        // Free allocated memory
        free_args(args);
    }
}

void tokenize(char buffer[BUFFER_SIZE], char *args[MAX_ARGS])
{
    int         token_counter = 0;
    char       *saveptr;
    const char *token = strtok_r(buffer, " \n", &saveptr);    // Handle newline characters
    while(token != NULL && token_counter < MAX_ARGS - 1)
    {
        args[token_counter] = strdup(token);
        token_counter++;
        token = strtok_r(NULL, " \n", &saveptr);
    }
    args[token_counter] = NULL;    // Ensure NULL termination

    // Handle empty command case
    if(token_counter == 0)
    {
        args[0] = NULL;
    }
}

void execute_command(char *args[], char *output, size_t output_size)
{
    int   pipefd[2];
    pid_t pid;
    int   status;

    if(args[0] == NULL)
    {
        snprintf(output, output_size, "Error: No command provided.\n");
        return;
    }

    // Create a pipe
    if(pipe(pipefd) == -1)    // NOLINT(android-cloexec-pipe)
    {
        snprintf(output, output_size, "Error: Failed to create pipe.\n");
        return;
    }

    fcntl(pipefd[0], F_SETFD, FD_CLOEXEC);
    fcntl(pipefd[1], F_SETFD, FD_CLOEXEC);

    pid = fork();
    if(pid < 0)
    {
        snprintf(output, output_size, "Error: Failed to fork.\n");
        return;
    }

    if(pid == 0)
    {
        // Child process
        close(pipefd[0]);    // Close read end of the pipe

        dup2(pipefd[1], STDOUT_FILENO);
        dup2(pipefd[1], STDERR_FILENO);
        close(pipefd[1]);    // Close write end after duplication

        execvp(args[0], args);

        // If execvp fails, print error and exit
        perror("execvp");
        exit(EXIT_FAILURE);
    }
    else
    {
        ssize_t bytes_read;
        size_t  total_bytes = 0;
        // Parent process
        close(pipefd[1]);    // Close write end

        while((bytes_read = read(pipefd[0], output + total_bytes, output_size - total_bytes - 1)) > 0)
        {
            total_bytes += (unsigned long)bytes_read;
            if(total_bytes >= output_size - 1)
            {
                break;    // Prevent overflow
            }
        }

        output[total_bytes] = '\0';    // Ensure null termination

        close(pipefd[0]);    // Close read end

        // Wait for the child process to finish
        waitpid(pid, &status, 0);

        // If child process failed, set error message
        if(WIFEXITED(status) && WEXITSTATUS(status) != 0)
        {
            snprintf(output, output_size, "Error: Command failed to execute.\n");
        }
    }
}

// cppcheck-suppress constParameter
int is_builtin(char *args[])
{
    if(args[0] == NULL)
    {
        return 0;
    }

    if(strcmp(args[0], "cd") == 0 || strcmp(args[0], "pwd") == 0 || strcmp(args[0], "echo") == 0 || strcmp(args[0], "exit") == 0)
    {
        return 1;
    }

    return 0;
}

// cppcheck-suppress constParameter
void handle_builtin(char *args[], int client_socket)
{
    char output[BUFFER_SIZE] = {0};    // Ensure it's initialized to an empty string

    if(strcmp(args[0], "cd") == 0)
    {
        if(args[1] == NULL)
        {
            snprintf(output, BUFFER_SIZE, "cd: No specified directory\n");
        }
        else if(chdir(args[1]) == -1)
        {
            snprintf(output, BUFFER_SIZE, "cd: No such directory\n");
        }
        else
        {
            snprintf(output, BUFFER_SIZE, "Changed directory to %s\n", args[1]);
        }
    }
    else if(strcmp(args[0], "pwd") == 0)
    {
        if(getcwd(output, BUFFER_SIZE) == NULL)
        {
            snprintf(output, BUFFER_SIZE, "pwd: Error getting current directory\n");
        }
    }
    else if(strcmp(args[0], "echo") == 0)
    {
        size_t available_space = sizeof(output) - strlen(output) - 1;
        output[0]              = '\0';
        for(int i = 1; args[i] != NULL; i++)
        {
            strncat(output, args[i], available_space);
            available_space = sizeof(output) - strlen(output) - 1;
            strncat(output, " ", available_space);
        }
        available_space = sizeof(output) - strlen(output) - 1;
        strncat(output, "\n", available_space);
    }
    else if(strcmp(args[0], "exit") == 0)
    {
        snprintf(output, BUFFER_SIZE, "Exiting shell...\n");
        running = 0;
    }

    write(client_socket, output, strlen(output));
}

void free_args(char *args[])
{
    for(int i = 0; args[i] != NULL; i++)
    {
        free(args[i]);
    }
}

void sig_handler(int sig)
{
    const char *message = "\nSIGINT received. Server shutting down.\n";
    (void)sig;
    write(1, message, strlen(message));
    running = 0;
}
