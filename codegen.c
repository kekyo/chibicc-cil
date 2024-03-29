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
static int lvar_offset = -1;

typedef enum {
  AS_NOTHING,
  AS_JUMP_ANOTHER,
  AS_CONTINUE,
} AfterStmt;

static void gen_expr(Node *node, bool will_discard);
static AfterStmt gen_stmt(Node *node);

__attribute__((format(printf, 1, 2)))
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

static void aggregate_type_recursive(Type *ty) {
  switch (ty->kind) {
    case TY_PTR:
    case TY_ARRAY:
      if (add_using_type(ty->base))
        aggregate_type_recursive(ty->base);
      break;
    case TY_ENUM:
      add_using_type(ty);
      break;
    case TY_STRUCT:
    case TY_UNION: {
      // Emit member type recursively.
      for (Member *mem = ty->members; mem; mem = mem->next) {
        if (add_using_type(mem->ty))
          aggregate_type_recursive(mem->ty);
      }
      break;
    }
    case TY_FUNC: {
      if (add_using_type(ty->return_ty))
        aggregate_type_recursive(ty->return_ty);
      for (Type *pty = ty->params; pty; pty = pty->next) {
        if (add_using_type(pty))
          aggregate_type_recursive(pty);
      }
      break;
    }
  }
}

static void aggregate_type(Type *ty) {
  if (add_using_type(ty))
    aggregate_type_recursive(ty);
}

static void make_public_type_recursive(Type *ty) {
  switch (ty->kind) {
    case TY_PTR:
    case TY_ARRAY:
      if (!ty->base->is_public) {
        ty->base->is_public = true;
        make_public_type_recursive(ty->base);
      }
      break;
    case TY_STRUCT:
    case TY_UNION: {
      // Emit member type recursively.
      for (Member *mem = ty->members; mem; mem = mem->next) {
        if (!mem->ty->is_public) {
          mem->ty->is_public = true;
          make_public_type_recursive(mem->ty);
        }
      }
      break;
    }
    case TY_FUNC: {
      // Emit return type recursively.
      if (!ty->return_ty->is_public) {
        ty->return_ty->is_public = true;
        make_public_type_recursive(ty->return_ty);
      }
      // Emit parameter type recursively.
      for (Type *pty = ty->params; pty; pty = pty->next) {
        if (!pty->is_public) {
          pty->is_public = true;
          make_public_type_recursive(pty);
        }
      }
      break;
    }
  }
}

static void make_public_type(Type *ty) {
  if (!ty->is_public) {
    ty->is_public = true;
    make_public_type_recursive(ty);
  }
}

static char *get_cil_callsite(Node *n) {
  if (n->cil_callsite)
    return n->cil_callsite;
  char *c = "";
  Node *pn = n->args;
  Type *pt = n->func_ty->params;
  while (pn && pt) {
    aggregate_type(pn->ty);
    c = *c == '\0' ? to_cil_typename(pn->ty) : format("%s,%s", c, to_cil_typename(pn->ty));
    pn = pn->next;
    pt = pt->next;
  }
  // .NET: See **VARARG**
  if (n->func_ty->is_variadic) {
    c = *c == '\0' ? "C.type.__va_arglist" : format("%s,C.type.__va_arglist", c);
  }
  aggregate_type(n->func_ty->return_ty);
  n->cil_callsite = format("%s(%s)", to_cil_typename(n->func_ty->return_ty), c);
  return n->cil_callsite;
}

static char *gen_make_temp(Type *ty) {
  char *name = format("__temp%d$", lvar_offset);
  println("  .local %s %s", to_cil_typename(ty), name);
  lvar_offset++;
  return name;
}

