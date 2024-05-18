#include "chibicc.h"

MemoryModel mem_model;

static Node *size0_node = &(Node){ND_NUM, 0};
static Node *size1_node = &(Node){ND_NUM, 1};
static Node *size2_node = &(Node){ND_NUM, 2};
static Node *size4_node = &(Node){ND_NUM, 4};
static Node *size8_node = &(Node){ND_NUM, 8};
static Node *sizenint_node = &(Node){ND_SIZEOF, 0};
static Node *sizenuint_node = &(Node){ND_SIZEOF, 0};
static Node *sizevalist_node = &(Node){ND_SIZEOF, 0};

Type *ty_void = &(Type){TY_VOID};
Type *ty_bool = &(Type){TY_BOOL};

Type *ty_char = &(Type){TY_CHAR};
Type *ty_short = &(Type){TY_SHORT};
Type *ty_int = &(Type){TY_INT};
Type *ty_long = &(Type){TY_LONG};

Type *ty_uchar = &(Type){TY_CHAR};
Type *ty_ushort = &(Type){TY_SHORT};
Type *ty_uint = &(Type){TY_INT};
Type *ty_ulong = &(Type){TY_LONG};

Type *ty_nint = &(Type){TY_NINT};
Type *ty_nuint = &(Type){TY_NINT};

Type *ty_float = &(Type){TY_FLOAT};
Type *ty_double = &(Type){TY_DOUBLE};

Type *ty_va_list = &(Type){TY_VA_LIST};

static void init_type(Type *ty, Node *sz, bool is_unsigned, bool is_fixed_size) {
  ty->size = sz;
  ty->align = sz;
  ty->is_unsigned = is_unsigned;
  ty->is_fixed_size = is_fixed_size;
}

void init_type_system(MemoryModel mm) {
  mem_model = mm;

  size0_node->ty = ty_nuint;
  size1_node->ty = ty_nuint;
  size2_node->ty = ty_nuint;
  size4_node->ty = ty_nuint;
  size8_node->ty = ty_nuint;

  switch (mem_model) {
  case M32:
    sizenint_node->kind = ND_NUM;
    sizenint_node->val = 4;
    sizenuint_node->kind = ND_NUM;
    sizenuint_node->val = 4;
    break;
  case M64:
    sizenint_node->kind = ND_NUM;
    sizenint_node->val = 8;
    sizenuint_node->kind = ND_NUM;
    sizenuint_node->val = 8;
    break;
  default:
    sizenint_node->kind = ND_SIZEOF;
    sizenint_node->sizeof_ty = ty_nint;
    sizenuint_node->kind = ND_SIZEOF;
    sizenuint_node->sizeof_ty = ty_nuint;
  }

  sizenint_node->ty = ty_nuint;    // size_t
  sizenuint_node->ty = ty_nuint;   // size_t

  sizevalist_node->kind = ND_SIZEOF;
  sizevalist_node->sizeof_ty = ty_va_list;

  init_type(ty_void, size1_node, false, true);
  init_type(ty_bool, size1_node, true, true);

  init_type(ty_char, size1_node, false, true);
  init_type(ty_short, size2_node, false, true);
  init_type(ty_int, size4_node, false, true);
  init_type(ty_long, size8_node, false, true);
  
  init_type(ty_uchar, size1_node, true, true);
  init_type(ty_ushort, size2_node, true, true);
  init_type(ty_uint, size4_node, true, true);
  init_type(ty_ulong, size8_node, true, true);

  bool is_pointer_fixed_size = mem_model != AnyCPU;
  init_type(ty_nint, sizenint_node, false, is_pointer_fixed_size);
  init_type(ty_nuint, sizenuint_node, true, is_pointer_fixed_size);

  init_type(ty_float, size4_node, false, true);
  init_type(ty_double, size8_node, false, true);

  init_type(ty_va_list, sizenuint_node, true, false);

  ty_va_list->size = sizevalist_node;
  ty_va_list->align = size8_node;
}

static Type *new_type(TypeKind kind) {
  Type *ty = calloc(1, sizeof(Type));
  ty->kind = kind;
  return ty;
}

bool is_integer(Type *ty) {
  TypeKind k = ty->kind;
  return k == TY_BOOL || k == TY_CHAR || k == TY_SHORT ||
         k == TY_INT  || k == TY_LONG || k == TY_NINT || k == TY_ENUM;
}

bool is_flonum(Type *ty) {
  return ty->kind == TY_FLOAT || ty->kind == TY_DOUBLE;
}

