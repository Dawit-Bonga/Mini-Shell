// Implement your shell in this source file.
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/_types/_pid_t.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#define LINE_MAX 81
int last_status = 0;
#define MAX_HISTORY 80
#define MAX_JOBS 5
#define MAX_ARGS 64
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
static int next_job_id = 1;

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

void cleanup_finished_jobs() {
  if (!have_zombies) {
    return;
  }
  have_zombies = 0;

  int status;
  pid_t pid;
  while ((pid = waitpid(-1, &status, WNOHANG | WUNTRACED | WCONTINUED)) > 0) {
    for (int i = 0; i < job_count; i++) {
      if (jobs[i].pid == pid) {
        if (WIFEXITED(status) || WIFSIGNALED(status)) {
          jobs[i].status = JOB_DONE;
          printf("\n[%d]+ Done                    %s\n", jobs[i].job_id,
                 jobs[i].command);
        } else if (WIFSTOPPED(status)) {
          jobs[i].status = JOB_STOPPED;
          printf("\n[%d]+ Stopped                 %s\n", jobs[i].job_id,
                 jobs[i].command);
        } else if (WIFCONTINUED(status)) {
          jobs[i].status = JOB_RUNNING;
        }
        break;
      }
    }
  }
}

void add_job(pid_t pid, char *command, int status) {
  if (job_count >= MAX_JOBS) {
    int j = 0;
    for (int i = 0; i < job_count; i++) {
      if (jobs[i].status != JOB_DONE) {
        if (i != j) {
          jobs[j] = jobs[i];
        }
        j++;
      }
    }
    job_count = j;
  }

  if (job_count < MAX_JOBS) {
    jobs[job_count].pid = pid;
    strncpy(jobs[job_count].command, command, LINE_MAX);
    jobs[job_count].command[LINE_MAX - 1] = '\0';
    jobs[job_count].job_id = next_job_id++;
    jobs[job_count].status = status;
    job_count++;
  }
}

int builtin_jobs(int argc, char *argv[]) {
  (void)argc;
  (void)argv;
  for (int i = 0; i < job_count; i++) {
    char *status_str = NULL;
    switch (jobs[i].status) {
    case JOB_RUNNING:
      status_str = "Running";
      break;
    case JOB_STOPPED:
      status_str = "Stopped";
      break;
    case JOB_DONE:
      continue;
    }
    printf("[%d]+ %-20s %s\n", jobs[i].job_id, status_str, jobs[i].command);
  }
  return 0;
}

int builtin_fg(int argc, char *argv[]) {
  int job_num = -1;

  if (argc >= 2) {
    job_num = atoi(argv[1]);
  } else {
    // Use most recent job
    for (int i = job_count - 1; i >= 0; i--) {
      if (jobs[i].status != JOB_DONE) {
        job_num = jobs[i].job_id;
        break;
      }
    }
  }

  if (job_num == -1) {
    fprintf(stderr, "fg: no current job\n");
    return 1;
  }

  int job_idx = -1;
  for (int i = 0; i < job_count; i++) {
    if (jobs[i].job_id == job_num) {
      job_idx = i;
      break;
    }
  }

  if (job_idx == -1) {
    fprintf(stderr, "fg: job %d not found\n", job_num);
    return 1;
  }

  pid_t pid = jobs[job_idx].pid;
  fg_pid = pid;

  // Continue the process if stopped
  if (jobs[job_idx].status == JOB_STOPPED) {
    kill(pid, SIGCONT);
  }

  jobs[job_idx].status = JOB_RUNNING;
  printf("%s\n", jobs[job_idx].command);

  // Wait for it
  int status;
  waitpid(pid, &status, WUNTRACED);
  fg_pid = 0;

  if (WIFEXITED(status)) {
    last_status = WEXITSTATUS(status);
    jobs[job_idx].status = JOB_DONE;
  } else if (WIFSIGNALED(status)) {
    last_status = 128 + WTERMSIG(status);
    jobs[job_idx].status = JOB_DONE;
  } else if (WIFSTOPPED(status)) {
    jobs[job_idx].status = JOB_STOPPED;
    printf("\n[%d]+ Stopped                 %s\n", jobs[job_idx].job_id,
           jobs[job_idx].command);
  }

  return 0;
}

