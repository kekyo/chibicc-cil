#include "chibicc.h"

static Obj *current_fn;

// Takes a printf-style format string and returns a formatted string.
static char *format(char *fmt, ...) {
  char *buf;
  size_t buflen;
  FILE *out = open_memstream(&buf, &buflen);

  va_list ap;
  va_start(ap, fmt);
  vfprintf(out, fmt, ap);
  va_end(ap);
  fclose(out);
  return buf;
}

int calculate_size(Type *ty) {
  switch (ty->kind) {
    case TY_CHAR:
      return 1;
    case TY_INT:
      return 4;
    case TY_ARRAY: {
      int sz = calculate_size(ty->base);
      if (sz >= 0) {
        return sz * ty->array_len;
      }
    }
  }
  return -1;
}

static const char *to_typename(Type *ty) {
  if (ty->base) {
    const char *base_name = to_typename(ty->base);
    switch (ty->kind) {
      case TY_ARRAY:
        return format("%s[%d]", base_name, ty->array_len);
      case TY_PTR:
        return format("%s*", base_name);
    }
    return "BUG3";
  }

  switch (ty->kind) {
    case TY_CHAR:
      return "int8";
    case TY_INT:
      return "int32";
  }
  return "BUG4";
}

static void gen_expr(Node *node);

static int count(void) {
  static int i = 1;
  return i++;
}

// Compute the absolute address of a given node.
// It's an error if a given node does not reside in memory.
static void gen_addr(Node *node) {
  switch (node->kind) {
  case ND_VAR:
    if (node->var->is_local)
      // Local variable
      printf("  ldloca %d\n", node->var->offset);
    else
      // Global variable
      printf("  ldsflda %s\n", node->var->name);
    return;
  case ND_DEREF:
    gen_expr(node->lhs);
    return;
  }

  error_tok(node->tok, "not an lvalue");
}

// Load a value from where stack top is pointing to.
static void load(Type *ty) {
  if (ty->kind == TY_ARRAY) {
    // If it is an array, do not attempt to load a value to the
    // register because in general we can't load an entire array to a
    // register. As a result, the result of an evaluation of an array
    // becomes not the array itself but the address of the array.
    // This is where "array is automatically converted to a pointer to
    // the first element of the array in C" occurs.
    return;
  }

  switch (ty->kind) {
    case TY_PTR:
      printf("  ldind.i\n");
      return;
    case TY_CHAR:
      printf("  ldind.i1\n");
      return;
    case TY_INT:
      printf("  ldind.i4\n");
      return;
  }

  // BUG
  printf("  BUG1\n");
}

// Store stack top to an address that the stack top is pointing to.
static void store(Type *ty) {
  switch (ty->kind) {
    case TY_PTR:
      printf("  stind.i\n");
      printf("  ldind.i\n");
      return;
    case TY_CHAR:
      printf("  stind.i1\n");
      printf("  ldind.i1\n");
      return;
    case TY_INT:
      printf("  stind.i4\n");
      printf("  ldind.i4\n");
      return;
  }

  // BUG
  printf("  BUG2\n");
}

static void gen_expr(Node *node) {
  switch (node->kind) {
  case ND_NUM:
    printf("  ldc.i4 %d\n", node->val);
    return;
  case ND_NEG:
    gen_expr(node->lhs);
    printf("  neg\n");
    return;
  case ND_VAR:
    gen_addr(node);
    load(node->ty);
    return;
  case ND_DEREF:
    gen_expr(node->lhs);
    load(node->ty);
    return;
  case ND_ADDR:
    gen_addr(node->lhs);
    return;
  case ND_SIZEOF:
    switch (node->ty->kind) {
      case TY_ARRAY:
        if (node->ty->array_len == 0) {
          printf("  ldc.i4.0\n");
          return;
        }
    }
    printf("  sizeof %s\n", to_typename(node->sizeof_ty));
    return;
  case ND_ASSIGN:
    gen_addr(node->lhs);
    printf("  dup\n");
    gen_expr(node->rhs);
    store(node->ty);
    return;
  case ND_FUNCALL:
    for (Node *arg = node->args; arg; arg = arg->next) {
      gen_expr(arg);
    }
    printf("  call %s\n", node->funcname);
    return;
  }

  gen_expr(node->lhs);
  gen_expr(node->rhs);

  switch (node->kind) {
  case ND_ADD:
    printf("  add\n");
    return;
  case ND_SUB:
    printf("  sub\n");
    return;
  case ND_MUL:
    printf("  mul\n");
    return;
  case ND_DIV:
    printf("  div\n");
    return;
  case ND_EQ:
    printf("  ceq\n");
    return;
  case ND_NE:
    printf("  ceq\n");
    printf("  ldc.i4.0\n");
    printf("  ceq\n");
    return;
  case ND_LT:
    printf("  clt\n");
    return;
  case ND_LE:
    printf("  cgt\n");
    printf("  ldc.i4.0\n");
    printf("  ceq\n");
    return;
  }

  error_tok(node->tok, "invalid expression");
}

