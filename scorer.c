/*
 * scorer.c
 * SO/SO1/OS Project - Phase 3: Pipes and Redirects
 *
 * Program extern apelat de city_hub pentru fiecare district.
 * Citeste reports.dat si calculeaza workload score per inspector
 * (suma severity-urilor tuturor rapoartelor depuse de acel inspector).
 * Scrie rezultatul pe stdout (care e redirectionat printr-un pipe catre hub).
 *
 * Utilizare:
 *   ./scorer <district_id>
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <sys/stat.h>

#define MAX_NAME    64
#define MAX_CAT     32
#define MAX_DESC   256
#define MAX_INSPECTORS 128

typedef struct {
    int    id;
    char   inspectod[MAX_NAME];
    double latitude;
    double longitude;
    char   category[MAX_CAT];
    int    severity;
    long   timestamp;   /* time_t */
    char   description[MAX_DESC];
} Report;

typedef struct {
    char name[MAX_NAME];
    int  score;
} InspectorScore;

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <district_id>\n", argv[0]);
        return 1;
    }

    const char *district = argv[1];

    /* Construim calea catre reports.dat */
    char rdat[512];
    snprintf(rdat, sizeof(rdat), "%s/reports.dat", district);

    int fd = open(rdat, O_RDONLY);
    if (fd < 0) {
        fprintf(stderr, "[scorer] ERROR: cannot open '%s': %s\n",
                rdat, strerror(errno));
        return 1;
    }

    /* Citim toate rapoartele si acumulam scoruri per inspector */
    InspectorScore scores[MAX_INSPECTORS];
    int n_inspectors = 0;

    Report r;
    while (read(fd, &r, sizeof(r)) == sizeof(r)) {
        /* Cautam inspectorul in lista */
        int found = 0;
        for (int i = 0; i < n_inspectors; i++) {
            if (strcmp(scores[i].name, r.inspectod) == 0) {
                scores[i].score += r.severity;
                found = 1;
                break;
            }
        }
        if (!found && n_inspectors < MAX_INSPECTORS) {
            strncpy(scores[n_inspectors].name, r.inspectod, MAX_NAME - 1);
            scores[n_inspectors].name[MAX_NAME - 1] = '\0';
            scores[n_inspectors].score = r.severity;
            n_inspectors++;
        }
    }
    close(fd);

    /* Scriem rezultatul pe stdout (pipe catre hub) */
    printf("=== District: %s ===\n", district);
    if (n_inspectors == 0) {
        printf("  (no reports)\n");
    } else {
        for (int i = 0; i < n_inspectors; i++) {
            printf("  Inspector: %-20s  Workload Score: %d\n",
                   scores[i].name, scores[i].score);
        }
    }
    fflush(stdout);

    return 0;
}