int builtin_bg(int argc, char *argv[]) {
  int job_num = -1;

  if (argc >= 2) {
    job_num = atoi(argv[1]);
  } else {
    for (int i = job_count - 1; i >= 0; i--) {
      if (jobs[i].status == JOB_STOPPED) {
        job_num = jobs[i].job_id;
        break;
      }
    }
  }

  if (job_num == -1) {
    fprintf(stderr, "bg: no stopped job\n");
    return 1;
  }

  int job_idx = -1;
  for (int i = 0; i < job_count; i++) {
    if (jobs[i].job_id == job_num) {
      job_idx = i;
      break;
    }
  }

  if (job_idx == -1 || jobs[job_idx].status != JOB_STOPPED) {
    fprintf(stderr, "bg: job %d did not get stopped\n", job_num);
    return 1;
  }

  kill(jobs[job_idx].pid, SIGCONT);
  jobs[job_idx].status = JOB_RUNNING;
  printf("[%d]+ %s &\n", jobs[job_idx].job_id, jobs[job_idx].command);

  return 0;
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
  if (strcmp(argv[0], "history") == 0) {
    last_status = builtin_history((int)argc, argv);
    return 1;
  }
  if (strcmp(argv[0], "jobs") == 0) {
    last_status = builtin_jobs((int)argc, argv);
    return 1;
  }
  if (strcmp(argv[0], "fg") == 0) {
    last_status = builtin_fg((int)argc, argv);
    return 1;
  }
  if (strcmp(argv[0], "bg") == 0) {
    last_status = builtin_bg((int)argc, argv);
    return 1;
  }

  return 0; // not a built-in
}

int execute_pipeline(char *commands[], int num_commands, int background) {
  int pipes[num_commands - 1][2];
  pid_t pids[num_commands];

  for (int i = 0; i < num_commands - 1; i++) {
    if (pipe(pipes[i]) < 0) {
      perror("pipe");
      return 1;
    }
  }

  for (int i = 0; i < num_commands; i++) {
    char *args[MAX_ARGS];
    int count = 0;
    char *token = strtok(commands[i], " \t");
    while (token && count < MAX_ARGS - 1) {
      args[count++] = token;
      token = strtok(NULL, " \t");
    }
    args[count] = NULL;

    if (count == 0) {
      continue;
    }

    pids[i] = fork();
    if (pids[i] < 0) {
      perror("fork");
      return 1;
    }

    if (pids[i] == 0) {
      signal(SIGINT, SIG_DFL);
      signal(SIGTSTP, SIG_DFL);

      if (i > 0) {
        dup2(pipes[i - 1][0], STDIN_FILENO);
      }

      if (i < num_commands - 1) {
        dup2(pipes[i][1], STDOUT_FILENO);
      }

      for (int j = 0; j < num_commands - 1; j++) {
        close(pipes[j][0]);
        close(pipes[j][1]);
      }

      execvp(args[0], args);
      fprintf(stderr,
              "mini-shell>Command not found--Did you mean something else?\n");
      _exit(127);
    }
  }

  for (int i = 0; i < num_commands - 1; i++) {
    close(pipes[i][0]);
    close(pipes[i][1]);
  }
  // Wait or track background
  if (!background) {
    for (int i = 0; i < num_commands; i++) {
      int status;
      fg_pid = pids[i];
      waitpid(pids[i], &status, WUNTRACED);
      fg_pid = 0;

      if (WIFEXITED(status)) {
        last_status = WEXITSTATUS(status);
      } else if (WIFSIGNALED(status)) {
        last_status = 128 + WTERMSIG(status);
      } else if (WIFSTOPPED(status)) {
        add_job(pids[i], commands[i], JOB_STOPPED);
        printf("\n[%d]+ Stopped                 %s\n",
               jobs[job_count - 1].job_id, commands[i]);
      }
    }
  } else {
    char full_cmd[LINE_MAX + 1] = "";
    for (int i = 0; i < num_commands; i++) {
      if (i > 0)
        strcat(full_cmd, " | ");
      strcat(full_cmd, commands[i]);
    }
    add_job(pids[num_commands - 1], full_cmd, JOB_RUNNING);
    printf("[%d] %d\n", jobs[job_count - 1].job_id, pids[num_commands - 1]);
  }

  return 0;
}