// Compute the absolute address of a given node.
// It's an error if a given node does not reside in memory.
static void gen_addr(Node *node) {
  switch (node->kind) {
  case ND_VAR:
  case ND_SWITCH:
  case ND_MEMZERO:
    if (node->var->ty->kind == TY_FUNC) {
      // Function symbol
      println("  ldftn %s", node->var->name);
      return;
    }
    switch (node->var->kind) {
      case OB_GLOBAL:
        // Global variable
        // ***MOVESFLD***: ldsflda is not used here. because even if it is a static field,
        //   the CLR sometimes moved its physical memory address at runtime, damn it!
        //   To deal with this problem, we use calloc in the type initializer
        //   to store true fixed addresses.
        if (node->var->is_tls) {
          println("  ldsfld __tls_%s__", node->var->name);
          println("  call __get_tls_value");
        } else
          println("  ldsfld %s", node->var->name);
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
  case ND_FUNCALL:
    gen_expr(node, false);
    // Will make address from lvar stored retval.
    println("  .local %s __retval%d$", to_cil_typename(node->ty), lvar_offset);
    println("  stloc __retval%d$", lvar_offset);
    println("  ldloca __retval%d$", lvar_offset);
    lvar_offset++;
    return;
  }

  error_tok(node->tok, "not an lvalue");
}

// Made native pointer when managed pointer store into it.
static void gen_make_ptr(Type *ty) {
  println("  .local %s* __ptr%d$", to_cil_typename(ty), lvar_offset);
  println("  stloc __ptr%d$", lvar_offset);
  println("  ldloc __ptr%d$", lvar_offset);
  lvar_offset++;
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
      gen_make_ptr(ty->base);
      return;
    case TY_FUNC:
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

static void cmp_zero(Type *ty) {
  println("  ldc.i4.0");
  switch (ty->kind) {
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
  println("  ceq");
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
  { NULL,   NULL,   NULL,   convi8, convnint,  convu1, convu2, convu4, convi8, convnint,  convr4,   convr8,  },    // i8
  { convi1, NULL,   NULL,   convi8, convnint,  convu1, convu2, convu4, convi8, convnint,  convr4,   convr8,  },    // i16
  { convi1, convi2, NULL,   convi8, convnint,  convu1, convu2, convu4, convi8, convnint,  convr4,   convr8,  },    // i32
  { convi1, convi2, convi4, NULL,   convnint,  convu1, convu2, convu4, convi8, convnint,  convr4,   convr8,  },    // i64
  { convi1, convi2, convi4, convi8, NULL,      convu1, convu2, convu4, convi8, convnint,  convr4,   convr8,  },    // nint
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
  if (from->kind == to->kind && from->is_unsigned == to->is_unsigned)
    return;
  if (to->kind == TY_VOID)
    return;

  if (to->kind == TY_BOOL) {
    cmp_zero(from);
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
  if (node->lhs->kind == ND_VAR) {
    // Special case: __builtin_trap()
    if (strcmp(node->lhs->var->name, "__builtin_trap") == 0) {
      if (node->args)
        error_tok(node->tok, "invalid argument");
      else
        println("  break");
      return;
    }

    // Special case: __builtin_va_start(&ap, arg)
    if (strcmp(node->lhs->var->name, "__builtin_va_start") == 0) {
      if (!node->args || !node->args->next)
        error_tok(node->tok, "invalid argument");
      else {
        gen_expr(node->args, false);
        println("  ldarg.s %d", node->args->next->var->offset + 1);
        println("  call __va_start");
      }
      return;
    }

    // Special case: __builtin_va_arg(&ap, (int*)0)
    if (strcmp(node->lhs->var->name, "__builtin_va_arg") == 0) {
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
      if (typename_expr->lhs->kind != ND_NUM || typename_expr->lhs->val != 0)
        error_tok(arg->tok, "invalid argument");

      if (!will_discard)
        println("  call __va_arg");

      // Avoid casting at parent.
      node->ty = typename_expr->ty;
      return;
    }

    // Special case: __builtin_va_copy(&dest, &src)
    if (strcmp(node->lhs->var->name, "__builtin_va_copy") == 0) {
      if (!node->args || !node->args->next)
        error_tok(node->tok, "invalid argument");
      else {
        gen_expr(node->args, false);
        gen_expr(node->args->next, false);
        println("  call __va_copy");
      }
      return;
    }
  }

  if (node->func_ty->is_variadic && node->func_ty->params) {  // See **VARARG**
    int param_count = 0;
    for (Type *param_ty = node->func_ty->params; param_ty; param_ty = param_ty->next, param_count++);
    int sentinel_count = 0;
    for (Node *arg = node->args; arg; arg = arg->next, sentinel_count++);
    sentinel_count -= param_count;

    Node *arg2 = node->args;
    for (int i = 0; arg2 && i < param_count; arg2 = arg2->next, i++)
      gen_expr(arg2, false);

    println("  ldc.i4 %d", sentinel_count);
    println("  call __va_arglist_new");

    for (; arg2; arg2 = arg2->next) {
      println("  dup");
      gen_expr(arg2, false);
      if (arg2->ty->kind == TY_PTR || arg2->ty->kind == TY_ARRAY)
        println("  box nint");
      else
        println("  box %s", to_cil_typename(arg2->ty));
      println("  call __va_arglist_add");
    }
  } else {
    for (Node *arg = node->args; arg; arg = arg->next)
      gen_expr(arg, false);
  }

  // Direct call
  if (node->lhs->kind == ND_VAR && node->lhs->var->ty->kind == TY_FUNC) {
    println("  call %s", node->lhs->var->name);
  } else {
    gen_expr(node->lhs, false);
    println("  calli %s", get_cil_callsite(node));
  }

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

static void gen_sizeof(Type *ty) {
  switch (ty->kind) {
    case TY_STRUCT:
      // Struct size with flexible array will be calculated invalid size by sizeof opcode.
      if (ty->is_flexible) {
        aggregate_type(ty);
        gen_expr(ty->size, false);
        return;
      }
      break;
    case TY_ARRAY:
      // Flexible array will be calculated invalid size by sizeof opcode.
      if (ty->array_len == 0) {
        aggregate_type(ty);
        println("  ldc.i4.0");
        println("  conv.u");   // Cast to size_t
        return;
      } else if (ty->array_len < 0)
        unreachable();
      break;
  }
  println("  sizeof %s", to_cil_typename(ty));
  println("  conv.u");   // Cast to size_t
  aggregate_type(ty);
}

static void gen_const_integer(Type *ty, long val) {
  switch (ty->kind) {
    case TY_BOOL:
      println("  ldc.i4.%d", val ? 1 : 0);
      return;
    case TY_CHAR:
    case TY_SHORT:
    case TY_INT:
    case TY_ENUM: {
      int32_t i32 = (int32_t)val;
      if (i32 > INT8_MAX || i32 < INT8_MIN)
        println("  ldc.i4 %d", i32);
      else if (i32 > 8 || i32 < -1)
        println("  ldc.i4.s %d", i32);
      else if (i32 == -1)
        println("  ldc.i4.m1");
      else
        println("  ldc.i4.%d", i32);
      return;
    }
    case TY_LONG: {
      if (val > INT32_MAX || val < INT32_MIN) {
        println("  ldc.i8 %ld", val);
      } else if (val > INT8_MAX || val < INT8_MIN) {
        println("  ldc.i4 %ld", val);
        println("  conv.i8");
      } else if (val > 8 || val < -1) {
        println("  ldc.i4.s %ld", val);
        println("  conv.i8");
      } else if (val == -1) {
        println("  ldc.i4.m1");
        println("  conv.i8");
      } else {
        println("  ldc.i4.%ld", val);
        println("  conv.i8");
      }
      return;
    }
    case TY_NINT:
    case TY_PTR: {
      if (val > INT32_MAX || val < INT32_MIN)
        println("  ldc.i8 %ld", val);
      else if (val > INT8_MAX || val < INT8_MIN)
        println("  ldc.i4 %ld", val);
      else if (val > 8 || val < -1)
        println("  ldc.i4.s %ld", val);
      else if (val == -1)
        println("  ldc.i4.m1");
      else
        println("  ldc.i4.%ld", val);
      println("  conv.i");
      return;
    }
  }
  unreachable();
}

static void gen_location(Node *node) {
  if (node->tok && node->tok->line_no)
    println("  .location %d %d %d %d %d",
      node->tok->file->file_no,
      node->tok->line_no - 1,
      node->tok->column_no - 1,
      node->tok->line_no - 1,
      node->tok->column_no + node->tok->len - 1);
}

// If will_discard is true, the result must be discarded.
static void gen_expr(Node *node, bool will_discard) {
  gen_location(node);

  switch (node->kind) {
  case ND_NULL_EXPR:
    return;
  case ND_NUM:
    if (!will_discard) {
      switch (node->ty->kind) {
        case TY_BOOL:
        case TY_CHAR:
        case TY_SHORT:
        case TY_INT:
        case TY_ENUM:
        case TY_LONG:
        case TY_NINT:
        case TY_PTR:
          gen_const_integer(node->ty, node->val);
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
    if (!will_discard) {
      // Load directly from local variable and the parameters
      // when it isn't an array, struct or union.
      switch (node->var->ty->kind) {
        case TY_ARRAY:
        case TY_STRUCT:
        case TY_UNION:
          break;
        default:
          switch (node->var->kind) {
            // Local variable
            case OB_LOCAL:
              println("  ldloc %d", node->var->offset);
              cast(node->var->ty, node->ty);
              return;
            // Parameter variable
            case OB_PARAM:
              println("  ldarg %d", node->var->offset);
              cast(node->var->ty, node->ty);
              return;
          }
          break;
      }
      // Load with indirect.
      gen_addr(node);
      load(node->ty);
    }
    return;
  case ND_MEMBER: {
    if (!will_discard) {
      gen_addr(node);
      
      Member *mem = node->member;
      if (mem->is_bitfield) {
        load(ty_long);

        gen_const_integer(ty_int, 64 - mem->bit_width);
        gen_expr(mem->bit_offset, false);
        println("  sub");
        println("  shl");

        gen_const_integer(ty_int, 64 - mem->bit_width);
        if (mem->ty->is_unsigned)
          println("  shr.un");
        else
          println("  shr");

        cast(ty_long, node->ty);
      } else
        load(node->ty);
    }
    return;
  }
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
    if (!will_discard)
      gen_sizeof(node->sizeof_ty);
    return;
  case ND_ASSIGN:
    if (node->lhs->kind == ND_MEMBER && node->lhs->member->is_bitfield) {
      gen_addr(node->lhs);

      // If the lhs is a bitfield, we need to read the current value
      // from memory and merge it with a new value.
      Member *mem = node->lhs->member;
      println("  dup");
      load(ty_long);

      gen_const_integer(ty_long, (1L << mem->bit_width) - 1);
      gen_expr(mem->bit_offset, false);
      println("  shl");
      println("  not");
      println("  and");

      gen_expr(node->rhs, false);
      println("  conv.i8");

      char *temp_name;
      if (!will_discard) {
        temp_name = gen_make_temp(ty_long);
        println("  stloc %s", temp_name);
        println("  ldloc %s", temp_name);
      }

      gen_const_integer(ty_long, (1L << mem->bit_width) - 1);
      println("  and");
      gen_expr(mem->bit_offset, false);
      println("  shl");
      println("  or");

      store(ty_long);
      if (!will_discard) {
        println("  ldloc %s", temp_name);
        cast(ty_long, node->ty);
      }
      return;
    }

    // Store directly to local variable and the parameters
    // when it isn't an array, struct or union.
    if (node->lhs->kind == ND_VAR) {
      switch (node->lhs->var->ty->kind) {
        case TY_ARRAY:
        case TY_STRUCT:
        case TY_UNION:
          break;
        default:
          switch (node->lhs->var->kind) {
            // Local variable
            case OB_LOCAL:
              gen_expr(node->rhs, false);
              cast(node->rhs->ty, node->lhs->var->ty);
              println("  stloc %d", node->lhs->var->offset);
              if (!will_discard) {
                println("  ldloc %d", node->lhs->var->offset);
                cast(node->lhs->var->ty, node->ty);
              }
              return;
            // Parameter variable
            case OB_PARAM:
              gen_expr(node->rhs, false);
              cast(node->rhs->ty, node->lhs->var->ty);
              println("  starg %d", node->lhs->var->offset);
              if (!will_discard) {
                println("  ldarg %d", node->lhs->var->offset);
                cast(node->lhs->var->ty, node->ty);
              }
              return;
          }
          break;
      }
    }

    // Store with indirect.
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
          if (gen_stmt(n) != AS_CONTINUE)
            dead = true;
        } else
          gen_expr(n->lhs, will_discard);
      } else if (n->kind == ND_LABEL && n->is_resolved_label) {
        dead = false;
        if (n->next) {
          if (gen_stmt(n) != AS_CONTINUE)
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
    cmp_zero(node->cond->ty);
    println("  brtrue _L_else_%d", c);
    gen_expr(node->then, will_discard);
    println("  br _L_end_%d", c);
    println("_L_else_%d:", c);
    gen_expr(node->els, will_discard);
    println("_L_end_%d:", c);
    return;
  }
  case ND_NOT:
    gen_expr(node->lhs, will_discard);
    if (!will_discard)
      cmp_zero(node->lhs->ty);
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
      cmp_zero(node->lhs->ty);
      println("  brtrue _L_false_%d", c);
      gen_expr(node->rhs, false);
      cmp_zero(node->rhs->ty);
      println("  brtrue.s _L_false_%d", c);
      println("  ldc.i4.1");
      println("  br.s _L_end_%d", c);
      println("_L_false_%d:", c);
      println("  ldc.i4.0");
      println("_L_end_%d:", c);
    } else {
      gen_expr(node->lhs, false);
      cmp_zero(node->lhs->ty);
      println("  brtrue _L_end_%d", c);
      gen_expr(node->rhs, true);
      println("_L_end_%d:", c);
    }
    return;
  }
  case ND_LOGOR: {
    int c = count();
    if (!will_discard) {
      gen_expr(node->lhs, false);
      cmp_zero(node->lhs->ty);
      println("  brfalse _L_true_%d", c);
      gen_expr(node->rhs, false);
      cmp_zero(node->rhs->ty);
      println("  brfalse.s _L_true_%d", c);
      println("  ldc.i4.0");
      println("  br.s _L_end_%d", c);
      println("_L_true_%d:", c);
      println("  ldc.i4.1");
      println("_L_end_%d:", c);
    } else {
      gen_expr(node->lhs, false);
      cmp_zero(node->lhs->ty);
      println("  brfalse _L_end_%d", c);
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
    if (node->lhs->ty->is_unsigned || is_flonum(node->lhs->ty))
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

static void gen_dummy_value(Type *ty) {
  switch (ty->kind) {
  case TY_BOOL:
  case TY_CHAR:
  case TY_SHORT:
  case TY_INT:
  case TY_ENUM:
    println("  ldc.i4.0");
    return;
  case TY_LONG:
    println("  ldc.i4.0");
    println("  conv.i8");
    return;
  case TY_FLOAT:
    println("  ldc.i4.0");
    println("  conv.r4");
    return;
  case TY_DOUBLE:
    println("  ldc.i4.0");
    println("  conv.r8");
    return;
  case TY_NINT:
  case TY_PTR:
  case TY_ARRAY:
  case TY_FUNC:
    println("  ldc.i4.0");
    println("  conv.i");
    return;
  case TY_STRUCT:
  case TY_UNION:
  case TY_VA_LIST: {
    const char *type_name = to_cil_typename(ty);
    println("  sizeof %s", type_name);
    println("  localloc");
    println("  ldobj %s", type_name);
    return;
  }
  }
  unreachable();
}

// When true is returned, the execution flow continues.
static AfterStmt gen_stmt(Node *node) {
  gen_location(node);

  switch (node->kind) {
  case ND_IF: {
    int c = count();
    gen_expr(node->cond, false);
    cmp_zero(node->cond->ty);
    println("  brtrue _L_else_%d", c);
    if (gen_stmt(node->then) == AS_CONTINUE)
      println("  br _L_end_%d", c);
    println("_L_else_%d:", c);
    if (node->els)
      gen_stmt(node->els);
    println("_L_end_%d:", c);
    return AS_CONTINUE;
  }
  case ND_FOR: {
    int c = count();
    if (node->init) {
      if (gen_stmt(node->init) != AS_CONTINUE)
        unreachable();
    }
    println("_L_begin_%d:", c);
    if (node->cond) {
      gen_expr(node->cond, false);
      cmp_zero(node->cond->ty);
      println("  brtrue %s", node->brk_label);
    }
    AfterStmt req = gen_stmt(node->then);
    if ((req == AS_CONTINUE) || node->is_resolved_cont) {
      println("%s:", node->cont_label);
      if (node->inc)
        gen_expr(node->inc, true);
      println("  br _L_begin_%d", c);
    }
    println("%s:", node->brk_label);
    return AS_CONTINUE;
  }
  case ND_DO: {
    int c = count();
    println("_L_begin_%d:", c);
    AfterStmt req = gen_stmt(node->then);
    if ((req == AS_CONTINUE) || node->is_resolved_cont) {
      println("%s:", node->cont_label);
      gen_expr(node->cond, false);
    cmp_zero(node->cond->ty);
    println("  brfalse _L_begin_%d", c);
    }
    println("%s:", node->brk_label);
    return AS_CONTINUE;
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
    return AS_CONTINUE;
  case ND_CASE:
    println("%s:", node->label);
    return gen_stmt(node->lhs);
  case ND_BLOCK: {
    Node *n = node->body;
    AfterStmt res = AS_CONTINUE;
    while (n) {
      if (res == AS_CONTINUE) {
        AfterStmt req = gen_stmt(n);
        if (req != AS_CONTINUE)
          res = req;
      } else if (n->kind == ND_CASE || (n->kind == ND_LABEL && n->is_resolved_label)) {
        res = AS_CONTINUE;
        AfterStmt req = gen_stmt(n);
        if (req != AS_CONTINUE)
          res = req;
      }
      n = n->next;
    }
    return res;
  }
  case ND_GOTO:
    println("  br %s", node->unique_label);
    return AS_JUMP_ANOTHER;
  case ND_LABEL:
    println("%s:", node->unique_label);
    return gen_stmt(node->lhs);
  case ND_RETURN:
    if (node->lhs) {
      if (current_fn->ty->return_ty->kind != TY_VOID)
        gen_expr(node->lhs, false);
      else
        gen_expr(node->lhs, true);
    } else {
      if (current_fn->ty->return_ty->kind != TY_VOID)
        // Made valid CIL sequence.
        gen_dummy_value(current_fn->ty->return_ty);
    }
    println("  br _L_return");
    return AS_JUMP_ANOTHER;
  case ND_EXPR_STMT:
    gen_expr(node->lhs, true);
    return AS_CONTINUE;
  case ND_ASM:
    println("  %s", node->asm_str);
    return AS_NOTHING;
  }

  error_tok(node->tok, "invalid statement");
}

static void aggregate_types(Obj *prog) {
  for (Obj *ob = prog; ob; ob = ob->next) {
    if (ob->kind == OB_GLOBAL_TYPE) {
      // The type of a global variable is always public,
      // even if it is marked static.
      // Since the C language does not allow types to be explicitly given a scope,
      // the type of a global variable can be considered to be in a visible state.
      make_public_type(ob->ty);
    }
    
    if (ob->is_function) {
      // Function
      aggregate_type(ob->ty->return_ty);
      make_public_type(ob->ty->return_ty);

      // Included parameter variable types.
      for (Obj *var = ob->params; var; var = var->next) {
        aggregate_type(var->ty);
        make_public_type(var->ty);
      }

      // Included local variable types.
      for (Obj *var = ob->locals; var; var = var->next)
        aggregate_type(var->ty);
    } else {
      // Global variable
      aggregate_type(ob->ty);
    }
  }
}

static char *safe_to_cil_typename(Type *ty) {
  switch (ty->kind) {
    case TY_PTR:
      return format("%s*", safe_to_cil_typename(ty->base));
    // HACK: The function pointer type will response invalid size on CLR.
    // So, this makes `void*`.
    case TY_FUNC:
      return "void";
    default:
      return to_cil_typename(ty);
  }
}

static Node *get_var_interior(Node *node)
{
  // Because maybe displacement node is cached.
  if (node->kind == ND_VAR) {
    Node *init_expr = node->var->init_expr;
    if (init_expr != NULL && init_expr->kind == ND_ASSIGN)
      return init_expr->rhs;
  }
  return node;
}

static char *get_member_name(Member *mem) {
  return mem->name != NULL ?
    get_string(mem->name) :
    format("__member%d", mem->idx);
}

static void emit_type(Obj *prog) {
  while (using_type) {
    Type *ty = using_type->ty;
    const char *ty_scope = ty->is_public ? "public" : "file";
    switch (ty->kind) {
      case TY_ENUM:
        println(".enumeration %s int32 %s", ty_scope, to_cil_typename(ty));
        for (EnumMember *mem = ty->enum_members; mem; mem = mem->next) {
          println("  %s %d", get_string(mem->name), mem->val);
        }
        break;
      case TY_STRUCT: {
        println(".structure %s %s", ty_scope, to_cil_typename(ty));
        Node zero = {ND_NUM, 0};
        Node *last_offset = &zero;
        Node *last_size = &zero;
        int bit_index = 0;
        int bit_width = 0;
        for (Member *mem = ty->members; mem; mem = mem->next) {
          if (mem->is_bitfield) {
            bit_width += mem->bit_width;
            while (bit_width >= 8) {
              println("  public uint8 __bitfield%d", bit_index++);
              bit_width -= 8;
            }
          } else {
            if (bit_width >= 1) {
              println("  public uint8 __bitfield%d", bit_index++);
              bit_width = 0;
            }
            // Exactly equals both offsets.
            if (equals_node(get_var_interior(mem->offset), mem->origin_offset))
              println("  public %s %s", safe_to_cil_typename(mem->ty), get_member_name(mem));
            // Couldn't adjust with padding when offset/size is not concrete.
            // (Because mem->offset and ty->size are reduced.)
            else if (get_var_interior(mem->offset)->kind != ND_NUM || last_size->kind != ND_NUM)
              error_tok(mem->name, "Could not adjust on alignment this member.");
            // (Assertion for concrete numeric value.)
            else if (last_offset->kind == ND_NUM) {
              int64_t pad_start = last_offset->val + last_size->val;
              int64_t pad_size = get_var_interior(mem->offset)->val - pad_start;
              if (pad_size >= 1)
                println("  internal uint8[%ld] $pad_$%ld", pad_size, pad_start);
              println("  public %s %s", safe_to_cil_typename(mem->ty), get_member_name(mem));
            } else
              unreachable();
          }
          last_offset = get_var_interior(mem->offset);
          last_size = get_var_interior(mem->ty->size);
        }
        // Not equals both sizes.
        if (!equals_node(get_var_interior(ty->size), ty->origin_size)) {
          if (get_var_interior(ty->size)->kind != ND_NUM || last_size->kind != ND_NUM)
            error_tok(ty->name, "Could not adjust on alignment this type.");
          else if (last_offset->kind == ND_NUM) {
            int64_t pad_start = last_offset->val + last_size->val;
            int64_t pad_size = get_var_interior(ty->size)->val - pad_start;
            if (pad_size >= 1)
              println("  internal uint8[%ld] $pad_$%ld", pad_size, pad_start);
          } else
            unreachable();
        }
        break;
      }
      case TY_UNION:
        println(".structure %s %s explicit", ty_scope, to_cil_typename(ty));
        for (Member *mem = ty->members; mem; mem = mem->next)
          println("  public %s %s 0", safe_to_cil_typename(mem->ty), get_member_name(mem));
        // Not equals both sizes.
        if (!equals_node(get_var_interior(ty->size), ty->origin_size)) {
          if (get_var_interior(ty->size)->kind != ND_NUM)
            error_tok(ty->name, "Could not adjust on alignment this type.");
          else if (get_var_interior(ty->size)->val >= 1)
            println("  internal uint8[%ld] $pad_$0 0", get_var_interior(ty->size)->val);
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

static void emit_data_alloc(Obj *var, bool is_static) {
  if (var->next)
    // Will make reversed order.
    emit_data_alloc(var->next, is_static);
  if (var->is_function || !var->is_definition)
    return;

  // C.data.ptr ??= <allocator>();
  int c = count();
  if (var->is_tls)
    println("  ldsfld __tls_%s__", var->name);
  else
    println("  ldsfld %s", var->name);
  println("  ldc.i4.0");
  println("  conv.u");
  println("  ceq");
  println("  brfalse.s _L_alloc_%d", c);

  if (var->is_tls) {
    // __alloc_tls_slot(size, __tls_init_$)
    if (var->ty->size->kind == ND_NUM) {
      println("  ldc.i8 %ld", var->ty->size->val);
      println("  conv.u");
    } else
      gen_sizeof(var->ty);
    println("  ldftn __tls_init_$%s", var->name);
    println("  call __alloc_tls_slot");
    println("  stsfld __tls_%s__", var->name);
  } else {
    // calloc(1, size)
    println("  ldc.i4.1");
    println("  conv.u");
    if (var->ty->size->kind == ND_NUM) {
      println("  ldc.i8 %ld", var->ty->size->val);
      println("  conv.u");
    } else
      gen_sizeof(var->ty);
    println("  call calloc");    // ***MOVESFLD***
    println("  stsfld %s", var->name);
  }

  println("_L_alloc_%d:", c);
}

static void emit_non_tls_data_init(Obj *var, bool is_static) {
  if (var->next)
    // Will make reversed order.
    emit_non_tls_data_init(var->next, is_static);
  if (var->is_function || !var->is_definition)
    return;
  if (var->is_static != is_static)
    return;
  if (var->is_tls)
    return;

  const char *ty_name = to_cil_typename(var->ty);

  if (var->init_data) {
    println("  ldsfld %s", var->name);
    println("  ldsflda _const_$%s", var->name);
    println("  ldobj %s", ty_name);
    println("  stobj %s", ty_name);
  }

  if (var->init_expr) {
    lvar_offset = 0;
    gen_expr(var->init_expr, true);
  }
}

static void emit_tls_data_init(Obj *var) {
  if (var->next)
    // Will make reversed order.
    emit_tls_data_init(var->next);
  if (var->is_function || !var->is_definition)
    return;
  if (!var->is_tls)
    return;

  const char *ty_name = to_cil_typename(var->ty);

  if (var->is_static)
    println(".function file void() __tls_init_$%s", var->name);
  else
    println(".function internal void() __tls_init_$%s", var->name);

  if (var->init_data) {
    println("  ldsfld __tls_%s__", var->name);
    println("  call __get_tls_value");
    println("  ldsflda _const_$%s", var->name);
    println("  ldobj %s", ty_name);
    println("  stobj %s", ty_name);
  }

  if (var->init_expr) {
    lvar_offset = 0;
    gen_expr(var->init_expr, true);
  }

  println("  ret");
}

static void emit_data(Obj *prog) {
  for (Obj *var = prog; var; var = var->next) {
    if (var->is_function || !var->is_definition)
      continue;

    const char *ty_name = to_cil_typename(var->ty);

    if (var->init_data) {
      // TODO: Apply constant
      if (var->is_static)
        print(".global file %s _const_$%s", ty_name, var->name);
      else
        print(".global internal %s _const_$%s", ty_name, var->name);

      for (int i = 0; i < var->init_data_size; i++)
        print(" 0x%hhx", var->init_data[i]);

      println("  ");
    }

    if (var->is_tls) {
      if (var->is_static)
        println(".global file __tls_slot* __tls_%s__", var->name);
      else
        println(".global public __tls_slot* __tls_%s__", var->name);
    } else {
      if (var->is_static)
        println(".global file %s* %s", ty_name, var->name);
      else
        println(".global public %s* %s", ty_name, var->name);

    }
  }

  if (prog) {
    println(".initializer file");
    emit_data_alloc(prog, true);
    emit_non_tls_data_init(prog, true);
    println("  ret");

    println(".initializer internal");
    emit_data_alloc(prog, false);
    emit_non_tls_data_init(prog, false);
    println("  ret");

    emit_tls_data_init(prog);
  }
}

static void emit_text(Obj *prog) {
  for (Obj *fn = prog; fn; fn = fn->next) {
    if (!fn->is_function || !fn->is_definition)
      continue;

    // No code is emitted for "static inline" functions
    // if no one is referencing them.
    if (!fn->is_live)
      continue;

    if (fn->is_static)
      print(".function file");
    else
      print(".function public");

    print(" %s(", to_cil_typename(fn->ty->return_ty));
 
    bool first = true;
    for (Obj *var = fn->params; var; var = var->next) {
      if (first)
        print("%s:%s", var->name, to_cil_typename(var->ty));
      else
        print(",%s:%s", var->name, to_cil_typename(var->ty));
      first = false;
    }

    // HACK: **VARARG**
    // chibicc-cil handles groups of parameters as follows:
    // * One or more fixed parameters:
    //   * Only fixed parameters: Same as general method calls.
    //   * Included variable parameters: Add an parameter of type `__va_arglist`.
    // * Empty any fixed parameters:
    //   * NET standard `vararg` calling convention is used.
    // A function with empty fixed parameters is treated as a variadic function
    // by C language spec. (6.7.5.3 Function declarators).
    // In such a case, if we let the function define `__va_arglist` type parameters,
    // it is assumed that the function will not define any parameters
    // by mistake when the function is written in a non-chibicc implementation (such as C#).
    // (I made a similar mistake several times when porting chibicc.)
    // So, the `vararg` calling convention is applied only in this case,
    // so that the caller does not have to worry about more than one argument (`__va_arglist`)
    // being put on the stack.
    // The reason for not using the `vararg` calling convention overall is
    // because the `System.ArgIterator` type used by the caller to implement
    // the `va_start` and `va_arg` macros for handling variable arguments
    // is NOT supported by any netstandards.
    // https://github.com/dotnet/standard/issues/20
    if (fn->ty->is_variadic)
    {
      if (fn->ty->params) {
        if (!first)
          print(",");
        print("C.type.__va_arglist");
      }
    }

    println(") %s", fn->name);
    current_fn = fn;

    // Prologue
    lvar_offset = 0;
    for (Obj *var = fn->locals; var; var = var->next) {
      if (var->name[0] != '\0')
        println("  .local %s %s", to_cil_typename(var->ty), var->name);
      else
        println("  .local %s", to_cil_typename(var->ty));
      lvar_offset++;
    }

    // Emit code
    AfterStmt req = gen_stmt(fn->body);
    if (req == AS_CONTINUE) {
      if (fn->ty->return_ty->kind != TY_VOID)
        // Made valid CIL sequence.
        gen_dummy_value(fn->ty->return_ty);
    }

    if (req != AS_NOTHING) {
      // Epilogue
      println("_L_return:");
      println("  ret");
    }
  }
}

void codegen(Obj *prog, FILE *out) {
  output_file = out;

  File **files = get_input_files();
  for (int i = 0; files[i]; i++)
    println(".file %d \"%s\" c\n", files[i]->file_no, files[i]->name);

  assign_lvar_offsets(prog);
  aggregate_types(prog);
  emit_data(prog);
  emit_text(prog);
  emit_type(prog);
}
