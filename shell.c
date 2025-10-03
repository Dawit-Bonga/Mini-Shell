// Implement your shell in this source file.
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#define LINE_MAX 80
int last_status = 0;
#define MAX_HISTORY 80
// static int next_job_id = 1;

typedef struct {
  pid_t pid;
  char command[LINE_MAX];
  int job_id;
  int status; // running, stopped, completed
} job_t;

enum { JOB_RUNNING = 0, JOB_STOPPED = 1, JOB_DONE = 2 };

static char history_container[MAX_HISTORY][LINE_MAX + 1];
static int history_count = 0;

int have_zombies = 0; // set when a child changes state
pid_t fg_pid = 0;

job_t jobs[5];
int job_count = 0;

void add_to_history(const char *cmd) {
  if (history_count < MAX_HISTORY) {
    strncpy(history_container[history_count], cmd, LINE_MAX);
    history_container[history_count][LINE_MAX] = '\0';
    history_count++;
  } else {
    // Shift history up and add new command
    for (int i = 0; i < MAX_HISTORY - 1; i++) {
      strcpy(history_container[i], history_container[i + 1]);
    }
    strncpy(history_container[MAX_HISTORY - 1], cmd, LINE_MAX);
    history_container[MAX_HISTORY - 1][LINE_MAX] = '\0';
  }
}

int builtin_history(int argc, char *argv[]) {
  (void)argc;
  (void)argv;

  if (history_count == 0) {
    printf("No commands in history.\n");
    return 0;
  }

  for (int i = 0; i < history_count; i++) {
    printf("%4d  %s\n", i + 1, history_container[i]);
  }
  return 0;
}

void my_signal_handler(int sig) {
  if (sig == SIGINT) {
    // Ctrl-C: best-effort cleanup
    if (fg_pid > 0) {
      kill(fg_pid, SIGTERM);
    }
    for (int i = 0; i < job_count; i++) {
      if (jobs[i].pid > 0)
        kill(jobs[i].pid, SIGTERM);
    }
    // async-signal-safe print
    write(STDOUT_FILENO, "\nmini-shell terminated\n", 22);
    _exit(0); // exit immediately from signal context
  } else if (sig == SIGCHLD) {
    // child changed state; do real work outside the handler
    have_zombies = 1;
  }
}

static int builtin_help(int argc, char *argv[]) {
  (void)argc;
  (void)argv;
  puts("Built-in commands:\n"
       "  cd [dir]    Change directory\n"
       "  pwd         Print current working directory\n"
       "  exit [n]    Exit the shell with status n (default: last status)\n"
       "  help        Show this help message\n"
       "  jobs        (stub) List background or stopped jobs\n"
       "  fg          (stub) Move a background job into the foreground\n"
       "  bg          (stub) Resume a stopped job in the background\n"
       "  history     (stub) Show command history\n");
  return 0;
}

int builtin_exit(int argc, char *argv[]) {
  int code = last_status; // default exit code

  if (argc >= 2) {
    char *end;
    long val = strtol(argv[1], &end, 10);
    if (*end != '\0') {
      fprintf(stderr, "exit: numeric argument required\n");
      code = 2; // convention: error
    } else {
      code = (int)(val & 0xFF); // shell exit codes are 0-255
    }
  }

  exit(code); // terminates the shell process
}

int builtin_cd(int argc, char *argv[]) {
  const char *target;

  if (argc < 2) { // "cd" with no path
    target = getenv("HOME");
    if (!target)
      target = "/";
  } else {
    target = argv[1];
  }

  if (chdir(target) != 0) {
    fprintf(stderr, "cd: %s: %s\n", target, strerror(errno));
    return 1;
  }
  return 0;
}

int builtin_pwd(int argc, char *argv[]) {
  char output[4096];
  if (getcwd(output, sizeof(output)) == NULL) {
    printf("Error: please use correct");
    return 1;
  }

  printf("%s\n", output);
  return 0;
}

