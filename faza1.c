#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <errno.h>

#define MAX_NAME     64
#define MAX_CATEGORY 32
#define MAX_DESC     256

#define REPORTS_FILE  "reports.dat"
#define CONFIG_FILE   "district.cfg"
#define LOG_FILE      "logged_district"

#define PERM_DIR      0750
#define PERM_REPORTS  0664
#define PERM_CFG      0640
#define PERM_LOG      0644

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

static char g_role[32] = "";
static char g_user[MAX_NAME] = "";

static void mode_to_string(mode_t mode, char out[10])
{
    out[0] = (mode & S_IRUSR) ? 'r' : '-';
    out[1] = (mode & S_IWUSR) ? 'w' : '-';
    out[2] = (mode & S_IXUSR) ? 'x' : '-';
    out[3] = (mode & S_IRGRP) ? 'r' : '-';
    out[4] = (mode & S_IWGRP) ? 'w' : '-';
    out[5] = (mode & S_IXGRP) ? 'x' : '-';
    out[6] = (mode & S_IROTH) ? 'r' : '-';
    out[7] = (mode & S_IWOTH) ? 'w' : '-';
    out[8] = (mode & S_IXOTH) ? 'x' : '-';
    out[9] = '\0';
}

static int role_can_read(mode_t mode)
{
    if (strcmp(g_role, "manager") == 0)
        return (mode & S_IRUSR) ? 1 : 0;
    return (mode & S_IRGRP) ? 1 : 0;
}

static int role_can_write(mode_t mode)
{
    if (strcmp(g_role, "manager") == 0)
        return (mode & S_IWUSR) ? 1 : 0;
    return (mode & S_IWGRP) ? 1 : 0;
}

static void log_action(const char *district, const char *action)
{
    char path[512];
    snprintf(path, sizeof(path), "%s/%s", district, LOG_FILE);

    int fd = open(path, O_WRONLY | O_APPEND | O_CREAT, PERM_LOG);
    if (fd < 0) { perror("log_action: open"); return; }

    time_t now = time(NULL);
    char ts[32];
    struct tm *tm_info = localtime(&now);
    strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", tm_info);

    char entry[600];
    int n = snprintf(entry, sizeof(entry),
                     "[%s] role=%s user=%s %s\n", ts, g_role, g_user, action);
    write(fd, entry, n);
    close(fd);
}

static void rep_path(char *buf, size_t sz, const char *d)
{ snprintf(buf, sz, "%s/%s", d, REPORTS_FILE); }

static void cfg_path(char *buf, size_t sz, const char *d)
{ snprintf(buf, sz, "%s/%s", d, CONFIG_FILE); }

static void ensure_district(const char *district)
{
    struct stat st;
    if (stat(district, &st) == 0) return;

    if (mkdir(district, PERM_DIR) < 0) {
        perror("mkdir"); exit(1);
    }
    chmod(district, PERM_DIR);

    char p[512];
    cfg_path(p, sizeof(p), district);
    int fd = open(p, O_WRONLY | O_CREAT | O_TRUNC, PERM_CFG);
    if (fd >= 0) { write(fd, "1\n", 2); close(fd); }
    chmod(p, PERM_CFG);

    snprintf(p, sizeof(p), "%s/%s", district, LOG_FILE);
    fd = open(p, O_WRONLY | O_CREAT | O_TRUNC, PERM_LOG);
    if (fd >= 0) close(fd);
    chmod(p, PERM_LOG);

    rep_path(p, sizeof(p), district);
    fd = open(p, O_WRONLY | O_CREAT | O_TRUNC, PERM_REPORTS);
    if (fd >= 0) close(fd);
    chmod(p, PERM_REPORTS);
}

static void refresh_symlink(const char *district)
{
    char link_name[256];
    snprintf(link_name, sizeof(link_name), "active_reports-%s", district);

    char target[512];
    rep_path(target, sizeof(target), district);

    struct stat lst;
    if (lstat(link_name, &lst) == 0) {
        if (S_ISLNK(lst.st_mode)) unlink(link_name);
        else return;
    }
    if (symlink(target, link_name) < 0)
        perror("symlink");
}

