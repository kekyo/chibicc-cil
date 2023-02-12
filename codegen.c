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

static const char *to_cil_typename(Type *ty) {
  if (ty->base) {
    const char *base_name = to_cil_typename(ty->base);
    switch (ty->kind) {
      case TY_ARRAY:
        int length1 = strlen(base_name) + 13;
        char *name1 = calloc(length1 + 1, sizeof(char));
        sprintf(name1, "%s[%d]", base_name, ty->array_len);
        return name1;
      case TY_PTR:
        int length2 = strlen(base_name) + 1;
        char *name2 = calloc(length2 + 1, sizeof(char));
        strcpy(name2, base_name);
        strcat(name2, "*");
        return name2;
    }
    unreachable();
  }

  switch (ty->kind) {
    case TY_VOID:
      return "void";
    case TY_BOOL:
      return "bool";
    case TY_CHAR:
      return "int8";
    case TY_SHORT:
      return "int16";
    case TY_INT:
      return "int32";
    case TY_LONG:
      return "int64";
    case TY_ENUM:
    case TY_STRUCT:
    case TY_UNION:
      if (ty->tag) {
        char *tag_name = get_string(ty->tag);
        char *name3 = calloc(strlen(tag_name) + 1 + 18 + 1, sizeof(char));
        sprintf(name3, "%s_%p", tag_name, ty);
        return name3;
      } else {
        char *name4 = calloc(4 + 18 + 1, sizeof(char));
        sprintf(name4, "tag_%p", ty);
        return name4;
      }
  }
  unreachable();
}

// Round up `n` to the nearest multiple of `align`. For instance,
// align_to(5, 8) returns 8 and align_to(11, 8) returns 16.
int align_to(int n, int align) {
  return (n + align - 1) / align * align;
}

// Compute the absolute address of a given node.
// It's an error if a given node does not reside in memory.
static void gen_addr(Node *node) {
  switch (node->kind) {
  case ND_VAR:
    switch (node->var->kind) {
      case OB_GLOBAL:
        // Global variable
        println("  ldsflda %s", node->var->name);
        return;
      case OB_LOCAL:
        // Local variable
        println("  ldloca %d", node->var->offset);
        return;
      case OB_PARAM:
        // Parameter variable
        println("  ldarga %d", node->var->offset);
        return;
    }
    break;
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
    println("  ldc.i4 %d", node->member->offset);
    println("  add");
    return;
  }

  error_tok(node->tok, "not an lvalue");
}

static void load(Type *ty) {
  if (ty->kind == TY_STRUCT || ty->kind == TY_UNION) {
    println("  ldobj %s", to_cil_typename(ty));
    return;
  }

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

  if (ty->size == 1) {
    println("  ldind.i1");
    return;
  } else if (ty->size == 2) {
    println("  ldind.i2");
    return;
  } else if (ty->size == 4) {
    println("  ldind.i4");
    return;
  } else if (ty->size == 8) {
    println("  ldind.i8");
    return;
  }
  unreachable();
}

static void store(Type *ty) {
  if (ty->kind == TY_STRUCT || ty->kind == TY_UNION) {
    println("  stobj %s", to_cil_typename(ty));
    println("  ldobj %s", to_cil_typename(ty));
    return;
  }

  if (ty->kind == TY_PTR) {
    println("  conv.u");
    println("  stind.i");
    println("  ldind.i");
    return;
  }

  if (ty->size == 1) {
    println("  stind.i1");
    println("  ldind.i1");
    return;
  } else if (ty->size == 2) {
    println("  stind.i2");
    println("  ldind.i2");
    return;
  } else if (ty->size == 4) {
    println("  stind.i4");
    println("  ldind.i4");
    return;
  } else if (ty->size == 8) {
    println("  stind.i8");
    println("  ldind.i8");
    return;
  }
  unreachable();
}

enum { I8, I16, I32, I64 };

static int getTypeId(Type *ty) {
  switch (ty->kind) {
  case TY_CHAR:
    return I8;
  case TY_SHORT:
    return I16;
  case TY_INT:
    return I32;
  }
  return I64;
}

// The table for type casts
static char convi8[] = "conv.i1";
static char convi16[] = "conv.i2";
static char convi32[] = "conv.i4";
static char convi64[] = "conv.i8";

