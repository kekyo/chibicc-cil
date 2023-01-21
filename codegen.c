#include "chibicc.h"

static Function *current_prog;
static Function *current_fn;

static const char *to_typename0(Type *ty, int is_array_ptr) {
  if (ty->base) {
    if (ty->kind == TY_ARRAY && is_array_ptr) {
      Type *base_ty = ty->base;
      while (base_ty) {
        if (!base_ty->base)
          break;
        base_ty = base_ty->base;
      }
      const char *base0_name = to_typename0(base_ty, is_array_ptr);
      int length2 = strlen(base0_name) + 1;
      char *name2 = calloc(length2 + 1, sizeof(char));
      strcpy(name2, base0_name);
      strcat(name2, "*");
      return name2;
    }
    const char *base_name = to_typename0(ty->base, is_array_ptr);
    switch (ty->kind) {
      case TY_ARRAY:
        int length1 = strlen(base_name) + 2;
        char *name1 = calloc(length1 + 1, sizeof(char));
        strcpy(name1, base_name);
        strcat(name1, "[]");
        return name1;
      case TY_PTR:
        int length2 = strlen(base_name) + 1;
        char *name2 = calloc(length2 + 1, sizeof(char));
        strcpy(name2, base_name);
        strcat(name2, "*");
        return name2;
      default:
        // BUG
        return base_name;
    }
  }

  switch (ty->kind) {
    case TY_INT:
      return "int32";
    default:
      // BUG
      return "BUG";
  }
}

static const char *to_typename(Type *ty) {
  return to_typename0(ty, 0);
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
    if (node->ty->kind == TY_ARRAY) {
      printf("  ldloc %d\n", node->var->offset);
    } else
      printf("  ldloca %d\n", node->var->offset);
    return;
  case ND_DEREF:
    gen_expr(node->lhs);
    return;
  }

  error_tok(node->tok, "not an lvalue");
}

// Load a value from where %rax is pointing to.
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

  printf("  ldind.i4\n");
}

// Store %rax to an address that the stack top is pointing to.
static void store(void) {
  printf("  stind.i4\n");
  printf("  ldind.i4\n");
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
  case ND_ASSIGN:
    gen_addr(node->lhs);
    printf("  dup\n");
    gen_expr(node->rhs);
    store();
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
static void assign_lvar_offsets(Function *prog) {
  for (Function *fn = prog; fn; fn = fn->next) {
    int offset = 0;
    for (Obj *var = fn->locals; var; var = var->next) {
      var->offset = offset;
      offset++;
    }
    fn->stack_size = offset;
  }
}

void codegen(Function *prog) {
  current_prog = prog;

  assign_lvar_offsets(prog);

  for (Function *fn = prog; fn; fn = fn->next) {
    printf(".function int32 %s", fn->name);
    for (Obj *var = fn->params; var; var = var->next) {
      printf(" %s:%s", var->name, to_typename(var->ty));
    }
    printf("\n");
    for (Obj *var = fn->locals; var; var = var->next) {
      printf("  .local %s %s\n", to_typename0(var->ty, 1), var->name);
    }

    // Initialize local variable when type is array
    for (Obj *var = fn->locals; var; var = var->next) {
      if (var->ty->kind == TY_ARRAY) {
        printf("  ldc.i4 %d\n", var->ty->array_len);
        printf("  sizeof %s\n", to_typename0(var->ty->base, 1));
        printf("  mul\n");
        printf("  localloc\n");
        printf("  stloc %d\n", var->offset);
      }
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
