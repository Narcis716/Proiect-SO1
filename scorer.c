#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <time.h>

#define MAX_NAME      64
#define MAX_CATEGORY  32
#define MAX_DESC      256
#define MAX_INSPECTORS 128

#define REPORTS_FILE "reports.dat"

typedef struct {
    int    report_id;
    char   inspector[MAX_NAME];
    double latitude;
    double longitude;
    char   category[MAX_CATEGORY];
    int    severity;
    time_t timestamp;
    char   description[MAX_DESC];
} Report;

typedef struct {
    char inspector[MAX_NAME];
    int  total_severity;
    int  report_count;
} Score;

static Score scores[MAX_INSPECTORS];
static int   score_count = 0;

static int find_or_add(const char *inspector)
{
    for (int i = 0; i < score_count; i++) {
        if (strcmp(scores[i].inspector, inspector) == 0)
            return i;
    }
    if (score_count >= MAX_INSPECTORS) return -1;
    strncpy(scores[score_count].inspector, inspector, MAX_NAME - 1);
    scores[score_count].total_severity = 0;
    scores[score_count].report_count   = 0;
    return score_count++;
}

static int cmp_score(const void *a, const void *b)
{
    const Score *sa = (const Score *)a;
    const Score *sb = (const Score *)b;
    return sb->total_severity - sa->total_severity;
}

int main(int argc, char *argv[])
{
    if (argc < 2) {
        write(STDOUT_FILENO, "Usage: scorer <district_id>\n", 28);
        return 1;
    }

    char path[512];
    snprintf(path, sizeof(path), "%s/%s", argv[1], REPORTS_FILE);

    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        char msg[256];
        int n = snprintf(msg, sizeof(msg),
                         "ERROR: cannot open %s\n", path);
        write(STDOUT_FILENO, msg, n);
        return 1;
    }

    Report r;
    while (read(fd, &r, sizeof(r)) == (ssize_t)sizeof(r)) {
        int idx = find_or_add(r.inspector);
        if (idx < 0) continue;
        scores[idx].total_severity += r.severity;
        scores[idx].report_count++;
    }
    close(fd);

    qsort(scores, score_count, sizeof(Score), cmp_score);

    char header[256];
    int n = snprintf(header, sizeof(header),
                     "District: %s\n"
                     "%-30s %-8s %-8s\n"
                     "%-30s %-8s %-8s\n",
                     argv[1],
                     "Inspector", "Reports", "Score",
                     "-----------------------------", "-------", "-------");
    write(STDOUT_FILENO, header, n);

    for (int i = 0; i < score_count; i++) {
        char line[256];
        int len = snprintf(line, sizeof(line),
                           "%-30s %-8d %-8d\n",
                           scores[i].inspector,
                           scores[i].report_count,
                           scores[i].total_severity);
        write(STDOUT_FILENO, line, len);
    }

    if (score_count == 0) {
        char msg[] = "(no reports found)\n";
        write(STDOUT_FILENO, msg, sizeof(msg) - 1);
    }

    return 0;
}