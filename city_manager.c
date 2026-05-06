#include<stdio.h>
#include<string.h>
#include<stdlib.h>
#include<fcntl.h>
#include<unistd.h>
#include<time.h>
#include<errno.h>
#include<sys/stat.h>
#include<sys/types.h>

#define MAX_NAME 64
#define MAX_CAT 32
#define MAX_DESC 256


typedef struct{
  int id;
  char inspectod[MAX_NAME];
  double latitude;
  double longitude;
  char category[MAX_CAT];
  int severity;
  time_t timestamp;
  char description[MAX_DESC];
}Report;

//FORWARD DECLARATIONS


/* Permission helpers */
void mode_to_string(mode_t mode, char *buf); /* buf must be ≥ 10 bytes */
int check_permission(const char *path, const char *role, mode_t required_bits, const char *action);

/* Logging */
void log_action(const char *district, const char *role, const char *user, const char *action);

/* District setup */
void ensure_district(const char *district, const char *role, const char *user);

/* Symlink helpers */
void create_symlink(const char *district);
void remove_symlink(const char *district);

/* Commands */
void cmd_add(const char *district, const char *role, const char *user);
void cmd_list(const char *district, const char *role, const char *user);
void cmd_view(const char *district, int report_id, const char *role, const char *user);
void cmd_remove_report(const char *district, int report_id, const char *role, const char *user);
void cmd_update_threshold(const char *district, int value, const char *role, const char *user);
void cmd_filter(const char *district, int argc, char **conditions, const char *role, const char *user);

/* AI-assisted filter functions */
int parse_condition(const char *input, char *field, char *op, char *value);
int match_condition(Report *r, const char *field, const char *op, const char *value);

/* Phase 2 */
void notify_monitor(const char *district, const char *role, const char *user, int report_id);
void cmd_remove_district(const char *district, const char *role, const char *user);

//PERMISSION HELPERS

/* Convert mode_t bits to a 9-char rwxrwxrwx string (written by student) */
void mode_to_string(mode_t mode, char *buf) {
  buf[0] = (mode & S_IRUSR) ? 'r' : '-';
  buf[1] = (mode & S_IWUSR) ? 'w' : '-';
  buf[2] = (mode & S_IXUSR) ? 'x' : '-';
  buf[3] = (mode & S_IRGRP) ? 'r' : '-';
  buf[4] = (mode & S_IWGRP) ? 'w' : '-';
  buf[5] = (mode & S_IXGRP) ? 'x' : '-';
  buf[6] = (mode & S_IROTH) ? 'r' : '-';
  buf[7] = (mode & S_IWOTH) ? 'w' : '-';
  buf[8] = (mode & S_IXOTH) ? 'x' : '-';
  buf[9] = '\0';
}

/*
* Check that 'path' has the expected permission bits for the given role.
* required_bits: the bit(s) from st_mode that MUST be set.
* Returns 1 if OK, 0 and prints error if not.
*/
int check_permission(const char *path, const char *role, mode_t required_bits, const char *action) {
  struct stat st;
  if (stat(path, &st) < 0) {
    fprintf(stderr, "ERROR: cannot stat '%s': %s\n", path, strerror(errno));
    return 0;
  }
  if ((st.st_mode & required_bits) != required_bits) {
    char perm_str[10];
    mode_to_string(st.st_mode & 0777, perm_str);
    fprintf(stderr,
	    "ERROR: permission denied for role '%s' to %s '%s' (current perms: %s)\n",
	    role, action, path, perm_str);
    return 0;
  }
  return 1;
}

/* ─────────────────────────────────────────────
   Logging
   ───────────────────────────────────────────── */

void log_action(const char *district, const char *role, const char *user, const char *action) {
  /* Build path: /logged_district */
  char log_path[256];
  snprintf(log_path, sizeof(log_path), "%s/logged_district", district);

  /* Check that manager can write (644 → owner write) and inspector cannot */
  if (strcmp(role, "inspector") == 0) {
    /* inspectors must NOT write to the log */
    fprintf(stderr,
	    "ERROR: inspector '%s' is not allowed to write to the log.\n", user);
    return;
  }

  /* Open for appending (create with 644 if needed) */
  int fd = open(log_path, O_WRONLY | O_APPEND | O_CREAT, 0644);
  if (fd < 0) {
    fprintf(stderr, "ERROR: cannot open log '%s': %s\n", log_path, strerror(errno));
    return;
  }
  chmod(log_path, 0644);

  time_t now = time(NULL);
  char ts[64];
  strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", localtime(&now));

  char entry[512];
  int len = snprintf(entry, sizeof(entry),
		     "[%s] role=%s user=%s action=%s\n",
		     ts, role, user, action);
  write(fd, entry, len);
  close(fd);
}

