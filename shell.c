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

typedef struct {
  pid_t pid;
  char command[LINE_MAX];
  int job_id;
  int status; // running, stopped, completed
} job_t;

// static char history_container[MAX_HISTORY][LINE_MAX + 1];
// static int history_count = 0;

job_t jobs[5];
int job_count = 0;

void signal_handler(int sig) {
  if (sig == SIGINT) {
    printf("\nmini-shell terminated\n");
    // Kill all child processes
    for (int i = 0; i < job_count; i++) {
      kill(jobs[i].pid, SIGTERM);
    }
    exit(0);
  }
}

void signal_handler_setup() {}

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

int builtin_help(int argc, char *argv[]) { return 0; }

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

int main(int argc, char **argv) {
  // Please leave in this line as the first statement in your program.
  alarm(120); // This will terminate your shell after 120 seconds,
              // and is useful in the case that you accidently create a 'fork
              // bomb'
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

    char *args[64];
    size_t count = 0; // num of tokens kinda
    char *token = strtok(line, " \t");
    while (token && count < 63) {
      args[count++] = token;
      token = strtok(NULL, " \t");
    }

    args[count] = NULL;

    if (count > 0 && (strcmp(args[0], "pwd") == 0)) {
      builtin_pwd(count, args);
      continue;
    }

    if (count > 0 && (strcmp(args[0], "cd") == 0)) {
      builtin_cd(count, args);
      continue;
    }

    if (count > 0 && strcmp(args[0], "exit") == 0) {
      builtin_exit((int)count, args);
      // no continue needed — exit() never returns
    }

    if (count > 0 && strcmp(args[0], "help") == 0) {
      last_status = 0;
      printf("Built-in commands:\n"
             "  cd     Change directory\n"
             "  pwd    Print current working directory\n"
             "  exit   Exit the shell\n"
             "  help   Show this help message\n"
             "  jobs   List background or stopped jobs\n"
             "  fg     Move a background job into the foreground\n"
             "  bg     Resume a job in the background\n"
             "  history   Show command history (your custom)\n");
      continue;
    }

    pid_t pid = fork();
    if (pid < 0) {
      perror("fork");
      last_status = 1;
      continue;
    }

    if (pid == 0) {
      // CHILD: restore default handling so Ctrl-C/Z affect the child, not the
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
        perror("waitpid");
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
