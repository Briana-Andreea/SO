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

static int pipe_write_fd = -1; /* fd pentru pipe catre hub_mon (Faza 3) */

static void handle_sigusr1(int signo) {
  (void)signo;
  const char *msg = "[monitor] New report has been added to a district.\n";
  if (pipe_write_fd >= 0) {
    write(pipe_write_fd, msg, strlen(msg));
  } else {
    write(STDOUT_FILENO, msg, strlen(msg));
  }
}

static volatile sig_atomic_t keep_running = 1;

static void handle_sigint(int signo) {
  (void)signo;
  keep_running = 0;
  const char *msg = "[monitor] SIGINT received. Shutting down...\n";
  if (pipe_write_fd >= 0) {
    write(pipe_write_fd, msg, strlen(msg));
  } else {
    write(STDOUT_FILENO, msg, strlen(msg));
  }
}

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
}

static void remove_pid_file(void) {
  unlink(PID_FILE);
}

/* Verifica daca un monitor ruleaza deja */
static pid_t check_existing_monitor(void) {
  int fd = open(PID_FILE, O_RDONLY);
  if (fd < 0) return -1;
  char buf[32];
  int n = read(fd, buf, sizeof(buf) - 1);
  close(fd);
  if (n <= 0) return -1;
  buf[n] = '\0';
  pid_t pid = (pid_t)atoi(buf);
  /* Verificam daca procesul exista cu kill(pid, 0) */
  if (pid > 0 && kill(pid, 0) == 0) return pid;
  return -1;
}

int main(int argc, char *argv[]) {
  /* Daca e apelat cu argument (fd pentru pipe), il retinem */
  if (argc >= 2) {
    pipe_write_fd = atoi(argv[1]);
  }

  /* Verifica daca un monitor ruleaza deja */
  pid_t existing = check_existing_monitor();
  if (existing > 0) {
    char msg[128];
    int len = snprintf(msg, sizeof(msg),
      "[monitor] ERROR: monitor already running with PID %d\n", existing);
    if (pipe_write_fd >= 0) {
      write(pipe_write_fd, msg, len);
    } else {
      write(STDOUT_FILENO, msg, len);
    }
    if (pipe_write_fd >= 0) close(pipe_write_fd);
    return 1;
  }

  write_pid_file();

  char start_msg[64];
  int slen = snprintf(start_msg, sizeof(start_msg),
    "[monitor] Started. PID = %d\n", (int)getpid());
  if (pipe_write_fd >= 0) {
    write(pipe_write_fd, start_msg, slen);
  } else {
    write(STDOUT_FILENO, start_msg, slen);
  }

  struct sigaction sa_usr1;
  memset(&sa_usr1, 0, sizeof(sa_usr1));
  sa_usr1.sa_handler = handle_sigusr1;
  sigemptyset(&sa_usr1.sa_mask);
  sa_usr1.sa_flags = SA_RESTART;
  sigaction(SIGUSR1, &sa_usr1, NULL);

  struct sigaction sa_int;
  memset(&sa_int, 0, sizeof(sa_int));
  sa_int.sa_handler = handle_sigint;
  sigemptyset(&sa_int.sa_mask);
  sa_int.sa_flags = 0;
  sigaction(SIGINT, &sa_int, NULL);

  while (keep_running) {
    pause();
  }

  remove_pid_file();

  const char *stop_msg = "[monitor] Stopped.\n";
  if (pipe_write_fd >= 0) {
    write(pipe_write_fd, stop_msg, strlen(stop_msg));
    close(pipe_write_fd);
  } else {
    write(STDOUT_FILENO, stop_msg, strlen(stop_msg));
  }

  return 0;
}
