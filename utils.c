#include "chibicc.h"

int calculate_size(Type *ty) {
  if (ty->size && ty->size->kind == ND_NUM)
    return ty->size->val;
  switch (ty->kind) {
    case TY_ARRAY: {
      if (!ty->array_len)
        return 0;
      int sz = calculate_size(ty->base);
      if (sz >= 0)
        return sz * ty->array_len;
      break;
    }
    case TY_STRUCT:
    case TY_UNION:
      if (!ty->members)
        return 0;
      break;
  }
  return -1;
}

Node *align_to_node(Node *n, Node *align) {
  // (n + align - 1) / align * align;
  Node *next = new_binary(
    ND_SUB, new_binary(ND_ADD, n, align, NULL), new_num(1, NULL), NULL);
  Node *based = new_binary(
    ND_DIV, next, align, NULL);
  return new_binary(
    ND_MUL, based, align, NULL);
}

char *to_cil_typename(Type *ty) {
  static int count = 0;

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
    case TY_FLOAT_COMPLEX:
      return "_FloatComplex";
    case TY_DOUBLE_COMPLEX:
      return "_DoubleComplex";
    case TY_VA_LIST:
      return "va_list";
  }

  if (ty->cil_name)
    return ty->cil_name;

  switch (ty->kind) {
    case TY_ARRAY: {
      if (ty->array_len >= 1)
        ty->cil_name = format("%s[%d]", to_cil_typename(ty->base), ty->array_len);
      else
        // Flexible array (0) / Unapplied size array (-1)
        ty->cil_name = format("%s[*]", to_cil_typename(ty->base));
      return ty->cil_name;
    }
    case TY_PTR: {
      ty->cil_name = format("%s*", to_cil_typename(ty->base));
      return ty->cil_name;
    }
    case TY_ENUM:
    case TY_STRUCT:
    case TY_UNION: {
      if (ty->tag_name)
        ty->cil_name = ty->is_public ?
          ty->tag_name :
          format("_%s_$%d", ty->tag_name, count++);
      else if (ty->typedef_name) {
        char *name = get_string(ty->typedef_name);
        ty->cil_name = ty->is_public ?
          name :
          format("_%s_$%d", name, count++);
      } else
        ty->cil_name = format("_tag_$%d", count++);
      return ty->cil_name;
    }
    case TY_FUNC: {
      char *c = "";
      Type *pty = ty->params;
      while (pty) {
        c = *c == '\0' ? to_cil_typename(pty) : format("%s,%s", c, to_cil_typename(pty));
        pty = pty->next;
      }
      // .NET: See **VARARG**
      ty->cil_name = ty->is_variadic ?
        format("%s(%s,C.type.__va_arglist)", to_cil_typename(ty->return_ty), c) :
        format("%s(%s)", to_cil_typename(ty->return_ty), c);
      return ty->cil_name;
    }
  }
  unreachable();
}

bool equals_type(Type *lhs, Type *rhs) {
  if (lhs == NULL || rhs == NULL)
    // Be not added types on add_type().
    unreachable();

  if (lhs == rhs)
    return true;

  if (lhs->origin)
    return equals_type(lhs->origin, rhs);

  if (rhs->origin)
    return equals_type(lhs, rhs->origin);

  if (lhs->kind != rhs->kind)
    return false;

  switch (lhs->kind) {
    case TY_CHAR:
    case TY_SHORT:
    case TY_INT:
    case TY_LONG:
    case TY_NINT:
      return lhs->is_unsigned == rhs->is_unsigned;
    case TY_FLOAT:
    case TY_DOUBLE:
      return true;
    case TY_PTR:
      return equals_type(lhs->base, rhs->base);
    case TY_FUNC: {
      if (!equals_type(lhs->return_ty, rhs->return_ty))
        return false;
      if (lhs->is_variadic != rhs->is_variadic)
        return false;

      Type *lt = lhs->params;
      Type *rt = lhs->params;
      for (; lt && rt; lt = lt->next, rt = rt->next)
        if (!equals_type(lt, rt))
          return false;
      return lt == NULL && rt == NULL;
    }
    case TY_ARRAY:
      if (!equals_type(lhs->base, rhs->base))
        return false;
      return lhs->array_len == rhs->array_len;
  }
  return false;
}