static int record_count(const char *district)
{
    char p[512]; rep_path(p, sizeof(p), district);
    struct stat st;
    if (stat(p, &st) < 0) return 0;
    return (int)(st.st_size / sizeof(Report));
}

void cmd_add(const char *district)
{
    ensure_district(district);

    char p[512]; rep_path(p, sizeof(p), district);

    struct stat st;
    if (stat(p, &st) < 0) { perror("stat reports.dat"); return; }
    if (!role_can_write(st.st_mode)) {
        fprintf(stderr, "Error: role '%s' has no write access to %s\n", g_role, p);
        return;
    }

    Report r;
    memset(&r, 0, sizeof(r));
    r.report_id = record_count(district) + 1;
    strncpy(r.inspector, g_user, MAX_NAME - 1);
    r.timestamp  = time(NULL);

    printf("GPS latitude  : "); scanf("%lf",  &r.latitude);
    printf("GPS longitude : "); scanf("%lf",  &r.longitude);
    printf("Category (road/lighting/flooding): "); scanf("%31s", r.category);
    printf("Severity (1=minor 2=moderate 3=critical): "); scanf("%d", &r.severity);
    getchar();
    printf("Description: "); fgets(r.description, MAX_DESC, stdin);
    r.description[strcspn(r.description, "\n")] = '\0';

    int fd = open(p, O_WRONLY | O_APPEND, 0);
    if (fd < 0) { perror("open reports.dat"); return; }
    if (write(fd, &r, sizeof(r)) != (ssize_t)sizeof(r))
        perror("write");
    close(fd);

    chmod(p, PERM_REPORTS);
    refresh_symlink(district);

    char act[128];
    snprintf(act, sizeof(act), "add district=%s report_id=%d", district, r.report_id);
    log_action(district, act);

    printf("Report %d added to district '%s'.\n", r.report_id, district);
}

void cmd_list(const char *district)
{
    char p[512]; rep_path(p, sizeof(p), district);

    struct stat st;
    if (stat(p, &st) < 0) { fprintf(stderr, "Error: %s: %s\n", p, strerror(errno)); return; }

    if (!role_can_read(st.st_mode)) {
        fprintf(stderr, "Error: role '%s' has no read access to %s\n", g_role, p);
        return;
    }

    char mode_str[10];
    mode_to_string(st.st_mode, mode_str);
    char mtime_str[32];
    struct tm *tm_mod = localtime(&st.st_mtime);
    strftime(mtime_str, sizeof(mtime_str), "%Y-%m-%d %H:%M:%S", tm_mod);

    printf("File        : %s\n", p);
    printf("Permissions : %s\n", mode_str);
    printf("Size        : %ld bytes\n", (long)st.st_size);
    printf("Modified    : %s\n\n", mtime_str);

    char sym_name[256];
    snprintf(sym_name, sizeof(sym_name), "active_reports-%s", district);
    struct stat lp;
    if (lstat(sym_name, &lp) == 0) {
        if (S_ISLNK(lp.st_mode))
            printf("Symlink     : %s -> %s\n\n", sym_name, p);
        else
            printf("Warning     : %s exists but is not a symlink\n\n", sym_name);
    } else {
        printf("Warning     : symlink %s is dangling or missing\n\n", sym_name);
    }

    printf("%-5s %-20s %-10s %-10s %-12s %-8s %-20s\n",
           "ID", "Inspector", "Lat", "Lon", "Category", "Sev", "Timestamp");
    printf("%-5s %-20s %-10s %-10s %-12s %-8s %-20s\n",
           "----", "-------------------", "---------", "---------",
           "-----------", "-------", "-------------------");

    int fd = open(p, O_RDONLY);
    if (fd < 0) { perror("open reports.dat"); return; }

    Report r; int n = 0;
    while (read(fd, &r, sizeof(r)) == (ssize_t)sizeof(r)) {
        char ts[24];
        struct tm *t = localtime(&r.timestamp);
        strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", t);
        printf("%-5d %-20s %-10.4f %-10.4f %-12s %-8d %-20s\n",
               r.report_id, r.inspector, r.latitude, r.longitude,
               r.category, r.severity, ts);
        n++;
    }
    close(fd);

    if (n == 0) printf("(no reports)\n");
    else        printf("\nTotal: %d report(s)\n", n);

    log_action(district, "list");
}

