#include "chibicc.h"

static Node size0_node = {ND_NUM, 0};
static Node size1_node = {ND_NUM, 1};
static Node size4_node = {ND_NUM, 4};

Type *ty_void = &(Type){TY_VOID, &(Node){ND_NUM, 1}, &(Node){ND_NUM, 1}};
Type *ty_bool = &(Type){TY_BOOL, &(Node){ND_NUM, 1}, &(Node){ND_NUM, 1}};

Type *ty_char = &(Type){TY_CHAR, &(Node){ND_NUM, 1}, &(Node){ND_NUM, 1}};
Type *ty_short = &(Type){TY_SHORT, &(Node){ND_NUM, 2}, &(Node){ND_NUM, 2}};
Type *ty_int = &(Type){TY_INT, &(Node){ND_NUM, 4}, &(Node){ND_NUM, 4}};
Type *ty_long = &(Type){TY_LONG, &(Node){ND_NUM, 8}, &(Node){ND_NUM, 8}};

static Type *new_type(TypeKind kind) {
  Type *ty = calloc(1, sizeof(Type));
  ty->kind = kind;
  return ty;
}

bool is_integer(Type *ty) {
  TypeKind k = ty->kind;
  return k == TY_BOOL || k == TY_CHAR || k == TY_SHORT ||
         k == TY_INT  || k == TY_LONG || k == TY_ENUM;
}

Type *copy_type(Type *ty) {
  Type *ret = calloc(1, sizeof(Type));
  *ret = *ty;
  return ret;
}

Type *pointer_to(Type *base, Token *tok) {
  Type *ty = new_type(TY_PTR);
  ty->base = base;
  Node *size = new_sizeof(ty, NULL);
  ty->size = size;
  ty->align = size;
  return ty;
}

Type *func_type(Type *return_ty) {
  Type *ty = calloc(1, sizeof(Type));
  ty->kind = TY_FUNC;
  ty->return_ty = return_ty;
  return ty;
}

Type *array_of(Type *base, int len) {
  Type *ty = new_type(TY_ARRAY);
  ty->base = base;
  ty->array_len = len;
  ty->align = base->align;
  ty->size = new_sizeof(ty, NULL);
  return ty;
}

Type *enum_type(void) {
  Type *ty = new_type(TY_ENUM);
  ty->align = &size4_node;
  ty->size = &size4_node;
  return ty;
}

Type *struct_type(void) {
  Type *ty = new_type(TY_STRUCT);
  ty->align = &size1_node;
  ty->size = &size0_node;
  return ty;
}

static Type *get_common_type(Type *ty1, Type *ty2, Token *tok) {
  if (ty1->base)
    return pointer_to(ty1->base, tok);
  if (ty1->kind == TY_LONG || ty2->kind == TY_LONG)
    return ty_long;
  return ty_int;
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
    node->ty = (node->val == (int)node->val) ? ty_int : ty_long;
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
    if (node->lhs->ty->kind != TY_STRUCT)
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
  case ND_ADDR:
    if (node->lhs->ty->kind == TY_ARRAY)
      node->ty = pointer_to(node->lhs->ty->base, node->tok);
    else
      node->ty = pointer_to(node->lhs->ty, node->tok);
    return;
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
  }
}
