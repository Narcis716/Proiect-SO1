#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>
#include <sys/stat.h>
#include <time.h>

#define MONITOR_PID ".monitor_pid"

static volatile sig_atomic_t g_running = 1;

static void handler_sigint(int sig)
{
    (void)sig;
    g_running = 0;
}

static void handler_sigusr1(int sig)
{
    (void)sig;
    time_t now = time(NULL);
    char ts[32];
    struct tm *t = localtime(&now);
    strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", t);
    const char *msg1 = "[";
    const char *msg2 = "] monitor_reports: new report added\n";
    write(STDOUT_FILENO, msg1, 1);
    write(STDOUT_FILENO, ts, strlen(ts));
    write(STDOUT_FILENO, msg2, strlen(msg2));
}

static void write_pid_file(void)
{
    int fd = open(MONITOR_PID, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) { perror("open .monitor_pid"); exit(1); }

    char buf[32];
    int n = snprintf(buf, sizeof(buf), "%ld\n", (long)getpid());
    write(fd, buf, n);
    close(fd);
}

static void remove_pid_file(void)
{
    unlink(MONITOR_PID);
}

int main(void)
{
    write_pid_file();

    struct sigaction sa_int;
    memset(&sa_int, 0, sizeof(sa_int));
    sa_int.sa_handler = handler_sigint;
    sigemptyset(&sa_int.sa_mask);
    sa_int.sa_flags = 0;
    sigaction(SIGINT, &sa_int, NULL);

    struct sigaction sa_usr1;
    memset(&sa_usr1, 0, sizeof(sa_usr1));
    sa_usr1.sa_handler = handler_sigusr1;
    sigemptyset(&sa_usr1.sa_mask);
    sa_usr1.sa_flags = SA_RESTART;
    sigaction(SIGUSR1, &sa_usr1, NULL);

    printf("monitor_reports started (PID %ld)\n", (long)getpid());
    fflush(stdout);

    while (g_running)
        pause();

    printf("monitor_reports: received SIGINT, shutting down\n");
    fflush(stdout);

    remove_pid_file();
    return 0;
}