/* ─────────────────────────────────────────────
   District initialisation
   ───────────────────────────────────────────── */

void ensure_district(const char *district, const char *role, const char *user) {
  (void)role; (void)user; /* kept in signature for future logging extension */
  struct stat st;

  /* Create directory if it does not exist */
  if (stat(district, &st) < 0) {
    if (mkdir(district, 0750) < 0) {
      fprintf(stderr, "ERROR: cannot create district dir '%s': %s\n",
	      district, strerror(errno));
      exit(1);
    }
    chmod(district, 0750); /* rwxr-x--- */
    /* Create empty reports.dat with 664 */
    char rdat[256];
    snprintf(rdat, sizeof(rdat), "%s/reports.dat", district);
    int fd = open(rdat, O_WRONLY | O_CREAT | O_TRUNC, 0664);
    if (fd < 0) {
      fprintf(stderr, "ERROR: cannot create '%s': %s\n", rdat, strerror(errno));
      exit(1);
    }
    chmod(rdat, 0664);
    close(fd);

    /* Create district.cfg with severity_threshold=1 */
    char cfg[256];
    snprintf(cfg, sizeof(cfg), "%s/district.cfg", district);
    fd = open(cfg, O_WRONLY | O_CREAT | O_TRUNC, 0640);
    if (fd < 0) {
      fprintf(stderr, "ERROR: cannot create '%s': %s\n", cfg, strerror(errno));
      exit(1);
    }
    chmod(cfg, 0640);
    const char *default_cfg = "severity_threshold=1\n";
    write(fd, default_cfg, strlen(default_cfg));
    close(fd);

    /* Create logged_district with 644 */
    char log[256];
    snprintf(log, sizeof(log), "%s/logged_district", district);
    fd = open(log, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
      fprintf(stderr, "ERROR: cannot create '%s': %s\n", log, strerror(errno));
      exit(1);
    }
    chmod(log, 0644);
    close(fd);

    /* Create symlink active_reports- → /reports.dat */
    create_symlink(district);
  }
}

/* ─────────────────────────────────────────────
   Symlink helpers
   ───────────────────────────────────────────── */

void create_symlink(const char *district) {
  char link_name[256];
  char target[256];
  snprintf(link_name, sizeof(link_name), "active_reports-%s", district);
  snprintf(target, sizeof(target), "%s/reports.dat", district);

  /* Remove stale link first (ignore error if it doesn't exist) */
  unlink(link_name);

  if (symlink(target, link_name) < 0) {
    fprintf(stderr, "WARNING: cannot create symlink '%s': %s\n",
	    link_name, strerror(errno));
  }
}

void remove_symlink(const char *district) {
  char link_name[256];
  snprintf(link_name, sizeof(link_name), "active_reports-%s", district);
  unlink(link_name);
}

/* Check if a symlink is dangling (target doesn't exist) */
static int symlink_dangling(const char *link_name) {
  struct stat st;
  /* stat() follows the link; if it fails the link is dangling */
  if (stat(link_name, &st) < 0) {
    return 1;
  }
  return 0;
}

/* ─────────────────────────────────────────────
   cmd_add
   ───────────────────────────────────────────── */

