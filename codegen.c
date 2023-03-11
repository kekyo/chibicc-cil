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

static void gen_expr(Node *node, bool will_discard);
static bool gen_stmt(Node *node);

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

static bool add_using_type(Type *ty) {
  UsingType *pt = using_type;
  while (pt) {
    if (pt->ty == ty)
      return false;
    pt = pt->next;
  }

  pt = calloc(sizeof(UsingType), 1);
  pt->ty = ty;
  pt->next = using_type;
  using_type = pt;
  return true;
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
      if (add_using_type(ty)) {
        // Emit member type recursively.
        for (Member *mem = ty->members; mem; mem = mem->next)
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
      return ty->is_unsigned ? "uint8" : "int8";
    case TY_SHORT:
      return ty->is_unsigned ? "uint16" : "int16";
    case TY_INT:
      return ty->is_unsigned ? "uint32" : "int32";
    case TY_LONG:
      return ty->is_unsigned ? "uint64" : "int64";
    case TY_NINT:
      return ty->is_unsigned ? "nuint" : "nint";
    case TY_FLOAT:
      return "float32";
    case TY_DOUBLE:
      return "float64";
    case TY_ARRAY:
      if (ty->array_len >= 1)
        return format("%s[%d]", to_cil_typename(ty->base), ty->array_len);
      else
        // Flexible array (0) / Unapplied size array (-1)
        return format("%s[*]", to_cil_typename(ty->base));
    case TY_PTR:
      return format("%s*", to_cil_typename(ty->base));
    case TY_ENUM:
    case TY_STRUCT:
    case TY_UNION:
      if (ty->tag)
        return format("%s_%p", get_string(ty->tag), ty);
      else
        return format("tag_%p", ty);
    case TY_VA_LIST:
      return "System.ArgIterator";
  }
  unreachable();
}

// Compute the absolute address of a given node.
// It's an error if a given node does not reside in memory.
static void gen_addr(Node *node) {
  switch (node->kind) {
  case ND_VAR:
  case ND_SWITCH:
  case ND_MEMZERO:
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
      default:
        unreachable();
        break;
    }
    unreachable();
    return;
  case ND_DEREF:
    gen_expr(node->lhs, false);
    return;
  case ND_COMMA:
    gen_expr(node->lhs, true);
    gen_addr(node->rhs);
    return;
  case ND_MEMBER:
    gen_addr(node->lhs);
    // Suppress of the calculation for offset 0 might be done by reducer,
    // but ND_MEMBER is lost when suppress by reducer.
    // As a result, if special consideration is needed for member access,
    // it will be inaccessible.
    if (node->member->offset->kind != ND_NUM || node->member->offset->val != 0) {
      gen_expr(node->member->offset, false);
      println("  add");
    }
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
    case TY_VA_LIST:
      println("  ldobj %s", to_cil_typename(ty));
      return;
    case TY_BOOL:
      println("  ldind.u1");
      return;
    case TY_ENUM:
      println("  ldind.i4");
      return;
    case TY_LONG:
      println("  ldind.i8");
      return;
    case TY_NINT:
    case TY_PTR:
      println("  ldind.i");
      return;
    case TY_FLOAT:
      println("  ldind.r4");
      return;
    case TY_DOUBLE:
      println("  ldind.r8");
      return;
  }

  char *insn = ty->is_unsigned ? "u" : "i";
  switch (ty->kind) {
    case TY_CHAR:
      println("  ldind.%s1", insn);
      return;
    case TY_SHORT:
      println("  ldind.%s2", insn);
      return;
    case TY_INT:
      println("  ldind.%s4", insn);
      return;
  }
  unreachable();
}

// Store stack top to an address that the stack top is pointing to.
static void store(Type *ty) {
  switch (ty->kind) {
    case TY_STRUCT:
    case TY_UNION:
    case TY_VA_LIST:
      println("  stobj %s", to_cil_typename(ty));
      return;
    case TY_NINT:
    case TY_PTR:
      println("  stind.i");
      return;
    case TY_BOOL:
    case TY_CHAR:
      println("  stind.i1");
      return;
    case TY_SHORT:
      println("  stind.i2");
      return;
    case TY_ENUM:
    case TY_INT:
      println("  stind.i4");
      return;
    case TY_LONG:
      println("  stind.i8");
      return;
    case TY_FLOAT:
      println("  stind.r4");
      return;
    case TY_DOUBLE:
      println("  stind.r8");
      return;
  }
  unreachable();
}

