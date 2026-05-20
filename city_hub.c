/*
 * city_hub.c
 * SO/SO1/OS Project - Phase 3: Pipes and Redirects
 *
 * Program interactiv cu interfata command-line.
 * Comenzi suportate:
 *   start_monitor              - porneste monitor_reports ca proces copil
 *   calculate_scores <d1> [d2] - calculeaza workload scores pentru districte
 *   exit                       - iese din hub
 *
 * Utilizare:
 *   ./city_hub
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <signal.h>

#define PID_FILE      ".monitor_pid"
#define MONITOR_BIN   "./monitor_reports"
#define SCORER_BIN    "./scorer"
#define MAX_LINE      1024
#define MAX_DISTRICTS 32

/* ─────────────────────────────────────────────
   Citeste PID-ul monitorului din .monitor_pid
   ───────────────────────────────────────────── */
static pid_t read_monitor_pid(void) {
    int fd = open(PID_FILE, O_RDONLY);
    if (fd < 0) return -1;
    char buf[32];
    int n = read(fd, buf, sizeof(buf) - 1);
    close(fd);
    if (n <= 0) return -1;
    buf[n] = '\0';
    return (pid_t)atoi(buf);
}

/* ─────────────────────────────────────────────
   hub_mon: proces intermediar care:
   1. Creaza un pipe
   2. Fork-uieste monitor_reports cu write-end-ul pipe-ului
   3. Citeste din read-end si afiseaza mesajele
   ───────────────────────────────────────────── */
static void run_hub_mon(void) {
    int pipefd[2];
    if (pipe(pipefd) < 0) {
        fprintf(stderr, "[hub_mon] ERROR: pipe failed: %s\n", strerror(errno));
        exit(1);
    }

    pid_t monitor_pid = fork();
    if (monitor_pid < 0) {
        fprintf(stderr, "[hub_mon] ERROR: fork for monitor failed: %s\n", strerror(errno));
        exit(1);
    }

    if (monitor_pid == 0) {
        /* Proces copil: monitor_reports */
        /* Inchidem read-end (nu il folosim) */
        close(pipefd[0]);

        /* Redirectam stdout catre write-end-ul pipe-ului cu dup2 */
        if (dup2(pipefd[1], STDOUT_FILENO) < 0) {
            fprintf(stderr, "[monitor] ERROR: dup2 failed: %s\n", strerror(errno));
            exit(1);
        }
        close(pipefd[1]);

        /* Executam monitor_reports cu fd-ul pipe-ului ca argument */
        /* Transmitem write_fd ca argument pentru compatibilitate */
        char fd_str[16];
        snprintf(fd_str, sizeof(fd_str), "%d", STDOUT_FILENO);
        execlp(MONITOR_BIN, "monitor_reports", fd_str, (char *)NULL);
        fprintf(stderr, "[hub_mon] ERROR: exec monitor_reports failed: %s\n", strerror(errno));
        exit(1);
    }

    /* Proces parinte (hub_mon): citim din read-end si afisam */
    close(pipefd[1]); /* nu scriem */

    char buf[1024];
    ssize_t n;
    int monitor_ended = 0;

    while (!monitor_ended) {
        n = read(pipefd[0], buf, sizeof(buf) - 1);
        if (n <= 0) break;
        buf[n] = '\0';
        printf("%s", buf);
        fflush(stdout);

        /* Daca monitorul s-a oprit, iesim */
        if (strstr(buf, "Stopped") || strstr(buf, "already running")) {
            monitor_ended = 1;
        }
    }

    close(pipefd[0]);

    /* Asteptam monitorul */
    int status;
    waitpid(monitor_pid, &status, 0);

    if (monitor_ended && strstr(buf, "already running")) {
        printf("[hub] Monitor was already running. hub_mon exiting.\n");
    } else {
        printf("[hub] Monitor process ended.\n");
    }
}

/* ─────────────────────────────────────────────
   cmd_start_monitor
   Creeaza un proces copil hub_mon care la randul
   lui porneste monitor_reports printr-un pipe
   ───────────────────────────────────────────── */