static char *cast_table[][10] = {
// to i8   i16      i32      i64       // from
  {NULL,   NULL,    NULL,    convi64}, // i8
  {convi8, NULL,    NULL,    convi64}, // i16
  {convi8, convi16, NULL,    convi64}, // i32
  {convi8, convi16, convi32, NULL},    // i64
};

static void cast(Type *from, Type *to) {
  if (to->kind == TY_VOID)
    return;

  if (to->kind == TY_BOOL) {
    println("  ldc.i4.0");
    println("  ceq");
    println("  ldc.i4.0");
    println("  ceq");
    return;
  }

  int t1 = getTypeId(from);
  int t2 = getTypeId(to);
  if (cast_table[t1][t2])
    println("  %s", cast_table[t1][t2]);
}

static void gen_expr(Node *node) {
  switch (node->kind) {
  case ND_NUM:
    if (node->ty->kind == TY_LONG)
      println("  ldc.i8 %ld", node->val);
    else
      println("  ldc.i4 %ld", node->val);
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
  case ND_CAST:
    gen_expr(node->lhs);
    cast(node->lhs->ty, node->ty);
    return;
  case ND_NOT:
    gen_expr(node->lhs);
    println("  ldc.i4.0");
    println("  ceq");
    return;
  case ND_BITNOT:
    gen_expr(node->lhs);
    println("  not");
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

static void emit_type_recursive(Type *ty) {
  switch (ty->kind) {
    case TY_PTR:
    case TY_ARRAY:
      emit_type_recursive(ty->base);
      break;
    case TY_ENUM:
      println(".enumeration public int32 %s", to_cil_typename(ty));
      for (EnumMember *mem = ty->enum_members; mem; mem = mem->next) {
        println("  %s %d", get_string(mem->name), mem->val);
      }
      break;
    case TY_STRUCT:
    case TY_UNION:
      println(".structure public %s explicit", to_cil_typename(ty));
      for (Member *mem = ty->members; mem; mem = mem->next) {
        println("  public %s %s %d", to_cil_typename(mem->ty), get_string(mem->name), mem->offset);
      }
      // Emit member type recursively.
      for (Member *mem = ty->members; mem; mem = mem->next) {
        emit_type_recursive(mem->ty);
      }
      break;
  }
}

static void emit_type(Obj *prog) {
  for (Obj *fn = prog; fn; fn = fn->next) {
    if (!fn->is_function)
      continue;

    // Included parameter/local variable types.
    for (Obj *var = fn->params; var; var = var->next) {
      emit_type_recursive(var->ty);
    }

    for (Obj *var = fn->locals; var; var = var->next) {
      emit_type_recursive(var->ty);
    }
  }
}

// Assign offsets to local variables.
static void assign_lvar_offsets(Obj *prog) {
  for (Obj *fn = prog; fn; fn = fn->next) {
    if (!fn->is_function)
      continue;

    int param_offset = 0;
    for (Obj *var = fn->params; var; var = var->next) {
      var->offset = param_offset;
      param_offset++;
    }

    int local_offset = 0;
    for (Obj *var = fn->locals; var; var = var->next) {
      var->offset = local_offset;
      local_offset++;
    }
  }
}

static void emit_data(Obj *prog) {
  for (Obj *var = prog; var; var = var->next) {
    if (var->is_function)
      continue;

    print(".global public %s %s", to_cil_typename(var->ty), var->name);

    if (var->init_data) {
      if (var->ty->kind == TY_ARRAY) {
        for (int i = 0; i < var->ty->size; i++)
          print(" 0x%hhx", var->init_data[i]);
      }
    }

    println("");
  }
}

static void emit_text(Obj *prog) {
  for (Obj *fn = prog; fn; fn = fn->next) {
    if (!fn->is_function || !fn->is_definition)
      continue;

    if (fn->is_static)
      print(".function file %s %s", to_cil_typename(fn->ty->return_ty), fn->name);
    else
      print(".function public %s %s", to_cil_typename(fn->ty->return_ty), fn->name);
    
    for (Obj *var = fn->params; var; var = var->next) {
      print(" %s:%s", var->name, to_cil_typename(var->ty));
    }
    println("");
    current_fn = fn;

    // Prologue
    for (Obj *var = fn->locals; var; var = var->next) {
      println("  .local %s %s", to_cil_typename(var->ty), var->name);
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
  emit_type(prog);
  emit_data(prog);
  emit_text(prog);
}
