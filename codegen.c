#include "chibicc.h"

static FILE *output_file;
static Obj *current_fn;

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
      break;
    }
    case TY_STRUCT:
      if (ty->size->kind == ND_NUM)
        return ty->size->val;
      break;
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
    case TY_STRUCT:
    case TY_UNION:
      if (ty->tag)
        return format("%s_%p", get_string(ty->tag), ty);
      else
        return format("tag_%p", ty);
  }

  return "BUG4";
}

// Compute the absolute address of a given node.
// It's an error if a given node does not reside in memory.
static void gen_addr(Node *node) {
  switch (node->kind) {
  case ND_VAR:
    if (node->var->is_local)
      // Local variable
      println("  ldloca %d", node->var->offset);
    else
      // Global variable
      println("  ldsflda %s", node->var->name);
    return;
  case ND_DEREF:
    gen_expr(node->lhs);
    return;
  case ND_COMMA:
    gen_expr(node->lhs);
    println("  pop");
    gen_addr(node->rhs);
    return;
  case ND_MEMBER:
    gen_addr(node->lhs);
    gen_expr(node->member->offset);
    println("  add");
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
      println("  ldind.i");
      return;
    case TY_CHAR:
      println("  ldind.i1");
      return;
    case TY_INT:
      println("  ldind.i4");
      return;
  }

  // BUG
  println("  BUG1");
}

// Store stack top to an address that the stack top is pointing to.
static void store(Type *ty) {
  switch (ty->kind) {
    case TY_PTR:
      println("  conv.u");
      println("  stind.i");
      println("  ldind.i");
      return;
    case TY_CHAR:
      println("  stind.i1");
      println("  ldind.i1");
      return;
    case TY_INT:
      println("  stind.i4");
      println("  ldind.i4");
      return;
  }

  // BUG
  println("  BUG2");
}

static void gen_expr(Node *node) {
  if (node->tok)
    println("  .location 1 %d %d %d %d",
      node->tok->line_no - 1,
      node->tok->column_no - 1,
      node->tok->line_no - 1,
      node->tok->column_no + node->tok->len - 1);

  switch (node->kind) {
  case ND_NUM:
    println("  ldc.i4 %d", node->val);
    return;
  case ND_NEG:
    gen_expr(node->lhs);
    println("  neg");
    return;
  case ND_VAR:
  case ND_MEMBER:
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
          println("  ldc.i4.0");
          return;
        }
    }
    println("  sizeof %s", to_typename(node->sizeof_ty));
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
  case ND_COND: {
    int c = count();
    gen_expr(node->cond);
    println("  ldc.i4.0");
    println("  beq _L_else_%d", c);
    gen_expr(node->then);
    println("  br _L_end_%d", c);
    println("_L_else_%d:", c);
    gen_expr(node->els);
    println("_L_end_%d:", c);
    return;
  }
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
  if (node->tok)
    println("  .location 1 %d %d %d %d",
      node->tok->line_no - 1,
      node->tok->column_no - 1,
      node->tok->line_no - 1,
      node->tok->column_no + node->tok->len - 1);

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

static void emit_struct_type(Type *ty) {
  switch (ty->kind) {
    case TY_PTR:
    case TY_ARRAY:
      emit_struct_type(ty->base);
      break;
    case TY_STRUCT:
      println(".structure public %s", to_typename(ty));
      for (Member *mem = ty->members; mem; mem = mem->next) {
        println("  public %s %s", to_typename(mem->ty), get_string(mem->name));
      }
      // Emit member type recursively.
      for (Member *mem = ty->members; mem; mem = mem->next) {
        emit_struct_type(mem->ty);
      }
      break;
    case TY_UNION:
      println(".structure public %s explicit", to_typename(ty));
      for (Member *mem = ty->members; mem; mem = mem->next) {
        println("  public %s %s 0", to_typename(mem->ty), get_string(mem->name));
      }
      // Emit member type recursively.
      for (Member *mem = ty->members; mem; mem = mem->next) {
        emit_struct_type(mem->ty);
      }
      break;
  }
}

static void emit_struct(Obj *prog) {
  for (Obj *fn = prog; fn; fn = fn->next) {
    if (!fn->is_function)
      continue;

    // Included parameter types.
    for (Obj *var = fn->locals; var; var = var->next) {
      emit_struct_type(var->ty);
    }
  }
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

    print(".global public %s %s", to_typename(var->ty), var->name);

    if (var->init_data) {
      for (int i = 0; i < var->init_data_size; i++)
        print(" 0x%hhx", var->init_data[i]);
    }

    println("");
  }
}

static void emit_text(Obj *prog) {
  for (Obj *fn = prog; fn; fn = fn->next) {
    if (!fn->is_function)
      continue;

    print(".function public int32 %s", fn->name);
    for (Obj *var = fn->params; var; var = var->next) {
      print(" %s:%s", var->name, to_typename(var->ty));
    }
    println("");
    current_fn = fn;

    // Prologue
    for (Obj *var = fn->locals; var; var = var->next) {
      println("  .local %s %s", to_typename(var->ty), var->name);
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
  emit_struct(prog);
  emit_data(prog);
  emit_text(prog);
}