bool equals_node(Node *lhs, Node *rhs) {
  if (lhs == rhs)
    return true;
  if (lhs == NULL || rhs == NULL)
    return false;

  if (lhs->kind != rhs->kind) {
    // For displacement caching (2)
    if (lhs->kind == ND_VAR &&
      lhs->var->init_expr &&
      lhs->var->init_expr->kind == ND_ASSIGN &&
      lhs->var->init_expr->lhs == lhs)
      return equals_node(lhs->var->init_expr->rhs, rhs);
    else if (rhs->kind == ND_VAR &&
      rhs->var->init_expr &&
      rhs->var->init_expr->kind == ND_ASSIGN &&
      rhs->var->init_expr->lhs == rhs)
      return equals_node(lhs, rhs->var->init_expr->rhs);
    return false;
  }

  switch (lhs->kind) {
    case ND_ADD:
    case ND_SUB:
    case ND_MUL:
    case ND_DIV:
    case ND_MOD:
    case ND_BITAND:
    case ND_BITOR:
    case ND_BITXOR:
    case ND_SHL:
    case ND_SHR:
    case ND_EQ:
    case ND_NE:
    case ND_LT:
    case ND_LE:
    case ND_LOGAND:
    case ND_LOGOR:
    case ND_COMMA:
    case ND_ASSIGN:
    case ND_COMPLEX:
      return
        equals_node(lhs->lhs, rhs->lhs) &&
        equals_node(lhs->rhs, rhs->rhs);
    case ND_NEG:
    case ND_NOT:
    case ND_BITNOT:
    case ND_ADDR:
    case ND_DEREF:
      return equals_node(lhs->lhs, rhs->lhs);
    case ND_CAST:
      return
        equals_type(lhs->ty, rhs->ty) &&
        equals_node(lhs->lhs, rhs->lhs);
    case ND_MEMBER:
      if (!equals_node(lhs->lhs, rhs->lhs))
        return false;
      return lhs->member == rhs->member;
    case ND_COND:
      return
        equals_node(lhs->cond, rhs->cond) &&
        equals_node(lhs->then, rhs->then) &&
        equals_node(lhs->els, rhs->els);
    case ND_NUM:
      return lhs->val == rhs->val;
    case ND_VAR:
    case ND_MEMZERO:
      return lhs->var == rhs->var;
    case ND_SIZEOF:
      return equals_type(lhs->sizeof_ty, rhs->sizeof_ty);
    case ND_NULL_EXPR:
      return true;
    default:
      unreachable();
  }
}

static bool is_immutable(Node *node) {
  switch (node->kind) {
    case ND_ADD:
    case ND_SUB:
    case ND_MUL:
    case ND_DIV:
    case ND_MOD:
    case ND_BITAND:
    case ND_BITOR:
    case ND_BITXOR:
    case ND_SHL:
    case ND_SHR:
    case ND_EQ:
    case ND_NE:
    case ND_LT:
    case ND_LE:
    case ND_LOGAND:
    case ND_LOGOR:
    case ND_COMMA:
    case ND_ASSIGN:
    case ND_COMPLEX:
      return is_immutable(node->lhs) && is_immutable(node->rhs);
    case ND_NEG:
    case ND_NOT:
    case ND_BITNOT:
    case ND_ADDR:
    case ND_DEREF:
    case ND_CAST:
      return is_immutable(node->lhs);
    case ND_COND:
      return is_immutable(node->cond) && is_immutable(node->then) && is_immutable(node->els);
    case ND_MEMBER:
    case ND_VAR:
    case ND_MEMZERO:
      return false;
    case ND_NUM:
    case ND_SIZEOF:
    case ND_NULL_EXPR:
      return true;
    default:
      unreachable();
  }
}

