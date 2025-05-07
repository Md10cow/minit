#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <errno.h>
#include <syslog.h>
#include <unistd.h>
#include <signal.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <sys/resource.h>
#include <stdarg.h>

#define MAX_PROCESSES 10

struct process_config {
    int argc;
    char **argv;
    char* input_file;
    char* output_file;
};

pid_t child_pids[MAX_PROCESSES];
int active_processes_count;
struct process_config process_configs[MAX_PROCESSES];
FILE* log_file;
char config_file_path[4096];

int append_to_array(char** array, char* str, int index) {
    array[index] = malloc(strlen(str) + 1);
    strcpy(array[index], str);
    return index + 1;
}

void write_to_log(const char* format, ...) {
    va_list args;
    va_start(args, format);
    int log_length = vsnprintf(NULL, 0, format, args);
    va_end(args);
    char* log_message = malloc(log_length + 1);
    va_start(args, format);
    vsnprintf(log_message, log_length + 1, format, args);
    va_end(args);
    fwrite(log_message, 1, log_length, log_file);
    fflush(log_file);
    free(log_message);
}

void initialize_logging() {
    log_file = fopen("/tmp/myinit.log", "w");
    write_to_log("myinit started\n");
}

void close_all_file_descriptors() {
    struct rlimit fd_limit;
    getrlimit(RLIMIT_NOFILE, &fd_limit);
    for (int fd = 0; fd < fd_limit.rlim_max; fd++)
        close(fd);
}

void validate_absolute_path(const char* path) {
    if (path[0] != '/') {
        write_to_log("Error: Only absolute paths are allowed\n");
        exit(EXIT_FAILURE);
    }
}

void setup_io_redirection(struct process_config config) {
    freopen(config.input_file, "r", stdin);
    freopen(config.output_file, "w", stdout);
}

struct process_config parse_config_line(char* line) {
    char* token = strtok(line, " ");
    validate_absolute_path(token);

    struct process_config config;

    config.argv = malloc(MAX_PROCESSES * sizeof(char*));
    int arg_index = 0;
    while (token != NULL) {
        arg_index = append_to_array(config.argv, token, arg_index);
        token = strtok(NULL, " ");
    }
    
    if (config.argv[arg_index-1][strlen(config.argv[arg_index-1]) - 1] == '\n') {
        config.argv[arg_index-1][strlen(config.argv[arg_index-1]) - 1] = '\0';
    }
    
    config.input_file = malloc(strlen(config.argv[arg_index-2]) + 1);
    config.output_file = malloc(strlen(config.argv[arg_index-1]) + 1);
    strcpy(config.input_file, config.argv[arg_index-2]);
    strcpy(config.output_file, config.argv[arg_index-1]);
    
    validate_absolute_path(config.input_file);
    validate_absolute_path(config.output_file);
    
    config.argv[arg_index-1] = NULL;
    config.argv[arg_index-2] = NULL;
    config.argc = arg_index - 2;
    return config;
}

void launch_process(int process_index) {
    struct process_config config = process_configs[process_index];
    pid_t pid = fork();
    switch (pid) {
    case -1:
        write_to_log("Failed to start process: %s\n", config.argv[0]);
        exit(EXIT_FAILURE);
        break;
    case 0:
        setup_io_redirection(config);
        execvp(config.argv[0], config.argv);
        exit(EXIT_FAILURE);
    default:
        child_pids[process_index] = pid;
        active_processes_count++;
        write_to_log("Process %d started: %s (PID: %d)\n", 
                    process_index, config.argv[0], pid);
        break;
    }
}

void run_processes() {
    FILE* config_file = fopen(config_file_path, "r");
    if (config_file == NULL) {
        write_to_log("Error: Cannot open config file\n");
        exit(EXIT_FAILURE);
    }
    
    char* line = NULL;
    size_t line_length = 0;
    int process_count = 0;
    
    while ((getline(&line, &line_length, config_file) != -1)) {
        process_configs[process_count] = parse_config_line(line);
        process_count++;
    }
    free(line);

    for (int i = 0; i < process_count; i++) {
        launch_process(i);
    }

    while (active_processes_count > 0) {
        int status = 0;
        pid_t terminated_pid = waitpid(-1, &status, 0);
        for (int i = 0; i < process_count; i++) {
            if (child_pids[i] == terminated_pid) {
                write_to_log("Process %d terminated with status: %d\n", i, status);
                child_pids[i] = 0;
                active_processes_count--;
                launch_process(i);
            }
        }
    }
    exit(EXIT_SUCCESS);
}

void handle_sighup(int signal_number) {
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (child_pids[i]) {
            kill(child_pids[i], SIGKILL);
            write_to_log("Process %d terminated by SIGHUP\n", i);
        }
    }

    write_to_log("Received SIGHUP - restarting myinit\n");
    run_processes();
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        printf("Usage: %s <config_file>\n", argv[0]);
        return EXIT_FAILURE;
    }
    
    pid_t pid = fork();
    switch (pid) {
    case -1:
        return EXIT_FAILURE;
    case 0:
        setsid();
        chdir("/");
        close_all_file_descriptors();
        initialize_logging();
        strcpy(config_file_path, argv[1]);
        
        struct sigaction signal_action;
        sigemptyset(&signal_action.sa_mask);
        signal_action.sa_handler = handle_sighup;
        signal_action.sa_flags = SA_NODEFER;
        sigaction(SIGHUP, &signal_action, NULL);
        
        run_processes();
        return EXIT_SUCCESS;
    default:
        return EXIT_SUCCESS;
    }
}
