## mini-shell

A small teaching shell implemented in C. It supports basic built-ins, running external programs, simple pipelines, background jobs, and short-circuit command operators.

### Build

- Prerequisite: `gcc` (or compatible) on macOS/Linux
- Build (and run once):

```bash
make
```

- Run without rebuilding:

```bash
make run
```

- Clean build artifact:

```bash
make clean
```

The compiled binary is written to `bin/shell`.

### Run

Start the shell directly:

```bash
./bin/shell
```

You should see the prompt:

```
mini-shell>
```

Note: The shell auto-terminates after 120 seconds via `alarm(120)` to protect against runaway processes during development.

### Features

- **Built-ins**

  - `cd [dir]`: Change directory (defaults to `$HOME`)
  - `pwd`: Print current working directory
  - `exit [n]`: Exit the shell (defaults to last command status)
  - `help`: Show built-in help
  - `history`: Show up to 80 recent commands
  - `jobs`: List background or stopped jobs
  - `fg [job_id]`: Bring a job to the foreground
  - `bg [job_id]`: Resume a stopped job in the background
  - `echo [args...]`: Print arguments; strips matching surrounding quotes on each arg

- **External commands**

  - Executes programs found in `PATH` via `execvp`

- **Pipelines**

  - Use `|` to connect commands, e.g. `ls | wc -l`

- **Command operators**

  - `cmd1 && cmd2`: Run `cmd2` only if `cmd1` succeeds (status 0)
  - `cmd1 || cmd2`: Run `cmd2` only if `cmd1` fails (non‑zero)
  - `cmd1 ; cmd2`: Always run `cmd2`

- **Background jobs**

  - Append `&` to run in background, e.g. `sleep 10 &`
  - `jobs`, `fg`, and `bg` manage basic job control

- **History**

  - Stores up to 80 commands; `history` prints them with indices

- **Exit status tracking**
  - The special variable concept of "last status" influences `&&` and `||`, and is the default for `exit`

### Usage examples

```bash
# Built-ins
mini-shell> pwd
mini-shell> cd /tmp
mini-shell> echo "hello world"

# External command
mini-shell> ls -la

# Pipelines
mini-shell> ls | wc -l

# Command operators
mini-shell> false && echo will-not-print
mini-shell> false || echo prints-because-left-failed
mini-shell> echo one ; echo two

# Background and jobs
mini-shell> sleep 30 &
mini-shell> jobs
mini-shell> fg        # bring most recent job to foreground
mini-shell> bg 1      # resume stopped job 1 in background

# Exit
mini-shell> exit      # exits with last command's status
mini-shell> exit 2    # exits with status 2
```

### Signals and behavior

- `Ctrl-C` (SIGINT) in this shell sends `SIGTERM` to the foreground job and known jobs, prints a message, and terminates the shell itself.
- Child processes run with default signal dispositions (they receive `SIGINT`/`SIGTSTP` normally).
- The shell ignores `SIGTTOU`, `SIGTTIN`, and `SIGTSTP` so it does not get stopped by the terminal.

### Limits and caveats

- `LINE_MAX` is 81 characters; commands longer than this are truncated.
- `MAX_ARGS` is 64; excess tokens are ignored.
- `MAX_JOBS` is 5; older completed entries are compacted to make room.
- Parsing is intentionally simple:
  - No full quoting/escaping rules; tokens split on spaces/tabs
  - No redirection (`>`, `<`), globbing, subshells, or variables
  - `echo` only strips matching surrounding quotes per-argument

### Project layout

- `shell.c`: The entire shell implementation
- `makefile`: Build, run, and clean targets
- `bin/shell`: Built binary after compilation

### Troubleshooting

- "Command not found" → The shell prints a helpful message and returns status 127.
- Build warnings treated as errors (`-Werror`): fix the underlying issue; compilation will fail otherwise.

### License

No license specified. Add one if you plan to distribute.