static void gen_comp_val(Type *comp_ty) {
  println("  ldc.i4.0");
  switch (comp_ty->kind) {
    case TY_LONG:
      println("  conv.i8");
      break;
    case TY_NINT:
    case TY_PTR:
      println("  conv.i");
      break;
    case TY_FLOAT:
      println("  conv.r4");
      break;
    case TY_DOUBLE:
      println("  conv.r8");
      break;
  }
}

typedef enum { I8, I16, I32, I64, NINT, U8, U16, U32, U64, NUINT, F32, F64 } TypeId;

static TypeId getTypeId(Type *ty) {
  switch (ty->kind) {
  case TY_BOOL:
    return U8;
  case TY_CHAR:
    return ty->is_unsigned ? U8 : I8;
  case TY_SHORT:
    return ty->is_unsigned ? U16 : I16;
  case TY_ENUM:
    return I32;
  case TY_INT:
    return ty->is_unsigned ? U32 : I32;
  case TY_LONG:
    return ty->is_unsigned ? U64 : I64;
  case TY_NINT:
  case TY_PTR:
    return ty->is_unsigned ? NUINT : NINT;
  case TY_FLOAT:
    return F32;
  case TY_DOUBLE:
    return F64;
  }
  return NUINT;
}

// The table for type casts
static char convi1[] = "conv.i1";
static char convu1[] = "conv.u1";
static char convi2[] = "conv.i2";
static char convu2[] = "conv.u2";
static char convi4[] = "conv.i4";
static char convu4[] = "conv.u4";
static char convi8[] = "conv.i8";
static char convu8[] = "conv.u8";
static char convnint[] = "conv.i";
static char convnuint[] = "conv.u";
static char convrun[] = "conv.r.un";
static char convr4[] = "conv.r4";
static char convr8[] = "conv.r8";

static char *cast_table[][12] = {
// to i8    i16     i32     i64     nint       u8      u16     u32     u64     nuint      f32       f64             // from
  { NULL,   NULL,   NULL,   convi8, convnint,  NULL,   NULL,   NULL,   convi8, convnint,  convr4,   convr8,  },    // i8
  { convi1, NULL,   NULL,   convi8, convnint,  convu1, NULL,   NULL,   convi8, convnint,  convr4,   convr8,  },    // i16
  { convi1, convi2, NULL,   convi8, convnint,  convu1, convu2, NULL,   convi8, convnint,  convr4,   convr8,  },    // i32
  { convi1, convi2, convi4, NULL,   convnint,  convu1, convu2, convu4, NULL,   convnint,  convr4,   convr8,  },    // i64
  { convi1, convi2, convi4, convi8, NULL,      convu1, convu2, convu4, convi8, NULL,      convr4,   convr8,  },    // nint
  { NULL,   NULL,   NULL,   convu8, convnuint, NULL,   NULL,   NULL,   convu8, convnuint, convrun,  convrun, },    // u8
  { convu1, NULL,   NULL,   convu8, convnuint, convu1, NULL,   NULL,   convu8, convnuint, convrun,  convrun, },    // u16
  { convu1, convu2, NULL,   convu8, convnuint, convu1, convu2, NULL,   convu8, convnuint, convrun,  convrun, },    // u32
  { convu1, convu2, convu4, NULL,   convnuint, convu1, convu2, convu4, NULL,   convnuint, convrun,  convrun, },    // u64
  { convu1, convu2, convu4, convu8, NULL,      convu1, convu2, convu4, convu8, NULL,      convrun,  convrun, },    // nuint
  { convi1, convi2, convi4, convi8, convnint,  convu1, convu2, convu4, convu8, convnuint, NULL,     convr8,  },    // f32
  { convi1, convi2, convi4, convi8, convnint,  convu1, convu2, convu4, convu8, convnuint, convr4,   NULL,    },    // f64
};

