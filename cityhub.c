#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <errno.h>

#define MAX_INPUT     1024
#define MAX_DISTRICTS 32

static pid_t g_hub_mon_pid = -1;

static ssize_t read_line(int fd, char *buf, size_t max)
{
    size_t i = 0;
    char c;
    while (i < max - 1) {
        ssize_t n = read(fd, &c, 1);
        if (n <= 0) return (i > 0) ? (ssize_t)i : n;
        buf[i++] = c;
        if (c == '\n') break;
    }
    buf[i] = '\0';
    return (ssize_t)i;
}

static void cmd_start_monitor(void)
{
    pid_t hub_mon = fork();
    if (hub_mon < 0) { perror("fork hub_mon"); return; }

    if (hub_mon == 0) {
        int pipefd[2];
        if (pipe(pipefd) < 0) { perror("pipe"); exit(1); }

        pid_t mon = fork();
        if (mon < 0) { perror("fork monitor"); exit(1); }

        if (mon == 0) {
            close(pipefd[0]);
            if (dup2(pipefd[1], STDOUT_FILENO) < 0) { perror("dup2"); exit(1); }
            close(pipefd[1]);
            execlp("./monitor_reports", "monitor_reports", (char *)NULL);
            perror("execlp monitor_reports");
            exit(1);
        }

        close(pipefd[1]);

        char line[512];
        ssize_t n;
        while ((n = read_line(pipefd[0], line, sizeof(line))) > 0) {
            write(STDOUT_FILENO, "[monitor] ", 10);
            write(STDOUT_FILENO, line, (size_t)n);
        }
        close(pipefd[0]);

        int status;
        waitpid(mon, &status, 0);

        const char end_msg[] = "[hub_mon] Monitor process has terminated.\n";
        write(STDOUT_FILENO, end_msg, sizeof(end_msg) - 1);
        exit(0);
    }

    g_hub_mon_pid = hub_mon;
    printf("[hub] Background monitor manager started (hub_mon PID: %d).\n",
           (int)hub_mon);
    fflush(stdout);
}

static void cmd_calculate_scores(char **districts, int count)
{
    printf("=== Workload Report ===\n");

    for (int i = 0; i < count; i++) {
        int pipefd[2];
        if (pipe(pipefd) < 0) { perror("pipe"); continue; }

        pid_t scorer = fork();
        if (scorer < 0) {
            perror("fork scorer");
            close(pipefd[0]);
            close(pipefd[1]);
            continue;
        }

        if (scorer == 0) {
            close(pipefd[0]);
            if (dup2(pipefd[1], STDOUT_FILENO) < 0) { perror("dup2"); exit(1); }
            close(pipefd[1]);
            execlp("./scorer", "scorer", districts[i], (char *)NULL);
            perror("execlp scorer");
            exit(1);
        }

        close(pipefd[1]);

        char buf[512];
        ssize_t n;
        while ((n = read(pipefd[0], buf, sizeof(buf) - 1)) > 0) {
            buf[n] = '\0';
            printf("%s", buf);
        }
        close(pipefd[0]);

        int status;
        waitpid(scorer, &status, 0);

        printf("\n");
    }

    printf("=== End of Report ===\n");
    fflush(stdout);
}

int main(void)
{
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = SIG_IGN;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_NOCLDWAIT;
    sigaction(SIGCHLD, &sa, NULL);

    printf("city_hub started. Commands: start_monitor | calculate_scores <d1> [d2 ...] | quit\n");

    char line[MAX_INPUT];

    while (1) {
        printf("> ");
        fflush(stdout);

        if (fgets(line, sizeof(line), stdin) == NULL) break;

        line[strcspn(line, "\n")] = '\0';
        if (strlen(line) == 0) continue;

        char copy[MAX_INPUT];
        strncpy(copy, line, sizeof(copy) - 1);

        char *token = strtok(copy, " ");
        if (!token) continue;

        if (strcmp(token, "start_monitor") == 0) {
            cmd_start_monitor();

        } else if (strcmp(token, "calculate_scores") == 0) {
            char *districts[MAX_DISTRICTS];
            int cnt = 0;
            char *d;
            while ((d = strtok(NULL, " ")) != NULL && cnt < MAX_DISTRICTS)
                districts[cnt++] = d;

            if (cnt == 0)
                printf("Usage: calculate_scores <district1> [district2 ...]\n");
            else
                cmd_calculate_scores(districts, cnt);

        } else if (strcmp(token, "quit") == 0 || strcmp(token, "exit") == 0) {
            break;

        } else {
            printf("Unknown command: %s\n", token);
            printf("Commands: start_monitor | calculate_scores <d1> [d2 ...] | quit\n");
        }
    }

    printf("[hub] city_hub exiting.\n");
    return 0;
}