bool is_numeric(Type *ty) {
  return is_integer(ty) || is_flonum(ty);
}

bool is_compatible(Type *t1, Type *t2) {
  if (t1 == t2)
    return true;

  if (t1->origin)
    return is_compatible(t1->origin, t2);

  if (t2->origin)
    return is_compatible(t1, t2->origin);

  if (t1->kind != t2->kind)
    return false;

  switch (t1->kind) {
  case TY_CHAR:
  case TY_SHORT:
  case TY_INT:
  case TY_LONG:
  case TY_NINT:
    return t1->is_unsigned == t2->is_unsigned;
  case TY_FLOAT:
  case TY_DOUBLE:
    return true;
  case TY_PTR:
    return is_compatible(t1->base, t2->base);
  case TY_FUNC: {
    if (!is_compatible(t1->return_ty, t2->return_ty))
      return false;
    if (t1->is_variadic != t2->is_variadic)
      return false;

    Type *p1 = t1->params;
    Type *p2 = t2->params;
    for (; p1 && p2; p1 = p1->next, p2 = p2->next)
      if (!is_compatible(p1, p2))
        return false;
    return p1 == NULL && p2 == NULL;
  }
  case TY_ARRAY:
    if (!is_compatible(t1->base, t2->base))
      return false;
    return t1->array_len < 0 && t2->array_len < 0 &&
           t1->array_len == t2->array_len;
  }
  return false;
}

Type *copy_type(Type *ty) {
  Type *ret = calloc(1, sizeof(Type));
  *ret = *ty;
  ret->origin = ty;
  return ret;
}

Type *pointer_to(Type *base, Token *tok) {
  Type *ty = new_type(TY_PTR);
  ty->base = base;
  ty->is_unsigned = true;
  switch (mem_model) {
  case M32:
    ty->size = new_typed_num(4, ty_nint, tok);
    ty->align = ty->size;
    ty->is_fixed_size = true;
    break;
  case M64:
    ty->size = new_typed_num(8, ty_nint, tok);
    ty->align = ty->size;
    ty->is_fixed_size = true;
    break;
  default:
    ty->size = new_sizeof(ty, tok);
    ty->align = ty->size;
    break;
  }
  return ty;
}

Type *func_type(Type *return_ty, Token *tok) {
  // The C spec disallows sizeof(<function type>), but
  // GCC allows that and the expression is evaluated to 1.
  Type *ty = new_type(TY_FUNC);
  ty->size = size1_node;
  ty->return_ty = return_ty;
  ty->tok = tok;
  ty->is_fixed_size = true;
  return ty;
}

Type *array_of(Type *base, int len, Token *tok) {
  Type *ty = new_type(TY_ARRAY);
  ty->base = base;
  ty->array_len = len;
  ty->align = base->align;
  ty->size = new_sizeof(ty, NULL);
  ty->tok = tok;
  ty->is_fixed_size = base->is_fixed_size;
  return ty;
}

Type *enum_type(Token *tok) {
  Type *ty = new_type(TY_ENUM);
  ty->align = size4_node;
  ty->size = size4_node;
  ty->tok = tok;
  return ty;
}

Type *struct_type(Token *tok) {
  Type *ty = new_type(TY_STRUCT);
  ty->align = size1_node;
  ty->size = size0_node;
  ty->tok = tok;
  return ty;
}

static int get_type_comparer(Type *ty) {
  switch (ty->kind) {
  case TY_BOOL:
  case TY_CHAR:
    return 1;
  case TY_SHORT:
    return 2;
  case TY_ENUM:
  case TY_INT:
    return 3;
  case TY_LONG:
    return 4;
  case TY_NINT:
  case TY_PTR:
  case TY_FUNC:
    return 5;
  }
  unreachable();
}

static Type *get_common_type(Type *ty1, Type *ty2, Token *tok) {
  if (ty1->base)
    return pointer_to(ty1->base, tok);

  if (ty1->kind == TY_FUNC)
    return pointer_to(ty1, tok);
  if (ty2->kind == TY_FUNC)
    return pointer_to(ty2, tok);

  if (ty1->kind == TY_DOUBLE || ty2->kind == TY_DOUBLE)
    return ty_double;
  if (ty1->kind == TY_FLOAT || ty2->kind == TY_FLOAT)
    return ty_float;

  int comp1 = get_type_comparer(ty1);
  int comp2 = get_type_comparer(ty2);

  if (comp1 < 3)   // < TY_INT
    ty1 = ty_int;
  if (comp2 < 3)   // < TY_INT
    ty2 = ty_int;

  if (comp1 != comp2)
    return (comp1 < comp2) ? ty2 : ty1;

  if (ty2->is_unsigned)
    return ty2;
  return ty1;
}