static void cast(Type *from, Type *to) {
  if (from == to)
    return;
  if (from->kind == to->kind)
    return;
  if (to->kind == TY_VOID)
    return;

  if (to->kind == TY_BOOL) {
    gen_comp_val(from);
    println("  ceq");
    println("  ldc.i4.0");
    println("  ceq");
    return;
  }

  TypeId t1 = getTypeId(from);
  TypeId t2 = getTypeId(to);
  char *caster = cast_table[t1][t2];
  if (caster) {
    println("  %s", caster);
    if (from->is_unsigned && to->kind == TY_DOUBLE)
      println("  conv.r8");
  }
}

static void gen_funcall(Node *node, bool will_discard) {
  // Special case: __builtin_va_start(&ap, arg)
  if (strcmp(node->funcname, "__builtin_va_start") == 0) {
    if (!node->args)
      error_tok(node->tok, "invalid argument");
    else {
      gen_expr(node->args, false);
      println("  arglist");
      println("  call System.ArgIterator..ctor System.RuntimeArgumentHandle");
    }
    return;
  }

  // Special case: __builtin_va_arg(&ap, (int*)0)
  if (strcmp(node->funcname, "__builtin_va_arg") == 0) {
    // &ap
    Node *arg = node->args;
    if (!arg)
      error_tok(node->tok, "invalid argument");
    gen_expr(arg, false);
    if (!arg->next)
      error_tok(arg->tok, "invalid argument");
    // (int*)0
    Node *typename_expr = arg->next;
    if (typename_expr->kind != ND_CAST || typename_expr->ty->kind != TY_PTR)
      error_tok(arg->tok, "invalid argument");
    // 0
    if (typename_expr->ty->base->kind != TY_INT || typename_expr->lhs->kind != ND_NUM || typename_expr->lhs->val != 0)
      error_tok(arg->tok, "invalid argument");

    println("  call System.ArgIterator.GetNextArg");
    if (!will_discard) {
      // int
      Type *typename_ty = typename_expr->ty->base;
      println("  refanyval %s", to_cil_typename(typename_ty));
    }
    else
      println("  pop");

    // Avoid casting at parent.
    node->ty = typename_expr->ty;
    return;
  }

  for (Node *arg = node->args; arg; arg = arg->next)
    gen_expr(arg, false);
  print("  call %s", node->funcname);
  if (node->func_ty->is_variadic) {
    for (Node *arg = node->args; arg; arg = arg->next) {
      if (arg->ty->kind == TY_ARRAY)
        print(" %s", to_cil_typename(pointer_to(arg->ty->base, arg->tok)));
      else
        print(" %s", to_cil_typename(arg->ty));
    }
  }
  println("");
  if (will_discard) {
    if (node->ty->kind != TY_VOID)
      println("  pop");
  } else {
    switch (node->ty->kind) {
    case TY_BOOL:
      println("  conv.u1");
      println("  ldc.i4.0");
      println("  ceq");
      println("  ldc.i4.0");
      println("  ceq");
      return;
    case TY_CHAR:
      if (node->ty->is_unsigned)
        println("  conv.u1");
      else
        println("  conv.i1");
      return;
    case TY_SHORT:
      if (node->ty->is_unsigned)
        println("  conv.u2");
      else
        println("  conv.i2");
      return;
    case TY_NINT:
    case TY_PTR:
      if (node->ty->is_unsigned)
        println("  conv.u");
      else
        println("  conv.i");
      return;
    case TY_FLOAT:
      println("  conv.r4");
      return;
    case TY_DOUBLE:
      println("  conv.r8");
      return;
    }
  }
}