static int try_builtin(size_t argc, char *argv[]) {
  if (argc == 0)
    return 1; // nothing to do, but "handled"
  if (strcmp(argv[0], "exit") == 0) {
    builtin_exit((int)argc, argv);
    return 1;
  }
  if (strcmp(argv[0], "cd") == 0) {
    last_status = builtin_cd((int)argc, argv);
    return 1;
  }
  if (strcmp(argv[0], "pwd") == 0) {
    last_status = builtin_pwd((int)argc, argv);
    return 1;
  }
  if (strcmp(argv[0], "help") == 0) {
    last_status = builtin_help((int)argc, argv);
    return 1;
  }
  // Stubs for assignment completeness; not implemented here
  if (strcmp(argv[0], "jobs") == 0) {
    puts("(jobs not implemented yet)");
    last_status = 0;
    return 1;
  }
  if (strcmp(argv[0], "fg") == 0) {
    puts("(fg not implemented yet)");
    last_status = 0;
    return 1;
  }
  if (strcmp(argv[0], "bg") == 0) {
    puts("(bg not implemented yet)");
    last_status = 0;
    return 1;
  }
  if (strcmp(argv[0], "history") == 0) {
    last_status = builtin_history((int)argc, argv);
    return 1;
  }

  return 0; // not a built-in
}

int main(int argc, char **argv) {
  // Please leave in this line as the first statement in your program.
  alarm(120); // This will terminate your shell after 120 seconds,
              // and is useful in the case that you accidently create a 'fork
              // bomb'

  signal(SIGINT, my_signal_handler);
  signal(SIGCHLD, my_signal_handler);

  // Avoid the shell itself being stopped by terminal I/O signals
  signal(SIGTTOU, SIG_IGN);
  signal(SIGTTIN, SIG_IGN);
  char line[LINE_MAX + 2];
  int interactive = isatty(STDIN_FILENO);

  for (;;) { // infinite loop
    if (interactive) {
      fputs("mini-shell> ", stdout);
      fflush(stdout);
    }

    if (!fgets(line, sizeof(line), stdin)) {
      if (interactive) {
        fputc('\n', stdout);
      }
      break;
    }

    size_t n = strlen(line);
    if (n && line[n - 1] == '\n') {
      line[n - 1] = '\0';
    }

    if (line[0] == '\0') {
      continue;
    }

    if (line[0] != '\0') {
      add_to_history(line);
    }

    char *args[64];
    size_t count = 0; // num of tokens kinda
    char *token = strtok(line, " \t");
    while (token && count < 63) {
      args[count++] = token;
      token = strtok(NULL, " \t");
    }

    args[count] = NULL;

    if (try_builtin(count, args)) {
      continue;
    }

    pid_t pid = fork();
    if (pid < 0) {
      perror("fork");
      last_status = 1;
      continue;
    }

    if (pid == 0) {
      // CHILD: restore default handling so Ctrl-C/Z affect the child, not
      // shell
      signal(SIGINT, SIG_DFL);
      signal(SIGTSTP, SIG_DFL);

      execvp(args[0], args); // search PATH and exec
      // If we’re here, exec failed
      fprintf(stderr,
              "mini-shell>Command not found--Did you mean something else?\n");
      _exit(127);
    } else {
      // PARENT: wait for the child to finish (or be stopped)
      int status = 0;
      if (waitpid(pid, &status, WUNTRACED) < 0) {
        last_status = 1;
        continue;
      }

      if (WIFEXITED(status)) {
        last_status = WEXITSTATUS(status);
      } else if (WIFSIGNALED(status)) {
        last_status = 128 + WTERMSIG(status); // shell convention
      } else if (WIFSTOPPED(status)) {
        // For now (pre-jobs), treat a stop like a signal status
        last_status = 128 + WSTOPSIG(status);
      }
    }
  }
  return 0;
}
