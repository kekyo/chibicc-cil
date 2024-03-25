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
      if (ty->tag_scope)
        ty->cil_name = ty->is_public ?
          ty->tag_scope->name :
          format("_%s_$%d", ty->tag_scope->name, count++);
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

static char *pretty_print(Node *node) {
  if (node == NULL)
    return "";

  switch (node->kind)
  {
    case ND_ADD:
      return format("/*(%s)*/(%s + %s)", to_cil_typename(node->ty), pretty_print(node->lhs), pretty_print(node->rhs));
    case ND_SUB:
      return format("/*(%s)*/(%s - %s)", to_cil_typename(node->ty), pretty_print(node->lhs), pretty_print(node->rhs));
    case ND_MUL:
      return format("/*(%s)*/(%s * %s)", to_cil_typename(node->ty), pretty_print(node->lhs), pretty_print(node->rhs));
    case ND_DIV:
      return format("/*(%s)*/(%s / %s)", to_cil_typename(node->ty), pretty_print(node->lhs), pretty_print(node->rhs));
    case ND_MOD:
      return format("/*(%s)*/(%s %% %s)", to_cil_typename(node->ty), pretty_print(node->lhs), pretty_print(node->rhs));
    case ND_BITAND:
      return format("/*(%s)*/(%s & %s)", to_cil_typename(node->ty), pretty_print(node->lhs), pretty_print(node->rhs));
    case ND_BITOR:
      return format("/*(%s)*/(%s | %s)", to_cil_typename(node->ty), pretty_print(node->lhs), pretty_print(node->rhs));
    case ND_BITXOR:
      return format("/*(%s)*/(%s ^ %s)", to_cil_typename(node->ty), pretty_print(node->lhs), pretty_print(node->rhs));
    case ND_SHL:
      return format("/*(%s)*/(%s << %s)", to_cil_typename(node->ty), pretty_print(node->lhs), pretty_print(node->rhs));
    case ND_SHR:
      return format("/*(%s)*/(%s >> %s)", to_cil_typename(node->ty), pretty_print(node->lhs), pretty_print(node->rhs));
    case ND_EQ:
      return format("/*(%s)*/(%s == %s)", to_cil_typename(node->ty), pretty_print(node->lhs), pretty_print(node->rhs));
    case ND_NE:
      return format("/*(%s)*/(%s != %s)", to_cil_typename(node->ty), pretty_print(node->lhs), pretty_print(node->rhs));
    case ND_LT:
      return format("/*(%s)*/(%s < %s)", to_cil_typename(node->ty), pretty_print(node->lhs), pretty_print(node->rhs));
    case ND_LE:
      return format("/*(%s)*/(%s <= %s)", to_cil_typename(node->ty), pretty_print(node->lhs), pretty_print(node->rhs));
    case ND_LOGAND:
      return format("/*(%s)*/(%s && %s)", to_cil_typename(node->ty), pretty_print(node->lhs), pretty_print(node->rhs));
    case ND_LOGOR:
      return format("/*(%s)*/(%s || %s)", to_cil_typename(node->ty), pretty_print(node->lhs), pretty_print(node->rhs));
    case ND_COMMA:
      return format("/*(%s)*/(%s, %s)", to_cil_typename(node->ty), pretty_print(node->lhs), pretty_print(node->rhs));
    case ND_ASSIGN:
      return format("/*(%s)*/(%s = %s)", to_cil_typename(node->ty), pretty_print(node->lhs), pretty_print(node->rhs));
    case ND_NEG:
      return format("/*(%s)*/(-%s)", to_cil_typename(node->ty), pretty_print(node->lhs));
    case ND_NOT:
      return format("/*(%s)*/(!%s)", to_cil_typename(node->ty), pretty_print(node->lhs));
    case ND_BITNOT:
      return format("/*(%s)*/(~%s)", to_cil_typename(node->ty), pretty_print(node->lhs));
    case ND_ADDR:
      return format("/*(%s)*/(&%s)", to_cil_typename(node->ty), pretty_print(node->lhs));
    case ND_DEREF:
      return format("/*(%s)*/(*%s)", to_cil_typename(node->ty), pretty_print(node->lhs));
    case ND_CAST:
      return format("((%s)%s)", to_cil_typename(node->ty), pretty_print(node->lhs));
    case ND_MEMBER:
      return format("/*(%s)*/(%s.%s)", to_cil_typename(node->ty), pretty_print(node->lhs), get_string(node->member->name));
    case ND_COND:
      return format("/*(%s)*/(%s ? %s : %s)", to_cil_typename(node->ty), pretty_print(node->cond), pretty_print(node->then), pretty_print(node->els));
    case ND_NUM:
      if (node->ty->kind == TY_FLOAT || node->ty->kind == TY_DOUBLE)
        return format("/*(%s)*/(%f)", to_cil_typename(node->ty), node->fval);
      else
        return format("/*(%s)*/(%ld)", to_cil_typename(node->ty), node->val);
    case ND_VAR:
      return format("/*(%s)*/(%s)", to_cil_typename(node->ty), node->var->name);
    case ND_MEMZERO:
      return format("/*(%s)*/(init %s)", to_cil_typename(node->ty), node->var->name);
    case ND_SIZEOF:
      return format("/*(%s)*/sizeof(%s)", to_cil_typename(node->ty), to_cil_typename(node->sizeof_ty));
    case ND_NULL_EXPR:
      return format("/*(%s)*/()", to_cil_typename(node->ty));
    case ND_FUNCALL: {
      Node *arg = node->next;
      if (arg) {
        char* c = pretty_print(arg);
        while (arg->next) {
          arg = arg->next;
          c = format("%s,%s", c, pretty_print(arg));
        }
        return format("/*(%s)*/(%s(%s))", to_cil_typename(node->ty), pretty_print(node->lhs), c);
      } else
        return format("/*(%s)*/(%s())", to_cil_typename(node->ty), pretty_print(node->lhs));
    }
    case ND_STMT_EXPR:
      return format("/*(%s)*/(%s)", to_cil_typename(node->ty), pretty_print(node->body));

    case ND_IF:
      if (node->els)
        return format("if %s %s else %s", pretty_print(node->cond), pretty_print(node->then), pretty_print(node->els));
      else
        return format("if %s %s", pretty_print(node->cond), pretty_print(node->then));
    case ND_FOR:
      return format("for (%s; %s; %s) %s", pretty_print(node->init), pretty_print(node->cond), pretty_print(node->inc), pretty_print(node->then));
    case ND_DO:
      return format("do %s while %s", pretty_print(node->then), pretty_print(node->cond));
    case ND_BLOCK: {
      Node *body = node->body;
      char *c = pretty_print(body);
      while (body->next) {
        body = body->next;
        c = format("%s; %s", c, pretty_print(body));
      }
      return format("{ %s }", c);
    }
    case ND_SWITCH:
      return format("switch %s %s", pretty_print(node->cond), pretty_print(node->then));
    case ND_CASE:
      return format("case %s: %s", node->val, pretty_print(node->lhs));
    case ND_RETURN:
      if (node->lhs)
        return format("return %s", pretty_print(node->lhs));
      else
        return format("return");
    case ND_GOTO:
      return format("goto %s", node->label);
    case ND_LABEL:
      return format("%s:", node->label);
    case ND_EXPR_STMT:
      return format("{ %s }", pretty_print(node->lhs));
    default:
      unreachable();
  }
}

