#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/wait.h>
#include <signal.h>
#include <fcntl.h>

// Define constants
#define MAX_COMMAND_LENGTH 2048
#define MAX_ARGUMENTS 512
#define MAX_BACKGROUND_PROCESSES 50

int isBackgroundAllowed = 1;
int isForegroundOnlyMode = 0;
pid_t smallsh_pid; // Variable to store the PID of the smallsh process

pid_t background_processes[MAX_BACKGROUND_PROCESSES];
int num_background_processes = 0;


/*
* Function to handle SIGTSTP signal
* Toggles foreground-only mode
* If in foreground-only mode, & is ignored
*/
void handle_SIGTSTP(int signum) {
    if (isForegroundOnlyMode) {
        // Exit foreground-only mode
        isForegroundOnlyMode = 0;
        // Print informative message
        char* message = "\nExiting foreground-only mode\n";
        write(STDOUT_FILENO, message, strlen(message)); // Corrected length of message
        fflush(stdout);
    } else {
        // Enter foreground-only mode
        isForegroundOnlyMode = 1;
        // Print informative message
        char* message = "\nEntering foreground-only mode (& is now ignored)\n";
        write(STDOUT_FILENO, message, strlen(message)); // Corrected length of message
        fflush(stdout);
    }
}

/*
* Function to handle SIGINT signal
* Sends SIGINT to the smallsh process
*/
void handle_SIGINT(int signum) {
    printf("\nTerminated by signal %d\n", signum);
    fflush(stdout);
    // Send SIGINT to the smallsh process
    kill(smallsh_pid, SIGINT);
}

/*
* Function to add a background process PID to the array
*/
void add_background_process(pid_t pid) {
    // Check if maximum number of background processes has been reached
    if (num_background_processes < MAX_BACKGROUND_PROCESSES) {
        background_processes[num_background_processes++] = pid;
    } else {
        fprintf(stderr, "Maximum number of background processes reached.\n");
    }
}

/*
* Function to clean up terminated background processes
*/
void cleanup_background_processes() {
    // Check for terminated background processes
    for (int i = 0; i < num_background_processes; ++i) {
        int status;
        pid_t result = waitpid(background_processes[i], &status, WNOHANG);
        if (result > 0) {
            // Background process has terminated
            printf("background pid %d is done: ", background_processes[i]);
            fflush(stdout);
            if (WIFEXITED(status)) {
                printf("exit value %d\n", WEXITSTATUS(status));
                fflush(stdout);
            } else if (WIFSIGNALED(status)) {
                printf("terminated by signal %d\n", WTERMSIG(status));
                fflush(stdout);
            }
            // Remove the terminated process from the array
            for (int j = i; j < num_background_processes - 1; ++j) {
                background_processes[j] = background_processes[j + 1];
            }
            num_background_processes--;
        }
    }
}