void cmd_add(const char *district, const char *role, const char *user) {
  /* Both roles may add */
  ensure_district(district, role, user);

  char rdat[256];
  snprintf(rdat, sizeof(rdat), "%s/reports.dat", district);

  /* Check write permission on reports.dat (664 → group write bit S_IWGRP) */
  if (!check_permission(rdat, role, S_IWGRP, "write")) {
    exit(1);
  }

  /* Count existing records to assign next ID */
  struct stat st;
  stat(rdat, &st);
  int count = (int)(st.st_size / sizeof(Report));

  Report r;
  memset(&r, 0, sizeof(r));
  r.id = count + 1;
  strncpy(r.inspectod, user, MAX_NAME - 1);
  /* Interactive input */
  printf("=== Add New Report to District '%s' ===\n", district);
  printf("Latitude: "); scanf("%lf", &r.latitude);
  printf("Longitude: "); scanf("%lf", &r.longitude);
  printf("Category (road/lighting/flooding/...): "); scanf("%31s", r.category);
  printf("Severity (1=minor, 2=moderate, 3=critical): "); scanf("%d", &r.severity);
  /* flush newline */
  int c; while ((c = getchar()) != '\n' && c != EOF);
  printf("Description: "); fgets(r.description, MAX_DESC, stdin);
  /* strip trailing newline */
  r.description[strcspn(r.description, "\n")] = '\0';

  r.timestamp = time(NULL);

  int fd = open(rdat, O_WRONLY | O_APPEND);
  if (fd < 0) {
    fprintf(stderr, "ERROR: cannot open '%s': %s\n", rdat, strerror(errno));
    exit(1);
  }
  if (write(fd, &r, sizeof(r)) != sizeof(r)) {
    fprintf(stderr, "ERROR: write failed: %s\n", strerror(errno));
    close(fd);
    exit(1);
  }
  close(fd);
  chmod(rdat, 0664);

  printf("Report #%d added successfully.\n", r.id);

  /* Log (only managers log; inspectors skip silently for log restriction) */
  if (strcmp(role, "manager") == 0) {
    char action[128];
    snprintf(action, sizeof(action), "add report #%d in %s", r.id, district);
    log_action(district, role, user, action);
  }

  /* Refresh symlink */
  create_symlink(district);
}

/* Notificam monitorul via SIGUSR1 */
notify_monitor(district, role, user, r.id);
}

/* ─────────────────────────────────────────────
   cmd_list
   ───────────────────────────────────────────── */

void cmd_list(const char *district, const char *role, const char *user) {
  ensure_district(district, role, user);

  char rdat[256];
  snprintf(rdat, sizeof(rdat), "%s/reports.dat", district);

  /* Check read permission (664 → S_IRUSR covers both owner and group in 664) */
  if (!check_permission(rdat, role, S_IRGRP, "read")) {
    exit(1);
  }

  /* Print file metadata first */
  struct stat st;
  if (stat(rdat, &st) < 0) {
    fprintf(stderr, "ERROR: cannot stat '%s': %s\n", rdat, strerror(errno));
    exit(1);
  }
  char perm_str[10];
  mode_to_string(st.st_mode & 0777, perm_str);
  char ts[64];
  strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", localtime(&st.st_mtime));
  printf("File: %s Permissions: %s Size: %lld bytes Last modified: %s\n\n",
	 rdat, perm_str, (long long)st.st_size, ts);

  /* Check symlink status */
  char link_name[256];
  snprintf(link_name, sizeof(link_name), "active_reports-%s", district);
  {
    struct stat lst;
    if (lstat(link_name, &lst) == 0) {
      if (S_ISLNK(lst.st_mode)) {
	if (symlink_dangling(link_name)) {
	  printf("WARNING: symlink '%s' is dangling (target does not exist).\n\n",
		 link_name);
	} else {
	  printf("Symlink '%s' → %s/reports.dat (OK)\n\n",
		 link_name, district);
	}
      }
    }
  }

  int fd = open(rdat, O_RDONLY);
  if (fd < 0) {
    fprintf(stderr, "ERROR: cannot open '%s': %s\n", rdat, strerror(errno));
    exit(1);
  }

  Report r;
  int count = 0;
  printf("%-4s %-16s %-10s %-10s %-12s %-8s %s\n",
	 "ID", "Inspector", "Lat", "Lon", "Category", "Severity", "Timestamp");
  printf("-------------------------------------------------------------------------------------\n");
  while (read(fd, &r, sizeof(r)) == sizeof(r)) {
    char ts2[32];
    strftime(ts2, sizeof(ts2), "%Y-%m-%d %H:%M", localtime(&r.timestamp));
    printf("%-4d %-16s %-10.4f %-10.4f %-12s %-8d %s\n",
	   r.id, r.inspectod, r.latitude, r.longitude,
	   r.category, r.severity, ts2);
    count++;
  }
  close(fd);

  if (count == 0) printf("(no reports)\n");
  else printf("\nTotal: %d report(s).\n", count);
}