int64_t get_by_integer(Node *node) {
  switch (node->ty->kind) {
  case TY_BOOL:
    return node->val ? 1 : 0;
  case TY_CHAR:
    return node->ty->is_unsigned ? (uint8_t)node->val : (int8_t)node->val;
  case TY_SHORT:
    return node->ty->is_unsigned ? (uint16_t)node->val : (int16_t)node->val;
  case TY_INT:
    return node->ty->is_unsigned ? (uint32_t)node->val : (int32_t)node->val;
  case TY_ENUM:
    return (int32_t)node->val;
  case TY_LONG:
    return node->ty->is_unsigned ? (uint64_t)node->val : node->val;
  case TY_NINT:
  case TY_PTR:
    return node->ty->is_unsigned ? (uint64_t)(void *)node->val : (int64_t)(void *)node->val;
  case TY_FLOAT:
    return (int64_t)(float)node->fval;
  case TY_DOUBLE:
    return (int64_t)(double)node->fval;
  }
  unreachable();
}

static double get_by_flonum(Node *node) {
  switch (node->ty->kind) {
  case TY_BOOL:
    return node->val ? 1.0 : 0.0;
  case TY_CHAR:
    return node->ty->is_unsigned ? (uint8_t)node->val : (int8_t)node->val;
  case TY_SHORT:
    return node->ty->is_unsigned ? (uint16_t)node->val : (int16_t)node->val;
  case TY_INT:
    return node->ty->is_unsigned ? (uint32_t)node->val : (int32_t)node->val;
  case TY_ENUM:
    return (int32_t)node->val;
  case TY_LONG:
    return node->ty->is_unsigned ? (uint64_t)node->val : node->val;
  case TY_NINT:
  case TY_PTR:
    return node->ty->is_unsigned ? (uint64_t)(void *)node->val : (int64_t)(void *)node->val;
  case TY_FLOAT:
    return (float)node->fval;
  case TY_DOUBLE:
    return node->fval;
  }
  unreachable();
}

static Node *cast_type(Node *node, Type *ty) {
  if (node->kind == ND_NUM) {
    switch (ty->kind) {
    case TY_BOOL:
    case TY_CHAR:
    case TY_SHORT:
    case TY_INT:
    case TY_ENUM:
    case TY_LONG:
    case TY_NINT:
    case TY_PTR:
      return new_typed_num(get_by_integer(node), ty, node->tok);
    case TY_FLOAT:
    case TY_DOUBLE:
      return new_flonum(get_by_flonum(node), ty, node->tok);
    }
    unreachable();
  }
  
  add_type(node);
  if (equals_type(node->ty, ty))
    return node;
  else
    return new_cast(node, ty);
}

static Node *reduce(Node *node);

static bool is_integer_equals(Node *node, int64_t val) {
  return node->kind == ND_NUM && is_integer(node->ty) && node->val == val;
}

static bool is_integer_not_equals(Node *node, int64_t val) {
  return node->kind == ND_NUM && is_integer(node->ty) && node->val != val;
}

static bool is_flonum_equals(Node *node, double fval) {
  return node->kind == ND_NUM && is_flonum(node->ty) && node->fval == fval;
}