// If will_discard is true, the result must be discarded.
static void gen_expr(Node *node, bool will_discard) {
  if (node->tok)
    println("  .location 1 %d %d %d %d",
      node->tok->line_no - 1,
      node->tok->column_no - 1,
      node->tok->line_no - 1,
      node->tok->column_no + node->tok->len - 1);

  switch (node->kind) {
  case ND_NULL_EXPR:
    return;
  case ND_NUM:
    if (!will_discard) {
      switch (node->ty->kind) {
        case TY_CHAR:
        case TY_SHORT:
        case TY_INT:
        case TY_ENUM:
          println("  ldc.i4 %d", (int32_t)node->val);
          return;
        case TY_LONG:
          println("  ldc.i8 %ld", node->val);
          return;
        case TY_NINT:
        case TY_PTR:
          println("  ldc.i8 %ld", node->val);
          println("  conv.i");
          return;
        case TY_FLOAT:
          println("  ldc.r4 %f", (float)node->fval);
          return;
        case TY_DOUBLE:
          println("  ldc.r8 %f", node->fval);
          return;
      }
      unreachable();
    }
    return;
  case ND_NEG:
    gen_expr(node->lhs, will_discard);
    if (!will_discard)
      println("  neg");
    return;
  case ND_VAR:
  case ND_MEMBER:
    if (!will_discard) {
      gen_addr(node);
      load(node->ty);
    }
    return;
  case ND_DEREF:
    gen_expr(node->lhs, will_discard);
    if (!will_discard)
      load(node->ty);
    return;
  case ND_ADDR:
    if (!will_discard)
      gen_addr(node->lhs);
    return;
  case ND_SIZEOF:
    if (!will_discard) {
      switch (node->sizeof_ty->kind) {
        case TY_STRUCT:
          // Struct size with flexible array will be calculated invalid size by sizeof opcode.
          if (node->sizeof_ty->is_flexible) {
            aggregate_type(node->sizeof_ty);
            gen_expr(node->sizeof_ty->size, false);
            return;
          }
          break;
        case TY_ARRAY:
          // Flexible array will be calculated invalid size by sizeof opcode.
          if (node->sizeof_ty->array_len == 0) {
            aggregate_type(node->sizeof_ty);
            println("  ldc.i4.0");
            println("  conv.u");   // Cast to size_t
            return;
          } else if (node->sizeof_ty->array_len < 0)
            unreachable();
          break;
      }
      println("  sizeof %s", to_cil_typename(node->sizeof_ty));
      println("  conv.u");   // Cast to size_t
      aggregate_type(node->sizeof_ty);
    }
    return;
  case ND_ASSIGN:
    gen_addr(node->lhs);
    if (!will_discard)
      println("  dup");
    gen_expr(node->rhs, false);
    store(node->ty);
    if (!will_discard)
      load(node->ty);
    return;
  case ND_STMT_EXPR: {
    bool dead = false;
    for (Node *n = node->body; n; n = n->next) {
      if (!dead) {
        if (n->next) {
          if (!gen_stmt(n))
            dead = true;
        } else
          gen_expr(n->lhs, will_discard);
      } else if (n->kind == ND_LABEL && n->is_resolved_label) {
        dead = false;
        if (n->next) {
          if (!gen_stmt(n))
            dead = true;
        } else
          gen_expr(n->lhs, will_discard);
      }
    }
    return;
  }
  case ND_COMMA:
    gen_expr(node->lhs, true);
    gen_expr(node->rhs, will_discard);
    return;
  case ND_CAST:
    gen_expr(node->lhs, will_discard);
    if (!will_discard)
      cast(node->lhs->ty, node->ty);
    return;
  case ND_MEMZERO:
    gen_addr(node);
    println("  initobj %s", to_cil_typename(node->var->ty));
    return;
  case ND_COND: {
    int c = count();
    gen_expr(node->cond, false);
    gen_comp_val(node->cond->ty);
    println("  beq _L_else_%d", c);
    gen_expr(node->then, will_discard);
    println("  br _L_end_%d", c);
    println("_L_else_%d:", c);
    gen_expr(node->els, will_discard);
    println("_L_end_%d:", c);
    return;
  }
  case ND_NOT:
    gen_expr(node->lhs, will_discard);
    if (!will_discard) {
      gen_comp_val(node->lhs->ty);
      println("  ceq");
    }
    return;
  case ND_BITNOT:
    gen_expr(node->lhs, will_discard);
    if (!will_discard)
      println("  not");
    return;
  case ND_LOGAND: {
    int c = count();
    if (!will_discard) {
      gen_expr(node->lhs, false);
      gen_comp_val(node->lhs->ty);
      println("  beq.s _L_false_%d", c);
      gen_expr(node->rhs, false);
      gen_comp_val(node->rhs->ty);
      println("  beq.s _L_false_%d", c);
      println("  ldc.i4.1");
      println("  br.s _L_end_%d", c);
      println("_L_false_%d:", c);
      println("  ldc.i4.0");
      println("_L_end_%d:", c);
    } else {
      gen_expr(node->lhs, false);
      gen_comp_val(node->lhs->ty);
      println("  beq.s _L_end_%d", c);
      gen_expr(node->rhs, true);
      println("_L_end_%d:", c);
    }
    return;
  }
  case ND_LOGOR: {
    int c = count();
    if (!will_discard) {
      gen_expr(node->lhs, false);
      gen_comp_val(node->lhs->ty);
      println("  bne.un.s _L_true_%d", c);
      gen_expr(node->rhs, false);
      gen_comp_val(node->rhs->ty);
      println("  bne.un.s _L_true_%d", c);
      println("  ldc.i4.0");
      println("  br.s _L_end_%d", c);
      println("_L_true_%d:", c);
      println("  ldc.i4.1");
      println("_L_end_%d:", c);
    } else {
      gen_expr(node->lhs, false);
      gen_comp_val(node->lhs->ty);
      println("  bne.un.s _L_end_%d", c);
      gen_expr(node->rhs, true);
      println("_L_end_%d:", c);
    }
    return;
  }
  case ND_FUNCALL:
    gen_funcall(node, will_discard);
    return;
  }

  gen_expr(node->lhs, will_discard);
  gen_expr(node->rhs, will_discard);

  if (will_discard)
    return;

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
    if (node->ty->is_unsigned)
      println("  div.un");
    else
      println("  div");
    return;
  case ND_MOD:
    if (node->ty->is_unsigned)
      println("  rem.un");
    else
      println("  rem");
    return;
  case ND_BITAND:
    println("  and");
    return;
  case ND_BITOR:
    println("  or");
    return;
  case ND_BITXOR:
    println("  xor");
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
    if (node->lhs->ty->is_unsigned)
      println("  clt.un");
    else
      println("  clt");
    return;
  case ND_LE:
    if (node->lhs->ty->is_unsigned)
      println("  cgt.un");
    else
      println("  cgt");
    println("  ldc.i4.0");
    println("  ceq");
    return;
  case ND_SHL:
    println("  shl");
    return;
  case ND_SHR:
    if (node->lhs->ty->is_unsigned)
      println("  shr.un");
    else
      println("  shr");
    return;
  }

  error_tok(node->tok, "invalid expression");
}

