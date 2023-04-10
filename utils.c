#include "chibicc.h"

static int calculate_size(Type *ty) {
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

Node *align_to_node(Node *n, Node *align) {
  // (n + align - 1) / align * align;
  Node *next = new_binary(
    ND_SUB, new_binary(ND_ADD, n, align, NULL), new_num(1, NULL), NULL);
  Node *based = new_binary(
    ND_DIV, next, align, NULL);
  return new_binary(
    ND_MUL, based, align, NULL);
}

static char *pretty_print(Node *node) {
  switch (node->kind)
  {
    case ND_ADD:
      return format("(%s + %s)", pretty_print(node->lhs), pretty_print(node->rhs));
    case ND_SUB:
      return format("(%s - %s)", pretty_print(node->lhs), pretty_print(node->rhs));
    case ND_MUL:
      return format("(%s * %s)", pretty_print(node->lhs), pretty_print(node->rhs));
    case ND_DIV:
      return format("(%s / %s)", pretty_print(node->lhs), pretty_print(node->rhs));
    case ND_MOD:
      return format("(%s %% %s)", pretty_print(node->lhs), pretty_print(node->rhs));
    case ND_BITAND:
      return format("(%s & %s)", pretty_print(node->lhs), pretty_print(node->rhs));
    case ND_BITOR:
      return format("(%s | %s)", pretty_print(node->lhs), pretty_print(node->rhs));
    case ND_BITXOR:
      return format("(%s ^ %s)", pretty_print(node->lhs), pretty_print(node->rhs));
    case ND_SHL:
      return format("(%s << %s)", pretty_print(node->lhs), pretty_print(node->rhs));
    case ND_SHR:
      return format("(%s >> %s)", pretty_print(node->lhs), pretty_print(node->rhs));
    case ND_EQ:
      return format("(%s == %s)", pretty_print(node->lhs), pretty_print(node->rhs));
    case ND_NE:
      return format("(%s != %s)", pretty_print(node->lhs), pretty_print(node->rhs));
    case ND_LT:
      return format("(%s < %s)", pretty_print(node->lhs), pretty_print(node->rhs));
    case ND_LE:
      return format("(%s <= %s)", pretty_print(node->lhs), pretty_print(node->rhs));
    case ND_LOGAND:
      return format("(%s && %s)", pretty_print(node->lhs), pretty_print(node->rhs));
    case ND_LOGOR:
      return format("(%s || %s)", pretty_print(node->lhs), pretty_print(node->rhs));
    case ND_COMMA:
      return format("(%s, %s)", pretty_print(node->lhs), pretty_print(node->rhs));
    case ND_ASSIGN:
      return format("(%s = %s)", pretty_print(node->lhs), pretty_print(node->rhs));
    case ND_NEG:
      return format("(-%s)", pretty_print(node->lhs));
    case ND_NOT:
      return format("(!%s)", pretty_print(node->lhs));
    case ND_BITNOT:
      return format("(~%s)", pretty_print(node->lhs));
    case ND_ADDR:
      return format("(&%s)", pretty_print(node->lhs));
    case ND_DEREF:
      return format("(*%s)", pretty_print(node->lhs));
    case ND_CAST:
      return format("(%s)%s", get_string(node->ty->name), pretty_print(node->lhs));
    case ND_MEMBER:
      return format("%s.%s", pretty_print(node->lhs), get_string(node->member->name));
    case ND_COND:
      return format("(%s ? %s : %s)", pretty_print(node->cond), pretty_print(node->then), pretty_print(node->els));
    case ND_NUM:
      return format("%ld", node->val);
    case ND_VAR:
    case ND_MEMZERO:
      return format("(init %s)", node->var->name);
    case ND_SIZEOF:
      return format("sizeof(%s)", get_string(node->sizeof_ty->name));
    case ND_NULL_EXPR:
      return format("()", get_string(node->sizeof_ty->name));
    default:
      unreachable();
  }
}

void pretty_print_node(Node *node) {
  char *pp = pretty_print(node);
  fprintf(stderr, "%s\n", pp);
}

bool equals_type(Type *lhs, Type *rhs) {
  if (lhs == NULL || rhs == NULL)
    // Be not added types on add_type().
    unreachable();
  if (lhs == rhs)
    return true;
  if (lhs->kind != rhs->kind)
    return false;
  if (lhs->is_unsigned != rhs->is_unsigned)
    return false;

  switch (lhs->kind) {
    case TY_PTR:
      return equals_type(lhs->base, rhs->base);
    case TY_ARRAY:
      return equals_type(lhs->base, rhs->base) && lhs->array_len == rhs->array_len;
    case TY_VOID:
    case TY_BOOL:
    case TY_CHAR:
    case TY_SHORT:
    case TY_INT:
    case TY_LONG:
    case TY_NINT:
    case TY_FLOAT:
    case TY_DOUBLE:
    case TY_VA_LIST:
      return true;
    case TY_ENUM:
    case TY_STRUCT:
    case TY_UNION:
      return false;
    default:
      unreachable();
  }
}

bool equals_node(Node *lhs, Node *rhs) {
  if (lhs == rhs)
    return true;
  if (lhs == NULL || rhs == NULL)
    return false;
  if (lhs->kind != rhs->kind)
    return false;

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

static int64_t get_by_integer(Node *node) {
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

static Node *reduce(Node *node) {
  if (node->is_reduced)
    return node;

  node->is_reduced = true;

  Node *lhs;
  Node *rhs;

  switch (node->kind) {
  case ND_ADD:
    lhs = reduce(node->lhs);
    rhs = reduce(node->rhs);
    if (lhs->kind != ND_NUM || rhs->kind != ND_NUM) {
      if (lhs->kind == ND_NUM && lhs->val == 0)
        return cast_type(rhs, node->ty);
      else if (rhs->kind == ND_NUM && rhs->val == 0)
        return cast_type(lhs, node->ty);
      else
        return new_binary(ND_ADD, lhs, rhs, node->tok);
    }
    break;
  case ND_SUB:
    lhs = reduce(node->lhs);
    rhs = reduce(node->rhs);
    if (lhs->kind != ND_NUM || rhs->kind != ND_NUM) {
      if (rhs->kind == ND_NUM && rhs->val == 0)
        return cast_type(lhs, node->ty);
      else
        return new_binary(ND_SUB, lhs, rhs, node->tok);
    }
    break;
  case ND_MUL:
    lhs = reduce(node->lhs);
    rhs = reduce(node->rhs);
    if (lhs->kind != ND_NUM || rhs->kind != ND_NUM) {
      if (lhs->kind == ND_NUM && lhs->val == 0)
        return cast_type(lhs, node->ty);
      else if (lhs->kind == ND_NUM && lhs->val == 1)
        return cast_type(rhs, node->ty);
      else if (rhs->kind == ND_NUM && rhs->val == 0)
        return cast_type(rhs, node->ty);
      else if (rhs->kind == ND_NUM && rhs->val == 1)
        return cast_type(lhs, node->ty);
      else
        return new_binary(ND_MUL, lhs, rhs, node->tok);
    }
    break;
  case ND_DIV:
    lhs = reduce(node->lhs);
    rhs = reduce(node->rhs);
    if (lhs->kind != ND_NUM || rhs->kind != ND_NUM) {
      if (lhs->kind == ND_NUM && lhs->val == 0)
        return cast_type(lhs, node->ty);
      else if (rhs->kind == ND_NUM && rhs->val == 1)
        return cast_type(lhs, node->ty);
      else
        return new_binary(ND_DIV, lhs, rhs, node->tok);
    }
    break;
  case ND_MOD:
    lhs = reduce(node->lhs);
    rhs = reduce(node->rhs);
    if (lhs->kind != ND_NUM || rhs->kind != ND_NUM) {
      if (lhs->kind == ND_NUM && lhs->val == 0)
        return cast_type(lhs, node->ty);
      else if (rhs->kind == ND_NUM && rhs->val == 1)
        return new_typed_num(0, node->ty, node->tok);
      else
        return new_binary(ND_MOD, lhs, rhs, node->tok);
    }
    break;
  case ND_BITAND:
    lhs = reduce(node->lhs);
    rhs = reduce(node->rhs);
    if (lhs->kind != ND_NUM || rhs->kind != ND_NUM) {
      if (lhs->kind == ND_NUM && lhs->val == 0)
        return new_typed_num(0, node->ty, node->tok);
      else if (rhs->kind == ND_NUM && rhs->val == 0)
        return new_typed_num(0, node->ty, node->tok);
      else
        return new_binary(ND_BITAND, lhs, rhs, node->tok);
    }
    break;
  case ND_BITOR:
    lhs = reduce(node->lhs);
    rhs = reduce(node->rhs);
    if (lhs->kind != ND_NUM || rhs->kind != ND_NUM) {
      if (lhs->kind == ND_NUM && lhs->val == 0)
        return cast_type(rhs, node->ty);
      else if (rhs->kind == ND_NUM && rhs->val == 0)
        return cast_type(lhs, node->ty);
      else
        return new_binary(ND_BITOR, lhs, rhs, node->tok);
    }
    break;
  case ND_BITXOR:
    lhs = reduce(node->lhs);
    rhs = reduce(node->rhs);
    if (lhs->kind != ND_NUM || rhs->kind != ND_NUM) {
      if (lhs->kind == ND_NUM && lhs->val == 0)
        return cast_type(rhs, node->ty);
      else if (rhs->kind == ND_NUM && rhs->val == 0)
        return cast_type(lhs, node->ty);
      else
        return new_binary(ND_BITXOR, lhs, rhs, node->tok);
    }
    break;
  case ND_SHL:
  case ND_SHR:
    lhs = reduce(node->lhs);
    rhs = reduce(node->rhs);
    if (lhs->kind != ND_NUM || rhs->kind != ND_NUM) {
      if (lhs->kind == ND_NUM && lhs->val == 0)
        return new_typed_num(0, node->ty, node->tok);
      else if (rhs->kind == ND_NUM && rhs->val == 0)
        return cast_type(lhs, node->ty);
      else
        return new_binary(node->kind, lhs, rhs, node->tok);
    }
    break;
  case ND_LOGAND:
    lhs = reduce(node->lhs);
    rhs = reduce(node->rhs);
    if (lhs->kind != ND_NUM || rhs->kind != ND_NUM) {
      if (lhs->kind == ND_NUM && lhs->val == 0)
        return cast_type(lhs, node->ty);
      else if (lhs->kind == ND_NUM && lhs->val != 0)
        return cast_type(rhs, node->ty);
      else if (rhs->kind == ND_NUM && rhs->val == 0)
        return cast_type(rhs, node->ty);
      else if (rhs->kind == ND_NUM && rhs->val != 0)
        return cast_type(lhs, node->ty);
      else
        return new_binary(ND_LOGAND, lhs, rhs, node->tok);
    }
    break;
  case ND_LOGOR:
    lhs = reduce(node->lhs);
    rhs = reduce(node->rhs);
    if (lhs->kind != ND_NUM || rhs->kind != ND_NUM) {
      if (lhs->kind == ND_NUM && lhs->val == 0)
        return cast_type(rhs, node->ty);
      else if (lhs->kind == ND_NUM && lhs->val != 0)
        return cast_type(lhs, node->ty);
      else if (rhs->kind == ND_NUM && rhs->val == 0)
        return cast_type(lhs, node->ty);
      else if (rhs->kind == ND_NUM && rhs->val != 0)
        return cast_type(rhs, node->ty);
      else
        return new_binary(ND_LOGOR, lhs, rhs, node->tok);
    }
    break;
  case ND_EQ:
    lhs = reduce(node->lhs);
    rhs = reduce(node->rhs);
    if (equals_node(lhs, rhs) && is_immutable(lhs))
      return new_typed_num(1, node->ty, node->tok);
    else
      return new_binary(ND_EQ, lhs, rhs, node->tok);
    break;
  case ND_NE:
    lhs = reduce(node->lhs);
    rhs = reduce(node->rhs);
    if (equals_node(lhs, rhs) && is_immutable(lhs))
      return new_typed_num(0, node->ty, node->tok);
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
  case ND_COND:
    lhs = reduce(node->cond);
    if (lhs->kind != ND_NUM) {
      Node *cond = new_node(ND_COND, node->tok);
      cond->cond = lhs;
      cond->then = reduce(node->then);
      cond->els = reduce(node->els);
      return cond;
    } else
      return lhs->val ? reduce(node->then) : reduce(node->els);
    break;
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
  case ND_NUM:
  case ND_VAR:
  case ND_MEMZERO:
  case ND_NULL_EXPR:
    return node;
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

Node *reduce_node(Node *node) {
  add_type(node);
  node = reduce(node);
  add_type(node);
  return node;
}