/* ─────────────────────────────────────────────
   cmd_view
   ───────────────────────────────────────────── */

void cmd_view(const char *district, int report_id, const char *role, const char *user) {
  ensure_district(district, role, user);

  char rdat[256];
  snprintf(rdat, sizeof(rdat), "%s/reports.dat", district);

  if (!check_permission(rdat, role, S_IRGRP, "read")) {
    exit(1);
  }

  int fd = open(rdat, O_RDONLY);
  if (fd < 0) {
    fprintf(stderr, "ERROR: cannot open '%s': %s\n", rdat, strerror(errno));
    exit(1);
  }

  Report r;
  int found = 0;
  while (read(fd, &r, sizeof(r)) == sizeof(r)) {
    if (r.id == report_id) {
      found = 1;
      break;
    }
  }
  close(fd);

  if (!found) {
    fprintf(stderr, "ERROR: report #%d not found in district '%s'.\n", report_id, district);
    exit(1);
  }

  char ts[64];
  strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", localtime(&r.timestamp));
  printf("=== Report #%d ===\n", r.id);
  printf("Inspector : %s\n", r.inspectod);
  printf("GPS : %.6f, %.6f\n", r.latitude, r.longitude);
  printf("Category : %s\n", r.category);
  printf("Severity : %d (%s)\n", r.severity,
	 r.severity == 1 ? "minor" : r.severity == 2 ? "moderate" : "critical");
  printf("Timestamp : %s\n", ts);
  printf("Description: %s\n", r.description);
}

/* ─────────────────────────────────────────────
   cmd_remove_report (manager only)
   ───────────────────────────────────────────── */

void cmd_remove_report(const char *district, int report_id, const char *role, const char *user) {
  if (strcmp(role, "manager") != 0) {
    fprintf(stderr, "ERROR: only managers can remove reports.\n");
    exit(1);
  }

  ensure_district(district, role, user);

  char rdat[256];
  snprintf(rdat, sizeof(rdat), "%s/reports.dat", district);

  if (!check_permission(rdat, role, S_IWUSR, "write")) {
    exit(1);
  }

  int fd = open(rdat, O_RDWR);
  if (fd < 0) {
    fprintf(stderr, "ERROR: cannot open '%s': %s\n", rdat, strerror(errno));
    exit(1);
  }

  /* Count records */
  struct stat st;
  fstat(fd, &st);
  int count = (int)(st.st_size / sizeof(Report));

  if (count == 0) {
    fprintf(stderr, "ERROR: no reports to remove.\n");
    close(fd);
    exit(1);
  }
  /* Find the index of the report */
  int idx = -1;
  Report r;
  for (int i = 0; i < count; i++) {
    lseek(fd, (off_t)(i * sizeof(Report)), SEEK_SET);
    read(fd, &r, sizeof(r));
    if (r.id == report_id) { idx = i; break; }
  }

  if (idx < 0) {
    fprintf(stderr, "ERROR: report #%d not found.\n", report_id);
    close(fd);
    exit(1);
  }

  /* Shift all subsequent records one position earlier */
  for (int i = idx + 1; i < count; i++) {
    lseek(fd, (off_t)(i * sizeof(Report)), SEEK_SET);
    read(fd, &r, sizeof(r));
    lseek(fd, (off_t)((i - 1) * sizeof(Report)), SEEK_SET);
    write(fd, &r, sizeof(r));
  }

  /* Truncate file */
  off_t new_size = (off_t)((count - 1) * sizeof(Report));
  ftruncate(fd, new_size);
  close(fd);

  printf("Report #%d removed from district '%s'.\n", report_id, district);

  char action[128];
  snprintf(action, sizeof(action), "remove report #%d in %s", report_id, district);
  log_action(district, role, user, action);

  create_symlink(district);
}

/* ─────────────────────────────────────────────
   cmd_update_threshold (manager only)
   ───────────────────────────────────────────── */