void cmd_view(const char *district, int report_id)
{
    char p[512]; rep_path(p, sizeof(p), district);

    struct stat st;
    if (stat(p, &st) < 0) { perror("stat reports.dat"); return; }
    if (!role_can_read(st.st_mode)) {
        fprintf(stderr, "Error: role '%s' has no read access\n", g_role);
        return;
    }

    int fd = open(p, O_RDONLY);
    if (fd < 0) { perror("open reports.dat"); return; }

    Report r; int found = 0;
    while (read(fd, &r, sizeof(r)) == (ssize_t)sizeof(r)) {
        if (r.report_id == report_id) { found = 1; break; }
    }
    close(fd);

    if (!found) {
        fprintf(stderr, "Error: report %d not found in district '%s'\n", report_id, district);
        return;
    }

    const char *sev_str = (r.severity == 1) ? "minor"
                        : (r.severity == 2) ? "moderate" : "critical";
    char ts[32];
    struct tm *t = localtime(&r.timestamp);
    strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", t);

    printf("Report      : %d\n",        r.report_id);
    printf("Inspector   : %s\n",        r.inspector);
    printf("GPS         : %.6f, %.6f\n", r.latitude, r.longitude);
    printf("Category    : %s\n",        r.category);
    printf("Severity    : %d (%s)\n",   r.severity, sev_str);
    printf("Timestamp   : %s\n",        ts);
    printf("Description : %s\n",        r.description);

    char act[64];
    snprintf(act, sizeof(act), "view report_id=%d", report_id);
    log_action(district, act);
}

void cmd_remove_report(const char *district, int report_id)
{
    if (strcmp(g_role, "manager") != 0) {
        fprintf(stderr, "Error: remove_report requires manager role\n");
        return;
    }

    char p[512]; rep_path(p, sizeof(p), district);

    struct stat st;
    if (stat(p, &st) < 0) { perror("stat reports.dat"); return; }
    if (!role_can_write(st.st_mode)) {
        fprintf(stderr, "Error: no write access to %s\n", p);
        return;
    }

    int fd = open(p, O_RDWR);
    if (fd < 0) { perror("open reports.dat"); return; }

    int total = (int)(st.st_size / sizeof(Report));
    off_t remove_off = -1;
    Report r;

    for (int i = 0; i < total; i++) {
        off_t cur = (off_t)i * sizeof(Report);
        lseek(fd, cur, SEEK_SET);
        if (read(fd, &r, sizeof(r)) != (ssize_t)sizeof(r)) break;
        if (r.report_id == report_id) { remove_off = cur; break; }
    }

    if (remove_off < 0) {
        fprintf(stderr, "Error: report %d not found\n", report_id);
        close(fd);
        return;
    }

    for (off_t src = remove_off + (off_t)sizeof(Report);
         src < st.st_size;
         src += sizeof(Report))
    {
        lseek(fd, src, SEEK_SET);
        if (read(fd, &r, sizeof(r)) != (ssize_t)sizeof(r)) break;
        lseek(fd, src - (off_t)sizeof(Report), SEEK_SET);
        write(fd, &r, sizeof(r));
    }

    off_t new_size = st.st_size - (off_t)sizeof(Report);
    if (ftruncate(fd, new_size) < 0) perror("ftruncate");
    close(fd);

    printf("Report %d removed from district '%s'.\n", report_id, district);

    char act[64];
    snprintf(act, sizeof(act), "remove_report report_id=%d", report_id);
    log_action(district, act);
}