/*
* Function to execute a command
* Handles input and output redirection
* Handles background processes
*/
void execute_command(char* args[], int is_background) {
    // Fork a new process
    pid_t pid = fork();

    // Replace occurrences of $$ with the smallsh PID
    for (int i = 0; args[i] != NULL; i++) {
        if (strcmp(args[i], "$$") == 0) {
            char pid_str[20]; // Assuming max PID length is 20 characters
            snprintf(pid_str, sizeof(pid_str), "%d", smallsh_pid);
            args[i] = pid_str;
        }
    }

    if (pid < 0) {
        perror("fork");
        exit(1);
    } else if (pid == 0) {
        // Child process
        // Set up signal handling for child
        signal(SIGINT, SIG_DFL);

        // Handle input and output redirection if necessary
        int in_fd = STDIN_FILENO;
        int out_fd = STDOUT_FILENO;
        for (int i = 0; args[i] != NULL; ++i) {
            if (strcmp(args[i], "<") == 0) {
                in_fd = open(args[i + 1], O_RDONLY);
                if (in_fd == -1) {
                    perror("open");
                    exit(1);
                }
                dup2(in_fd, STDIN_FILENO);
                close(in_fd);
                args[i] = NULL; // Remove "<" from args
            } else if (strcmp(args[i], ">") == 0) {
                out_fd = open(args[i + 1], O_WRONLY | O_CREAT | O_TRUNC, 0644);
                if (out_fd == -1) {
                    perror("open");
                    exit(1);
                }
                dup2(out_fd, STDOUT_FILENO);
                close(out_fd);
                args[i] = NULL; // Remove ">" from args
            }
        }

        // Execute the command
        execvp(args[0], args);
        // If execvp returns, it must have failed
        perror("execvp");
        exit(1);
    } else {
        // Parent process
        if (!is_background) {
            // Wait for foreground process to complete
            int status;
            waitpid(pid, &status, 0);
            // Check if foreground process terminated by signal
            if (WIFSIGNALED(status)) {
                printf("terminated by signal %d\n", WTERMSIG(status));
                fflush(stdout);
            }
        } else {
            // Background process
            printf("background pid is %d\n", pid);
            fflush(stdout);
            // Add background process PID to array
            add_background_process(pid);
        }
        // Clean up terminated background processes before returning control to user
        cleanup_background_processes();
    }
}

/*
* Main function
* Sets up signal handling for SIGTSTP and SIGINT
* Reads commands from user and executes them
* Handles:
*        built-in commands exit, cd, and status
*        foreground-only mode
*        background processes
*
* Compile the program as follows:
*                           gcc -o smallsh main.c -std=gnu99
*/
int main() {

    struct sigaction SIGTSTP_action;
    struct sigaction SIGINT_action;

    SIGTSTP_action.sa_handler = handle_SIGTSTP;
    sigemptyset(&SIGTSTP_action.sa_mask);
    SIGTSTP_action.sa_flags = SA_RESTART;
    sigaction(SIGTSTP, &SIGTSTP_action, NULL);

    SIGINT_action.sa_handler = handle_SIGINT;
    sigemptyset(&SIGINT_action.sa_mask);
    SIGINT_action.sa_flags = SA_RESTART;
    sigaction(SIGINT, &SIGINT_action, NULL);

    char command[MAX_COMMAND_LENGTH];
    char* args[MAX_ARGUMENTS];

    // Get the PID of the smallsh process
    smallsh_pid = getpid();

    // Set the environment variable
    char smallsh_pid_str[10]; // Assuming max PID length is 10 characters
    snprintf(smallsh_pid_str, sizeof(smallsh_pid_str), "%d", smallsh_pid);
    setenv("SMALLSH_PID", smallsh_pid_str, 1);

    while (1) {
        // Display prompt
        printf(": ");
        fflush(stdout);

        // Read command from user
        if (fgets(command, sizeof(command), stdin) == NULL) {
            clearerr(stdin);
            continue;
        }

        // Tokenize command
        char* token = strtok(command, " \n");
        int arg_count = 0;
        int is_background = 0;
        while (token != NULL) {
            // Check for background process indicator
            if (strcmp(token, "&") == 0) {
                is_background = 1;
                break;
            }
            // Add argument to args array
            args[arg_count++] = token;
            token = strtok(NULL, " \n");
        }
        args[arg_count] = NULL;

        // Check for empty command or comment
        if (arg_count == 0 || args[0][0] == '#') {
            continue;
        }

        // Check for built-in commands
        if (strcmp(args[0], "exit") == 0) {
            exit(0);
        } else if (strcmp(args[0], "cd") == 0) {
            if (arg_count == 1) {
                chdir(getenv("HOME"));
            } else {
                chdir(args[1]);
            }
        } else if (strcmp(args[0], "status") == 0) {
            // Print status of last foreground process
            // (Not implemented in this basic version)
            printf("status command\n");
            fflush(stdout);
        } else {
            // Execute non-built-in commands
            execute_command(args, is_background);
        }
    }

    return 0;
}