void cmd_update_threshold(const char *district, int value, const char *role, const char *user) {
  if (strcmp(role, "manager") != 0) {
    fprintf(stderr, "ERROR: only managers can update the severity threshold.\n");
    exit(1);
  }

  ensure_district(district, role, user);

  char cfg[256];
  snprintf(cfg, sizeof(cfg), "%s/district.cfg", district);

  /* Verify permission bits are exactly 640 */
  struct stat st;
  if (stat(cfg, &st) < 0) {
    fprintf(stderr, "ERROR: cannot stat '%s': %s\n", cfg, strerror(errno));
    exit(1);
  }
  if ((st.st_mode & 0777) != 0640) {
    char perm_str[10];
    mode_to_string(st.st_mode & 0777, perm_str);
    fprintf(stderr,
	    "ERROR: '%s' has permissions %s (expected 640). Refusing to write.\n",
	    cfg, perm_str);
    exit(1);
  }

  int fd = open(cfg, O_WRONLY | O_TRUNC);
  if (fd < 0) {
    fprintf(stderr, "ERROR: cannot open '%s': %s\n", cfg, strerror(errno));
    exit(1);
  }
  char buf[64];
  int len = snprintf(buf, sizeof(buf), "severity_threshold=%d\n", value);
  write(fd, buf, len);
  close(fd);
  chmod(cfg, 0640);

  printf("Severity threshold for '%s' updated to %d.\n", district, value);

  char action[128];
  snprintf(action, sizeof(action), "update_threshold=%d in %s", value, district);
  log_action(district, role, user, action);
}

/* ─────────────────────────────────────────────
AI-assisted functions: parse_condition / match_condition
(Generated with AI assistance, reviewed and adapted by student)
  ───────────────────────────────────────────── */

/*
 * parse_condition - splits "field:op:value" into its three parts.
 *
 * Returns 1 on success, 0 on failure.
 * field, op, value must point to buffers of at least 64 bytes each.
 *
 * Note: The AI initially generated this without handling multi-character
 * operators (>=, <=, !=). The student fixed the tokenisation to use
 * strstr/strchr on the colon delimiter rather than strtok, because
 * strtok collapsed multiple colons and broke ">=", "<=", "!=" when the
 * value itself contained a colon. Final version uses manual pointer
 * arithmetic as reviewed below.
 */
  int parse_condition(const char *input, char *field, char *op, char *value) {
  if (!input || !field || !op || !value) return 0;

  /* Find first colon → separates field from op */
  const char *p1 = strchr(input, ':');
  if (!p1) return 0;

  /* Find second colon → separates op from value */
  const char *p2 = strchr(p1 + 1, ':');
  if (!p2) return 0;

  /* Extract field */
  size_t flen = (size_t)(p1 - input);
  if (flen == 0 || flen >= 64) return 0;
  strncpy(field, input, flen);
  field[flen] = '\0';

  /* Extract op */
  size_t olen = (size_t)(p2 - (p1 + 1));
  if (olen == 0 || olen >= 64) return 0;
  strncpy(op, p1 + 1, olen);
  op[olen] = '\0';

  /* Extract value (rest of string) */
  size_t vlen = strlen(p2 + 1);
  if (vlen == 0 || vlen >= 64) return 0;
  strncpy(value, p2 + 1, 63);
  value[63] = '\0';

  return 1;
}

/*
 * match_condition - returns 1 if record *r satisfies field op value, 0 otherwise.
 *
 * Supported fields: severity, category, inspector, timestamp
 * Supported ops: ==, !=, <, <=, >, >=
 *
 * Note: The AI-generated version treated all comparisons as string
 * comparisons (strcmp). The student corrected it to convert the value
 * to an integer for 'severity' and 'timestamp' before comparing,
 * because numeric ordering differs from lexicographic ordering
 * (e.g. "9" > "10" lexicographically but 9 < 10 numerically).
 */