int execute_command(char *cmd_line) {
  // STEP 1: Check for background and remove '&'
  int background = 0;
  size_t len = strlen(cmd_line);
  if (len > 0 && cmd_line[len - 1] == '&') {
    background = 1;
    cmd_line[len - 1] = '\0';
    len--;
    // Trim trailing spaces
    while (len > 1 && (cmd_line[len - 2] == ' ' || cmd_line[len - 2] == '\t')) {
      cmd_line[len - 2] = '\0';
      len--;
    }
  }

  // STEP 2: SAVE THE CLEANED COMMAND BEFORE ANY strtok!
  char original_cmd[LINE_MAX + 1];
  strncpy(original_cmd, cmd_line, LINE_MAX);
  original_cmd[LINE_MAX] = '\0';

  // STEP 3: Check for pipes (this uses strtok_r which also modifies cmd_line)
  char *pipe_cmds[MAX_ARGS];
  int num_pipes = 0;
  char *saveptr;
  char *token = strtok_r(cmd_line, "|", &saveptr);
  while (token && num_pipes < MAX_ARGS) {
    // Trim leading/trailing spaces
    while (*token == ' ' || *token == '\t')
      token++;
    pipe_cmds[num_pipes++] = token;
    token = strtok_r(NULL, "|", &saveptr);
  }

  if (num_pipes > 1) {
    return execute_pipeline(pipe_cmds, num_pipes, background);
  }

  // STEP 4: Parse single command
  // cmd_line is already tokenized by the pipe check above, so re-parse from
  // pipe_cmds[0]
  char *args[MAX_ARGS];
  size_t count = 0;
  token = strtok(pipe_cmds[0], " \t");
  while (token && count < MAX_ARGS - 1) {
    args[count++] = token;
    token = strtok(NULL, " \t");
  }
  args[count] = NULL;

  if (count == 0)
    return 0;

  if (try_builtin(count, args)) {
    return 0;
  }

  pid_t pid = fork();
  if (pid < 0) {
    perror("fork");
    return 1;
  }

  if (pid == 0) {
    signal(SIGINT, SIG_DFL);
    signal(SIGTSTP, SIG_DFL);

    execvp(args[0], args);
    fprintf(stderr,
            "mini-shell>Command not found--Did you mean something else?\n");
    _exit(127);
  } else {
    if (!background) {
      int status;
      fg_pid = pid;
      waitpid(pid, &status, WUNTRACED);
      fg_pid = 0;

      if (WIFEXITED(status)) {
        last_status = WEXITSTATUS(status);
      } else if (WIFSIGNALED(status)) {
        last_status = 128 + WTERMSIG(status);
      } else if (WIFSTOPPED(status)) {
        add_job(pid, original_cmd, JOB_STOPPED);
        printf("\n[%d]+ Stopped                 %s\n",
               jobs[job_count - 1].job_id, original_cmd);
      }
    } else {
      add_job(pid, original_cmd, JOB_RUNNING);
      printf("[%d] %d\n", jobs[job_count - 1].job_id, pid);
    }
  }

  return 0;
}

int main(int argc, char **argv) {
  (void)argc; // Suppress unused parameter warning
  (void)argv;

  // Please leave in this line as the first statement in your program.
  alarm(120); // This will terminate your shell after 120 seconds,
              // and is useful in the case that you accidently create a 'fork
              // bomb'

  signal(SIGINT, my_signal_handler);
  signal(SIGCHLD, my_signal_handler);

  // Avoid the shell itself being stopped by terminal I/O signals
  signal(SIGTTOU, SIG_IGN);
  signal(SIGTTIN, SIG_IGN);
  signal(SIGTSTP, SIG_IGN); // <-- ADD THIS! Prevents shell from being stopped

  char line[LINE_MAX + 2];
  int interactive = isatty(STDIN_FILENO);

  for (;;) { // infinite loop
    cleanup_finished_jobs();

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

    add_to_history(line);
    execute_command(line);
  }

  return 0;
}