// When true is returned, the execution flow continues.
static bool gen_stmt(Node *node) {
  if (node->tok)
    println("  .location 1 %d %d %d %d",
      node->tok->line_no - 1,
      node->tok->column_no - 1,
      node->tok->line_no - 1,
      node->tok->column_no + node->tok->len - 1);

  switch (node->kind) {
  case ND_IF: {
    int c = count();
    gen_expr(node->cond, false);
    println("  brfalse _L_else_%d", c);
    if (gen_stmt(node->then))
      println("  br _L_end_%d", c);
    println("_L_else_%d:", c);
    if (node->els)
      gen_stmt(node->els);
    println("_L_end_%d:", c);
    return true;
  }
  case ND_FOR: {
    int c = count();
    if (node->init) {
      if (!gen_stmt(node->init))
        unreachable();
    }
    println("_L_begin_%d:", c);
    if (node->cond) {
      gen_expr(node->cond, false);
      println("  brfalse %s", node->brk_label);
    }
    bool req = gen_stmt(node->then);
    if (req || node->is_resolved_cont) {
      println("%s:", node->cont_label);
      if (node->inc)
        gen_expr(node->inc, true);
      println("  br _L_begin_%d", c);
    }
    println("%s:", node->brk_label);
    return true;
  }
  case ND_DO: {
    int c = count();
    println("_L_begin_%d:", c);
    bool req = gen_stmt(node->then);
    if (req || node->is_resolved_cont) {
      println("%s:", node->cont_label);
      gen_expr(node->cond, false);
      println("  brtrue _L_begin_%d", c);
    }
    println("%s:", node->brk_label);
    return true;
  }
  case ND_SWITCH:
    gen_expr(node->cond, true);
    for (Node *n = node->case_next; n; n = n->case_next) {
      gen_addr(node);
      load(node->var->ty);
      switch (node->cond->ty->kind) {
      case TY_LONG:
        println("  ldc.i8 %ld", n->val);
        break;
      case TY_NINT:
        println("  ldc.i8 %ld", n->val);
        println("  conv.i");
        break;
      default:
        println("  ldc.i4 %d", (int32_t)n->val);
        break;
      }
      println("  beq %s", n->label);
    }
    if (node->default_case)
      println("  br %s", node->default_case->label);
    else
      println("  br %s", node->brk_label);
    gen_stmt(node->then);
    println("%s:", node->brk_label);
    return true;
  case ND_CASE:
    println("%s:", node->label);
    return gen_stmt(node->lhs);
  case ND_BLOCK: {
    Node *n = node->body;
    bool dead = false;
    while (n) {
      if (!dead) {
        if (!gen_stmt(n))
          dead = true;
      } else if (n->kind == ND_CASE || (n->kind == ND_LABEL && n->is_resolved_label)) {
        dead = false;
        if (!gen_stmt(n))
          dead = true;
      }
      n = n->next;
    }
    return !dead;
  }
  case ND_GOTO:
    println("  br %s", node->unique_label);
    return false;
  case ND_LABEL:
    println("%s:", node->unique_label);
    return gen_stmt(node->lhs);
  case ND_RETURN:
    if (node->lhs)
      gen_expr(node->lhs, false);
    println("  br _L_return");
    return false;
  case ND_EXPR_STMT:
    gen_expr(node->lhs, true);
    return true;
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
      case TY_STRUCT: {
        println(".structure public %s", to_cil_typename(ty));
        Node zero = {ND_NUM, 0};
        Node *last_offset = &zero;
        Node *last_size = &zero;
        for (Member *mem = ty->members; mem; mem = mem->next) {
          // Exactly equals both offsets.
          if (equals_node(mem->offset, mem->origin_offset))
            println("  public %s %s", to_cil_typename(mem->ty), get_string(mem->name));
          // Couldn't adjust with padding when offset/size is not concrete.
          // (Because mem->offset and ty->size are reduced.)
          else if (mem->offset->kind != ND_NUM || last_size->kind != ND_NUM)
            error_tok(mem->name, "Could not adjust on alignment this member.");
          // (Assertion for concrete numeric value.)
          else if (last_offset->kind == ND_NUM) {
            int64_t pad_start = last_offset->val + last_size->val;
            int64_t pad_size = mem->offset->val - pad_start;
            println("  internal uint8[%ld] $pad_$%ld", pad_size, pad_start);
            println("  public %s %s", to_cil_typename(mem->ty), get_string(mem->name));
          } else
            unreachable();
          last_offset = mem->offset;
          last_size = mem->ty->size;
        }
        // Not equals both sizes.
        if (!equals_node(ty->size, ty->origin_size)) {
          if (ty->size->kind != ND_NUM || last_size->kind != ND_NUM)
            error_tok(ty->name, "Could not adjust on alignment this type.");
          else if (last_offset->kind == ND_NUM) {
            int64_t pad_start = last_offset->val + last_size->val;
            int64_t pad_size = ty->size->val - pad_start;
            println("  internal uint8[%ld] $pad_$%ld", pad_size, pad_start);
          } else
            unreachable();
        }
        break;
      }
      case TY_UNION:
        println(".structure public %s explicit", to_cil_typename(ty));
        for (Member *mem = ty->members; mem; mem = mem->next)
          println("  public %s %s 0", to_cil_typename(mem->ty), get_string(mem->name));
        // Not equals both sizes.
        if (!equals_node(ty->size, ty->origin_size)) {
          if (ty->size->kind != ND_NUM)
            error_tok(ty->name, "Could not adjust on alignment this type.");
          else
            println("  internal uint8[%ld] $pad_$0 0", ty->size->val);
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
    if (var->is_function || !var->is_definition)
      continue;

    if (var->is_static)
      print(".global file %s %s", to_cil_typename(var->ty), var->name);
    else
      print(".global public %s %s", to_cil_typename(var->ty), var->name);

    if (var->init_data) {
      for (int i = 0; i < var->init_data_size; i++)
        print(" 0x%hhx", var->init_data[i]);
    }
    
    println("");
    
    if (var->init_expr) {
      println(".initializer public");
        gen_expr(var->init_expr, true);
        println("  ret");
    }
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
    
    for (Obj *var = fn->params; var; var = var->next)
      print(" %s:%s", var->name, to_cil_typename(var->ty));

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