void cmd_update_threshold(const char *district, int value)
{
    if (strcmp(g_role, "manager") != 0) {
        fprintf(stderr, "Error: update_threshold requires manager role\n");
        return;
    }

    char p[512]; cfg_path(p, sizeof(p), district);

    struct stat st;
    if (stat(p, &st) < 0) { perror("stat district.cfg"); return; }

    mode_t actual = st.st_mode & 0777;
    if (actual != PERM_CFG) {
        char sym[10]; mode_to_string(st.st_mode, sym);
        fprintf(stderr,
                "Error: permissions on %s have been altered "
                "(expected %04o, found %04o / %s). Refusing.\n",
                p, PERM_CFG, (unsigned)actual, sym);
        return;
    }
    if (!role_can_write(st.st_mode)) {
        fprintf(stderr, "Error: no write access to %s\n", p);
        return;
    }

    int fd = open(p, O_WRONLY | O_TRUNC);
    if (fd < 0) { perror("open district.cfg"); return; }

    char buf[32];
    int n = snprintf(buf, sizeof(buf), "%d\n", value);
    write(fd, buf, n);
    close(fd);

    printf("Threshold for district '%s' updated to %d.\n", district, value);

    char act[64];
    snprintf(act, sizeof(act), "update_threshold value=%d", value);
    log_action(district, act);
}

int parse_condition(const char *input, char *field, char *op, char *value)
{
    const char *ops[] = { "==", "!=", "<=", ">=", "<", ">", NULL };

    for (int i = 0; ops[i]; i++) {
        const char *colon1 = strchr(input, ':');
        if (!colon1) return 0;

        if (strncmp(colon1 + 1, ops[i], strlen(ops[i])) == 0) {
            int flen = (int)(colon1 - input);
            strncpy(field, input, flen);
            field[flen] = '\0';

            strcpy(op, ops[i]);

            const char *val_start = colon1 + 1 + strlen(ops[i]);
            if (*val_start == ':') val_start++;
            strcpy(value, val_start);

            return 1;
        }
    }
    return 0;
}

int match_condition(Report *r, const char *field, const char *op, const char *value)
{
#define CMP_INT(a, b) \
    ( strcmp(op,"==") == 0 ? (a)==(b) \
    : strcmp(op,"!=") == 0 ? (a)!=(b) \
    : strcmp(op,"<")  == 0 ? (a)<(b)  \
    : strcmp(op,"<=") == 0 ? (a)<=(b) \
    : strcmp(op,">")  == 0 ? (a)>(b)  \
    : strcmp(op,">=") == 0 ? (a)>=(b) \
    : 0 )

    if (strcmp(field, "severity") == 0) {
        int v = atoi(value);
        return CMP_INT(r->severity, v);
    }
    if (strcmp(field, "timestamp") == 0) {
        time_t v = (time_t)atol(value);
        return CMP_INT(r->timestamp, v);
    }
    if (strcmp(field, "category") == 0) {
        int c = strcmp(r->category, value);
        if (strcmp(op, "==") == 0) return c == 0;
        if (strcmp(op, "!=") == 0) return c != 0;
    }
    if (strcmp(field, "inspector") == 0) {
        int c = strcmp(r->inspector, value);
        if (strcmp(op, "==") == 0) return c == 0;
        if (strcmp(op, "!=") == 0) return c != 0;
    }

#undef CMP_INT
    fprintf(stderr, "Warning: unknown field '%s'\n", field);
    return 0;
}

