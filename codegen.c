#include "chibicc.h"

static FILE *output_file;
static Obj *current_fn;

typedef struct UsingType UsingType;
struct UsingType
{
  UsingType *next;
  Type *ty;
};

static UsingType *using_type = NULL;

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
  if (ty->size && ty->size->kind == ND_NUM)
    return ty->size->val;
  switch (ty->kind) {
    case TY_ARRAY: {
      int sz = calculate_size(ty->base);
      if (sz >= 0) {
        return sz * ty->array_len;
      }
      break;
    }
  }
  return -1;
}

static void add_using_type(Type *ty) {
  UsingType *pt = calloc(sizeof(UsingType), 1);
  pt->ty = ty;
  pt->next = using_type;
  using_type = pt;
}

static void aggregate_type(Type *ty) {
  switch (ty->kind) {
    case TY_PTR:
    case TY_ARRAY:
      aggregate_type(ty->base);
      break;
    case TY_ENUM:
      add_using_type(ty);
      break;
    case TY_STRUCT:
    case TY_UNION:
      add_using_type(ty);
      // Emit member type recursively.
      for (Member *mem = ty->members; mem; mem = mem->next) {
        aggregate_type(mem->ty);
      }
      break;
  }
}

static const char *to_cil_typename(Type *ty) {
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
    case TY_ARRAY:
      return format("%s[%d]", to_cil_typename(ty->base), ty->array_len);
    case TY_PTR:
      return format("%s*", to_cil_typename(ty->base));
    case TY_ENUM:
    case TY_STRUCT:
    case TY_UNION:
      if (ty->tag)
        return format("%s_%p", get_string(ty->tag), ty);
      else
        return format("tag_%p", ty);
  }
  unreachable();
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
    unreachable();
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
  switch (ty->kind) {
    case TY_ARRAY:
      // If it is an array, do not attempt to load a value to the
      // register because in general we can't load an entire array to a
      // register. As a result, the result of an evaluation of an array
      // becomes not the array itself but the address of the array.
      // This is where "array is automatically converted to a pointer to
      // the first element of the array in C" occurs.
      return;
    case TY_STRUCT:
    case TY_UNION:
      println("  ldobj %s", to_cil_typename(ty));
      return;
    case TY_PTR:
      println("  ldind.i");
      return;
    case TY_BOOL:
      println("  ldind.u1");
      return;
    case TY_CHAR:
      println("  ldind.i1");
      return;
    case TY_SHORT:
      println("  ldind.i2");
      return;
    case TY_ENUM:
    case TY_INT:
      println("  ldind.i4");
      return;
    case TY_LONG:
      println("  ldind.i8");
      return;
  }
  unreachable();
}

// Store stack top to an address that the stack top is pointing to.
static void store(Type *ty) {
  switch (ty->kind) {
    case TY_STRUCT:
    case TY_UNION:
      println("  stobj %s", to_cil_typename(ty));
      println("  ldobj %s", to_cil_typename(ty));
      return;
    case TY_PTR:
      println("  conv.u");
      println("  stind.i");
      println("  ldind.i");
      return;
    case TY_BOOL:
      println("  stind.i1");
      println("  ldind.u1");
      return;
    case TY_CHAR:
      println("  stind.i1");
      println("  ldind.i1");
      return;
    case TY_SHORT:
      println("  stind.i2");
      println("  ldind.i2");
      return;
    case TY_ENUM:
    case TY_INT:
      println("  stind.i4");
      println("  ldind.i4");
      return;
    case TY_LONG:
      println("  stind.i8");
      println("  ldind.i8");
      return;
  }
  unreachable();
}

enum { I8, I16, I32, I64, IPTR };

static int getTypeId(Type *ty) {
  switch (ty->kind) {
  case TY_BOOL:
  case TY_CHAR:
    return I8;
  case TY_SHORT:
    return I16;
  case TY_ENUM:
  case TY_INT:
    return I32;
  case TY_LONG:
    return I64;
  case TY_PTR:
  case TY_ARRAY:
    return IPTR;
  }
  error("internal error at %s:%d, %d", __FILE__, __LINE__, ty->kind);
}

// The table for type casts
static char convi8[] = "conv.i1";
static char convi16[] = "conv.i2";
static char convi32[] = "conv.i4";
static char convi64[] = "conv.i8";
static char conviptr[] = "conv.i";

static char *cast_table[][10] = {
// to i8    i16      i32      i64      iptr         // from
  { NULL,   NULL,    NULL,    convi64, conviptr },  // i8
  { convi8, NULL,    NULL,    convi64, conviptr },  // i16
  { convi8, convi16, NULL,    convi64, conviptr },  // i32
  { convi8, convi16, convi32, NULL,    conviptr },  // i64
  { convi8, convi16, convi32, convi64, NULL     },  // iptr
};

static void cast(Type *from, Type *to) {
  if (from->kind == to->kind)
    return;
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
  if (node->tok)
    println("  .location 1 %d %d %d %d",
      node->tok->line_no - 1,
      node->tok->column_no - 1,
      node->tok->line_no - 1,
      node->tok->column_no + node->tok->len - 1);

  switch (node->kind) {
  case ND_NUM:
    if (node->ty->kind == TY_LONG)
      println("  ldc.i8 %ld", node->val);
    else
      println("  ldc.i4 %d", (int)node->val);
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
  case ND_SIZEOF: {
    switch (node->ty->kind) {
      case TY_ARRAY:
        if (node->ty->array_len == 0) {
          println("  ldc.i4.0");
          return;
        }
    }
    println("  sizeof %s", to_cil_typename(node->sizeof_ty));
    aggregate_type(node->sizeof_ty);
    return;
  }
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
  case ND_NOT:
    gen_expr(node->lhs);
    println("  ldc.i4.0");
    if (node->lhs->ty->kind == TY_LONG) {
      println("  conv.i8");
    }
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
  case ND_MOD:
    println("  rem");
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

static void emit_type(Obj *prog) {
  for (Obj *fn = prog; fn; fn = fn->next) {
    if (fn->is_function) {
      aggregate_type(fn->ty->return_ty);

      // Included parameter/local variable types.
      for (Obj *var = fn->params; var; var = var->next) {
        aggregate_type(var->ty);
      }
      for (Obj *var = fn->locals; var; var = var->next) {
        aggregate_type(var->ty);
      }
    } else
      aggregate_type(fn->ty);
  }

  while (using_type) {
    Type *ty = using_type->ty;
    switch (ty->kind) {
      case TY_ENUM:
        println(".enumeration public int32 %s", to_cil_typename(ty));
        for (EnumMember *mem = ty->enum_members; mem; mem = mem->next) {
          println("  %s %d", get_string(mem->name), mem->val);
        }
        break;
      case TY_STRUCT:
        println(".structure public %s", to_cil_typename(ty));
        for (Member *mem = ty->members; mem; mem = mem->next) {
          println("  public %s %s", to_cil_typename(mem->ty), get_string(mem->name));
        }
        break;
      case TY_UNION:
        println(".structure public %s explicit", to_cil_typename(ty));
        for (Member *mem = ty->members; mem; mem = mem->next) {
          println("  public %s %s 0", to_cil_typename(mem->ty), get_string(mem->name));
        }
        break;
    }
    using_type = using_type->next;
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
      for (int i = 0; i < var->init_data_size; i++)
        print(" 0x%hhx", var->init_data[i]);
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
      if (var->name[0] != '\0')
        println("  .local %s %s", to_cil_typename(var->ty), var->name);
      else
        println("  .local %s", to_cil_typename(var->ty));
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
  emit_type(prog);
}