int match_condition(Report *r, const char *field, const char *op, const char *value) {
  if (!r || !field || !op || !value) return 0;

  if (strcmp(field, "severity") == 0) {
    int threshold = atoi(value);
    int sev = r->severity;
    if (strcmp(op, "==") == 0) return sev == threshold;
    if (strcmp(op, "!=") == 0) return sev != threshold;
    if (strcmp(op, "<") == 0) return sev < threshold;
    if (strcmp(op, "<=") == 0) return sev <= threshold;
    if (strcmp(op, ">") == 0) return sev > threshold;
    if (strcmp(op, ">=") == 0) return sev >= threshold;
    return 0;
  }

  if (strcmp(field, "timestamp") == 0) {
    time_t threshold = (time_t)atol(value);
    time_t ts = r->timestamp;
    if (strcmp(op, "==") == 0) return ts == threshold;
    if (strcmp(op, "!=") == 0) return ts != threshold;
    if (strcmp(op, "<") == 0) return ts < threshold;
    if (strcmp(op, "<=") == 0) return ts <= threshold;
    if (strcmp(op, ">") == 0) return ts > threshold;
    if (strcmp(op, ">=") == 0) return ts >= threshold;
    return 0;
  }

  if (strcmp(field, "category") == 0) {
    int cmp = strcmp(r->category, value);
    if (strcmp(op, "==") == 0) return cmp == 0;
    if (strcmp(op, "!=") == 0) return cmp != 0;
    if (strcmp(op, "<") == 0) return cmp < 0;
    if (strcmp(op, "<=") == 0) return cmp <= 0;
    if (strcmp(op, ">") == 0) return cmp > 0;
    if (strcmp(op, ">=") == 0) return cmp >= 0;
    return 0;
  }

  if (strcmp(field, "inspector") == 0) {
    int cmp = strcmp(r->inspectod, value);
    if (strcmp(op, "==") == 0) return cmp == 0;
    if (strcmp(op, "!=") == 0) return cmp != 0;
    if (strcmp(op, "<") == 0) return cmp < 0;
    if (strcmp(op, "<=") == 0) return cmp <= 0;
    if (strcmp(op, ">") == 0) return cmp > 0;
    if (strcmp(op, ">=") == 0) return cmp >= 0;
    return 0;
  }

  fprintf(stderr, "WARNING: unknown filter field '%s'\n", field);
  return 0;
}

/* ─────────────────────────────────────────────
   notify_monitor - trimite SIGUSR1 către monitor
   ───────────────────────────────────────────── */

void notify_monitor(const char *district, const char *role, const char *user, int report_id) {
  /* Citim PID-ul din .monitor_pid */
  int fd = open(".monitor_pid", O_RDONLY);
  if (fd < 0) {
    /* Fisierul nu exista - monitorul nu ruleaza */
    char log_path[256];
    snprintf(log_path, sizeof(log_path), "%s/logged_district", district);
    if (strcmp(role, "manager") == 0) {
      int lfd = open(log_path, O_WRONLY | O_APPEND | O_CREAT, 0644);
      if (lfd >= 0) {
	time_t now = time(NULL);
	char ts[64];
	strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", localtime(&now));
	char entry[512];
	int len = snprintf(entry, sizeof(entry),
			   "[%s] role=%s user=%s action=notify_monitor report #%d: monitor could not be informed (no .monitor_pid)\n",
			   ts, role, user, report_id);
	write(lfd, entry, len);
	close(lfd);
      }
    }
    fprintf(stderr, "WARNING: monitor not running (.monitor_pid not found). Could not notify.\n");
    return;
  }
  char buf[32];
  int n = read(fd, buf, sizeof(buf) - 1);
  close(fd);
  if (n <= 0) {
    fprintf(stderr, "WARNING: .monitor_pid is empty. Could not notify monitor.\n");
    return;
  }
  buf[n] = '\0';
  pid_t monitor_pid = (pid_t)atoi(buf);
  if (monitor_pid <= 0) {
    fprintf(stderr, "WARNING: invalid PID in .monitor_pid. Could not notify monitor.\n");
    return;
  }

  /* Trimitem SIGUSR1 */
  if (kill(monitor_pid, SIGUSR1) < 0) {
    fprintf(stderr, "WARNING: could not send SIGUSR1 to monitor (PID %d): %s\n",
	    monitor_pid, strerror(errno));
    /* Scriem in log ca notificarea a esuat */
    if (strcmp(role, "manager") == 0) {
      char log_path[256];
      snprintf(log_path, sizeof(log_path), "%s/logged_district", district);
      int lfd = open(log_path, O_WRONLY | O_APPEND | O_CREAT, 0644);
      if (lfd >= 0) {
	time_t now = time(NULL);
	char ts[64];
	strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", localtime(&now));
	char entry[512];
	int len = snprintf(entry, sizeof(entry),
			   "[%s] role=%s user=%s action=notify_monitor report #%d: signal failed (PID %d: %s)\n",
			   ts, role, user, report_id, monitor_pid, strerror(errno));
	write(lfd, entry, len);
	close(lfd);
      }
    }
    return;
  } 
  printf("Monitor (PID %d) notified of new report #%d.\n", monitor_pid, report_id);

  /* Scriem in log ca notificarea a reusit */
  if (strcmp(role, "manager") == 0) {
    char log_path[256];
    snprintf(log_path, sizeof(log_path), "%s/logged_district", district);
    int lfd = open(log_path, O_WRONLY | O_APPEND | O_CREAT, 0644);
    if (lfd >= 0) {
      time_t now = time(NULL);
      char ts[64];
      strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", localtime(&now));
      char entry[512];
      int len = snprintf(entry, sizeof(entry),
			 "[%s] role=%s user=%s action=notify_monitor report #%d: monitor (PID %d) notified successfully\n",
			 ts, role, user, report_id, monitor_pid);
      write(lfd, entry, len);
      close(lfd);
    }
  }
}


