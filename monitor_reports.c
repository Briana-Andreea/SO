/*
* monitor_reports.c
* SO/SO1/OS Project - Phase 2: Processes and Signals
*
* Program de monitorizare pentru city_manager.
*
* Comportament:
* - La pornire: scrie PID-ul sau in fisierul .monitor_pid
* - La primirea SIGUSR1: afiseaza mesaj ca un raport nou a fost adaugat
* - La primirea SIGINT (Ctrl+C): afiseaza mesaj de oprire si sterge .monitor_pid
* - Ruleaza continuu pana primeste SIGINT
*
* Utilizare:
* ./monitor_reports
*/

#include<stdio.h>
#include<stdlib.h>
#include<string.h>
#include<unistd.h>
#include<signal.h>
#include<fcntl.h>
#include<errno.h>
#include<sys/types.h>
#include<time.h>

#define PID_FILE ".monitor_pid"

/* ─────────────────────────────────────────────
   Handler pentru SIGUSR1 - raport nou adaugat
   ───────────────────────────────────────────── */
static void handle_sigusr1(int signo) {
  (void)signo;
  /* write() este signal-safe, printf nu este */
  const char *msg = "[monitor] New report has been added to a district.\n";
  write(STDOUT_FILENO, msg, strlen(msg));
}

/* ─────────────────────────────────────────────
   Handler pentru SIGINT - oprire monitor
   ───────────────────────────────────────────── */
static volatile sig_atomic_t keep_running = 1;

static void handle_sigint(int signo) {
  (void)signo;
  keep_running = 0;
  const char *msg = "[monitor] SIGINT received. Shutting down...\n";
  write(STDOUT_FILENO, msg, strlen(msg));
}
/* ─────────────────────────────────────────────
   Scrie PID-ul in .monitor_pid
   ───────────────────────────────────────────── */
static void write_pid_file(void) {
  int fd = open(PID_FILE, O_WRONLY | O_CREAT | O_TRUNC, 0644);
  if (fd < 0) {
    fprintf(stderr, "ERROR: cannot create %s: %s\n", PID_FILE, strerror(errno));
    exit(1);
  }
  char buf[32];
  int len = snprintf(buf, sizeof(buf), "%d\n", (int)getpid());
  write(fd, buf, len);
  close(fd);
  printf("[monitor] Started. PID = %d written to %s\n", (int)getpid(), PID_FILE);
}

/* ─────────────────────────────────────────────
   Sterge .monitor_pid la iesire
   ───────────────────────────────────────────── */
static void remove_pid_file(void) {
  if (unlink(PID_FILE) < 0) {
    fprintf(stderr, "WARNING: could not remove %s: %s\n", PID_FILE, strerror(errno));
  } else {
    printf("[monitor] %s removed.\n", PID_FILE);
  }
}

/* ─────────────────────────────────────────────
   main
   ───────────────────────────────────────────── */
int main(void) {
  /* Scriem PID-ul in fisier */
  write_pid_file();

  /* Configuram handlerul pentru SIGUSR1 cu sigaction (nu signal()) */
  struct sigaction sa_usr1;
  memset(&sa_usr1, 0, sizeof(sa_usr1));
  sa_usr1.sa_handler = handle_sigusr1;
  sigemptyset(&sa_usr1.sa_mask);
  sa_usr1.sa_flags = SA_RESTART; /* restart syscalls intrerupte */
  if (sigaction(SIGUSR1, &sa_usr1, NULL) < 0) {
    fprintf(stderr, "ERROR: sigaction SIGUSR1 failed: %s\n", strerror(errno));
    remove_pid_file();
    exit(1);
  }

  /* Configuram handlerul pentru SIGINT */
  struct sigaction sa_int;
  memset(&sa_int, 0, sizeof(sa_int));
  sa_int.sa_handler = handle_sigint;
  sigemptyset(&sa_int.sa_mask);
  sa_int.sa_flags = 0;
  if (sigaction(SIGINT, &sa_int, NULL) < 0) {
    fprintf(stderr, "ERROR: sigaction SIGINT failed: %s\n", strerror(errno));
    remove_pid_file();
    exit(1);
  }

  printf("[monitor] Waiting for signals. Press Ctrl+C to stop.\n");

  /* Bucla principala: asteptam semnale */
  while (keep_running) {
    pause(); /* suspenda procesul pana vine un semnal */
  }

  /* Curatam la iesire */
  remove_pid_file();
  printf("[monitor] Stopped.\n");

  return 0;
}
