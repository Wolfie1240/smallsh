/*************************************************
* Leo Wolf
* smallsh program
* Operating Systems I
* Nov 14 2024
* Simple C shell to execute bash commands
****************************************************/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <signal.h>

#define MAX_INPUT_SIZE 2048
#define MAX_ARG_SIZE 512

void handleCommand(char *input);
void handleExit();
void handleCd(char **args);
void handleStatus();
void parseInput(char *input, char **args, char **inputFile, char **outputFile, int *background);
void executeCommand(char **args, char *inputFile, char *outputFile, int background);
void handle_sigtstp(int signo);
void handle_sigchld(int signo);
char *expandShellID(char* token);

// Global variables to store the last exit status or last signal
int lastExitStatus = 0;
int lastTerminalSignal = 0;
// Global to store whether or not background process is allowed
int foreground_only_mode = 0; 


/*************************************************
* Main Function
* Initializes signal handling, clears the screen, 
* and enters an infinite loop to handle user input.
****************************************************/
int main() {
    struct sigaction sa_sigchld = {0}, sa_sigtstp = {0}, sa_sigint = {0};

    sa_sigchld.sa_handler = handle_sigchld;
    sa_sigchld.sa_flags = SA_RESTART | SA_NOCLDSTOP;
    sigaction(SIGCHLD, &sa_sigchld, NULL);

    sa_sigtstp.sa_handler = handle_sigtstp;
    sa_sigtstp.sa_flags = SA_RESTART;   
    sigaction(SIGTSTP, &sa_sigtstp, NULL);

    sa_sigint.sa_handler = SIG_IGN;
    sigaction(SIGINT, &sa_sigint, NULL);

    system("clear");  // clear screen to provide blank slate for smallsh
    printf("smallsh$\n"); 
    fflush(stdout);

    char input[MAX_INPUT_SIZE];

    while (1) {
        // Infinite loop to display colon prompt and process input
        
        printf(": ");
        fflush(stdout);

        // Read input
        if (fgets(input, MAX_INPUT_SIZE, stdin) == NULL) {
            clearerr(stdin);  // Clear error if any
            continue;
        }

        // Remove trailing newline
        input[strcspn(input, "\n")] = 0;

        // Ignore blank lines and comments
        if (input[0] == '\0' || input[0] == '#') {
            continue;
        }

        // Handle command
        handleCommand(input);
    }
    return 0;
}



/*************************************************
* handleCommand Function
* Determines the command type (built-in or external)
* and executes the corresponding logic.
****************************************************/
void handleCommand(char *input) {
    // Built-in commands
    char *args[MAX_ARG_SIZE];
    char *inputFile = NULL, *outputFile = NULL;
    int background = 0;

    parseInput(input, args, &inputFile, &outputFile, &background);

    if (strcmp(args[0], "exit") == 0) handleExit();
    else if (strcmp(args[0], "cd") == 0) handleCd(args);
    else if (strcmp(args[0], "status") == 0) handleStatus();
    else executeCommand(args, inputFile, outputFile, background);
}

/*************************************************
* parseInput Function
* Splits the input into command arguments, 
* input/output files, and background flag.
****************************************************/
void parseInput(char *input, char **args, char **inputFile, char **outputFile, int *background) {
    char *token;
    int i = 0;

    // Tokenize input
    token = strtok(input, " ");
    while (token != NULL) {
        if (strcmp(token, "<") == 0) *inputFile = strtok(NULL, " ");
        else if (strcmp(token, ">") == 0) *outputFile = strtok(NULL, " ");  // Output redirection
        else if (strcmp(token, "&") == 0) {  // Background execution
            if (!foreground_only_mode) {
                *background = 1;
            }
        }
        else if (strstr(token, "$$") != NULL) args[i++] = expandShellID(token);     
        else args[i++] = token;  // Must be an argument / command
        
        token = strtok(NULL, " ");
    }
    args[i] = NULL;  // for execvp
}


