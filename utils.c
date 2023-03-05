#include "chibicc.h"

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

Node *align_to_node(Node *n, Node *align, Token *tok) {
  // (n + align - 1) / align * align;
  Node *next = new_binary(
    ND_SUB, new_binary(ND_ADD, n, align, tok), new_num(1, tok), tok);
  Node *based = new_binary(
    ND_DIV, next, align, tok);
  return new_binary(
    ND_MUL, based, align, tok);
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

static Node *reduce_node(Node *node) {
  Node *lhs;
  Node *rhs;

  switch (node->kind) {
  case ND_ADD:
    lhs = reduce_node(node->lhs);
    rhs = reduce_node(node->rhs);
    if (lhs->kind != ND_NUM || rhs->kind != ND_NUM) {
      if (lhs->kind == ND_NUM && lhs->val == 0)
        return rhs;
      else if (rhs->kind == ND_NUM && rhs->val == 0)
        return lhs;
      else
        return new_binary(node->kind, lhs, rhs, node->tok);
    }
    break;
  case ND_MUL:
    lhs = reduce_node(node->lhs);
    rhs = reduce_node(node->rhs);
    if (lhs->kind != ND_NUM || rhs->kind != ND_NUM) {
      if (lhs->kind == ND_NUM && lhs->val == 0)
        return lhs;
      else if (lhs->kind == ND_NUM && lhs->val == 1)
        return rhs;
      else if (rhs->kind == ND_NUM && rhs->val == 0)
        return rhs;
      else if (rhs->kind == ND_NUM && rhs->val == 1)
        return lhs;
      else
        return new_binary(node->kind, lhs, rhs, node->tok);
    }
    break;
  case ND_SUB:
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
    lhs = reduce_node(node->lhs);
    rhs = reduce_node(node->rhs);
    if (lhs->kind != ND_NUM || rhs->kind != ND_NUM)
      return new_binary(node->kind, lhs, rhs, node->tok);
    break;
  case ND_COMMA:
    lhs = reduce_node(node->lhs);
    rhs = reduce_node(node->rhs);
    if (lhs->kind == ND_NULL_EXPR)
      return rhs;
    return new_binary(ND_COMMA, lhs, rhs, node->tok);
  case ND_NEG:
  case ND_NOT:
  case ND_BITNOT:
    lhs = reduce_node(node->lhs);
    if (lhs->kind != ND_NUM)
      return new_unary(node->kind, lhs, node->tok);
    break;
  case ND_COND:
    lhs = reduce_node(node->cond);
    if (lhs->kind != ND_NUM) {
      Node *cond = new_node(ND_COND, node->tok);
      cond->cond = lhs;
      cond->then = reduce_node(node->then);
      cond->els = reduce_node(node->els);
      return cond;
    } else
      return lhs->val ? reduce_node(node->then) : reduce_node(node->els);
    break;
  case ND_CAST:
    lhs = reduce_node(node->lhs);
    if (lhs->kind == ND_NUM) {
      switch (node->ty->kind) {
      case TY_BOOL:
        return new_num(lhs->val ? 1 : 0, node->tok);
      case TY_CHAR:
        return new_num((uint8_t)lhs->val, node->tok);
      case TY_SHORT:
        return new_num((uint16_t)lhs->val, node->tok);
      case TY_ENUM:
      case TY_INT:
        return new_num((uint32_t)lhs->val, node->tok);
      case TY_LONG:
        return new_long(lhs->val, node->tok);
      case TY_PTR: {
        Node *nnode = new_num(lhs->val, node->tok);
        nnode->ty = node->ty;
        return nnode;
      }
      }
    }
    return new_cast(lhs, node->ty);
  case ND_ASSIGN:
    lhs = reduce_node(node->lhs);
    rhs = reduce_node(node->rhs);
    return new_binary(ND_ASSIGN, lhs, rhs, node->tok);
  case ND_ADDR:
  case ND_DEREF: {
    lhs = reduce_node(node->lhs);
    Node *nnode = new_node(node->kind, node->tok);
    nnode->lhs = lhs;
    return nnode;
  }
  case ND_MEMBER: {
    lhs = reduce_node(node->lhs);
    Node *nnode = new_node(ND_MEMBER, node->tok);
    nnode->lhs = lhs;
    nnode->member = node->member;
    return nnode;
  }
  case ND_NUM:
  case ND_VAR:
  case ND_SIZEOF:
  case ND_MEMZERO:
  case ND_NULL_EXPR:
    return node;
  default:
    unreachable();
  }

  switch (node->kind) {
  case ND_ADD:
    return new_num(lhs->val + rhs->val, node->tok);
  case ND_SUB:
    return new_num(lhs->val - rhs->val, node->tok);
  case ND_MUL:
    return new_num(lhs->val * rhs->val, node->tok);
  case ND_DIV:
    return new_num(lhs->val / rhs->val, node->tok);
  case ND_NEG:
    return new_num(-lhs->val, node->tok);
  case ND_MOD:
    return new_num(lhs->val % rhs->val, node->tok);
  case ND_BITAND:
    return new_num(lhs->val & rhs->val, node->tok);
  case ND_BITOR:
    return new_num(lhs->val | rhs->val, node->tok);
  case ND_BITXOR:
    return new_num(lhs->val ^ rhs->val, node->tok);
  case ND_SHL:
    return new_num(lhs->val << rhs->val, node->tok);
  case ND_SHR:
    return new_num(lhs->val >> rhs->val, node->tok);
  case ND_EQ:
    return new_num(lhs->val == rhs->val ? 1 : 0, node->tok);
  case ND_NE:
    return new_num(lhs->val != rhs->val ? 1 : 0, node->tok);
  case ND_LT:
    return new_num(lhs->val < rhs->val ? 1 : 0, node->tok);
  case ND_LE:
    return new_num(lhs->val <= rhs->val ? 1 : 0, node->tok);
  case ND_NOT:
    return new_num(!lhs->val, node->tok);
  case ND_BITNOT:
    return new_num(~lhs->val, node->tok);
  case ND_LOGAND:
    return new_num(lhs->val && rhs->val, node->tok);
  case ND_LOGOR:
    return new_num(lhs->val || rhs->val, node->tok);
  }

  unreachable();
}

Node *reduce(Node *node) {
  add_type(node);
  node = reduce_node(node);
  add_type(node);
  return node;
}