void pretty_print_node(Node *node) {
  char *pp = pretty_print(node);
  fprintf(stderr, "%s\n", pp);
}

void pretty_print_obj(Obj *obj) {
  if (obj->is_function) {
    char *pp = pretty_print(obj->body);
    fprintf(stderr, "%s %s(...) { %s }\n", to_cil_typename(obj->ty->return_ty), obj->name, pp);
  }
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
    case TY_FUNC:
      if (equals_type(lhs->return_ty, rhs->return_ty)) {
        Type *lt = lhs->params;
        Type *rt = lhs->params;
        while (lt != NULL && rt != NULL) {
          if (!equals_type(lt, rt))
            return false;
          lt = lt->next;
          rt = rt->next;
        }
        return lt == NULL && rt == NULL;
      }
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

static void verify_assert(bool assertion) {
  if (!assertion) {
    fprintf(stderr, "failed verification.\n");
    __builtin_trap();
  }
}

static void verify_c_str(char *p) {
  (void)strlen(p);
}

static void verify_c_str_len(char *p, int len) {
  for (int i = 0; i < len; i++) {
    (void)*(p + i);
  }
}

static void verify_token(Token *tok) {
  verify_c_str_len(tok->loc, tok->len);
}

typedef struct VerifyContext VerifyContext;
struct VerifyContext {
  VerifyContext *next;
  Node *node;
  Type *type;
};

static void verify_node0(Node *node);

static VerifyContext *verify_context;

static void verify_type0(Type *ty) {
  if (ty == NULL) {
    return;
  }

  VerifyContext *pc = verify_context;
  while (pc != NULL) {
    if (pc->type == ty)
      return;
    pc = pc->next;
  }

  VerifyContext *cc = calloc(1, sizeof(VerifyContext));
  cc->next = verify_context;
  cc->type = ty;
  verify_context = cc;

  switch (ty->kind) {
    case TY_VOID:
      equals_node(ty->align, ty_void->align);
      equals_node(ty->size, ty_void->size);
      equals_node(ty->origin_size, ty_void->origin_size);
      return;
    case TY_BOOL:
      equals_node(ty->align, ty_bool->align);
      equals_node(ty->size, ty_bool->size);
      equals_node(ty->origin_size, ty_bool->origin_size);
      return;
    case TY_CHAR: {
      Type *t = ty->is_unsigned ? ty_uchar : ty_char;
      equals_node(ty->align, t->align);
      equals_node(ty->size, t->size);
      equals_node(ty->origin_size, t->origin_size);
      return;
    }
    case TY_SHORT: {
      Type *t = ty->is_unsigned ? ty_ushort : ty_short;
      equals_node(ty->align, t->align);
      equals_node(ty->size, t->size);
      equals_node(ty->origin_size, t->origin_size);
      return;
    }
    case TY_INT: {
      Type *t = ty->is_unsigned ? ty_uint : ty_int;
      equals_node(ty->align, t->align);
      equals_node(ty->size, t->size);
      equals_node(ty->origin_size, t->origin_size);
      return;
    }
    case TY_LONG: {
      Type *t = ty->is_unsigned ? ty_ulong : ty_long;
      equals_node(ty->align, t->align);
      equals_node(ty->size, t->size);
      equals_node(ty->origin_size, t->origin_size);
      return;
    }
    case TY_FLOAT:
      equals_node(ty->align, ty_float->align);
      equals_node(ty->size, ty_float->size);
      equals_node(ty->origin_size, ty_float->origin_size);
      return;
    case TY_DOUBLE:
      equals_node(ty->align, ty_double->align);
      equals_node(ty->size, ty_double->size);
      equals_node(ty->origin_size, ty_double->origin_size);
      return;
    case TY_NINT: {
      Type *t = ty->is_unsigned ? ty_nuint : ty_nint;
      equals_node(ty->align, t->align);
      equals_node(ty->size, t->size);
      equals_node(ty->origin_size, t->origin_size);
      return;
    }
    case TY_VA_LIST:
      verify_assert(ty->align->kind == ND_NUM);
      verify_assert(ty->align->val == 8);
      verify_assert(ty->size->kind == ND_SIZEOF);
      verify_assert(ty->size->sizeof_ty->kind == TY_VA_LIST);
      verify_assert(ty->origin_size->kind == ND_SIZEOF);
      verify_assert(ty->origin_size->sizeof_ty->kind == TY_VA_LIST);
      return;
    case TY_ARRAY:
      verify_type0(ty->base);
      return;
    case TY_PTR:
      verify_type0(ty->base);
      equals_node(ty->align, ty_nint->align);
      equals_node(ty->size, ty_nint->size);
      equals_node(ty->origin_size, ty_nint->origin_size);
      return;
    case TY_ENUM:
      if (ty->tag_scope)
        verify_c_str(ty->tag_scope->name);
      else if (ty->typedef_name)
        verify_token(ty->typedef_name);
      for (EnumMember *mem = ty->enum_members; mem; mem = mem->next)
        verify_token(mem->name);
      return;
    case TY_STRUCT:
    case TY_UNION:
      if (ty->tag_scope)
        verify_c_str(ty->tag_scope->name);
      else if (ty->typedef_name)
        verify_c_str_len(ty->typedef_name->loc, ty->typedef_name->len);
      for (Member *mem = ty->members; mem; mem = mem->next) {
        verify_type0(mem->ty);
        verify_token(mem->name);
        verify_node0(mem->align);
        verify_node0(mem->offset);
        verify_node0(mem->origin_offset);
      }
      return;
  }
  unreachable();
}

static void verify_node0(Node *node) {
  VerifyContext *pc = verify_context;
  while (pc != NULL) {
    if (pc->node == node)
      return;
    pc = pc->next;
  }

  VerifyContext *cc = calloc(1, sizeof(VerifyContext));
  cc->next = verify_context;
  cc->node = node;
  verify_context = cc;

  switch (node->kind)
  {
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
      verify_type0(node->ty);
      verify_node0(node->lhs);
      verify_node0(node->rhs);
      return;
    case ND_NEG:
    case ND_NOT:
    case ND_BITNOT:
    case ND_ADDR:
    case ND_DEREF:
    case ND_CAST:
      verify_type0(node->ty);
      verify_node0(node->lhs);
      return;
    case ND_MEMBER:
      verify_type0(node->ty);
      verify_node0(node->lhs);
      verify_token(node->member->name);
      verify_node0(node->member->align);
      verify_node0(node->member->offset);
      verify_node0(node->member->origin_offset);
      return;
    case ND_COND:
      verify_type0(node->ty);
      verify_node0(node->cond);
      verify_node0(node->then);
      verify_node0(node->els);
      return;
    case ND_NUM:
      switch (node->ty->kind) {
      case TY_BOOL:
      case TY_CHAR:
      case TY_SHORT:
      case TY_INT:
      case TY_LONG:
      case TY_ENUM:
      case TY_NINT:
      case TY_PTR:
        verify_assert(node->fval == 0.0);
        return;
      case TY_FLOAT:
      case TY_DOUBLE:
        verify_assert(node->val == 0.0);
        return;
      }
      verify_assert(false);
      break;
    case ND_VAR:
    case ND_MEMZERO:
      verify_type0(node->ty);
      verify_c_str(node->var->name);
      verify_assert(node->var->kind != OB_GLOBAL_TYPE);
      verify_node0(node->var->align);
      return;
    case ND_SIZEOF:
      verify_type0(node->ty);
      verify_type0(node->sizeof_ty);
      return;
    case ND_NULL_EXPR:
      verify_type0(node->ty);
      return;
    case ND_FUNCALL: {
      verify_type0(node->ty);
      verify_node0(node->lhs);
      Node *arg = node->args;
      while (arg) {
        verify_node0(arg);
        arg = arg->next;
      }
      return;
    }
    case ND_STMT_EXPR:
      verify_type0(node->ty);
      verify_node0(node->body);
      return;

    case ND_IF:
      verify_assert(node->ty == NULL);
      verify_node0(node->cond);
      verify_node0(node->then);
      if (node->els)
        verify_node0(node->els);
      return;
    case ND_FOR:
      verify_assert(node->ty == NULL);
      if (node->init)
        verify_node0(node->init);
      if (node->cond)
        verify_node0(node->cond);
      verify_node0(node->then);
      if (node->inc)
        verify_node0(node->inc);
      return;
    case ND_DO:
      verify_assert(node->ty == NULL);
      verify_assert(node->init == NULL);
      if (node->cond)
        verify_node0(node->cond);
      verify_node0(node->then);
      verify_assert(node->inc == NULL);
      return;
    case ND_BLOCK: {
      verify_assert(node->ty == NULL);
      Node *body = node->body;
      while (body) {
        verify_node0(body);
        body = body->next;
      }
      return;
    }
    case ND_SWITCH:
      verify_assert(node->ty == NULL);
      verify_node0(node->cond);
      verify_node0(node->then);
      return;
    case ND_CASE:
      verify_assert(node->ty == NULL);
      verify_node0(node->lhs);
      return;
    case ND_RETURN:
      verify_assert(node->ty == NULL);
      if (node->lhs)
        verify_node0(node->lhs);
      return;
    case ND_GOTO:
    case ND_LABEL:
      verify_assert(node->ty == NULL);
      verify_c_str(node->unique_label);
      return;
    case ND_EXPR_STMT:
      verify_assert(node->ty == NULL);
      verify_node0(node->lhs);
      return;
    default:
      unreachable();
  }
}

void verify_type(Type *ty) {
  VerifyContext head = { };
  verify_context = &head;

  verify_type0(ty);

  while (verify_context != &head) {
    VerifyContext *next = verify_context->next;
    free(verify_context);
    verify_context = next;
  }
}

void verify_node(Node *node) {
  VerifyContext head = { };
  verify_context = &head;

  verify_node0(node);

  while (verify_context != &head) {
    VerifyContext *next = verify_context->next;
    free(verify_context);
    verify_context = next;
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

//static bool is_flonum_not_equals(Node *node, double fval) {
//  return node->kind == ND_NUM && is_flonum(node->ty) && node->fval != fval;
//}

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
    // Reduce initializer expression assigner.
    if (node->var->init_expr && !node->var->init_expr->is_reduced && node->var->init_expr->kind == ND_ASSIGN) {
      rhs = reduce(node->var->init_expr->rhs);
      Node *nnode = new_node(ND_ASSIGN, node->var->init_expr->tok);
      nnode->lhs = node->var->init_expr->lhs;
      nnode->rhs = rhs;
      nnode->ty = node->var->init_expr->ty;
      node->var->init_expr = nnode;
    }
    return node;
  }
  case ND_NUM:
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
