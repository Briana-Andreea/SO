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

int main(int argc, char *argv[])
{
  if (argc < 2) {
    fprintf(stderr,
	    "Usage: %s --role --user -- [args...]\n"
	    " Roles : inspector | manager\n"
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
  return 0;
}