/*************************************************
* expandShellID Function
* Replaces "$$" in a string with the process ID.
****************************************************/
char* expandShellID(char *token) {
    char SS_PID[7];
    snprintf(SS_PID, sizeof(SS_PID), "%d", getpid());

    // dollarCount is number of times there is an instance of $$ to be replaced.
    // Use ptr and strstr methods to iterate through token
    int dollarCount = 0;
    char *ptr = strstr(token, "$$");
    while (ptr) {
        dollarCount++;
        ptr = strstr(ptr + 2, "$$");
    }

    // Now given amount of $$, create new string to hold the PID version
    size_t newSize = strlen(token) + ((strlen(SS_PID) - 2) * dollarCount) + 1;
    char *expandedArg = malloc(newSize);

    char *writePtr = expandedArg;
    ptr = token;

    // Similar to dollarCount, while there exists an instance of $$ within the string,
    // Copy any non $$ chars, replace $$ with shell process id.
    while ((ptr = strstr(ptr, "$$"))) {
        size_t segmentLength = ptr - token;  // Length of the segment before "$$"
        memcpy(writePtr, token, segmentLength);
        writePtr += segmentLength;

        strcpy(writePtr, SS_PID);  // Append PID
        writePtr += strlen(SS_PID);
        
        ptr += 2;  // Skip "$$"
        token = ptr;  // Move the start pointer
    }

    return expandedArg; 
}


/*************************************************
* executeCommand Function
* Forks a new process to execute external commands.
* Handles input/output redirection and background execution.
****************************************************/
void executeCommand(char **args, char *inputFile, char *outputFile, int background) {
    pid_t spawnPid = fork();
    if (spawnPid == -1) {
        perror("fork failed");
        exit(1);
    } else if (spawnPid == 0) { // This is the child
        signal(SIGTSTP, SIG_IGN); // ignore ctrl z (we have custom handler)
        signal(SIGINT, SIG_DFL); // default ctrl c behavior

        // Create / open files, make sure they have the correct permissions and open successfully.
        if (inputFile) {
            int fd = open(inputFile, O_RDONLY);
            if (fd == -1) { perror("Input redirection failed"); exit(1); }
            dup2(fd, 0); close(fd);
        }
        if (outputFile) {
            int fd = open(outputFile, O_WRONLY | O_CREAT | O_TRUNC, 0644);
            if (fd == -1) { perror("Output redirection failed"); exit(1); }
            dup2(fd, 1); close(fd);
        }
        execvp(args[0], args); // Use execvp to execute all non-built in bash commands
        perror("Command execution failed"); // only runs on execvp failure
        exit(1);
    } else if (background && !foreground_only_mode) { // Parent process that is running in background
        printf("Background process ID: %d\n", spawnPid); // Process is already running, child signal will print message when process finishes
        fflush(stdout);
    } else { // Parent process, run in foreground. Must wait for child to finish.
        int status;
        waitpid(spawnPid, &status, 0);
        if (WIFSIGNALED(status)) {
            lastTerminalSignal = WTERMSIG(status);
            lastExitStatus = -1;
        }
        else lastExitStatus = WEXITSTATUS(status);
    }
}


/*************************************************
* handleExit Function
* Terminates all processes and exits the shell.
****************************************************/
void handleExit() {
    kill(0, SIGTERM);
    exit(0);
}


/*************************************************
* handleCd Function
* Changes the current working directory.
****************************************************/
void handleCd(char **args) {
    if (!args[1]) chdir(getenv("HOME"));  // No path specified, go to home
    else if (chdir(args[1]) != 0) perror("cd failed");
}
    

/*************************************************
* handleStatus Function
* Prints the exit status or termination signal 
* of the last foreground process.
****************************************************/
void handleStatus() {
    if (lastExitStatus != -1) printf("exit value: %d\n", lastExitStatus); // Process terminated with an exit status      
    else printf("terminated by signal %d\n", lastTerminalSignal);   // Program received a terminating signal
    fflush(stdout);
}


/*************************************************
* handle_sigtstp Function
* Toggles foreground-only mode when SIGTSTP (ctrl z) is received.
****************************************************/
void handle_sigtstp(int signo) {
    if (foreground_only_mode) {
        write(STDOUT_FILENO, "\nExiting foreground-only mode\n", 30);
        foreground_only_mode = 0;
    } else {
        write(STDOUT_FILENO, "\nEntering foreground-only mode (& is now ignored)\n", 51);
        foreground_only_mode = 1;
    }
}


/*************************************************
* handle_sigchld Function
* Cleans up background processes when they terminate.
* Prints background process pid to terminal when finsihed
****************************************************/
void handle_sigchld(int signo) {
    int status;
    
    pid_t child_pid;

    while ((child_pid = waitpid(-1, &status, WNOHANG)) > 0) {
        if (WIFSIGNALED(status)) {
            printf("Background process with PID %d terminated by signal %d\n", child_pid, WTERMSIG(status));
        } else {
            printf("Background process with PID %d exited with status %d\n", child_pid, WEXITSTATUS(status));
        }
    }
    fflush(stdout);
}