// For many binary operators, we implicitly promote operands so that
// both operands have the same type. Any integral type smaller than
// int is always promoted to int. If the type of one operand is larger
// than the other's (e.g. "long" vs. "int"), the smaller operand will
// be promoted to match with the other.
//
// This operation is called the "usual arithmetic conversion".
static void usual_arith_conv(Node **lhs, Node **rhs, Token *tok) {
  Type *ty = get_common_type((*lhs)->ty, (*rhs)->ty, tok);
  if (!equals_type(ty, (*lhs)->ty))
    *lhs = new_cast(*lhs, ty);
  if (!equals_type(ty, (*rhs)->ty))
    *rhs = new_cast(*rhs, ty);
}

void add_type(Node *node) {
  if (!node || node->ty)
    return;

  add_type(node->lhs);
  add_type(node->rhs);
  add_type(node->cond);
  add_type(node->then);
  add_type(node->els);
  add_type(node->init);
  add_type(node->inc);

  for (Node *n = node->body; n; n = n->next)
    add_type(n);
  for (Node *n = node->args; n; n = n->next)
    add_type(n);

  switch (node->kind) {
  case ND_NUM:
    node->ty = ty_int;
    return;
  case ND_ADD:
  case ND_SUB:
  case ND_MUL:
  case ND_DIV:
  case ND_MOD:
  case ND_BITAND:
  case ND_BITOR:
  case ND_BITXOR:
    usual_arith_conv(&node->lhs, &node->rhs, node->tok);
    node->ty = node->lhs->ty;
    return;
  case ND_NEG: {
    Type *ty = get_common_type(ty_int, node->lhs->ty, node->tok);
    node->lhs = new_cast(node->lhs, ty);
    node->ty = ty;
    return;
  }
  case ND_ASSIGN:
    if (node->lhs->ty->kind == TY_ARRAY)
      error_tok(node->lhs->tok, "not an lvalue");
    if (node->lhs->ty->kind != TY_STRUCT && !equals_type(node->rhs->ty, node->lhs->ty))
      node->rhs = new_cast(node->rhs, node->lhs->ty);
    node->ty = node->lhs->ty;
    return;
  case ND_EQ:
  case ND_NE:
  case ND_LT:
  case ND_LE:
    usual_arith_conv(&node->lhs, &node->rhs, node->tok);
    node->ty = ty_int;
    return;
  case ND_FUNCALL:
    node->ty = ty_int;
    return;
  case ND_NOT:
  case ND_LOGOR:
  case ND_LOGAND:
    node->ty = ty_int;
    return;
  case ND_BITNOT:
  case ND_SHL:
  case ND_SHR:
    node->ty = node->lhs->ty;
    return;
  case ND_VAR:
    node->ty = node->var->ty;
    add_type(node->var->init_expr);
    return;
  case ND_COND:
    if (node->then->ty->kind == TY_VOID || node->els->ty->kind == TY_VOID)
      node->ty = ty_void;
    else {
      usual_arith_conv(&node->then, &node->els, node->tok);
      node->ty = node->then->ty;
    }
    return;
  case ND_COMMA:
    node->ty = node->rhs->ty;
    return;
  case ND_MEMBER:
    node->ty = node->member->ty;
    return;
  case ND_ADDR: {
    Type *ty = node->lhs->ty;
    if (ty->kind == TY_ARRAY)
      node->ty = pointer_to(ty->base, node->tok);
    else
      node->ty = pointer_to(ty, node->tok);
    return;
  }
  case ND_DEREF:
    if (!node->lhs->ty->base)
      error_tok(node->tok, "invalid pointer dereference");
    if (node->lhs->ty->base->kind == TY_VOID)
      error_tok(node->tok, "dereferencing a void pointer");
    node->ty = node->lhs->ty->base;
    return;
  case ND_STMT_EXPR:
    if (node->body) {
      Node *stmt = node->body;
      while (stmt->next)
        stmt = stmt->next;
      if (stmt->kind == ND_EXPR_STMT) {
        node->ty = stmt->lhs->ty;
        return;
      }
    }
    error_tok(node->tok, "statement expression returning void is not supported");
    return;
  case ND_NULL_EXPR:
    node->ty = ty_void;
    return;
  }
}
