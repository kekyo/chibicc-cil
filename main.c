#include "chibicc.h"

static MemoryModel opt_mm = AnyCPU;

static bool opt_E;
static bool opt_S;
static bool opt_c;
static bool opt_cc1;
static bool opt_hash_hash_hash;
static char *opt_o;

char *base_file;
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

    if (!strcmp(argv[i], "-E")) {
      opt_E = true;
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

static bool endswith(char *p, char *q) {
  int len1 = strlen(p);
  int len2 = strlen(q);
  return (len1 >= len2) && !strcmp(p + len1 - len2, q);
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

// Print tokens to stdout. Used for -E.
void print_tokens(Token *tok) {
  FILE *out = open_file(opt_o ? opt_o : "-");

  int line = 1;
  for (; tok->kind != TK_EOF; tok = tok->next) {
    if (line > 1 && tok->at_bol)
      fprintf(out, "\n");
    if (tok->has_space && !tok->at_bol)
      fprintf(out, " ");
    fprintf(out, "%.*s", tok->len, tok->loc);
    line++;
  }
  fprintf(out, "\n");
}

static void cc1(void) {
  init_type_system(opt_mm);

  // Tokenize and parse.
  Token *tok = tokenize_file(base_file);
  if (!tok)
    error("%s: %s", base_file, strerror(errno));

  tok = preprocess(tok);

  // If -E is given, print out preprocessed C code as a result.
  if (opt_E) {
    print_tokens(tok);
    return;
  }

  Obj *prog = parse(tok);

  // Traverse the AST to emit assembly.
  FILE *out = open_file(output_file);
  codegen(prog, out);
  fflush(out);
}

static void assemble(char *input, char *output) {
  char *cmd[] = {
    "cp", input, output, NULL };
  run_subprocess(cmd);
}

static char *find_file(char *pattern) {
  char *path = NULL;
  glob_t buf = {};
  glob(pattern, 0, NULL, &buf);
  if (buf.gl_pathc > 0)
    path = strdup(buf.gl_pathv[buf.gl_pathc - 1]);
  globfree(&buf);
  return path;
}

// Returns true if a given file exists.
static bool file_exists(char *path) {
  struct stat st;
  return !stat(path, &st);
}

static char *get_dotnet_root(void) {
  static char *dnp = NULL;
  if (dnp == NULL) {
    dnp = getenv("DOTNET_ROOT");
    if (dnp == NULL)
      dnp = format("%s/.dotnet", getenv("HOME"));
  }
  return dnp;
}

static char *find_libpath(void) {
  char *rcpath = format("%s/shared/Microsoft.NETCore.App/6.0.*/System.Private.CoreLib.dll", get_dotnet_root());
  char *libpath = find_file(rcpath);
  if (libpath)
    return strdup(dirname(libpath));
  error("library path is not found");
}

static char *find_gcc_libpath(void) {
  char *libcpath = format("%s/Projects/libc-cil/libc-bootstrap/bin/Debug/netstandard2.0/libc-bootstrap.dll", getenv("HOME"));
  char *paths[] = {
    libcpath
  };

  for (int i = 0; i < sizeof(paths) / sizeof(*paths); i++) {
    char *path = find_file(paths[i]);
    if (path)
      return strdup(dirname(path));
  }

  error("gcc library path is not found");
}

static char *find_apphostpath(void) {
  char *rcpath = format("%s/sdk/6.0.*/AppHostTemplate/apphost", get_dotnet_root());
  char *apphostpath = find_file(rcpath);
  return apphostpath;
}

static void run_linker(StringArray *inputs, char *output) {
  StringArray arr = {};

  char *apphostpath = find_apphostpath();

  //strarray_push(&arr, "/home/kouji/Projects/chibias-cil/chibias/bin/Debug/net6.0/chibias");
  strarray_push(&arr, "chibias");
  strarray_push(&arr, "-o");
  strarray_push(&arr, output);
  strarray_push(&arr, "-f");
  strarray_push(&arr, "net6.0");

  if (apphostpath) {
    strarray_push(&arr, "-a");
    strarray_push(&arr, apphostpath);
  }

  char *libpath = find_libpath();
  char *gcc_libpath = find_gcc_libpath();

  strarray_push(&arr, format("-L%s", gcc_libpath));
  strarray_push(&arr, format("-L%s", libpath));

  for (int i = 0; i < inputs->len; i++)
    strarray_push(&arr, inputs->data[i]);

  strarray_push(&arr, "-lc-bootstrap");
  strarray_push(&arr, "-lSystem.Private.CoreLib");
  strarray_push(&arr, NULL);

  run_subprocess(arr.data);
}

int main(int argc, char **argv) {
  atexit(cleanup);
  parse_args(argc, argv);

  if (opt_cc1) {
    cc1();
    return 0;
  }

  if (input_paths.len > 1 && opt_o && (opt_c || opt_S || opt_E))
    error("cannot specify '-o' with '-c,' '-S' or '-E' with multiple files");

  StringArray ld_args = {};

  for (int i = 0; i < input_paths.len; i++) {
    char *input = input_paths.data[i];

    char *output;
    if (opt_o)
      output = opt_o;
    else if (opt_S)
      output = replace_extn(input, ".s");
    else
      output = replace_extn(input, ".o");

    // Handle .o
    if (endswith(input, ".o")) {
      strarray_push(&ld_args, input);
      continue;
    }

    // Handle .s
    if (endswith(input, ".s")) {
      if (!opt_S)
        assemble(input, output);
      continue;
    }

    // Handle .c
    if (!endswith(input, ".c") && strcmp(input, "-"))
      error("unknown file extension: %s", input);

    // Just preprocess
    if (opt_E) {
      run_cc1(argc, argv, input, NULL);
      continue;
    }

    // Compile
    // .NET: chibias always consumes only assembler source code (into .o file.)
    if (opt_S || opt_c) {
      run_cc1(argc, argv, input, output);
      continue;
    }

    // Compile, assemble and link
    char *tmp1 = create_tmpfile();
    char *tmp2 = create_tmpfile();
    run_cc1(argc, argv, input, tmp1);
    assemble(tmp1, tmp2);
    strarray_push(&ld_args, tmp2);
    continue;
  }

  if (ld_args.len > 0)
    run_linker(&ld_args, opt_o ? opt_o : "a.out.exe");
  return 0;
}