static Node *reduce_(Node *node) {
  Node *lhs;
  Node *rhs;

  switch (node->kind) {
  case ND_ADD:
    lhs = reduce(node->lhs);
    rhs = reduce(node->rhs);
    if (is_integer_equals(lhs, 0) || is_flonum_equals(lhs, 0.0))
      return cast_type(rhs, node->ty);
    else if (is_integer_equals(rhs, 0) || is_flonum_equals(rhs, 0.0))
      return cast_type(lhs, node->ty);
    else if (lhs->kind != ND_NUM || rhs->kind != ND_NUM)
      return new_binary(ND_ADD, lhs, rhs, node->tok);
    break;
  case ND_SUB:
    lhs = reduce(node->lhs);
    rhs = reduce(node->rhs);
    if (is_integer_equals(rhs, 0) || is_flonum_equals(rhs, 0.0))
      return cast_type(lhs, node->ty);
    else if (lhs->kind != ND_NUM || rhs->kind != ND_NUM)
      return new_binary(ND_SUB, lhs, rhs, node->tok);
    break;
  case ND_MUL:
    lhs = reduce(node->lhs);
    rhs = reduce(node->rhs);
    if (is_integer_equals(lhs, 0) || is_flonum_equals(lhs, 0.0))
      return cast_type(lhs, node->ty);
    else if (is_integer_equals(lhs, 1) || is_flonum_equals(lhs, 1.0))
      return cast_type(rhs, node->ty);
    else if (is_integer_equals(rhs, 0) || is_flonum_equals(rhs, 0.0))
      return cast_type(rhs, node->ty);
    else if (is_integer_equals(rhs, 1) || is_flonum_equals(rhs, 1.0))
      return cast_type(lhs, node->ty);
    else if (lhs->kind != ND_NUM || rhs->kind != ND_NUM)
      return new_binary(ND_MUL, lhs, rhs, node->tok);
    break;
  case ND_DIV:
    lhs = reduce(node->lhs);
    rhs = reduce(node->rhs);
    if (is_integer_equals(lhs, 0) || is_flonum_equals(lhs, 0.0))
      return cast_type(lhs, node->ty);
    else if (is_integer_equals(rhs, 1) || is_flonum_equals(rhs, 1.0))
      return cast_type(lhs, node->ty);
    else if (lhs->kind != ND_NUM || rhs->kind != ND_NUM)
      return new_binary(ND_DIV, lhs, rhs, node->tok);
    break;
  case ND_MOD:
    lhs = reduce(node->lhs);
    rhs = reduce(node->rhs);
    if (is_integer_equals(lhs, 0) || is_flonum_equals(lhs, 0.0))
      return cast_type(lhs, node->ty);
    else if (is_integer_equals(rhs, 1))
      return new_typed_num(0, node->ty, node->tok);
    else if (is_flonum_equals(rhs, 1.0))
      return new_flonum(0.0, node->ty, node->tok);
    else if (lhs->kind != ND_NUM || rhs->kind != ND_NUM)
      return new_binary(ND_MOD, lhs, rhs, node->tok);
    break;
  case ND_BITAND:
    lhs = reduce(node->lhs);
    rhs = reduce(node->rhs);
    if (is_integer_equals(lhs, 0))
      return new_typed_num(0, node->ty, node->tok);
    else if (is_integer_equals(rhs, 0))
      return new_typed_num(0, node->ty, node->tok);
    else if (lhs->kind != ND_NUM || rhs->kind != ND_NUM)
      return new_binary(ND_BITAND, lhs, rhs, node->tok);
    break;
  case ND_BITOR:
    lhs = reduce(node->lhs);
    rhs = reduce(node->rhs);
    if (is_integer_equals(lhs, 0))
      return cast_type(rhs, node->ty);
    else if (is_integer_equals(rhs, 0))
      return cast_type(lhs, node->ty);
    else if (lhs->kind != ND_NUM || rhs->kind != ND_NUM)
      return new_binary(ND_BITOR, lhs, rhs, node->tok);
    break;
  case ND_BITXOR:
    lhs = reduce(node->lhs);
    rhs = reduce(node->rhs);
    if (is_integer_equals(lhs, 0))
      return cast_type(rhs, node->ty);
    else if (is_integer_equals(rhs, 0))
      return cast_type(lhs, node->ty);
    else if (lhs->kind != ND_NUM || rhs->kind != ND_NUM)
      return new_binary(ND_BITXOR, lhs, rhs, node->tok);
    break;
  case ND_SHL:
  case ND_SHR:
    lhs = reduce(node->lhs);
    rhs = reduce(node->rhs);
    if (is_integer_equals(lhs, 0))
      return new_typed_num(0, node->ty, node->tok);
    else if (is_integer_equals(rhs, 0))
      return cast_type(lhs, node->ty);
    else if (lhs->kind != ND_NUM || rhs->kind != ND_NUM)
      return new_binary(node->kind, lhs, rhs, node->tok);
    break;
  case ND_LOGAND:
    lhs = reduce(node->lhs);
    rhs = reduce(node->rhs);
    if (is_integer_equals(lhs, 0))
      return cast_type(lhs, node->ty);
    else if (is_integer_not_equals(lhs, 0))
      return cast_type(rhs, node->ty);
    else if (is_integer_equals(rhs, 0))
      return cast_type(rhs, node->ty);
    else if (is_integer_not_equals(rhs, 0))
      return cast_type(lhs, node->ty);
    else if (lhs->kind != ND_NUM || rhs->kind != ND_NUM)
      return new_binary(ND_LOGAND, lhs, rhs, node->tok);
    break;
  case ND_LOGOR:
    lhs = reduce(node->lhs);
    rhs = reduce(node->rhs);
    if (is_integer_equals(lhs, 0))
      return cast_type(rhs, node->ty);
    else if (is_integer_not_equals(lhs, 0))
      return cast_type(lhs, node->ty);
    else if (is_integer_equals(rhs, 0))
      return cast_type(lhs, node->ty);
    else if (is_integer_not_equals(rhs, 0))
      return cast_type(rhs, node->ty);
    else if (lhs->kind != ND_NUM || rhs->kind != ND_NUM)
      return new_binary(ND_LOGOR, lhs, rhs, node->tok);
    break;
  case ND_EQ:
    lhs = reduce(node->lhs);
    rhs = reduce(node->rhs);
    if (equals_node(lhs, rhs) && is_immutable(lhs))
      return new_typed_num(1, node->ty, node->tok);
    else if (lhs->kind == ND_NUM && rhs->kind == ND_NUM) {
      bool li = is_integer(lhs->ty);
      bool ri = is_integer(rhs->ty);
      if (li && ri)
        return new_typed_num((lhs->val == rhs->val) ? 1 : 0, node->ty, node->tok);
      double lv = li ? lhs->val : lhs->fval;
      double rv = ri ? rhs->val : rhs->fval;
      return new_typed_num((lv == rv) ? 1 : 0, node->ty, node->tok);
    }
    else
      return new_binary(ND_EQ, lhs, rhs, node->tok);
    break;
  case ND_NE:
    lhs = reduce(node->lhs);
    rhs = reduce(node->rhs);
    if (!equals_node(lhs, rhs) && is_immutable(lhs))
      return new_typed_num(0, node->ty, node->tok);
    else if (lhs->kind == ND_NUM && rhs->kind == ND_NUM) {
      bool li = is_integer(lhs->ty);
      bool ri = is_integer(rhs->ty);
      if (li && ri)
        return new_typed_num((lhs->val != rhs->val) ? 1 : 0, node->ty, node->tok);
      double lv = li ? lhs->val : lhs->fval;
      double rv = ri ? rhs->val : rhs->fval;
      return new_typed_num((lv != rv) ? 1 : 0, node->ty, node->tok);
    }
    else
      return new_binary(ND_NE, lhs, rhs, node->tok);
    break;
  case ND_LE:
    lhs = reduce(node->lhs);
    rhs = reduce(node->rhs);
    if (equals_node(lhs, rhs) && is_immutable(lhs))
      return new_typed_num(1, node->ty, node->tok);
    else if (lhs->kind != ND_NUM || rhs->kind != ND_NUM)
      return new_binary(ND_LE, lhs, rhs, node->tok);
    break;
  case ND_LT:
    lhs = reduce(node->lhs);
    rhs = reduce(node->rhs);
    if (equals_node(lhs, rhs) && is_immutable(lhs))
      return new_typed_num(0, node->ty, node->tok);
    else if (lhs->kind != ND_NUM || rhs->kind != ND_NUM)
      return new_binary(ND_LT, lhs, rhs, node->tok);
    break;
  case ND_COMMA:
    lhs = reduce(node->lhs);
    rhs = reduce(node->rhs);
    if (lhs->kind == ND_NULL_EXPR)
      return cast_type(rhs, node->ty);
    return new_binary(ND_COMMA, lhs, rhs, node->tok);
  case ND_NEG:
  case ND_NOT:
  case ND_BITNOT:
    lhs = reduce(node->lhs);
    if (lhs->kind != ND_NUM)
      return new_unary(node->kind, lhs, node->tok);
    break;
  case ND_COND: {
    Node *cond = reduce(node->cond);
    if (cond->kind != ND_NUM) {
      Node *nnode = new_node(ND_COND, node->tok);
      nnode->cond = cond;
      nnode->then = reduce(node->then);
      nnode->els = reduce(node->els);
      return nnode;
    } else if (is_integer(cond->ty))
      return cond->val ? reduce(node->then) : reduce(node->els);
    else
      return reduce(node->els);
    break;
  }
  case ND_CAST:
    lhs = reduce(node->lhs);
    return cast_type(lhs, node->ty);
  case ND_ASSIGN:
    lhs = reduce(node->lhs);
    rhs = reduce(node->rhs);
    return new_binary(ND_ASSIGN, lhs, rhs, node->tok);
  case ND_ADDR:
  case ND_DEREF: {
    lhs = reduce(node->lhs);
    Node *nnode = new_node(node->kind, node->tok);
    nnode->lhs = lhs;
    return nnode;
  }
  case ND_MEMBER: {
    lhs = reduce(node->lhs);
    Node *nnode = new_node(ND_MEMBER, node->tok);
    nnode->lhs = lhs;
    nnode->member = node->member;
    return nnode;
  }
  case ND_SIZEOF: {
    int sz = calculate_size(node->sizeof_ty);
    if (sz >= 0)
      return new_typed_num(sz, node->ty, node->tok);
    else
      return node;
  }
  case ND_VAR: {
    // W: Updated init_expr.
    if (node->var->init_expr)
      node->var->init_expr = reduce(node->var->init_expr);
    return node;
  }
  case ND_NUM:
  case ND_MEMZERO:
  case ND_NULL_EXPR:
    return node;
  case ND_COMPLEX:
    lhs = reduce(node->lhs);
    rhs = reduce(node->rhs);
    return new_binary(ND_COMPLEX, lhs, rhs, node->tok);
  default:
    unreachable();
  }

  if (is_integer(node->ty))
  {
    switch (node->kind) {
    case ND_ADD:
      return new_typed_num(get_by_integer(lhs) + get_by_integer(rhs), node->ty, node->tok);
    case ND_SUB:
      return new_typed_num(get_by_integer(lhs) - get_by_integer(rhs), node->ty, node->tok);
    case ND_MUL:
      return new_typed_num(get_by_integer(lhs) * get_by_integer(rhs), node->ty, node->tok);
    case ND_DIV:
      if (node->ty->is_unsigned)
        return new_typed_num((uint64_t)get_by_integer(lhs) / get_by_integer(rhs), node->ty, node->tok);
      else
        return new_typed_num(get_by_integer(lhs) / get_by_integer(rhs), node->ty, node->tok);
    case ND_NEG:
      return new_typed_num(-get_by_integer(lhs), node->ty, node->tok);
    case ND_MOD:
      if (node->ty->is_unsigned)
        return new_typed_num((uint64_t)get_by_integer(lhs) % get_by_integer(rhs), node->ty, node->tok);
      else
        return new_typed_num(get_by_integer(lhs) % get_by_integer(rhs), node->ty, node->tok);
    case ND_BITAND:
      return new_typed_num(get_by_integer(lhs) & get_by_integer(rhs), node->ty, node->tok);
    case ND_BITOR:
      return new_typed_num(get_by_integer(lhs) | get_by_integer(rhs), node->ty, node->tok);
    case ND_BITXOR:
      return new_typed_num(get_by_integer(lhs) ^ get_by_integer(rhs), node->ty, node->tok);
    case ND_SHL:
      return new_typed_num(get_by_integer(lhs) << (int32_t)get_by_integer(rhs), node->ty, node->tok);
    case ND_SHR:
      if (node->ty->is_unsigned)
        return new_typed_num((uint64_t)get_by_integer(lhs) >> (int32_t)get_by_integer(rhs), node->ty, node->tok);
      else
        return new_typed_num(get_by_integer(lhs) >> (int32_t)get_by_integer(rhs), node->ty, node->tok);
    case ND_EQ:
      return new_typed_num(get_by_integer(lhs) == get_by_integer(rhs) ? 1 : 0, node->ty, node->tok);
    case ND_NE:
      return new_typed_num(get_by_integer(lhs) != get_by_integer(rhs) ? 1 : 0, node->ty, node->tok);
    case ND_LT:
      if (node->ty->is_unsigned)
        return new_typed_num((uint64_t)get_by_integer(lhs) < get_by_integer(rhs) ? 1 : 0, node->ty, node->tok);
      else
        return new_typed_num(get_by_integer(lhs) < get_by_integer(rhs) ? 1 : 0, node->ty, node->tok);
    case ND_LE:
      if (node->ty->is_unsigned)
        return new_typed_num((uint64_t)get_by_integer(lhs) <= get_by_integer(rhs) ? 1 : 0, node->ty, node->tok);
      else
        return new_typed_num(get_by_integer(lhs) <= get_by_integer(rhs) ? 1 : 0, node->ty, node->tok);
    case ND_NOT:
      return new_typed_num(!get_by_integer(lhs), node->ty, node->tok);
    case ND_BITNOT:
      return new_typed_num(~get_by_integer(lhs), node->ty, node->tok);
    case ND_LOGAND:
      return new_typed_num(get_by_integer(lhs) && get_by_integer(rhs), node->ty, node->tok);
    case ND_LOGOR:
      return new_typed_num(get_by_integer(lhs) || get_by_integer(rhs), node->ty, node->tok);
    }
  } else {
    switch (node->kind) {
    case ND_ADD:
      return new_flonum(get_by_flonum(lhs) + get_by_flonum(rhs), node->ty, node->tok);
    case ND_SUB:
      return new_flonum(get_by_flonum(lhs) - get_by_flonum(rhs), node->ty, node->tok);
    case ND_MUL:
      return new_flonum(get_by_flonum(lhs) * get_by_flonum(rhs), node->ty, node->tok);
    case ND_DIV:
      return new_flonum(get_by_flonum(lhs) / get_by_flonum(rhs), node->ty, node->tok);
    case ND_NEG:
      return new_flonum(-get_by_flonum(lhs), node->ty, node->tok);
    case ND_MOD:
      return new_flonum(get_by_integer(lhs) % get_by_integer(rhs), node->ty, node->tok);
    case ND_EQ:
      return new_flonum(get_by_flonum(lhs) == get_by_flonum(rhs) ? 1 : 0, node->ty, node->tok);
    case ND_NE:
      return new_flonum(get_by_flonum(lhs) != get_by_flonum(rhs) ? 1 : 0, node->ty, node->tok);
    case ND_LT:
      return new_flonum(get_by_flonum(lhs) < get_by_flonum(rhs) ? 1 : 0, node->ty, node->tok);
    case ND_LE:
      return new_flonum(get_by_flonum(lhs) <= get_by_flonum(rhs) ? 1 : 0, node->ty, node->tok);
    case ND_NOT:
      return new_flonum(!get_by_flonum(lhs), node->ty, node->tok);
    }
  }

  unreachable();
}

static Node *reduce(Node *node) {
  if (!node)
    return node;
  if (!node->is_reduced) {
    node = reduce_(node);
    node->is_reduced = true;
  }
  return node;
}

Node *reduce_node(Node *node) {
  add_type(node);
  node = reduce(node);
  add_type(node);
  return node;
}