static void gen_stmt(Node *node) {
  switch (node->kind) {
  case ND_IF: {
    int c = count();
    gen_expr(node->cond);
    printf("  brfalse _L_else_%d\n", c);
    gen_stmt(node->then);
    printf("  br _L_end_%d\n", c);
    printf("_L_else_%d:\n", c);
    if (node->els)
      gen_stmt(node->els);
    printf("_L_end_%d:\n", c);
    return;
  }
  case ND_FOR: {
    int c = count();
    if (node->init)
      gen_stmt(node->init);
    printf("_L_begin_%d:\n", c);
    if (node->cond) {
      gen_expr(node->cond);
      printf("  brfalse _L_end_%d\n", c);
    }
    gen_stmt(node->then);
    if (node->inc) {
      gen_expr(node->inc);
      printf("  pop\n");
    }
    printf("  br _L_begin_%d\n", c);
    printf("_L_end_%d:\n", c);
    return;
  }
  case ND_BLOCK:
    for (Node *n = node->body; n; n = n->next)
      gen_stmt(n);
    return;
  case ND_RETURN:
    gen_expr(node->lhs);
    printf("  br _L_return\n");
    return;
  case ND_EXPR_STMT:
    gen_expr(node->lhs);
    printf("  pop\n");
    return;
  }

  error_tok(node->tok, "invalid statement");
}

// Assign offsets to local variables.
static void assign_lvar_offsets(Obj *prog) {
  for (Obj *fn = prog; fn; fn = fn->next) {
    if (!fn->is_function)
      continue;

    int offset = 0;
    for (Obj *var = fn->locals; var; var = var->next) {
      var->offset = offset;
      offset++;
    }
    fn->stack_size = offset;
  }
}

static void emit_data(Obj *prog) {
  for (Obj *var = prog; var; var = var->next) {
    if (var->is_function)
      continue;

    printf(".global public %s %s", to_typename(var->ty), var->name);

    if (var->init_data) {
      for (int i = 0; i < var->init_data_size; i++)
        printf(" 0x%hhx", var->init_data[i]);
    }

    printf("\n");
  }
}

static void emit_text(Obj *prog) {
  assign_lvar_offsets(prog);

  for (Obj *fn = prog; fn; fn = fn->next) {
    if (!fn->is_function)
      continue;

    printf(".function public int32 %s", fn->name);
    for (Obj *var = fn->params; var; var = var->next) {
      printf(" %s:%s", var->name, to_typename(var->ty));
    }
    printf("\n");
    for (Obj *var = fn->locals; var; var = var->next) {
      printf("  .local %s %s\n", to_typename(var->ty), var->name);
    }

    // Save passed-by-register arguments to the stack
    int i = 0;
    for (Obj *var = fn->params; var; var = var->next) {
      printf("  ldarg %d\n", i);
      printf("  stloc %d\n", var->offset);
      i++;
    }

    gen_stmt(fn->body);

    printf("_L_return:\n");
    printf("  ret\n");
  }
}

void codegen(Obj *prog) {
  assign_lvar_offsets(prog);
  emit_data(prog);
  emit_text(prog);
}