/* ─────────────────────────────────────────────
   cmd_remove_district (manager only)
   ───────────────────────────────────────────── */

void cmd_remove_district(const char *district, const char *role, const char *user) {
  (void)user;
  if (strcmp(role, "manager") != 0) {
    fprintf(stderr, "ERROR: only managers can remove districts.\n");
    exit(1);
  }

  /* Verificam ca districtul exista */
  struct stat st;
  if (stat(district, &st) < 0) {
    fprintf(stderr, "ERROR: district '%s' does not exist.\n", district);
    exit(1);
  }
  if (!S_ISDIR(st.st_mode)) {
    fprintf(stderr, "ERROR: '%s' is not a directory.\n", district);
    exit(1);
  }
  /* Stergem symlink-ul inainte de director */
  remove_symlink(district);

  /* fork() + exec() pentru rm -rf */
  pid_t pid = fork();
  if (pid < 0) {
    fprintf(stderr, "ERROR: fork failed: %s\n", strerror(errno));
    exit(1);
  }

  if (pid == 0) {
    /* Proces copil: executam rm -rf */
    /* Construim argumentele explicit - fara shell, fara riscuri */
    char district_copy[256];
    strncpy(district_copy, district, sizeof(district_copy) - 1);
    district_copy[sizeof(district_copy) - 1] = '\0';

    execlp("rm", "rm", "-rf", district_copy, (char *)NULL);
    /* Daca ajungem aici, exec a esuat */
    fprintf(stderr, "ERROR: exec rm failed: %s\n", strerror(errno));
    exit(1);
  }  
  /* Proces parinte: asteptam copilul */
  int status;
  waitpid(pid, &status, 0);

  if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
    printf("District '%s' removed successfully.\n", district);
    /* Log actiunea */
    /* Nu mai putem scrie in logged_district (a fost sters) - afisam doar mesaj */
  } else {
    fprintf(stderr, "ERROR: rm -rf failed for district '%s'.\n", district);
    exit(1);
  }
}

/* ─────────────────────────────────────────────
   cmd_filter
   ───────────────────────────────────────────── */

void cmd_filter(const char *district, int ncond, char **conditions,
		const char *role, const char *user) {
  ensure_district(district, role, user);

  char rdat[256];
  snprintf(rdat, sizeof(rdat), "%s/reports.dat", district);

  if (!check_permission(rdat, role, S_IRGRP, "read")) {
    exit(1);
  }

  /* Parse all conditions */
  char fields[16][64], ops[16][64], values[16][64];
  for (int i = 0; i < ncond; i++) {
    if (!parse_condition(conditions[i], fields[i], ops[i], values[i])) {
      fprintf(stderr, "ERROR: invalid condition '%s'\n", conditions[i]);
      exit(1);
    }
  }

  int fd = open(rdat, O_RDONLY);
  if (fd < 0) {
    fprintf(stderr, "ERROR: cannot open '%s': %s\n", rdat, strerror(errno));
    exit(1);
  }

  Report r;
  int printed = 0;
  printf("%-4s %-16s %-10s %-10s %-12s %-8s %s\n",
	 "ID", "Inspector", "Lat", "Lon", "Category", "Severity", "Timestamp");
  printf("-------------------------------------------------------------------------------------\n");
  while (read(fd, &r, sizeof(r)) == sizeof(r)) {
    int match = 1;
    for (int i = 0; i < ncond; i++) {
      if (!match_condition(&r, fields[i], ops[i], values[i])) {
	match = 0;
	break;
      }
    }
    if (match) {
      char ts[32];
      strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M", localtime(&r.timestamp));
      printf("%-4d %-16s %-10.4f %-10.4f %-12s %-8d %s\n",
	     r.id, r.inspectod, r.latitude, r.longitude,
	     r.category, r.severity, ts);
      printed++;
    }
  }
  close(fd);

  if (printed == 0) printf("(no matching reports)\n");
  else printf("\nMatched: %d report(s).\n", printed);
}


