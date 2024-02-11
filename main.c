#include "chibicc.h"

static MemoryModel opt_mm = AnyCPU;
static bool opt_S;
static bool opt_c;
static bool opt_cc1;
static bool opt_hash_hash_hash;
static char *opt_o;

static char *base_file;
static char *output_file;

static StringArray input_paths;
static StringArray tmpfiles;

extern char **environ;

static void usage(int status) {
  fprintf(stderr, "chibicc [ -o <path> ] <file>\n");
  exit(status);
}

static bool take_arg(char *arg) {
  return !strcmp(arg, "-o");
}

static void parse_args(int argc, char **argv) {
  // Make sure that all command line options that take an argument
  // have an argument.
  for (int i = 1; i < argc; i++)
    if (take_arg(argv[i]))
      if (!argv[++i])
        usage(1);

  for (int i = 1; i < argc; i++) {
    if (!strcmp(argv[i], "-###")) {
      opt_hash_hash_hash = true;
      continue;
    }

    if (!strcmp(argv[i], "-cc1")) {
      opt_cc1 = true;
      continue;
    }

    if (!strcmp(argv[i], "--help"))
      usage(0);

    if (!strcmp(argv[i], "-march=any")) {
      opt_mm = AnyCPU;
      continue;
    }

    if (!strcmp(argv[i], "-march=m32")) {
      opt_mm = M32;
      continue;
    }

    if (!strcmp(argv[i], "-march=m64")) {
      opt_mm = M64;
      continue;
    }

    if (!strcmp(argv[i], "-march=native")) {
      opt_mm = sizeof(void*) == 8 ? M64 : M32;
      continue;
    }

    if (!strcmp(argv[i], "-o")) {
      opt_o = argv[++i];
      continue;
    }

    if (!strncmp(argv[i], "-o", 2)) {
      opt_o = argv[i] + 2;
      continue;
    }

    if (!strcmp(argv[i], "-S")) {
      opt_S = true;
      continue;
    }

    if (!strcmp(argv[i], "-c")) {
      opt_c = true;
      continue;
    }

    if (!strcmp(argv[i], "-cc1-input")) {
      base_file = argv[++i];
      continue;
    }

    if (!strcmp(argv[i], "-cc1-output")) {
      output_file = argv[++i];
      continue;
    }

    if (argv[i][0] == '-' && argv[i][1] != '\0')
      error("unknown argument: %s", argv[i]);

    strarray_push(&input_paths, argv[i]);
  }

  if (input_paths.len == 0)
    error("no input files");
}

static FILE *open_file(char *path) {
  if (!path || strcmp(path, "-") == 0)
    return stdout;

  FILE *out = fopen(path, "w");
  if (!out)
    error("cannot open output file: %s: %s", path, strerror(errno));
  return out;
}

// Replace file extension
static char *replace_extn(char *tmpl, char *extn) {
  char *filename = basename(strdup(tmpl));
  char *dot = strrchr(filename, '.');
  if (dot)
    *dot = '\0';
  return format("%s%s", filename, extn);
}

static void cleanup(void) {
  for (int i = 0; i < tmpfiles.len; i++)
    unlink(tmpfiles.data[i]);
}

static char *create_tmpfile(void) {
  char *path = strdup("/tmp/chibicc-XXXXXX");
  int fd = mkstemp(path);
  if (fd == -1)
    error("mkstemp failed: %s", strerror(errno));
  close(fd);

  strarray_push(&tmpfiles, path);
  return path;
}

static void run_subprocess(char **argv) {
  // If -### is given, dump the subprocess's command line.
  if (opt_hash_hash_hash) {
    fprintf(stderr, "%s", argv[0]);
    for (int i = 1; argv[i]; i++)
      fprintf(stderr, " %s", argv[i]);
    fprintf(stderr, "\n");
  }

  // Child process. Run a new command.
  // Wait for the child process to finish.
  pid_t pid;
  int status = posix_spawnp(&pid, argv[0], NULL, NULL, argv, environ);
  if (status == -1) {
    fprintf(stderr, "spawn failed: %s: %s\n", argv[0], strerror(errno));
    exit(1);
  }
  do {
    if (waitpid(pid, &status, 0) == -1) {
      fprintf(stderr, "waitpid failed: %s: %s\n", argv[0], strerror(errno));
      exit(1);
    }
  } while (!WIFEXITED(status) && !WIFSIGNALED(status));
}

static void run_cc1(int argc, char **argv, char *input, char *output) {
  char **args = calloc(argc + 10, sizeof(char *));
  memcpy(args, argv, argc * sizeof(char *));
  args[argc++] = "-cc1";

  if (input) {
    args[argc++] = "-cc1-input";
    args[argc++] = input;
  }

  if (output) {
    args[argc++] = "-cc1-output";
    args[argc++] = output;
  }

  run_subprocess(args);
}

static void cc1(void) {
  init_type_system(opt_mm);

  // Tokenize and parse.
  Token *tok = tokenize_file(base_file);
  Obj *prog = parse(tok);

  // Traverse the AST to emit assembly.
  FILE *out = open_file(output_file);
  fprintf(out, ".file 1 \"%s\" c\n", base_file);
  codegen(prog, out);
  fflush(out);
}

static void assemble(char *input, char *output) {
  char *cmd[] = {
    "chibias",
    //"/home/kouji/Projects/chibias-cil/chibias/bin/Debug/net8.0/chibias",
    "-f", "net6.0",
    "-r", "/home/kouji/.dotnet/shared/Microsoft.NETCore.App/6.0.26/System.Private.CoreLib.dll",
    "-r", "/home/kouji/Projects/libc-cil/libc-bootstrap/bin/Debug/netstandard2.0/libc-bootstrap.dll",
    "-c", input, "-o", output, NULL};
  run_subprocess(cmd);
}

int main(int argc, char **argv) {
  atexit(cleanup);
  parse_args(argc, argv);

  if (opt_cc1) {
    cc1();
    return 0;
  }

  if (input_paths.len > 1 && opt_o)
    error("cannot specify '-o' with multiple files");

  for (int i = 0; i < input_paths.len; i++) {
    char *input = input_paths.data[i];

    char *output;
    if (opt_o)
      output = opt_o;
    else if (opt_S)
      output = replace_extn(input, ".s");
    else
      output = replace_extn(input, ".o");

    // If -S is given, assembly text is the final output.
    // .NET: chibias always consumes only assembler source code (into .o file.)
    if (opt_S || opt_c) {
      run_cc1(argc, argv, input, output);
      continue;
    }

    // Otherwise, run the assembler to assemble our output.
    char *tmpfile = create_tmpfile();
    run_cc1(argc, argv, input, tmpfile);
    assemble(tmpfile, output);
  }

  return 0;
}
