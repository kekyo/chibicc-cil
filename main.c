#include "chibicc.h"

static MemoryModel opt_mm = AnyCPU;
static bool opt_cc1;
static bool opt_hash_hash_hash;
static char *opt_o;

static char *input_path;

static void usage(int status) {
  fprintf(stderr, "chibicc [ -o <path> ] <file>\n");
  exit(status);
}

static void parse_args(int argc, char **argv) {
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
      if (!argv[++i])
        usage(1);
      opt_o = argv[i];
      continue;
    }

    if (!strncmp(argv[i], "-o", 2)) {
      opt_o = argv[i] + 2;
      continue;
    }

    if (argv[i][0] == '-' && argv[i][1] != '\0')
      error("unknown argument: %s", argv[i]);

    input_path = argv[i];
  }

  if (!input_path)
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
  int status = posix_spawn(&pid, argv[0], NULL, NULL, argv, NULL);
  if (status == -1) {
    fprintf(stderr, "exec failed: %s: %s\n", argv[0], strerror(errno));
    exit(1);
  }
  do {
    if (waitpid(pid, &status, 0) == -1) {
      fprintf(stderr, "waitpid failed: %s: %s\n", argv[0], strerror(errno));
      exit(1);
    }
  } while (!WIFEXITED(status) && !WIFSIGNALED(status));
}

static void run_cc1(int argc, char **argv) {
  char **args = calloc(argc + 10, sizeof(char *));
  memcpy(args, argv, argc * sizeof(char *));
  args[argc++] = "-cc1";
  run_subprocess(args);
}

static void cc1(void) {
  init_type_system(opt_mm);

  // Tokenize and parse.
  Token *tok = tokenize_file(input_path);
  Obj *prog = parse(tok);

  // Traverse the AST to emit assembly.
  FILE *out = open_file(opt_o);
  fprintf(out, ".file 1 \"%s\" c\n", input_path);
  codegen(prog, out);
  fflush(out);
}

int main(int argc, char **argv) {
  parse_args(argc, argv);

  if (opt_cc1) {
    cc1();
    return 0;
  }

  run_cc1(argc, argv);
  return 0;
}