//MAIN


int main(int argc, char *argv[])
{
  if (argc < 2) {
    fprintf(stderr,
	    "Usage: %s --role --user -- [args...]\n"
	    " Roles : inspectod | manager\n"
	    " Commands: --add \n"
	    " --list \n"
	    " --view \n"
	    " --remove_report \n"
	    " --update_threshold \n"
	    " --filter [cond2 ...]\n",
	    argv[0]);
    return 1;
  }

  /* ── Parse global flags ── */
  const char *role = NULL;
  const char *user = NULL;
  const char *command = NULL;
  /* Remaining positional args after command */
  char **cmd_args = NULL;
  int cmd_argc = 0;

  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "--role") == 0 && i + 1 < argc) {
      role = argv[++i];
    } else if (strcmp(argv[i], "--user") == 0 && i + 1 < argc) {
      user = argv[++i];
    } else if (argv[i][0] == '-' && argv[i][1] == '-' && !command) {
      command = argv[i] + 2; /* skip "--" */
      cmd_args = &argv[i + 1];
      cmd_argc = argc - i - 1;
      break;
    }
  }

  /* Validate role */
  if (!role || (strcmp(role, "inspector") != 0 && strcmp(role, "manager") != 0)) {
    fprintf(stderr, "ERROR: --role must be 'inspector' or 'manager'.\n");
    return 1;
  }
  if (!user || strlen(user) == 0) {
    fprintf(stderr, "ERROR: --user is required.\n");
    return 1;
  }
  if (!command) {
    fprintf(stderr, "ERROR: no command specified.\n");
    return 1;
  }

  /* ── Dispatch ── */
  if (strcmp(command, "add") == 0) {
    if (cmd_argc < 1) { fprintf(stderr, "ERROR: --add requires \n"); return 1; }
    cmd_add(cmd_args[0], role, user);

  } else if (strcmp(command, "list") == 0) {
    if (cmd_argc < 1) { fprintf(stderr, "ERROR: --list requires \n"); return 1; }
    cmd_list(cmd_args[0], role, user);

  } else if (strcmp(command, "view") == 0) {
    if (cmd_argc < 2) { fprintf(stderr, "ERROR: --view requires \n"); return 1; }
    cmd_view(cmd_args[0], atoi(cmd_args[1]), role, user);

  } else if (strcmp(command, "remove_report") == 0) {
    if (cmd_argc < 2) { fprintf(stderr, "ERROR: --remove_report requires \n"); return 1; }
    cmd_remove_report(cmd_args[0], atoi(cmd_args[1]), role, user);

  } else if (strcmp(command, "update_threshold") == 0) {
    if (cmd_argc < 2) { fprintf(stderr, "ERROR: --update_threshold requires \n"); return 1; }
    cmd_update_threshold(cmd_args[0], atoi(cmd_args[1]), role, user);

  } else if (strcmp(command, "filter") == 0) {
    if (cmd_argc < 2) { fprintf(stderr, "ERROR: --filter requires \n"); return 1; }
    cmd_filter(cmd_args[0], cmd_argc - 1, &cmd_args[1], role, user);

  }
  else if (strcmp(command, "remove_district") == 0) {
    if (cmd_argc < 1) { fprintf(stderr, "ERROR: --remove_district requires \n"); return 1; }
    cmd_remove_district(cmd_args[0], role, user);

  }else {
    fprintf(stderr, "ERROR: unknown command '--%s'\n", command);
    return 1;
  }
  
  return 0;
}