void cmd_filter(const char *district, int ncond, char **cond_strs)
{
    char p[512]; rep_path(p, sizeof(p), district);

    struct stat st;
    if (stat(p, &st) < 0) { perror("stat reports.dat"); return; }
    if (!role_can_read(st.st_mode)) {
        fprintf(stderr, "Error: role '%s' has no read access to %s\n", g_role, p);
        return;
    }

    char fields[16][64], ops[16][8], values[16][64];
    int vc = 0;
    for (int i = 0; i < ncond && i < 16; i++) {
        if (parse_condition(cond_strs[i], fields[vc], ops[vc], values[vc]))
            vc++;
        else
            fprintf(stderr, "Warning: invalid condition '%s' - skipped\n", cond_strs[i]);
    }

    int fd = open(p, O_RDONLY);
    if (fd < 0) { perror("open reports.dat"); return; }

    Report r; int hits = 0;
    printf("%-5s %-20s %-10s %-10s %-12s %-8s %-20s\n",
           "ID", "Inspector", "Lat", "Lon", "Category", "Sev", "Timestamp");
    printf("%-5s %-20s %-10s %-10s %-12s %-8s %-20s\n",
           "----", "-------------------", "---------", "---------",
           "-----------", "-------", "-------------------");

    while (read(fd, &r, sizeof(r)) == (ssize_t)sizeof(r)) {
        int ok = 1;
        for (int i = 0; i < vc && ok; i++)
            ok = match_condition(&r, fields[i], ops[i], values[i]);
        if (ok) {
            char ts[24];
            struct tm *t = localtime(&r.timestamp);
            strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", t);
            printf("%-5d %-20s %-10.4f %-10.4f %-12s %-8d %-20s\n",
                   r.report_id, r.inspector, r.latitude, r.longitude,
                   r.category, r.severity, ts);
            hits++;
        }
    }
    close(fd);

    if (hits == 0) printf("(no matching reports)\n");
    else           printf("\nTotal: %d match(es)\n", hits);

    log_action(district, "filter");
}

static void usage(const char *prog)
{
    fprintf(stderr,
        "Usage:\n"
        "  %s --role <manager|inspector> --user <n> --add <district>\n"
        "  %s --role <role> --user <n> --list <district>\n"
        "  %s --role <role> --user <n> --view <district> <report_id>\n"
        "  %s --role manager --user <n> --remove_report <district> <report_id>\n"
        "  %s --role manager --user <n> --update_threshold <district> <value>\n"
        "  %s --role <role> --user <n> --filter <district> [cond ...]\n",
        prog, prog, prog, prog, prog, prog);
}

int main(int argc, char *argv[])
{
    if (argc < 2) { usage(argv[0]); return 1; }

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--role") == 0 && i + 1 < argc)
            strncpy(g_role, argv[++i], sizeof(g_role) - 1);
        else if (strcmp(argv[i], "--user") == 0 && i + 1 < argc)
            strncpy(g_user, argv[++i], sizeof(g_user) - 1);
    }

    if (strlen(g_role) == 0) {
        fprintf(stderr, "Error: --role is required\n"); return 1;
    }
    if (strcmp(g_role, "manager") != 0 && strcmp(g_role, "inspector") != 0) {
        fprintf(stderr, "Error: invalid role '%s'\n", g_role); return 1;
    }
    if (strlen(g_user) == 0) {
        fprintf(stderr, "Error: --user is required\n"); return 1;
    }

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--add") == 0) {
            if (i + 1 >= argc) { usage(argv[0]); return 1; }
            cmd_add(argv[i + 1]);
            return 0;
        }
        if (strcmp(argv[i], "--list") == 0) {
            if (i + 1 >= argc) { usage(argv[0]); return 1; }
            cmd_list(argv[i + 1]);
            return 0;
        }
        if (strcmp(argv[i], "--view") == 0) {
            if (i + 2 >= argc) { usage(argv[0]); return 1; }
            cmd_view(argv[i + 1], atoi(argv[i + 2]));
            return 0;
        }
        if (strcmp(argv[i], "--remove_report") == 0) {
            if (i + 2 >= argc) { usage(argv[0]); return 1; }
            cmd_remove_report(argv[i + 1], atoi(argv[i + 2]));
            return 0;
        }
        if (strcmp(argv[i], "--update_threshold") == 0) {
            if (i + 2 >= argc) { usage(argv[0]); return 1; }
            cmd_update_threshold(argv[i + 1], atoi(argv[i + 2]));
            return 0;
        }
        if (strcmp(argv[i], "--filter") == 0) {
            if (i + 1 >= argc) { usage(argv[0]); return 1; }
            cmd_filter(argv[i + 1], argc - i - 2, argv + i + 2);
            return 0;
        }
    }

    fprintf(stderr, "Error: no command specified\n");
    usage(argv[0]);
    return 1;
}