static void cmd_start_monitor(void) {
    pid_t hub_mon_pid = fork();
    if (hub_mon_pid < 0) {
        fprintf(stderr, "[hub] ERROR: fork failed: %s\n", strerror(errno));
        return;
    }

    if (hub_mon_pid == 0) {
        /* Procesul hub_mon */
        run_hub_mon();
        exit(0);
    }

    /* Hub-ul principal nu asteapta hub_mon - ruleaza in background */
    printf("[hub] hub_mon started (PID %d). Monitor launching...\n", hub_mon_pid);

    /* Mic delay ca monitorul sa porneasca si sa scrie PID-ul */
    usleep(200000); /* 200ms */

    pid_t mpid = read_monitor_pid();
    if (mpid > 0) {
        printf("[hub] Monitor is running with PID %d.\n", mpid);
    }
}

/* ─────────────────────────────────────────────
   cmd_calculate_scores
   Pentru fiecare district: fork + pipe + exec scorer
   Colecteaza output si afiseaza raportul combinat
   ───────────────────────────────────────────── */
static void cmd_calculate_scores(char **districts, int n) {
    if (n == 0) {
        printf("[hub] No districts specified.\n");
        return;
    }

    printf("\n=== Workload Report ===\n");

    for (int i = 0; i < n; i++) {
        int pipefd[2];
        if (pipe(pipefd) < 0) {
            fprintf(stderr, "[hub] ERROR: pipe failed: %s\n", strerror(errno));
            continue;
        }

        pid_t scorer_pid = fork();
        if (scorer_pid < 0) {
            fprintf(stderr, "[hub] ERROR: fork failed: %s\n", strerror(errno));
            close(pipefd[0]);
            close(pipefd[1]);
            continue;
        }

        if (scorer_pid == 0) {
            /* Procesul scorer */
            close(pipefd[0]); /* nu citim */

            /* Redirectam stdout catre write-end cu dup2 */
            if (dup2(pipefd[1], STDOUT_FILENO) < 0) {
                fprintf(stderr, "[scorer] ERROR: dup2 failed: %s\n", strerror(errno));
                exit(1);
            }
            close(pipefd[1]);

            execlp(SCORER_BIN, "scorer", districts[i], (char *)NULL);
            fprintf(stderr, "[hub] ERROR: exec scorer failed: %s\n", strerror(errno));
            exit(1);
        }

        /* Hub-ul parinte: citim output-ul scorer-ului */
        close(pipefd[1]);

        char buf[4096];
        ssize_t total = 0;
        ssize_t n_read;
        while ((n_read = read(pipefd[0], buf + total,
                              sizeof(buf) - 1 - total)) > 0) {
            total += n_read;
        }
        buf[total] = '\0';
        close(pipefd[0]);

        /* Asteptam scorer-ul */
        int status;
        waitpid(scorer_pid, &status, 0);

        /* Afisam output-ul */
        printf("%s", buf);
    }

    printf("=== End of Workload Report ===\n\n");
}

/* ─────────────────────────────────────────────
   main - bucla interactiva
   ───────────────────────────────────────────── */
int main(void) {
    printf("=== City Hub ===\n");
    printf("Commands: start_monitor | calculate_scores <district1> [district2 ...] | exit\n\n");

    char line[MAX_LINE];

    while (1) {
        printf("hub> ");
        fflush(stdout);

        if (fgets(line, sizeof(line), stdin) == NULL) {
            /* EOF (Ctrl+D) */
            printf("\n[hub] EOF received. Exiting.\n");
            break;
        }

        /* Eliminam newline */
        line[strcspn(line, "\n")] = '\0';

        /* Ignoram linii goale */
        if (strlen(line) == 0) continue;

        /* Parsam comanda */
        char *tokens[MAX_DISTRICTS + 2];
        int ntok = 0;
        char *tok = strtok(line, " \t");
        while (tok && ntok < MAX_DISTRICTS + 1) {
            tokens[ntok++] = tok;
            tok = strtok(NULL, " \t");
        }
        tokens[ntok] = NULL;

        if (ntok == 0) continue;

        if (strcmp(tokens[0], "exit") == 0) {
            printf("[hub] Exiting.\n");
            break;

        } else if (strcmp(tokens[0], "start_monitor") == 0) {
            cmd_start_monitor();

        } else if (strcmp(tokens[0], "calculate_scores") == 0) {
            if (ntok < 2) {
                printf("[hub] Usage: calculate_scores <district1> [district2 ...]\n");
            } else {
                cmd_calculate_scores(&tokens[1], ntok - 1);
            }

        } else {
            printf("[hub] Unknown command: '%s'\n", tokens[0]);
            printf("[hub] Commands: start_monitor | calculate_scores <d1> [d2 ...] | exit\n");
        }
    }

    return 0;
}
