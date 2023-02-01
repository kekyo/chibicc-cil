#include "chibicc.h"

static FILE *output_file;
static Obj *current_fn;

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
    case TY_CHAR:
      return "int8";
    default:
      // BUG
      return "BUG";
  }
}

static const char *to_typename(Type *ty) {
  return to_typename0(ty, 0);
}

static void gen_expr(Node *node);
static void gen_stmt(Node *node);

static void println(char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  vfprintf(output_file, fmt, ap);
  va_end(ap);
  fprintf(output_file, "\n");
}

static void print(char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  vfprintf(output_file, fmt, ap);
  va_end(ap);
}

static int count(void) {
  static int i = 1;
  return i++;
}

// Compute the absolute address of a given node.
// It's an error if a given node does not reside in memory.
static void gen_addr(Node *node) {
  switch (node->kind) {
  case ND_VAR:
    if (node->var->is_local) {
      // Local variable
      if (node->ty->kind == TY_ARRAY)
        println("  ldloc %d", node->var->offset);
      else
        println("  ldloca %d", node->var->offset);
    } else {
      // Global variable
      if (node->ty->kind == TY_ARRAY)
        println("  ldsfld %s", node->var->name);
      else
        println("  ldsflda %s", node->var->name);
    }
    return;
  case ND_DEREF:
    gen_expr(node->lhs);
    return;
  case ND_COMMA:
    gen_expr(node->lhs);
    println("  pop");
    gen_addr(node->rhs);
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

  if (ty->kind == TY_PTR) {
    println("  ldind.i");
    return;
  }

  if (ty->size == 1)
    println("  ldind.i1");
  else
    println("  ldind.i4");
}

// Store %rax to an address that the stack top is pointing to.
static void store(Type *ty) {
  if (ty->kind == TY_PTR) {
    println("  conv.u");
    println("  stind.i");
    println("  ldind.i");
    return;
  }

  if (ty->size == 1) {
    println("  stind.i1");
    println("  ldind.i1");
  } else {
    println("  stind.i4");
    println("  ldind.i4");
  }
}

static void gen_expr(Node *node) {
  println("  .location 1 %d %d %d %d",
    node->tok->line_no - 1,
    node->tok->start_column_no - 1,
    node->tok->line_no - 1,
    node->tok->end_column_no - 1);

  switch (node->kind) {
  case ND_NUM:
    println("  ldc.i4 %d", node->val);
    return;
  case ND_NEG:
    gen_expr(node->lhs);
    println("  neg");
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
    println("  dup");
    gen_expr(node->rhs);
    store(node->ty);
    return;
  case ND_STMT_EXPR:
    for (Node *n = node->body; n; n = n->next)
      if (n->next)
        gen_stmt(n);
      else
        gen_expr(n->lhs);
    return;
  case ND_COMMA:
    gen_expr(node->lhs);
    println("  pop");
    gen_expr(node->rhs);
    return;
  case ND_FUNCALL:
    for (Node *arg = node->args; arg; arg = arg->next) {
      gen_expr(arg);
    }
    println("  call %s", node->funcname);
    return;
  }

  gen_expr(node->lhs);
  gen_expr(node->rhs);

  switch (node->kind) {
  case ND_ADD:
    println("  add");
    return;
  case ND_SUB:
    println("  sub");
    return;
  case ND_MUL:
    println("  mul");
    return;
  case ND_DIV:
    println("  div");
    return;
  case ND_EQ:
    println("  ceq");
    return;
  case ND_NE:
    println("  ceq");
    println("  ldc.i4.0");
    println("  ceq");
    return;
  case ND_LT:
    println("  clt");
    return;
  case ND_LE:
    println("  cgt");
    println("  ldc.i4.0");
    println("  ceq");
    return;
  }

  error_tok(node->tok, "invalid expression");
}

static void gen_stmt(Node *node) {
  println("  .location 1 %d %d %d %d",
    node->tok->line_no - 1,
    node->tok->start_column_no - 1,
    node->tok->line_no - 1,
    node->tok->end_column_no - 1);

  switch (node->kind) {
  case ND_IF: {
    int c = count();
    gen_expr(node->cond);
    println("  brfalse _L_else_%d", c);
    gen_stmt(node->then);
    println("  br _L_end_%d", c);
    println("_L_else_%d:", c);
    if (node->els)
      gen_stmt(node->els);
    println("_L_end_%d:", c);
    return;
  }
  case ND_FOR: {
    int c = count();
    if (node->init)
      gen_stmt(node->init);
    println("_L_begin_%d:", c);
    if (node->cond) {
      gen_expr(node->cond);
      println("  brfalse _L_end_%d", c);
    }
    gen_stmt(node->then);
    if (node->inc) {
      gen_expr(node->inc);
      println("  pop");
    }
    println("  br _L_begin_%d", c);
    println("_L_end_%d:", c);
    return;
  }
  case ND_BLOCK:
    for (Node *n = node->body; n; n = n->next)
      gen_stmt(n);
    return;
  case ND_RETURN:
    gen_expr(node->lhs);
    println("  br _L_return");
    return;
  case ND_EXPR_STMT:
    gen_expr(node->lhs);
    println("  pop");
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

    println(".global %s %s", to_typename0(var->ty, 1), var->name);

    if (var->ty->kind == TY_ARRAY) {
      const char *element_type_name = to_typename0(var->ty->base, 1);
      int total_length = var->ty->array_len;
      Type *current_ty = var->ty->base;
      while (current_ty->kind == TY_ARRAY) {
        total_length *= current_ty->array_len;
        current_ty = current_ty->base;
      }

      println(".initializer");
      println("  ldc.i4 %d", total_length);
      println("  newarr %s", element_type_name);
      println("  dup");

      if (var->init_data) {
        println("  dup");
        println("  ldtoken <initdata>_%s", var->name);
        println("  call System.Runtime.CompilerServices.RuntimeHelpers.InitializeArray System.Array System.RuntimeFieldHandle");
      }

      println("  ldc.i4.3");  // Pinned
      println("  call System.Runtime.InteropServices.GCHandle.Alloc object System.Runtime.InteropServices.GCHandleType");
      println("  pop");
      println("  ldc.i4.0");
      println("  conv.i");
      println("  ldelema %s", element_type_name);
      println("  stsfld %s", var->name);
      println("  ret");

      if (var->init_data) {
        print(".constant <initdata>_%s", var->name);
        for (int i = 0; i < var->ty->size; i++)
          print(" 0x%hhx", var->init_data[i]);
        println("");
      }
    }
  }
}

static void emit_text(Obj *prog) {
  assign_lvar_offsets(prog);

  for (Obj *fn = prog; fn; fn = fn->next) {
    if (!fn->is_function)
      continue;

    print(".function int32 %s", fn->name);
    for (Obj *var = fn->params; var; var = var->next) {
      print(" %s:%s", var->name, to_typename(var->ty));
    }
    println("");
    current_fn = fn;

    // Prologue
    for (Obj *var = fn->locals; var; var = var->next) {
      println("  .local %s %s", to_typename0(var->ty, 1), var->name);
    }

    // Initialize local variable when type is array
    for (Obj *var = fn->locals; var; var = var->next) {
      if (var->ty->kind == TY_ARRAY) {
        println("  ldc.i4 %d", var->ty->array_len);
        println("  sizeof %s", to_typename0(var->ty->base, 1));
        println("  mul");
        println("  localloc");
        println("  stloc %d", var->offset);
      }
    }

    // Save passed-by-register arguments to the stack
    int i = 0;
    for (Obj *var = fn->params; var; var = var->next) {
      println("  ldarg %d", i);
      println("  stloc %d", var->offset);
      i++;
    }

    // Emit code
    gen_stmt(fn->body);

    // Epilogue
    println("_L_return:");
    println("  ret");
  }
}

void codegen(Obj *prog, FILE *out) {
  output_file = out;
  
  assign_lvar_offsets(prog);
  emit_data(prog);
  emit_text(prog);
}
