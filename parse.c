// This file contains a recursive descent parser for C.
//
// Most functions in this file are named after the symbols they are
// supposed to read from an input token list. For example, stmt() is
// responsible for reading a statement from a token list. The function
// then construct an AST node representing a statement.
//
// Each function conceptually returns two values, an AST node and
// remaining part of the input tokens. Since C doesn't support
// multiple return values, the remaining tokens are returned to the
// caller via a pointer argument.
//
// Input tokens are represented by a linked list. Unlike many recursive
// descent parsers, we don't have the notion of the "input token stream".
// Most parsing functions don't change the global state of the parser.
// So it is very easy to lookahead arbitrary number of tokens in this
// parser.

#include "chibicc.h"

// Variable attributes such as typedef or extern.
typedef struct {
  bool is_typedef;
  bool is_static;
  bool is_extern;
  bool is_inline;
  bool is_tls;
  Node *align;
} VarAttr;

// This struct represents a variable initializer. Since initializers
// can be nested (e.g. `int x[2][2] = {{1, 2}, {3, 4}}`), this struct
// is a tree data structure.
typedef struct Initializer Initializer;
struct Initializer {
  Initializer *next;
  Type *ty;
  Token *tok;
  bool is_flexible;

  // If it's not an aggregate type and has an initializer,
  // `expr` has an initialization expression.
  Node *expr;

  // If it's an initializer for an aggregate type (e.g. array or struct),
  // `children` has initializers for its children.
  Initializer **children;

  // Only one member can be initialized for a union.
  // `mem` is used to clarify which member is initialized.
  Member *mem;
};

// For local variable initializer.
typedef struct InitDesg InitDesg;
struct InitDesg {
  InitDesg *next;
  int idx;
  Member *member;
  Obj *var;
};

// All local variable instances created during parsing are
// accumulated to this list.
static Obj *locals;

// Likewise, global variables are accumulated to this list.
static Obj *globals;

static Scope *scope = &(Scope){};

// Points to the function object the parser is currently parsing.
static Obj *current_fn;

// Lists of all goto statements and labels in the curent function.
static Node *gotos;
static Node *labels;

// Current "goto" and "continue" jump targets.
static char *brk_label;
static Node* cont_target;

// Points to a node representing a switch if we are parsing
// a switch statement. Otherwise, NULL.
static Node *current_switch;

static bool is_typename(Token *tok);
static Type *declspec(Token **rest, Token *tok, VarAttr *attr);
static Type *typename(Token **rest, Token *tok);
static Type *enum_specifier(Token **rest, Token *tok);
static Type *typeof_specifier(Token **rest, Token *tok);
static Type *type_suffix(Token **rest, Token *tok, Type *ty);
static Type *declarator(Token **rest, Token *tok, Type *ty);
static Node *declaration(Token **rest, Token *tok, Type *basety, VarAttr *attr);
static void array_initializer2(Token **rest, Token *tok, Initializer *init, int i);
static void struct_initializer2(Token **rest, Token *tok, Initializer *init, Member *mem);
static void initializer2(Token **rest, Token *tok, Initializer *init);
static Initializer *initializer(Token **rest, Token *tok, Type *ty, Type **new_ty);
static Node *lvar_initializer(Token **rest, Token *tok, Obj *var);
static void gvar_initializer(Token **rest, Token *tok, Obj *var);
static Node *compound_stmt(Token **rest, Token *tok);
static Node *stmt(Token **rest, Token *tok);
static Node *expr_stmt(Token **rest, Token *tok);
static Node *expr(Token **rest, Token *tok);
static int64_t eval(Node *node);
static Node *assign(Token **rest, Token *tok);
static Node *logor(Token **rest, Token *tok);
static double eval_double(Node *node);
static Node *conditional(Token **rest, Token *tok);
static Node *logand(Token **rest, Token *tok);
static Node *bitor(Token **rest, Token *tok);
static Node *bitxor(Token **rest, Token *tok);
static Node *bitand(Token **rest, Token *tok);
static Node *equality(Token **rest, Token *tok);
static Node *relational(Token **rest, Token *tok);
static Node *shift(Token **rest, Token *tok);
static Node *add(Token **rest, Token *tok);
static Node *new_add(Node *lhs, Node *rhs, Token *tok);
static Node *new_sub(Node *lhs, Node *rhs, Token *tok);
static Node *mul(Token **rest, Token *tok);
static Node *cast(Token **rest, Token *tok);
static Member *get_struct_member(Type *ty, Token *tok);
static Type *struct_decl(Token **rest, Token *tok);
static Type *union_decl(Token **rest, Token *tok);
static Node *postfix(Token **rest, Token *tok);
static Node *funcall(Token **rest, Token *tok, Node *node);
static Node *unary(Token **rest, Token *tok);
static Node *primary(Token **rest, Token *tok);
static Token *parse_typedef(Token *tok, Type *basety);
static bool is_function(Token *tok);
static Token *function(Token *tok, Type *basety, VarAttr *attr);
static Token *global_variable(Token *tok, Type *basety, VarAttr *attr);

static Node *align_down_node(Node *n, Node *align) {
  return align_to_node(new_add(new_sub(n, align, n->tok), new_typed_num(1, ty_uintptr, n->tok), n->tok), align);
}

static void enter_scope(void) {
  Scope *sc = calloc(1, sizeof(Scope));
  sc->next = scope;
  scope = sc;
}

static void leave_scope(void) {
  scope = scope->next;
}

// Find a variable by name.
static VarScope *find_var(Token *tok) {
  for (Scope *sc = scope; sc; sc = sc->next) {
    VarScope *sc2 = hashmap_get2(&sc->vars, tok->loc, tok->len);
    if (sc2)
      return sc2;
  }
  return NULL;
}

static Type *find_tag(Token *tok) {
  for (Scope *sc = scope; sc; sc = sc->next) {
    Type *ty = hashmap_get2(&sc->tags, tok->loc, tok->len);
    if (ty)
      return ty;
  }
  return NULL;
}

Node *new_node(NodeKind kind, Token *tok) {
  Node *node = calloc(1, sizeof(Node));
  node->kind = kind;
  node->tok = tok;
  return node;
}

Node *new_binary(NodeKind kind, Node *lhs, Node *rhs, Token *tok) {
  Node *node = new_node(kind, tok);
  node->lhs = lhs;
  node->rhs = rhs;
  return node;
}

Node *new_unary(NodeKind kind, Node *expr, Token *tok) {
  Node *node = new_node(kind, tok);
  node->lhs = expr;
  return node;
}

Node *new_num(int64_t val, Token *tok) {
  Node *node = new_node(ND_NUM, tok);
  node->val = val;
  node->is_reduced = true;
  return node;
}

Node *new_flonum(double fval, Type *ty, Token *tok) {
  Node *node = new_node(ND_NUM, tok);
  node->fval = fval;
  node->ty = ty;
  node->is_reduced = true;
  return node;
}

Node *new_typed_num(int64_t val, Type *ty, Token *tok) {
  Node *node = new_node(ND_NUM, tok);
  node->val = val;
  node->ty = ty;
  node->is_reduced = true;
  return node;
}

Node *new_complex(Node *real, Node *imag, Type *ty, Token *tok) {
  Node *node = new_node(ND_COMPLEX, tok);
  node->lhs = real;
  node->rhs = imag;
  node->ty = ty;
  return node;
}

Node *new_sizeof(Type *ty, Token *tok) {
  Node *node = new_node(ND_SIZEOF, tok);
  node->sizeof_ty = ty;
  node->ty = ty_uintptr;    // size_t
  return node;
}

Node *new_var_node(Obj *var, Token *tok) {
  Node *node = new_node(ND_VAR, tok);
  node->var = var;
  return node;
}

Node *new_cast(Node *expr, Type *ty) {
  add_type(expr);

  Node *node = new_node(ND_CAST, expr->tok);
  node->lhs = expr;
  node->ty = copy_type(ty);
  return node;
}

static VarScope *push_scope(char *name) {
  VarScope *sc = calloc(1, sizeof(VarScope));
  sc->scope = scope;
  hashmap_put(&scope->vars, name, sc);
  return sc;
}

static Initializer *new_initializer(Type *ty, bool is_flexible) {
  Initializer *init = calloc(1, sizeof(Initializer));
  init->ty = ty;

  if (ty->kind == TY_ARRAY) {
    if (is_flexible && ty->array_len < 0) {
      init->is_flexible = true;
      return init;
    }

    init->children = calloc(ty->array_len, sizeof(Initializer *));
    for (int i = 0; i < ty->array_len; i++)
      init->children[i] = new_initializer(ty->base, false);
    return init;
  }

  if (ty->kind == TY_STRUCT || ty->kind == TY_UNION) {
    // Count the number of struct members.
    int len = 0;
    for (Member *mem = ty->members; mem; mem = mem->next)
      len++;

    init->children = calloc(len, sizeof(Initializer *));

    for (Member *mem = ty->members; mem; mem = mem->next) {
      if (is_flexible && ty->is_flexible && !mem->next) {
        Initializer *child = calloc(1, sizeof(Initializer));
        child->ty = mem->ty;
        child->is_flexible = true;
        init->children[mem->idx] = child;
      } else {
        init->children[mem->idx] = new_initializer(mem->ty, false);
      }
    }
    return init;
  }

  return init;
}

static Obj *new_var(char *name, Type *ty) {
  Obj *var = calloc(1, sizeof(Obj));
  var->name = name;
  var->ty = ty;
  var->align = ty->align;
  push_scope(name)->var = var;
  return var;
}

static Obj *new_lvar(char *name, Type *ty) {
  Obj *var = new_var(name, ty);
  var->kind = OB_LOCAL;
  var->next = locals;
  locals = var;
  return var;
}

static Obj *new_param_lvar(char *name, Type *ty) {
  Obj *var = new_var(name, ty);
  var->kind = OB_PARAM;
  var->next = locals;
  locals = var;
  return var;
}

static Obj *new_gvar(char *name, Type *ty) {
  Obj *var = new_var(name, ty);
  var->kind = OB_GLOBAL;
  var->next = globals;
  var->is_static = true;
  var->is_definition = true;
  globals = var;
  return var;
}

static char *new_unique_name(const char *name) {
  static int id = 0;
  char *p = strndup(name, 20);
  char *pc = p;
  while (*pc != 0) {
    if (!(isalpha(*pc) || isdigit(*pc)))
      *pc = '_';
    pc++;
  }
  return format("_L%d$_%s", id++, p);
}

static Obj *new_anon_gvar(Type *ty, const char *name) {
  return new_gvar(new_unique_name(name), ty);
}

static Obj *new_string_literal(char *p, Type *ty) {
  Obj *var = new_anon_gvar(ty, p);
  var->init_data = p;
  if (ty->kind == TY_ARRAY)
    var->init_data_size = ty->array_len * calculate_size(ty->base);
  else
    unreachable();
  return var;
}

static char *get_ident(Token *tok) {
  if (tok->kind != TK_IDENT)
    error_tok(tok, "expected an identifier");
  return strndup(tok->loc, tok->len);
}

static Type *find_typedef(Token *tok) {
  if (tok->kind == TK_IDENT) {
    VarScope *sc = find_var(tok);
    if (sc)
      return sc->type_def;
  }
  return NULL;
}

static void push_tag_scope(Token *tok, Type *ty) {
  ty->tag_name = strndup(tok->loc, tok->len);
  hashmap_put2(&scope->tags, tok->loc, tok->len, ty);
}

// declspec = ("void" | "_Bool" | "char" | "short" | "int" | "long"
//             | "__builtin_intptr" | "__builtin_uintptr" | "__builtin_va_list"
//             | "typedef" | "static" | "extern" | "inline"
//             | "_Thread_local" | "__thread"
//             | "signed" | "unsigned"
//             | struct-decl | union-decl | typedef-name
//             | enum-specifier | typeof-specifier
//             | "const" | "volatile" | "auto" | "register" | "restrict"
//             | "__restrict" | "__restrict__" | "_Noreturn")+
//
// The order of typenames in a type-specifier doesn't matter. For
// example, `int long static` means the same as `static long int`.
// That can also be written as `static long` because you can omit
// `int` if `long` or `short` are specified. However, something like
// `char int` is not a valid type specifier. We have to accept only a
// limited combinations of the typenames.
//
// In this function, we count the number of occurrences of each typename
// while keeping the "current" type object that the typenames up
// until that point represent. When we reach a non-typename token,
// we returns the current type object.
static Type *declspec(Token **rest, Token *tok, VarAttr *attr) {
  // We use a single integer as counters for all typenames.
  // For example, bits 0 and 1 represents how many times we saw the
  // keyword "void" so far. With this, we can use a switch statement
  // as you can see below.
  enum {
    VOID     = 1 << 0,
    BOOL     = 1 << 2,
    CHAR     = 1 << 4,
    SHORT    = 1 << 6,
    INT      = 1 << 8,
    LONG     = 1 << 10,
    FLOAT    = 1 << 12,
    DOUBLE   = 1 << 14,
    COMPLEX  = 1 << 16,
    OTHER    = 1 << 18,
    SIGNED   = 1 << 19,
    UNSIGNED = 1 << 20,
  };

  Type *ty = ty_int;
  int counter = 0;

  while (is_typename(tok)) {
    // Handle storage class specifiers.
    if (equal(tok, "typedef") || equal(tok, "static") || equal(tok, "extern") ||
        equal(tok, "inline") || equal(tok, "_Thread_local") || equal(tok, "__thread")) {
      if (!attr)
        error_tok(tok, "storage class specifier is not allowed in this context");

      if (equal(tok, "typedef"))
        attr->is_typedef = true;
      else if (equal(tok, "static"))
        attr->is_static = true;
      else if (equal(tok, "extern"))
        attr->is_extern = true;
      else if (equal(tok, "inline"))
        attr->is_inline = true;
      else
        attr->is_tls = true;

      if (attr->is_typedef &&
          attr->is_static + attr->is_extern + attr->is_inline + attr->is_tls > 1)
        error_tok(tok, "typedef may not be used together with static,"
                  " extern, inline, __thread or _Thread_local");
      tok = tok->next;
      continue;
    }

    // These keywords are recognized but ignored.
    if (consume(&tok, tok, "const") || consume(&tok, tok, "volatile") ||
        consume(&tok, tok, "auto") || consume(&tok, tok, "register") ||
        consume(&tok, tok, "restrict") || consume(&tok, tok, "__restrict") ||
        consume(&tok, tok, "__restrict__") || consume(&tok, tok, "_Noreturn"))
      continue;

    if (equal(tok, "_Alignas")) {
      if (!attr)
        error_tok(tok, "_Alignas is not allowed in this context");
      tok = skip(tok->next, "(");

      if (is_typename(tok))
        attr->align = typename(&tok, tok)->align;
      else
        attr->align = new_num(const_expr(&tok, tok), tok);
      tok = skip(tok, ")");
      continue;
    }

    if (equal(tok, "__builtin_intptr")) {
      if (counter)
        break;
      ty = ty_intptr;
      tok = tok->next;
      break;
    }

    if (equal(tok, "__builtin_uintptr")) {
      if (counter)
        break;
      ty = ty_uintptr;
      tok = tok->next;
      break;
    }

    if (equal(tok, "__builtin_va_list")) {
      if (counter)
        break;
      ty = ty_va_list;
      tok = tok->next;
      break;
    }

    // Handle user-defined types.
    Type *ty2 = find_typedef(tok);
    if (equal(tok, "struct") || equal(tok, "union") || equal(tok, "enum") ||
        equal(tok, "typeof") || ty2) {
      if (counter)
        break;

      if (equal(tok, "struct")) {
        ty = struct_decl(&tok, tok->next);
      } else if (equal(tok, "union")) {
        ty = union_decl(&tok, tok->next);
      } else if (equal(tok, "enum")) {
        ty = enum_specifier(&tok, tok->next);
      } else if (equal(tok, "typeof")) {
        ty = typeof_specifier(&tok, tok->next);
      } else {
        ty = ty2;
        tok = tok->next;
      }

      counter += OTHER;
      continue;
    }

    // Handle built-in types.
    if (equal(tok, "void"))
      counter += VOID;
    else if (equal(tok, "_Bool"))
      counter += BOOL;
    else if (equal(tok, "char"))
      counter += CHAR;
    else if (equal(tok, "short"))
      counter += SHORT;
    else if (equal(tok, "int"))
      counter += INT;
    else if (equal(tok, "long"))
      counter += LONG;
    else if (equal(tok, "float"))
      counter += FLOAT;
    else if (equal(tok, "double"))
      counter += DOUBLE;
    else if (equal(tok, "_Complex"))
      counter += COMPLEX;
    else if (equal(tok, "signed"))
      counter |= SIGNED;
    else if (equal(tok, "unsigned"))
      counter |= UNSIGNED;
    else
      unreachable();

    switch (counter) {
    case VOID:
      ty = ty_void;
      break;
    case BOOL:
      ty = ty_bool;
      break;
    case CHAR:
    case SIGNED + CHAR:
      ty = ty_char;
      break;
    case UNSIGNED + CHAR:
      ty = ty_uchar;
      break;
    case SHORT:
    case SHORT + INT:
    case SIGNED + SHORT:
    case SIGNED + SHORT + INT:
      ty = ty_short;
      break;
    case UNSIGNED + SHORT:
    case UNSIGNED + SHORT + INT:
      ty = ty_ushort;
      break;
    case INT:
    case SIGNED:
    case SIGNED + INT:
      ty = ty_int;
      break;
    case UNSIGNED:
    case UNSIGNED + INT:
      ty = ty_uint;
      break;
    case LONG:
    case LONG + INT:
    case LONG + LONG:
    case LONG + LONG + INT:
    case SIGNED + LONG:
    case SIGNED + LONG + INT:
    case SIGNED + LONG + LONG:
    case SIGNED + LONG + LONG + INT:
      ty = ty_long;
      break;
    case UNSIGNED + LONG:
    case UNSIGNED + LONG + INT:
    case UNSIGNED + LONG + LONG:
    case UNSIGNED + LONG + LONG + INT:
      ty = ty_ulong;
      break;
    case FLOAT:
      ty = ty_float;
      break;
    case DOUBLE:
    case LONG + DOUBLE:
      ty = ty_double;
      break;
    case FLOAT + COMPLEX:
      ty = ty_float_complex;
      break;
    case DOUBLE + COMPLEX:
    case LONG + DOUBLE + COMPLEX:
      ty = ty_double_complex;
      break;
    default:
      error_tok(tok, "invalid type");
    }

    tok = tok->next;
  }

  *rest = tok;
  return ty;
}

// func-params = ("void" | param ("," param)* ("," "...")?)? ")"
// param       = declspec declarator
static Type *func_params(Token **rest, Token *tok, Type *ty) {
  if (equal(tok, "void") && equal(tok->next, ")")) {
    *rest = tok->next->next;
    return func_type(ty, ty->tok);
  }

  Type head = {};
  Type *cur = &head;
  bool is_variadic = false;

  while (!equal(tok, ")")) {
    if (cur != &head)
      tok = skip(tok, ",");

    if (equal(tok, "...")) {
      is_variadic = true;
      tok = tok->next;
      skip(tok, ")");
      break;
    }

    Type *ty2 = declspec(&tok, tok, NULL);
    ty2 = declarator(&tok, tok, ty2);

    Token *name = ty2->name;

    if (ty2->kind == TY_ARRAY) {
      // "array of T" is converted to "pointer to T" only in the parameter
      // context. For example, *argv[] is converted to **argv by this.
      ty2 = pointer_to(ty2->base, name);
      ty2->name = name;
    } else if (ty2->kind == TY_FUNC) {
      // Likewise, a function is converted to a pointer to a function
      // only in the parameter context.
      ty2 = pointer_to(ty2, name);
      ty2->name = name;
    }

    cur = cur->next = copy_type(ty2);
  }

  ty = func_type(ty, ty->tok);
  ty->params = head.next;
  ty->is_variadic = is_variadic;
  *rest = tok->next;
  return ty;
}

// array-dimensions = ("static" | "restrict")* const-expr? "]" type-suffix
static Type *array_dimensions(Token **rest, Token *tok, Type *ty) {
  Token *origin = tok;
  while (equal(tok, "static") || equal(tok, "restrict"))
    tok = tok->next;

  if (equal(tok, "]")) {
    ty = type_suffix(rest, tok->next, ty);
    // Unapplied size array (length=-1) when not update length later: `int(*)[][~]`
    return array_of(ty, -1, origin);
  }

  int sz = const_expr(&tok, tok);
  tok = skip(tok, "]");
  ty = type_suffix(rest, tok, ty);
  return array_of(ty, sz, origin);
}

// type-suffix = "(" func-params
//             | "[" array-dimensions
//             | ε
static Type *type_suffix(Token **rest, Token *tok, Type *ty) {
  if (equal(tok, "("))
    return func_params(rest, tok->next, ty);

  if (equal(tok, "["))
    return array_dimensions(rest, tok->next, ty);

  *rest = tok;
  return ty;
}

// pointers = ("*" ("const" | "volatile" | "restrict")*)*
static Type *pointers(Token **rest, Token *tok, Type *ty) {
  while (consume(&tok, tok, "*")) {
    ty = pointer_to(ty, tok);
    while (equal(tok, "const") || equal(tok, "volatile") || equal(tok, "restrict") ||
           equal(tok, "__restrict") || equal(tok, "__restrict__"))
      tok = tok->next;
  }
  *rest = tok;
  return ty;
}

// declarator = pointers ("(" ident ")" | "(" declarator ")" | ident) type-suffix
static Type *declarator(Token **rest, Token *tok, Type *ty) {
  ty = pointers(&tok, tok, ty);

  if (equal(tok, "(")) {
    Token *start = tok;
    Type dummy = {};
    declarator(&tok, start->next, &dummy);
    tok = skip(tok, ")");
    ty = type_suffix(rest, tok, ty);
    return declarator(&tok, start->next, ty);
  }

  Token *name = NULL;
  Token *name_pos = tok;

  if (tok->kind == TK_IDENT) {
    name = tok;
    tok = tok->next;
  }

  ty = type_suffix(rest, tok, ty);
  ty->name = name;
  ty->name_pos = name_pos;
  return ty;
}

// abstract-declarator = pointers ("(" abstract-declarator ")")? type-suffix
static Type *abstract_declarator(Token **rest, Token *tok, Type *ty) {
  ty = pointers(&tok, tok, ty);

  if (equal(tok, "(")) {
    Token *start = tok;
    Type dummy = {};
    abstract_declarator(&tok, start->next, &dummy);
    tok = skip(tok, ")");
    ty = type_suffix(rest, tok, ty);
    return abstract_declarator(&tok, start->next, ty);
  }

  return type_suffix(rest, tok, ty);
}

// type-name = declspec abstract-declarator
static Type *typename(Token **rest, Token *tok) {
  Type *ty = declspec(&tok, tok, NULL);
  return abstract_declarator(rest, tok, ty);
}

static bool is_end(Token *tok) {
  return equal(tok, "}") || (equal(tok, ",") && equal(tok->next, "}"));
}

static bool consume_end(Token **rest, Token *tok) {
  if (equal(tok, "}")) {
    *rest = tok->next;
    return true;
  }

  if (equal(tok, ",") && equal(tok->next, "}")) {
    *rest = tok->next->next;
    return true;
  }

  return false;
}

// enum-specifier = ident? "{" enum-list? "}"
//                | ident ("{" enum-list? "}")?
//
// enum-list      = ident ("=" num)? ("," ident ("=" num)?)* ","?
static Type *enum_specifier(Token **rest, Token *tok) {
  Type *ty = enum_type(tok);

  // Read a struct tag.
  Token *tag = NULL;
  if (tok->kind == TK_IDENT) {
    tag = tok;
    tok = tok->next;
  }

  if (tag && !equal(tok, "{")) {
    Type *ty = find_tag(tag);
    if (!ty)
      error_tok(tag, "unknown enum type");
    if (ty->kind != TY_ENUM)
      error_tok(tag, "not an enum tag");
    *rest = tok;
    return ty;
  }

  tok = skip(tok, "{");

  // Read an enum-list.
  EnumMember head = { };
  EnumMember *cur = &head;
  int i = 0;
  int val = 0;
  while (!consume_end(rest, tok)) {
    if (i++ > 0)
      tok = skip(tok, ",");
    
    EnumMember *mem = calloc(1, sizeof(EnumMember));
    mem->name = tok;
    cur = cur->next = mem;

    char *name = get_ident(tok);
    tok = tok->next;

    if (equal(tok, "="))
      val = const_expr(&tok, tok->next);
    
    mem->val = val;

    VarScope *sc = push_scope(name);
    sc->enum_ty = ty;
    sc->enum_val = val++;
  }

  ty->enum_members = head.next;

  if (tag)
    push_tag_scope(tag, ty);
  return ty;
}

// typeof-specifier = "(" (expr | typename) ")"
static Type *typeof_specifier(Token **rest, Token *tok) {
  tok = skip(tok, "(");

  Type *ty;
  if (is_typename(tok)) {
    ty = typename(&tok, tok);
  } else {
    Node *node = expr(&tok, tok);
    add_type(node);
    ty = node->ty;
  }
  *rest = skip(tok, ")");
  return ty;
}

// declaration = declspec (declarator ("=" expr)? ("," declarator ("=" expr)?)*)? ";"
static Node *declaration(Token **rest, Token *tok, Type *basety, VarAttr *attr) {
  Node head = {};
  Node *cur = &head;
  int i = 0;

  while (!equal(tok, ";")) {
    if (i++ > 0)
      tok = skip(tok, ",");

    Type *ty = declarator(&tok, tok, basety);
    if (ty->kind == TY_VOID)
      error_tok(tok, "variable declared void");
    if (!ty->name)
      error_tok(ty->name_pos, "variable name omitted");

    if (attr && attr->is_static) {
      // static local variable
      Obj *var = new_anon_gvar(ty, "slvar");
      push_scope(get_ident(ty->name))->var = var;
      if (equal(tok, "="))
        gvar_initializer(&tok, tok->next, var);
      continue;
    }

    Obj *var = new_lvar(get_ident(ty->name), ty);
    if (attr && attr->align)
      var->align = attr->align;

    if (equal(tok, "=")) {
      Node *expr = lvar_initializer(&tok, tok->next, var);
      cur = cur->next = new_unary(ND_EXPR_STMT, expr, tok);
    }

    if (ty->size->kind == ND_NUM && ty->size->val < 0)
      error_tok(ty->name, "variable has incomplete type");
    if (var->ty->kind == TY_VOID)
      error_tok(ty->name, "variable declared void");
  }

  Node *node = new_node(ND_BLOCK, tok);
  node->body = head.next;
  *rest = tok->next;
  return node;
}

static Token *skip_excess_element(Token *tok) {
  if (equal(tok, "{")) {
    tok = skip_excess_element(tok->next);
    return skip(tok, "}");
  }

  assign(&tok, tok);
  return tok;
}

// string-initializer = string-literal
static void string_initializer(Token **rest, Token *tok, Initializer *init) {
  if (init->is_flexible)
    *init = *new_initializer(array_of(init->ty->base, tok->ty->array_len, tok), false);

  int len = MIN(init->ty->array_len, tok->ty->array_len);

  switch (init->ty->base->kind) {
  case TY_CHAR: {
    uint8_t *str = (uint8_t *)tok->str;
    for (int i = 0; i < len; i++)
      init->children[i]->expr = new_num(str[i], tok);
    break;
  }
  case TY_SHORT: {
    uint16_t *str = (uint16_t *)tok->str;
    for (int i = 0; i < len; i++)
      init->children[i]->expr = new_num(str[i], tok);
    break;
  }
  case TY_INT: {
    uint32_t *str = (uint32_t *)tok->str;
    for (int i = 0; i < len; i++)
      init->children[i]->expr = new_num(str[i], tok);
    break;
  }
  default:
    unreachable();
  }

  *rest = tok->next;
}

// array-designator = "[" const-expr "]"
//
// C99 added the designated initializer to the language, which allows
// programmers to move the "cursor" of an initializer to any element.
// The syntax looks like this:
//
//   int x[10] = { 1, 2, [5]=3, 4, 5, 6, 7 };
//
// `[5]` moves the cursor to the 5th element, so the 5th element of x
// is set to 3. Initialization then continues forward in order, so
// 6th, 7th, 8th and 9th elements are initialized with 4, 5, 6 and 7,
// respectively. Unspecified elements (in this case, 3rd and 4th
// elements) are initialized with zero.
//
// Nesting is allowed, so the following initializer is valid:
//
//   int x[5][10] = { [5][8]=1, 2, 3 };
//
// It sets x[5][8], x[5][9] and x[6][0] to 1, 2 and 3, respectively.
//
// Use `.fieldname` to move the cursor for a struct initializer. E.g.
//
//   struct { int a, b, c; } x = { .c=5 };
//
// The above initializer sets x.c to 5.
static void array_designator(Token **rest, Token *tok, Type *ty, int *begin, int *end) {
  *begin = const_expr(&tok, tok->next);
  if (*begin >= ty->array_len)
    error_tok(tok, "array designator index exceeds array bounds");

  if (equal(tok, "...")) {
    *end = const_expr(&tok, tok->next);
    if (*end >= ty->array_len)
      error_tok(tok, "array designator index exceeds array bounds");
    if (*end < *begin)
      error_tok(tok, "array designator range [%d, %d] is empty", *begin, *end);
  } else {
    *end = *begin;
  }

  *rest = skip(tok, "]");
}

// struct-designator = "." ident
static Member *struct_designator(Token **rest, Token *tok, Type *ty) {
  Token *start = tok;
  tok = skip(tok, ".");
  if (tok->kind != TK_IDENT)
    error_tok(tok, "expected a field designator");

  for (Member *mem = ty->members; mem; mem = mem->next) {
    // Anonymous struct member
    if (mem->ty->kind == TY_STRUCT && !mem->name) {
      if (get_struct_member(mem->ty, tok)) {
        *rest = start;
        return mem;
      }
      continue;
    }

    // Regular struct member
    if (mem->name->len == tok->len && !strncmp(mem->name->loc, tok->loc, tok->len)) {
      *rest = tok->next;
      return mem;
    }
  }

  error_tok(tok, "struct has no such member");
}

// designation = ("[" const-expr "]" | "." ident)* "="? initializer
static void designation(Token **rest, Token *tok, Initializer *init) {
  if (equal(tok, "[")) {
    if (init->ty->kind != TY_ARRAY)
      error_tok(tok, "array index in non-array initializer");

    int begin, end;
    array_designator(&tok, tok, init->ty, &begin, &end);

    Token *tok2;
    for (int i = begin; i <= end; i++)
      designation(&tok2, tok, init->children[i]);
    array_initializer2(rest, tok2, init, begin + 1);
    return;
  }

  if (equal(tok, ".") && init->ty->kind == TY_STRUCT) {
    Member *mem = struct_designator(&tok, tok, init->ty);
    designation(&tok, tok, init->children[mem->idx]);
    init->expr = NULL;
    struct_initializer2(rest, tok, init, mem->next);
    return;
  }

  if (equal(tok, ".") && init->ty->kind == TY_UNION) {
    Member *mem = struct_designator(&tok, tok, init->ty);
    init->mem = mem;
    designation(rest, tok, init->children[mem->idx]);
    return;
  }

  if (equal(tok, "."))
    error_tok(tok, "field name not in struct or union initializer");

  if (equal(tok, "="))
    tok = tok->next;
  initializer2(rest, tok, init);
}

// An array length can be omitted if an array has an initializer
// (e.g. `int x[] = {1,2,3}`). If it's omitted, count the number
// of initializer elements.
static int count_array_init_elements(Token *tok, Type *ty) {
  bool first = true;
  Initializer *dummy = new_initializer(ty->base, true);

  int i = 0, max = 0;

  while (!consume_end(&tok, tok)) {
    if (!first)
      tok = skip(tok, ",");
    first = false;

    if (equal(tok, "[")) {
      i = const_expr(&tok, tok->next);
      if (equal(tok, "..."))
        i = const_expr(&tok, tok->next);
      tok = skip(tok, "]");
      designation(&tok, tok, dummy);
    } else {
      initializer2(&tok, tok, dummy);
    }

    i++;
    max = MAX(max, i);
  }
  return max;
}

// array-initializer1 = "{" initializer ("," initializer)* ","? "}"
static void array_initializer1(Token **rest, Token *tok, Initializer *init) {
  Token *origin = tok;
  tok = skip(tok, "{");

  if (init->is_flexible) {
    int len = count_array_init_elements(tok, init->ty);
    *init = *new_initializer(array_of(init->ty->base, len, origin), false);
  }

  bool first = true;

  if (init->is_flexible) {
    int len = count_array_init_elements(tok, init->ty);
    *init = *new_initializer(array_of(init->ty->base, len, origin), false);
  }

  for (int i = 0; !consume_end(rest, tok); i++) {
    if (!first)
      tok = skip(tok, ",");
    first = false;

    if (equal(tok, "[")) {
      int begin, end;
      array_designator(&tok, tok, init->ty, &begin, &end);

      Token *tok2;
      for (int j = begin; j <= end; j++)
        designation(&tok2, tok, init->children[j]);
      tok = tok2;
      i = end;
      continue;
    }

    if (i < init->ty->array_len)
      initializer2(&tok, tok, init->children[i]);
    else
      tok = skip_excess_element(tok);
  }
}

// array-initializer2 = initializer ("," initializer)*
static void array_initializer2(Token **rest, Token *tok, Initializer *init, int i) {
  if (init->is_flexible) {
    int len = count_array_init_elements(tok, init->ty);
    Type *array_ty = array_of(init->ty->base, len, tok);
    array_ty->size = reduce_node(array_ty->size);
    array_ty->align = reduce_node(array_ty->align);
    *init = *new_initializer(array_ty, false);
  }

  for (; i < init->ty->array_len && !is_end(tok); i++) {
    Token *start = tok;
    if (i > 0)
      tok = skip(tok, ",");

    if (equal(tok, "[") || equal(tok, ".")) {
      *rest = start;
      return;
    }

    initializer2(&tok, tok, init->children[i]);
  }
  *rest = tok;
}

// struct-initializer1 = "{" initializer ("," initializer)* ","? "}"
static void struct_initializer1(Token **rest, Token *tok, Initializer *init) {
  tok = skip(tok, "{");

  Member *mem = init->ty->members;
  bool first = true;

  while (!consume_end(rest, tok)) {
    if (!first)
      tok = skip(tok, ",");
    first = false;

    if (equal(tok, ".")) {
      mem = struct_designator(&tok, tok, init->ty);
      designation(&tok, tok, init->children[mem->idx]);
      mem = mem->next;
      continue;
    }

    if (mem) {
      initializer2(&tok, tok, init->children[mem->idx]);
      mem = mem->next;
    } else {
      tok = skip_excess_element(tok);
    }
  }
}

// struct-initializer2 = initializer ("," initializer)*
static void struct_initializer2(Token **rest, Token *tok, Initializer *init, Member *mem) {
  bool first = true;

  for (; mem && !is_end(tok); mem = mem->next) {
    Token *start = tok;

    if (!first)
      tok = skip(tok, ",");
    first = false;

    if (equal(tok, "[") || equal(tok, ".")) {
      *rest = start;
      return;
    }

    initializer2(&tok, tok, init->children[mem->idx]);
  }
  *rest = tok;
}

static void union_initializer(Token **rest, Token *tok, Initializer *init) {
  // Unlike structs, union initializers take only one initializer,
  // and that initializes the first union member by default.
  // You can initialize other member using a designated initializer.
  if (equal(tok, "{") && equal(tok->next, ".")) {
    Member *mem = struct_designator(&tok, tok->next, init->ty);
    init->mem = mem;
    designation(&tok, tok, init->children[mem->idx]);
    *rest = skip(tok, "}");
    return;
  }

  init->mem = init->ty->members;

  if (equal(tok, "{")) {
    initializer2(&tok, tok->next, init->children[0]);
    consume(&tok, tok, ",");
    *rest = skip(tok, "}");
  } else {
    initializer2(rest, tok, init->children[0]);
  }
}

// initializer = string-initializer | array-initializer
//             | struct-initializer | union-initializer
//             | assign
static void initializer2(Token **rest, Token *tok, Initializer *init) {
  if (init->ty->kind == TY_ARRAY && tok->kind == TK_STR) {
    string_initializer(rest, tok, init);
    return;
  }

  if (init->ty->kind == TY_ARRAY) {
    if (equal(tok, "{"))
      array_initializer1(rest, tok, init);
    else
      array_initializer2(rest, tok, init, 0);
    return;
  }

  if (init->ty->kind == TY_STRUCT) {
    if (equal(tok, "{")) {
      struct_initializer1(rest, tok, init);
      return;
    }

    // A struct can be initialized with another struct. E.g.
    // `struct T x = y;` where y is a variable of type `struct T`.
    // Handle that case first.
    Node *expr = assign(rest, tok);
    add_type(expr);
    if (expr->ty->kind == TY_STRUCT) {
      init->expr = expr;
      return;
    }

    struct_initializer2(rest, tok, init, init->ty->members);
    return;
  }

  if (init->ty->kind == TY_UNION) {
    union_initializer(rest, tok, init);
    return;
  }

  if (equal(tok, "{")) {
    // An initializer for a scalar variable can be surrounded by
    // braces. E.g. `int x = {3};`. Handle that case.
    initializer2(&tok, tok->next, init);
    *rest = skip(tok, "}");
    return;
  }

  init->expr = assign(rest, tok);
}

static Type *copy_struct_type(Type *ty) {
  ty = copy_type(ty);

  Member head = {};
  Member *cur = &head;
  for (Member *mem = ty->members; mem; mem = mem->next) {
    Member *m = calloc(1, sizeof(Member));
    *m = *mem;
    cur = cur->next = m;
  }

  ty->members = head.next;
  return ty;
}

static Initializer *initializer(Token **rest, Token *tok, Type *ty, Type **new_ty) {
  Initializer *init = new_initializer(ty, true);
  initializer2(rest, tok, init);

  if ((ty->kind == TY_STRUCT || ty->kind == TY_UNION) && ty->is_flexible) {
    ty = copy_struct_type(ty);

    Member *mem = ty->members;
    while (mem->next)
      mem = mem->next;
    mem->ty = init->children[mem->idx]->ty;
    ty->size = reduce_node(new_binary(ND_ADD, ty->size, mem->ty->size, tok));

    *new_ty = ty;
    return init;
  }

  *new_ty = init->ty;
  return init;
}

static Node *init_desg_expr(InitDesg *desg, Token *tok) {
  if (desg->var)
    return new_var_node(desg->var, tok);

  if (desg->member) {
    Node *node = new_unary(ND_MEMBER, init_desg_expr(desg->next, tok), tok);
    node->member = desg->member;
    return node;
  }

  Node *lhs = init_desg_expr(desg->next, tok);
  Node *rhs = new_num(desg->idx, tok);
  return new_unary(ND_DEREF, new_add(lhs, rhs, tok), tok);
}

static Node *create_lvar_init(Initializer *init, Type *ty, InitDesg *desg, Token *tok) {
  if (ty->kind == TY_ARRAY) {
    Node *node = new_node(ND_NULL_EXPR, tok);
    node->is_reduced = true;
    for (int i = 0; i < ty->array_len; i++) {
      InitDesg desg2 = {desg, i};
      Node *rhs = create_lvar_init(init->children[i], ty->base, &desg2, tok);
      node = new_binary(ND_COMMA, node, rhs, tok);
    }
    return node;
  }

  if (ty->kind == TY_STRUCT && !init->expr) {
    Node *node = new_node(ND_NULL_EXPR, tok);
    node->is_reduced = true;
    for (Member *mem = ty->members; mem; mem = mem->next) {
      InitDesg desg2 = {desg, 0, mem};
      Node *rhs = create_lvar_init(init->children[mem->idx], mem->ty, &desg2, tok);
      node = new_binary(ND_COMMA, node, rhs, tok);
    }
    return node;
  }

  if (ty->kind == TY_UNION) {
    Member *mem = init->mem ? init->mem : ty->members;
    InitDesg desg2 = {desg, 0, mem};
    return create_lvar_init(init->children[mem->idx], mem->ty, &desg2, tok);
  }

  if (!init->expr) {
    Node *node = new_node(ND_NULL_EXPR, tok);
    node->is_reduced = true;
    return node;
  }

  Node *lhs = init_desg_expr(desg, tok);
  return new_binary(ND_ASSIGN, lhs, init->expr, tok);
}

// A variable definition with an initializer is a shorthand notation
// for a variable definition followed by assignments. This function
// generates assignment expressions for an initializer. For example,
// `int x[2][2] = {{6, 7}, {8, 9}}` is converted to the following
// expressions:
//
//   x[0][0] = 6;
//   x[0][1] = 7;
//   x[1][0] = 8;
//   x[1][1] = 9;
static Node *lvar_initializer(Token **rest, Token *tok, Obj *var) {
  Initializer *init = initializer(rest, tok, var->ty, &var->ty);
  InitDesg desg = {NULL, 0, NULL, var};

  if (var->ty->kind != TY_ARRAY &&
      var->ty->kind != TY_STRUCT &&
      var->ty->kind != TY_UNION)
    return create_lvar_init(init, var->ty, &desg, tok);
  else {
    // If a partial initializer list is given, the standard requires
    // that unspecified elements are set to 0. Here, we simply
    // zero-initialize the entire memory region of a variable before
    // initializing it with user-supplied values.
    Node *lhs = new_node(ND_MEMZERO, tok);
    lhs->var = var;

    Node *rhs = create_lvar_init(init, var->ty, &desg, tok);
    return new_binary(ND_COMMA, lhs, rhs, tok);
  }
}

static void gvar_initializer(Token **rest, Token *tok, Obj *var) {
  Initializer *init = initializer(rest, tok, var->ty, &var->ty);
  InitDesg desg = {NULL, 0, NULL, var};

  Node *init_expr = create_lvar_init(init, var->ty, &desg, tok);
  var->init_expr = reduce_node(init_expr);
}

// Returns true if a given token represents a type.
static bool is_typename(Token *tok) {
  static HashMap map;

  if (map.capacity == 0) {
    static char *kw[] = {
      "void", "_Bool", "char", "short", "int", "long", "struct", "union",
      "typedef", "enum", "static", "extern", "_Alignas", "__builtin_va_list", "signed", "unsigned",
      "__builtin_intptr", "__builtin_uintptr",
      "const", "volatile", "auto", "register", "restrict", "__restrict",
      "__restrict__", "_Noreturn", "float", "double", "typeof", "inline",
      "_Thread_local", "__thread", "_Complex",
    };

    for (int i = 0; i < sizeof(kw) / sizeof(*kw); i++)
      hashmap_put(&map, kw[i], (void *)1);
  }

  return hashmap_get2(&map, tok->loc, tok->len) || find_typedef(tok);
}

// asm-stmt = "__asm__" ("volatile" | "inline")* "(" string-literal ")"
static Node *asm_stmt(Token **rest, Token *tok) {
  Node *node = new_node(ND_ASM, tok);
  tok = tok->next;

  while (equal(tok, "volatile") || equal(tok, "inline"))
    tok = tok->next;

  tok = skip(tok, "(");
  if (tok->kind != TK_STR || tok->ty->base->kind != TY_CHAR)
    error_tok(tok, "expected string literal");
  node->asm_str = tok->str;
  *rest = skip(tok->next, ")");
  return node;
}

// stmt = "return" expr? ";"
//      | "if" "(" expr ")" stmt ("else" stmt)?
//      | "switch" "(" expr ")" stmt
//      | "case" const-expr ("..." const-expr)? ":" stmt
//      | "default" ":" stmt
//      | "for" "(" expr-stmt expr? ";" expr? ")" stmt
//      | "while" "(" expr ")" stmt
//      | "do" stmt "while" "(" expr ")" ";"
//      | "__asm__" asm-stmt
//      | "goto" ident ";"
//      | "break" ";"
//      | "continue" ";"
//      | ident ":" stmt
//      | "{" compound-stmt
//      | expr-stmt
static Node *stmt(Token **rest, Token *tok) {
  if (equal(tok, "return")) {
    Node *node = new_node(ND_RETURN, tok);
    if (consume(rest, tok->next, ";"))
      return node;

    Node *exp = expr(&tok, tok->next);
    *rest = skip(tok, ";");

    add_type(exp);
    Type *ty = current_fn->ty->return_ty;
    if (ty->kind != TY_STRUCT && ty->kind != TY_UNION)
      exp = new_cast(exp, current_fn->ty->return_ty);

    node->lhs = exp;
    return node;
  }

  if (equal(tok, "if")) {
    Node *node = new_node(ND_IF, tok);
    tok = skip(tok->next, "(");
    node->cond = expr(&tok, tok);
    tok = skip(tok, ")");
    node->then = stmt(&tok, tok);
    if (equal(tok, "else"))
      node->els = stmt(&tok, tok->next);
    *rest = tok;
    return node;
  }

  if (equal(tok, "switch")) {
    Node *node = new_node(ND_SWITCH, tok);
    tok = skip(tok->next, "(");

    // .NET: Since the .NET CLR is a stack machine, the stack alone cannot satisfy
    //   the requirement to compare the value of cond repeately.
    //   Therefore, we assign an invisible local variable to store the value of cond once,
    //   so that it can be used over and over.
    Node *cond = expr(&tok, tok);
    add_type(cond);
    Obj *var = new_lvar("", cond->ty);
    node->cond = new_binary(ND_ASSIGN, new_var_node(var, tok), cond, tok);
    node->var = var;
    
    tok = skip(tok, ")");

    Node *sw = current_switch;
    current_switch = node;

    char *brk = brk_label;
    brk_label = node->brk_label = new_unique_name("switch_break");

    node->then = stmt(rest, tok);

    current_switch = sw;
    brk_label = brk;
    return node;
  }

  if (equal(tok, "case")) {
    if (!current_switch)
      error_tok(tok, "stray case");

    Node *node = new_node(ND_CASE, tok);
    long begin = const_expr(&tok, tok->next);
    long end;

    if (equal(tok, "...")) {
      // [GNU] Case ranges, e.g. "case 1 ... 5:"
      end = const_expr(&tok, tok->next);
      if (end < begin)
        error_tok(tok, "empty case range specified");
      node->label = new_unique_name(format("switch_case_%ld", begin));
    } else {
      end = begin;
      node->label = new_unique_name(format("switch_case_%ld_%ld", begin, end));
    }

    tok = skip(tok, ":");
    node->lhs = stmt(rest, tok);
    node->begin = begin;
    node->end = end;

    // Append it.
    Node *last_case = current_switch;
    while (last_case->case_next)
      last_case = last_case->case_next;
    last_case->case_next = node;

    return node;
  }

  if (equal(tok, "default")) {
    if (!current_switch)
      error_tok(tok, "stray default");

    Node *node = new_node(ND_CASE, tok);
    tok = skip(tok->next, ":");
    node->label = new_unique_name("switch_default");
    node->lhs = stmt(rest, tok);
    current_switch->default_case = node;
    return node;
  }

  if (equal(tok, "for")) {
    Node *node = new_node(ND_FOR, tok);
    tok = skip(tok->next, "(");

    enter_scope();

    char *brk = brk_label;
    Node *cont = cont_target;
    brk_label = node->brk_label = new_unique_name("for_break");
    node->cont_label = new_unique_name("for_continue");
    cont_target = node;

    if (is_typename(tok)) {
      Type *basety = declspec(&tok, tok, NULL);
      node->init = declaration(&tok, tok, basety, NULL);
    } else {
      node->init = expr_stmt(&tok, tok);
    }

    if (!equal(tok, ";"))
      node->cond = expr(&tok, tok);
    tok = skip(tok, ";");

    if (!equal(tok, ")"))
      node->inc = expr(&tok, tok);
    tok = skip(tok, ")");

    node->then = stmt(rest, tok);

    leave_scope();
    brk_label = brk;
    cont_target = cont;
    return node;
  }

  if (equal(tok, "while")) {
    Node *node = new_node(ND_FOR, tok);
    tok = skip(tok->next, "(");
    node->cond = expr(&tok, tok);
    tok = skip(tok, ")");

    char *brk = brk_label;
    Node *cont = cont_target;
    brk_label = node->brk_label = new_unique_name("while_break");
    node->cont_label = new_unique_name("while_continue");
    cont_target = node;

    node->then = stmt(rest, tok);

    brk_label = brk;
    cont_target = cont;
    return node;
  }

  if (equal(tok, "do")) {
    Node *node = new_node(ND_DO, tok);

    char *brk = brk_label;
    Node *cont = cont_target;
    brk_label = node->brk_label = new_unique_name("do_break");
    node->cont_label = new_unique_name("do_continue");
    cont_target = node;

    node->then = stmt(&tok, tok->next);

    brk_label = brk;
    cont_target = cont;

    tok = skip(tok, "while");
    tok = skip(tok, "(");
    node->cond = expr(&tok, tok);
    tok = skip(tok, ")");
    *rest = skip(tok, ";");
    return node;
  }

  if (equal(tok, "__asm__"))
    return asm_stmt(rest, tok);

  if (equal(tok, "goto")) {
    Node *node = new_node(ND_GOTO, tok);
    node->label = get_ident(tok->next);
    node->goto_next = gotos;
    gotos = node;
    *rest = skip(tok->next->next, ";");
    return node;
  }

  if (equal(tok, "break")) {
    if (!brk_label)
      error_tok(tok, "stray break");
    Node *node = new_node(ND_GOTO, tok);
    node->unique_label = brk_label;
    *rest = skip(tok->next, ";");
    return node;
  }

  if (equal(tok, "continue")) {
    if (!cont_target)
      error_tok(tok, "stray continue");
    Node *node = new_node(ND_GOTO, tok);
    node->unique_label = cont_target->cont_label;
    cont_target->is_resolved_cont = true;
    *rest = skip(tok->next, ";");
    return node;
  }

  if (tok->kind == TK_IDENT && equal(tok->next, ":")) {
    Node *node = new_node(ND_LABEL, tok);
    node->label = strndup(tok->loc, tok->len);
    node->unique_label = new_unique_name(node->label);
    node->lhs = stmt(rest, tok->next->next);
    node->goto_next = labels;
    labels = node;
    return node;
  }

  if (equal(tok, "{"))
    return compound_stmt(rest, tok->next);

  return expr_stmt(rest, tok);
}

// compound-stmt = (typedef | declaration | stmt)* "}"
static Node *compound_stmt(Token **rest, Token *tok) {
  Node *node = new_node(ND_BLOCK, tok);
  Node head = {};
  Node *cur = &head;

  enter_scope();

  while (!equal(tok, "}")) {
    if (is_typename(tok) && !equal(tok->next, ":")) {
      VarAttr attr = {};
      Type *basety = declspec(&tok, tok, &attr);

      if (attr.is_typedef) {
        tok = parse_typedef(tok, basety);
        continue;
      }

      if (is_function(tok)) {
        tok = function(tok, basety, &attr);
        continue;
      }

      if (attr.is_extern) {
        tok = global_variable(tok, basety, &attr);
        continue;
      }

      cur = cur->next = declaration(&tok, tok, basety, &attr);
    } else {
      cur = cur->next = stmt(&tok, tok);
    }
    add_type(cur);
  }

  leave_scope();

  node->body = head.next;
  *rest = tok->next;
  return node;
}

// expr-stmt = expr? ";"
static Node *expr_stmt(Token **rest, Token *tok) {
  if (equal(tok, ";")) {
    *rest = tok->next;
    return new_node(ND_BLOCK, tok);
  }

  Node *node = new_node(ND_EXPR_STMT, tok);
  node->lhs = expr(&tok, tok);
  *rest = skip(tok, ";");
  return node;
}

// expr = assign ("," expr)?
static Node *expr(Token **rest, Token *tok) {
  Node *node = assign(&tok, tok);

  if (equal(tok, ","))
    return new_binary(ND_COMMA, node, expr(rest, tok->next), tok);

  *rest = tok;
  return node;
}

// Evaluate a given node as a constant expression.
static int64_t eval(Node *node) {
  add_type(node);

  if (is_flonum(node->ty))
    return eval_double(node);

  switch (node->kind) {
  case ND_ADD:
    return eval(node->lhs) + eval(node->rhs);
  case ND_SUB:
    return eval(node->lhs) - eval(node->rhs);
  case ND_MUL:
    return eval(node->lhs) * eval(node->rhs);
  case ND_DIV:
    if (node->ty->is_unsigned)
      return (uint64_t)eval(node->lhs) / eval(node->rhs);
    return eval(node->lhs) / eval(node->rhs);
  case ND_NEG:
    return -eval(node->lhs);
  case ND_MOD:
    if (node->ty->is_unsigned)
      return (uint64_t)eval(node->lhs) % eval(node->rhs);
    return eval(node->lhs) % eval(node->rhs);
  case ND_BITAND:
    return eval(node->lhs) & eval(node->rhs);
  case ND_BITOR:
    return eval(node->lhs) | eval(node->rhs);
  case ND_BITXOR:
    return eval(node->lhs) ^ eval(node->rhs);
  case ND_SHL:
    return eval(node->lhs) << (int32_t)eval(node->rhs);
  case ND_SHR:
    if (node->ty->is_unsigned)
      return (uint64_t)eval(node->lhs) >> (int32_t)eval(node->rhs);
    return eval(node->lhs) >> (int32_t)eval(node->rhs);
  case ND_EQ:
    return eval(node->lhs) == eval(node->rhs);
  case ND_NE:
    return eval(node->lhs) != eval(node->rhs);
  case ND_LT:
    if (node->lhs->ty->is_unsigned)
      return (uint64_t)eval(node->lhs) < eval(node->rhs);
    return eval(node->lhs) < eval(node->rhs);
  case ND_LE:
    if (node->lhs->ty->is_unsigned)
      return (uint64_t)eval(node->lhs) <= eval(node->rhs);
    return eval(node->lhs) <= eval(node->rhs);
  case ND_COND:
    return eval(node->cond) ? eval(node->then) : eval(node->els);
  case ND_COMMA:
    return eval(node->rhs);
  case ND_NOT:
    return !eval(node->lhs);
  case ND_BITNOT:
    return ~eval(node->lhs);
  case ND_LOGAND:
    return eval(node->lhs) && eval(node->rhs);
  case ND_LOGOR:
    return eval(node->lhs) || eval(node->rhs);
  case ND_CAST: {
    int64_t val = eval(node->lhs);
    if (is_integer(node->ty)) {
      switch (node->ty->kind) {
      case TY_BOOL:
        return val ? 1 : 0;
      case TY_CHAR:
        return node->ty->is_unsigned ? (uint8_t)val : (int8_t)val;
      case TY_SHORT:
        return node->ty->is_unsigned ? (uint16_t)val : (int16_t)val;
      case TY_ENUM:
        return (int32_t)val;
      case TY_INT:
        return node->ty->is_unsigned ? (uint32_t)val : (int32_t)val;
      }
    }
    return val;
  }
  case ND_NUM:
    return node->val;
  }

  error_tok(node->tok, "not a compile-time constant");
}

int64_t const_expr(Token **rest, Token *tok) {
  Node *node = conditional(rest, tok);
  return eval(node);
}

static double eval_double(Node *node) {
  add_type(node);

  if (is_integer(node->ty)) {
    if (node->ty->is_unsigned)
      return (uint64_t)eval(node);
    return eval(node);
  }

  switch (node->kind) {
  case ND_ADD:
    return eval_double(node->lhs) + eval_double(node->rhs);
  case ND_SUB:
    return eval_double(node->lhs) - eval_double(node->rhs);
  case ND_MUL:
    return eval_double(node->lhs) * eval_double(node->rhs);
  case ND_DIV:
    return eval_double(node->lhs) / eval_double(node->rhs);
  case ND_NEG:
    return -eval_double(node->lhs);
  case ND_COND:
    return eval_double(node->cond) ? eval_double(node->then) : eval_double(node->els);
  case ND_COMMA:
    return eval_double(node->rhs);
  case ND_CAST:
    if (is_flonum(node->lhs->ty))
      return eval_double(node->lhs);
    return eval(node->lhs);
  case ND_NUM:
    return node->fval;
  }

  error_tok(node->tok, "not a compile-time constant");
}

// Convert op= operators to expressions containing an assignment.
//
// In general, `A op= C` is converted to ``tmp = &A, *tmp = *tmp op B`.
// However, if a given expression is of form `A.x op= C`, the input is
// converted to `tmp = &A, (*tmp).x = (*tmp).x op C` to handle assignments
// to bitfields.
static Node *to_assign(Node *binary) {
  add_type(binary->lhs);
  add_type(binary->rhs);
  Token *tok = binary->tok;

  // Convert `A.x op= C` to `tmp = &A, (*tmp).x = (*tmp).x op C`.
  if (binary->lhs->kind == ND_MEMBER) {
    Obj *var = new_lvar("", pointer_to(binary->lhs->lhs->ty, tok));

    Node *expr1 = new_binary(ND_ASSIGN, new_var_node(var, tok),
                             new_unary(ND_ADDR, binary->lhs->lhs, tok), tok);

    Node *expr2 = new_unary(ND_MEMBER,
                            new_unary(ND_DEREF, new_var_node(var, tok), tok),
                            tok);
    expr2->member = binary->lhs->member;

    Node *expr3 = new_unary(ND_MEMBER,
                            new_unary(ND_DEREF, new_var_node(var, tok), tok),
                            tok);
    expr3->member = binary->lhs->member;

    Node *expr4 = new_binary(ND_ASSIGN, expr2,
                             new_binary(binary->kind, expr3, binary->rhs, tok),
                             tok);

    return new_binary(ND_COMMA, expr1, expr4, tok);
  }

  // Convert `A op= C` to ``tmp = &A, *tmp = *tmp op B`.
  Obj *var = new_lvar("", pointer_to(binary->lhs->ty, tok));

  Node *expr1 = new_binary(ND_ASSIGN, new_var_node(var, tok),
                           new_unary(ND_ADDR, binary->lhs, tok), tok);

  Node *expr2 =
    new_binary(ND_ASSIGN,
               new_unary(ND_DEREF, new_var_node(var, tok), tok),
               new_binary(binary->kind,
                          new_unary(ND_DEREF, new_var_node(var, tok), tok),
                          binary->rhs,
                          tok),
               tok);

  return new_binary(ND_COMMA, expr1, expr2, tok);
}

// assign    = conditional (assign-op assign)?
// assign-op = "=" | "+=" | "-=" | "*=" | "/=" | "%=" | "&=" | "|=" | "^="
//           | "<<=" | ">>="
static Node *assign(Token **rest, Token *tok) {
  Node *node = conditional(&tok, tok);

  if (equal(tok, "="))
    return new_binary(ND_ASSIGN, node, assign(rest, tok->next), tok);

  if (equal(tok, "+="))
    return to_assign(new_add(node, assign(rest, tok->next), tok));

  if (equal(tok, "-="))
    return to_assign(new_sub(node, assign(rest, tok->next), tok));

  if (equal(tok, "*="))
    return to_assign(new_binary(ND_MUL, node, assign(rest, tok->next), tok));

  if (equal(tok, "/="))
    return to_assign(new_binary(ND_DIV, node, assign(rest, tok->next), tok));

  if (equal(tok, "%="))
    return to_assign(new_binary(ND_MOD, node, assign(rest, tok->next), tok));

  if (equal(tok, "&="))
    return to_assign(new_binary(ND_BITAND, node, assign(rest, tok->next), tok));

  if (equal(tok, "|="))
    return to_assign(new_binary(ND_BITOR, node, assign(rest, tok->next), tok));

  if (equal(tok, "^="))
    return to_assign(new_binary(ND_BITXOR, node, assign(rest, tok->next), tok));

  if (equal(tok, "<<="))
    return to_assign(new_binary(ND_SHL, node, assign(rest, tok->next), tok));

  if (equal(tok, ">>="))
    return to_assign(new_binary(ND_SHR, node, assign(rest, tok->next), tok));

  *rest = tok;
  return node;
}

// conditional = logor ("?" expr? ":" conditional)?
static Node *conditional(Token **rest, Token *tok) {
  Node *cond = logor(&tok, tok);

  if (!equal(tok, "?")) {
    *rest = tok;
    return cond;
  }

  if (equal(tok->next, ":")) {
    // [GNU] Compile `a ?: b` as `tmp = a, tmp ? tmp : b`.
    add_type(cond);
    Obj *var = new_lvar("", cond->ty);
    Node *lhs = new_binary(ND_ASSIGN, new_var_node(var, tok), cond, tok);
    Node *rhs = new_node(ND_COND, tok);
    rhs->cond = new_var_node(var, tok);
    rhs->then = new_var_node(var, tok);
    rhs->els = conditional(rest, tok->next->next);
    return new_binary(ND_COMMA, lhs, rhs, tok);
  }

  Node *node = new_node(ND_COND, tok);
  node->cond = cond;
  node->then = expr(&tok, tok->next);
  tok = skip(tok, ":");
  node->els = conditional(rest, tok);
  return node;
}

// logor = logand ("||" logand)*
static Node *logor(Token **rest, Token *tok) {
  Node *node = logand(&tok, tok);
  while (equal(tok, "||")) {
    Token *start = tok;
    node = new_binary(ND_LOGOR, node, logand(&tok, tok->next), start);
  }
  *rest = tok;
  return node;
}

// logand = bitor ("&&" bitor)*
static Node *logand(Token **rest, Token *tok) {
  Node *node = bitor(&tok, tok);
  while (equal(tok, "&&")) {
    Token *start = tok;
    node = new_binary(ND_LOGAND, node, bitor(&tok, tok->next), start);
  }
  *rest = tok;
  return node;
}

// bitor = bitxor ("|" bitxor)*
static Node *bitor(Token **rest, Token *tok) {
  Node *node = bitxor(&tok, tok);
  while (equal(tok, "|")) {
    Token *start = tok;
    node = new_binary(ND_BITOR, node, bitxor(&tok, tok->next), start);
  }
  *rest = tok;
  return node;
}

// bitxor = bitand ("^" bitand)*
static Node *bitxor(Token **rest, Token *tok) {
  Node *node = bitand(&tok, tok);
  while (equal(tok, "^")) {
    Token *start = tok;
    node = new_binary(ND_BITXOR, node, bitand(&tok, tok->next), start);
  }
  *rest = tok;
  return node;
}

// bitand = equality ("&" equality)*
static Node *bitand(Token **rest, Token *tok) {
  Node *node = equality(&tok, tok);
  while (equal(tok, "&")) {
    Token *start = tok;
    node = new_binary(ND_BITAND, node, equality(&tok, tok->next), start);
  }
  *rest = tok;
  return node;
}

// equality = relational ("==" relational | "!=" relational)*
static Node *equality(Token **rest, Token *tok) {
  Node *node = relational(&tok, tok);

  for (;;) {
    Token *start = tok;

    if (equal(tok, "==")) {
      node = new_binary(ND_EQ, node, relational(&tok, tok->next), start);
      continue;
    }

    if (equal(tok, "!=")) {
      node = new_binary(ND_NE, node, relational(&tok, tok->next), start);
      continue;
    }

    *rest = tok;
    return node;
  }
}

// relational = shift ("<" shift | "<=" shift | ">" shift | ">=" shift)*
static Node *relational(Token **rest, Token *tok) {
  Node *node = shift(&tok, tok);

  for (;;) {
    Token *start = tok;

    if (equal(tok, "<")) {
      node = new_binary(ND_LT, node, shift(&tok, tok->next), start);
      continue;
    }

    if (equal(tok, "<=")) {
      node = new_binary(ND_LE, node, shift(&tok, tok->next), start);
      continue;
    }

    if (equal(tok, ">")) {
      node = new_binary(ND_LT, shift(&tok, tok->next), node, start);
      continue;
    }

    if (equal(tok, ">=")) {
      node = new_binary(ND_LE, shift(&tok, tok->next), node, start);
      continue;
    }

    *rest = tok;
    return node;
  }
}

// shift = add ("<<" add | ">>" add)*
static Node *shift(Token **rest, Token *tok) {
  Node *node = add(&tok, tok);

  for (;;) {
    Token *start = tok;

    if (equal(tok, "<<")) {
      node = new_binary(ND_SHL, node, add(&tok, tok->next), start);
      continue;
    }

    if (equal(tok, ">>")) {
      node = new_binary(ND_SHR, node, add(&tok, tok->next), start);
      continue;
    }

    *rest = tok;
    return node;
  }
}

// In C, `+` operator is overloaded to perform the pointer arithmetic.
// If p is a pointer, p+n adds not n but sizeof(*p)*n to the value of p,
// so that p+n points to the location n elements (not bytes) ahead of p.
// In other words, we need to scale an integer value before adding to a
// pointer value. This function takes care of the scaling.
static Node *new_add(Node *lhs, Node *rhs, Token *tok) {
  add_type(lhs);
  add_type(rhs);

  // num + num
  if (is_numeric(lhs->ty) && is_numeric(rhs->ty))
    return new_binary(ND_ADD, lhs, rhs, tok);

  if (lhs->ty->base && rhs->ty->base)
    error_tok(tok, "invalid operands");

  // Canonicalize `num + ptr` to `ptr + num`.
  if (!lhs->ty->base && rhs->ty->base) {
    Node *tmp = lhs;
    lhs = rhs;
    rhs = tmp;
  }

  // ptr + num
  rhs = new_binary(ND_MUL, rhs, lhs->ty->base->size, tok);
  return new_binary(ND_ADD, lhs, rhs, tok);
}

// Like `+`, `-` is overloaded for the pointer type.
static Node *new_sub(Node *lhs, Node *rhs, Token *tok) {
  add_type(lhs);
  add_type(rhs);

  // num - num
  if (is_numeric(lhs->ty) && is_numeric(rhs->ty))
    return new_binary(ND_SUB, lhs, rhs, tok);

  // ptr - num
  if (lhs->ty->base && is_integer(rhs->ty)) {
    rhs = new_binary(ND_MUL, rhs, lhs->ty->base->size, tok);
    Node *node = new_binary(ND_SUB, lhs, rhs, tok);
    return node;
  }

  // ptr - ptr, which returns how many elements are between the two.
  if (lhs->ty->base && rhs->ty->base) {
    Node *node = new_binary(ND_SUB, lhs, rhs, tok);
    node = new_binary(ND_DIV, node, lhs->ty->base->size, tok);
    add_type(node);
    node->ty = ty_intptr;   // Forced ptrdiff_t
    return node;
  }

  error_tok(tok, "invalid operands");
}

// add = mul ("+" mul | "-" mul)*
static Node *add(Token **rest, Token *tok) {
  Node *node = mul(&tok, tok);

  for (;;) {
    Token *start = tok;

    if (equal(tok, "+")) {
      node = new_add(node, mul(&tok, tok->next), start);
      continue;
    }

    if (equal(tok, "-")) {
      node = new_sub(node, mul(&tok, tok->next), start);
      continue;
    }

    *rest = tok;
    return node;
  }
}

// mul = cast ("*" cast | "/" cast | "%" cast)*
static Node *mul(Token **rest, Token *tok) {
  Node *node = cast(&tok, tok);

  for (;;) {
    Token *start = tok;

    if (equal(tok, "*")) {
      node = new_binary(ND_MUL, node, cast(&tok, tok->next), start);
      continue;
    }

    if (equal(tok, "/")) {
      node = new_binary(ND_DIV, node, cast(&tok, tok->next), start);
      continue;
    }

    if (equal(tok, "%")) {
      node = new_binary(ND_MOD, node, cast(&tok, tok->next), start);
      continue;
    }

    *rest = tok;
    return node;
  }
}

// cast = "(" type-name ")" cast | unary
static Node *cast(Token **rest, Token *tok) {
  if (equal(tok, "(") && is_typename(tok->next)) {
    Token *start = tok;
    Type *ty = typename(&tok, tok->next);
    tok = skip(tok, ")");

    // compound literal
    if (equal(tok, "{"))
      return unary(rest, start);

    // type cast
    Node *node = new_cast(cast(rest, tok), ty);
    node->tok = start;
    return node;
  }

  return unary(rest, tok);
}

// unary = ("+" | "-" | "*" | "&" | "!" | "~") cast
//       | ("++" | "--") unary
//       | postfix
static Node *unary(Token **rest, Token *tok) {
  if (equal(tok, "+"))
    return cast(rest, tok->next);

  if (equal(tok, "-"))
    return new_unary(ND_NEG, cast(rest, tok->next), tok);

  if (equal(tok, "&")) {
    Node *lhs = cast(rest, tok->next);
    add_type(lhs);
    if (lhs->kind == ND_MEMBER && lhs->member->is_bitfield)
      error_tok(tok, "cannot take address of bitfield");
    return new_unary(ND_ADDR, lhs, tok);
  }

  if (equal(tok, "*")) {
    // [C18 6.5.3.2p4] This is an oddity in the C spec, but dereferencing
    // a function shouldn't do anything. If foo is a function, `*foo`,
    // `**foo` or `*****foo` are all equivalent to just `foo`.
    Node *node = cast(rest, tok->next);
    add_type(node);
    if (node->ty->kind == TY_FUNC)
      return node;
    return new_unary(ND_DEREF, node, tok);
  }

  if (equal(tok, "!"))
    return new_unary(ND_NOT, cast(rest, tok->next), tok);

  if (equal(tok, "~"))
    return new_unary(ND_BITNOT, cast(rest, tok->next), tok);

  // Read ++i as i+=1
  if (equal(tok, "++"))
    return to_assign(new_add(unary(rest, tok->next), new_num(1, tok), tok));

  // Read --i as i-=1
  if (equal(tok, "--"))
    return to_assign(new_sub(unary(rest, tok->next), new_num(1, tok), tok));

  return postfix(rest, tok);
}

// struct-members = (declspec declarator (","  declarator)* ";")*
static void struct_members(Token **rest, Token *tok, Type *ty) {
  Member head = {};
  Member *cur = &head;
  int idx = 0;

  while (!equal(tok, "}")) {
    VarAttr attr = {};
    Type *basety = declspec(&tok, tok, &attr);
    bool first = true;

    // Anonymous struct member
    if ((basety->kind == TY_STRUCT || basety->kind == TY_UNION) &&
        consume(&tok, tok, ";")) {
      Member *mem = calloc(1, sizeof(Member));
      mem->ty = basety;
      mem->idx = idx++;
      mem->align = attr.align ? attr.align : mem->ty->align;
      mem->is_aligning = attr.align != NULL;
      cur = cur->next = mem;
      continue;
    }

    // Regular struct members
    while (!consume(&tok, tok, ";")) {
      if (!first)
        tok = skip(tok, ",");
      first = false;

      Member *mem = calloc(1, sizeof(Member));
      mem->ty = declarator(&tok, tok, basety);
      mem->name = mem->ty->name;
      mem->idx = idx++;
      mem->align = attr.align ? attr.align : mem->ty->align;

      if (consume(&tok, tok, ":")) {
        mem->is_bitfield = true;
        mem->bit_width = const_expr(&tok, tok);
      }

      cur = cur->next = mem;
    }
  }

  // If the last element is an array of incomplete type, it's
  // called a "flexible array member". It should behave as if
  // if were a zero-sized array.
  if (cur != &head && cur->ty->kind == TY_ARRAY && cur->ty->array_len < 0) {
    // Flexible array (length=0) 
    cur->ty = array_of(cur->ty->base, 0, cur->ty->tok);
    ty->is_flexible = true;
  }

  *rest = tok->next;
  ty->members = head.next;
}

// struct-union-decl = ident? ("{" struct-members)?
static Type *struct_union_decl(Token **rest, Token *tok) {
  Token *origin = tok;

  // Read a tag.
  Token *tag = NULL;
  if (tok->kind == TK_IDENT) {
    tag = tok;
    tok = tok->next;
  }

  if (tag && !equal(tok, "{")) {
    *rest = tok;

    Type *ty = find_tag(tag);
    if (ty)
      return ty;

    ty = struct_type(origin);
    static Node sizem1_node = {ND_NUM, -1};
    ty->size = &sizem1_node;
//    ty->origin_size = &sizem1_node;   // AAAA
    push_tag_scope(tag, ty);
    return ty;
  }

  tok = skip(tok, "{");

  // Construct a struct object.
  Type *ty = struct_type(origin);
  struct_members(rest, tok, ty);

  if (tag) {
    // If this is a redefinition, overwrite a previous type.
    // Otherwise, register the struct type.
    Type *ty2 = hashmap_get2(&scope->tags, tag->loc, tag->len);
    if (ty2) {
      *ty2 = *ty;
      return ty2;
    }
    push_tag_scope(tag, ty);
  }

  return ty;
}

static Node *max_node(Node *lhs, Node *rhs)
{
    // cond = lhs < rhs ? rhs : lhs
    Node *cond = new_node(ND_COND, NULL);
    cond->cond = new_binary(ND_LT, lhs, rhs, NULL);
    cond->then = rhs;
    cond->els = lhs;
    return cond;
}

static Node *reduce_and_cache_disp(Node *node) {
  node = reduce_node(node);
  switch (node->kind) {
  case ND_ADD:
  case ND_SUB:
  case ND_MUL:
  case ND_DIV:
  case ND_LT:
  case ND_LE:
  case ND_COND:
  case ND_COMMA:
  case ND_STMT_EXPR: {
    // Cache calculated displacement into anonymous global variable (2)
    Obj *var = new_anon_gvar(node->ty, "disp");
    Node *node_var = new_var_node(var, NULL);
    var->init_expr = reduce_node(new_binary(ND_ASSIGN, node_var, node, NULL));
    return reduce_node(node_var);
  }
  }
  return node;
}

// struct-decl = struct-union-decl
static Type *struct_decl(Token **rest, Token *tok) {
  Type *ty = struct_union_decl(rest, tok);
  ty->kind = TY_STRUCT;

  if (ty->size->kind == ND_NUM && ty->size->val < 0)
    return ty;

  // Assign offsets within the struct to members.
  Node *node0 = new_typed_num(0, ty_uintptr, NULL);   // (size_t)0
  Node *node1 = new_typed_num(1, ty_uintptr, NULL);   // (size_t)1
  Node *node8 = new_typed_num(8, ty_uintptr, NULL);   // (size_t)8
  Node *bits = node0;
  Node *align = node1;
  bool is_overall_fixed_size = true;

  for (Member *mem = ty->members; mem; mem = mem->next) {
    if (mem->is_bitfield) {
      // int sz = mem->ty->size;
      Node *sz8 = new_binary(ND_MUL, mem->ty->size, node8, NULL);

      if (mem->is_bitfield && mem->bit_width == 0)
        // Zero-width anonymous bitfield has a special meaning.
        // It affects only alignment.
        bits = align_to_node(bits, sz8);
      else {
        // bits = (bits / (sz * 8) != (bits + mem->bit_width - 1) / (sz * 8)) ?
        //    align_to(bits, sz * 8) : bits
        Node *cond = new_node(ND_COND, NULL);
        cond->cond = new_binary(ND_NE,
          new_binary(ND_DIV, bits, sz8, NULL),
          new_binary(ND_DIV,
            new_binary(ND_SUB,
              new_binary(ND_ADD,
                bits,
                new_typed_num(mem->bit_width, ty_uintptr, NULL),
                NULL),
              node1, NULL),
            sz8, NULL),
          NULL);
        cond->then = align_to_node(bits, sz8);
        cond->els = bits;
        bits = reduce_node(cond);

        // mem->offset = align_down(bits / 8, sz);
        mem->offset = reduce_and_cache_disp(
          align_down_node(
            new_binary(ND_DIV, bits, node8, NULL),
            mem->ty->size));

        // mem->bit_offset = (int)(bits % (sz * 8));
        mem->bit_offset = reduce_and_cache_disp(
          new_cast(
            new_binary(ND_MOD, bits, sz8, NULL),
            ty_int));

        // bits += mem->bit_width;
        bits = reduce_node(
          new_binary(ND_ADD,
            bits,
            new_typed_num(mem->bit_width, ty_uintptr, NULL),
            NULL));
      }
    } else {
      // bits = align_to(bits, mem->align * 8)
      bits = reduce_node(
        align_to_node(
          bits, new_binary(ND_MUL, mem->align, node8, NULL)));

      // mem->offset = bits / 8
      mem->offset = reduce_and_cache_disp(
        new_binary(ND_DIV, bits, node8, NULL));

      // bits = bits + mem->ty->size * 8
      bits = reduce_node(
        new_binary(ND_ADD,
          bits,
          new_binary(ND_MUL, mem->ty->size, node8, NULL),
          NULL));

      is_overall_fixed_size &= mem->ty->is_fixed_size;
    }

    // align = max(align, mem->align)
    align = reduce_and_cache_disp(
      max_node(align, mem->align));
  }

  ty->align = align;

  // ty->size = align_to(offset, align * 8) / 8
  ty->size = reduce_and_cache_disp(
    new_binary(ND_DIV,
      align_to_node(
        bits,
        new_binary(ND_MUL, align, node8, NULL)),
        node8,
        NULL));

  ty->is_fixed_size = is_overall_fixed_size;

  return ty;
}

// union-decl = struct-union-decl
static Type *union_decl(Token **rest, Token *tok) {
  Type *ty = struct_union_decl(rest, tok);
  ty->kind = TY_UNION;

  if (ty->size->kind == ND_NUM && ty->size->val < 0)
    return ty;

  // We need to compute the alignment and the size though.
  Node *node0 = new_typed_num(0, ty_uintptr, NULL);   // (size_t)0
  Node *node1 = new_typed_num(1, ty_uintptr, NULL);   // (size_t)1
  Node *size = node0;
  Node *align = node1;
  bool is_overall_fixed_size = true;

  for (Member *mem = ty->members; mem; mem = mem->next) {
    // mem->offset = mem->origin_offset = 0
    mem->offset = node0;
    //mem->origin_offset = node0;   // AAAA

    // align = max(align, mem->align)
    align = reduce_and_cache_disp(max_node(align, mem->align));
    // size = max(size, mem->ty->size)
    size = reduce_and_cache_disp(max_node(size, mem->ty->size));

    is_overall_fixed_size &= mem->ty->is_fixed_size;
  }

  ty->align = reduce_and_cache_disp(align);

  // ty->size = align_to(size, ty->align)
  ty->size = reduce_and_cache_disp(align_to_node(size, ty->align));

  ty->is_fixed_size = is_overall_fixed_size;

  return ty;
}

// Find a struct member by name.
static Member *get_struct_member(Type *ty, Token *tok) {
  for (Member *mem = ty->members; mem; mem = mem->next) {
    // Anonymous struct member
    if ((mem->ty->kind == TY_STRUCT || mem->ty->kind == TY_UNION) &&
        !mem->name) {
      if (get_struct_member(mem->ty, tok))
        return mem;
      continue;
    }

    // Regular struct member
    if (mem->name->len == tok->len &&
        !strncmp(mem->name->loc, tok->loc, tok->len))
      return mem;
  }
  return NULL;
}

// Create a node representing a struct member access, such as foo.bar
// where foo is a struct and bar is a member name.
//
// C has a feature called "anonymous struct" which allows a struct to
// have another unnamed struct as a member like this:
//
//   struct { struct { int a; }; int b; } x;
//
// The members of an anonymous struct belong to the outer struct's
// member namespace. Therefore, in the above example, you can access
// member "a" of the anonymous struct as "x.a".
//
// This function takes care of anonymous structs.
static Node *struct_ref(Node *node, Token *tok) {
  add_type(node);
  if (node->ty->kind != TY_STRUCT && node->ty->kind != TY_UNION)
    error_tok(node->tok, "not a struct nor a union");

  Type *ty = node->ty;

  for (;;) {
    Member *mem = get_struct_member(ty, tok);
    if (!mem)
      error_tok(tok, "no such member");
    node = new_unary(ND_MEMBER, node, tok);
    node->member = mem;
    if (mem->name)
      break;
    ty = mem->ty;
  }
  return node;
}

// Convert A++ to `(typeof A)((A += 1) - 1)`
static Node *new_inc_dec(Node *node, Token *tok, int addend) {
  add_type(node);
  return new_cast(new_add(to_assign(new_add(node, new_num(addend, tok), tok)),
                          new_num(-addend, tok), tok),
                  node->ty);
}

// postfix = "(" type-name ")" "{" initializer-list "}"
//         = ident "(" func-args ")" postfix-tail*
//         | primary postfix-tail*
//
// postfix-tail = "[" expr "]"
//              | "(" func-args ")"
//              | "." ident
//              | "->" ident
//              | "++"
//              | "--"
static Node *postfix(Token **rest, Token *tok) {
  if (equal(tok, "(") && is_typename(tok->next)) {
    // Compound literal
    Token *start = tok;
    Type *ty = typename(&tok, tok->next);
    tok = skip(tok, ")");

    if (scope->next == NULL) {
      Obj *var = new_anon_gvar(ty, "gvar");
      gvar_initializer(rest, tok, var);
      return new_var_node(var, start);
    }

    Obj *var = new_lvar("", ty);
    Node *lhs = lvar_initializer(rest, tok, var);
    Node *rhs = new_var_node(var, tok);
    return new_binary(ND_COMMA, lhs, rhs, start);
  }

  Node *node = primary(&tok, tok);

  for (;;) {
    if (equal(tok, "(")) {
      node = funcall(&tok, tok->next, node);
      continue;
    }

    if (equal(tok, "[")) {
      // x[y] is short for *(x+y)
      Token *start = tok;
      Node *idx = expr(&tok, tok->next);
      tok = skip(tok, "]");
      node = new_unary(ND_DEREF, new_add(node, idx, start), start);
      continue;
    }

    if (equal(tok, ".")) {
      node = struct_ref(node, tok->next);
      tok = tok->next->next;
      continue;
    }

    if (equal(tok, "->")) {
      // x->y is short for (*x).y
      node = new_unary(ND_DEREF, node, tok);
      node = struct_ref(node, tok->next);
      tok = tok->next->next;
      continue;
    }

    if (equal(tok, "++")) {
      node = new_inc_dec(node, tok, 1);
      tok = tok->next;
      continue;
    }

    if (equal(tok, "--")) {
      node = new_inc_dec(node, tok, -1);
      tok = tok->next;
      continue;
    }

    *rest = tok;
    return node;
  }
}

// funcall = (assign ("," assign)*)? ")"
static Node *funcall(Token **rest, Token *tok, Node *fn) {
  add_type(fn);

  if (fn->ty->kind != TY_FUNC &&
      (fn->ty->kind != TY_PTR || fn->ty->base->kind != TY_FUNC))
    error_tok(fn->tok, "not a function");

  Type *ty = (fn->ty->kind == TY_FUNC) ? fn->ty : fn->ty->base;
  Type *param_ty = ty->params;

  Node head = {};
  Node *cur = &head;

  while (!equal(tok, ")")) {
    if (cur != &head)
      tok = skip(tok, ",");

    Node *arg = assign(&tok, tok);
    add_type(arg);

    if (param_ty) {
      if (param_ty->kind != TY_STRUCT && param_ty->kind != TY_UNION)
        arg = new_cast(arg, param_ty);
      param_ty = param_ty->next;
    } else if (arg->ty->kind == TY_FLOAT) {
      // If parameter type is omitted (e.g. in "..."), float
      // arguments are promoted to double.
      arg = new_cast(arg, ty_double);
    }
    
    cur = cur->next = arg;
  }

  *rest = skip(tok, ")");

  Node *node = new_unary(ND_FUNCALL, fn, tok);
  node->func_ty = ty;
  node->ty = ty->return_ty;
  node->args = head.next;
  return node;
}

// generic-selection = "(" assign "," generic-assoc ("," generic-assoc)* ")"
//
// generic-assoc = type-name ":" assign
//               | "default" ":" assign
static Node *generic_selection(Token **rest, Token *tok) {
  Token *start = tok;
  tok = skip(tok, "(");

  Node *ctrl = assign(&tok, tok);
  add_type(ctrl);

  Type *t1 = ctrl->ty;
  if (t1->kind == TY_FUNC)
    t1 = pointer_to(t1, tok);
  else if (t1->kind == TY_ARRAY)
    t1 = pointer_to(t1->base, tok);

  Node *ret = NULL;

  while (!consume(rest, tok, ")")) {
    tok = skip(tok, ",");

    if (equal(tok, "default")) {
      tok = skip(tok->next, ":");
      Node *node = assign(&tok, tok);
      if (!ret)
        ret = node;
      continue;
    }

    Type *t2 = typename(&tok, tok);
    tok = skip(tok, ":");
    Node *node = assign(&tok, tok);
    if (is_compatible(t1, t2))
      ret = node;
  }

  if (!ret)
    error_tok(start, "controlling expression type not compatible with"
              " any generic association type");
  return ret;
}

static Node *builtin_overflow(NodeKind kind, Token **rest, Token *tok, Token *start) {
    tok = skip(tok->next, "(");
    Node *lhs_arg = assign(&tok, tok);
    add_type(lhs_arg);
    tok = skip(tok, ",");
    Node *rhs_arg = assign(&tok, tok);
    add_type(rhs_arg);
    tok = skip(tok, ",");
    Node *res_arg = assign(&tok, tok);
    add_type(res_arg);

    if (!is_integer(lhs_arg->ty) || !is_integer(rhs_arg->ty) || res_arg->ty->kind != TY_PTR || !is_integer(res_arg->ty->base)) {
      error_tok(tok, "invalid type combination on overflow checker");
    }

    *rest = skip(tok, ")");

    Node *op = new_binary(kind, lhs_arg, rhs_arg, start);
    op->res = res_arg;
    add_type(op);

    return op;
}

// primary = "(" "{" stmt+ "}" ")"
//         | "(" expr ")"
//         | "sizeof" "(" type-name ")"
//         | "sizeof" unary
//         | "_Alignof" "(" type-name ")"
//         | "_Alignof" unary
//         | "_Generic" generic-selection
//         | "__builtin_types_compatible_p" "(" type-name, type-name, ")"
//         | "__builtin_complex" "(" assign, assign ")"
//         | "__builtin_add_overflow" "(" assign, assign, assign ")"
//         | ident
//         | str
//         | num
static Node *primary(Token **rest, Token *tok) {
  Token *start = tok;

  if (equal(tok, "(") && equal(tok->next, "{")) {
    // This is a GNU statement expresssion.
    Node *node = new_node(ND_STMT_EXPR, tok);
    node->body = compound_stmt(&tok, tok->next->next)->body;
    *rest = skip(tok, ")");
    return node;
  }

  if (equal(tok, "(")) {
    Node *node = expr(&tok, tok->next);
    *rest = skip(tok, ")");
    return node;
  }

  if (equal(tok, "sizeof") && equal(tok->next, "(") && is_typename(tok->next->next)) {
    Type *ty = typename(&tok, tok->next->next);
    *rest = skip(tok, ")");
    return reduce_node(new_sizeof(ty, start));
  }

  if (equal(tok, "sizeof")) {
    Node *node = unary(rest, tok->next);
    add_type(node);
    return reduce_node(new_sizeof(node->ty, start));
  }

  if (equal(tok, "_Alignof") && equal(tok->next, "(") && is_typename(tok->next->next)) {
    Type *ty = typename(&tok, tok->next->next);
    *rest = skip(tok, ")");
    return ty->align;
  }

  if (equal(tok, "_Alignof")) {
    Node *node = unary(rest, tok->next);
    add_type(node);
    return node->ty->align;
  }

  if (equal(tok, "_Generic"))
    return generic_selection(rest, tok->next);

  if (equal(tok, "__builtin_types_compatible_p")) {
    tok = skip(tok->next, "(");
    Type *t1 = typename(&tok, tok);
    tok = skip(tok, ",");
    Type *t2 = typename(&tok, tok);
    *rest = skip(tok, ")");
    return new_num(is_compatible(t1, t2), start);
  }

  if (equal(tok, "__builtin_complex")) {
    tok = skip(tok->next, "(");
    Node *real_arg = assign(&tok, tok);
    add_type(real_arg);
    tok = skip(tok, ",");
    Node *imag_arg = assign(&tok, tok);
    add_type(imag_arg);

    if (real_arg->ty->kind != imag_arg->ty->kind || !is_flonum(real_arg->ty)) {
      error_tok(tok, "invalid type combination on complex constructor");
    }

    *rest = skip(tok, ")");

    return new_complex(real_arg, imag_arg,
      real_arg->ty->kind == TY_FLOAT ? ty_float_complex : ty_double_complex, start);
  }

  if (equal(tok, "__builtin_add_overflow"))
    return builtin_overflow(ND_ADD_OVF, rest, tok, start);
  if (equal(tok, "__builtin_sub_overflow"))
    return builtin_overflow(ND_SUB_OVF, rest, tok, start);
  if (equal(tok, "__builtin_mul_overflow"))
    return builtin_overflow(ND_MUL_OVF, rest, tok, start);

  if (tok->kind == TK_IDENT) {
    // Variable or enum constant
    VarScope *sc = find_var(tok);
    *rest = tok->next;

    // For "static inline" function
    if (sc && sc->var && sc->var->is_function) {
      if (current_fn)
        strarray_push(&current_fn->refs, sc->var->name);
      else
        sc->var->is_root = true;
    }

    if (sc) {
      if (sc->var)
        return new_var_node(sc->var, tok);
      if (sc->enum_ty)
        return new_num(sc->enum_val, tok);
    }

    if (equal(tok->next, "("))
      error_tok(tok, "implicit declaration of a function");
    error_tok(tok, "undefined variable");
  }

  if (tok->kind == TK_STR) {
    Obj *var = new_string_literal(tok->str, tok->ty);
    *rest = tok->next;
    return new_var_node(var, tok);
  }

  if (tok->kind == TK_NUM) {
    Node *node;
    if (is_flonum(tok->ty))
      node = new_flonum(tok->fval, tok->ty, tok);
    else if (tok->ty->kind == TY_FLOAT_COMPLEX) {
      node = new_complex(
        new_flonum(0.0f, ty_float, NULL),
        new_flonum((float)tok->fval, ty_float, NULL),
        tok->ty, tok);
    } else if (tok->ty->kind == TY_DOUBLE_COMPLEX) {
      node = new_complex(
        new_flonum(0.0, ty_double, NULL),
        new_flonum(tok->fval, ty_double, NULL),
        tok->ty, tok);
    } else
      node = new_typed_num(tok->val, tok->ty, tok);

    *rest = tok->next;
    return node;
  }

  error_tok(tok, "expected an expression");
}

static Token *parse_typedef(Token *tok, Type *basety) {
  bool first = true;

  while (!consume(&tok, tok, ";")) {
    if (!first)
      tok = skip(tok, ",");
    first = false;

    Type *ty = declarator(&tok, tok, basety);
    if (!ty->name)
      error_tok(ty->name_pos, "typedef name omitted");
    basety->typedef_name = ty->name;
    push_scope(get_ident(ty->name))->type_def = ty;
  }
  return tok;
}

static void create_param_lvars(Type *param) {
  if (param) {
    create_param_lvars(param->next);
    if (!param->name)
      new_param_lvar(new_unique_name("param"), param);
    else
      new_param_lvar(get_ident(param->name), param);
  }
}

// This function matches gotos with labels.
//
// We cannot resolve gotos as we parse a function because gotos
// can refer a label that appears later in the function.
// So, we need to do this after we parse the entire function.
static void resolve_goto_labels(void) {
  for (Node *x = gotos; x; x = x->goto_next) {
    for (Node *y = labels; y; y = y->goto_next) {
      if (!strcmp(x->label, y->label)) {
        x->unique_label = y->unique_label;
        y->is_resolved_label = true;
        break;
      }
    }

    if (x->unique_label == NULL)
      error_tok(x->tok->next, "use of undeclared label");
  }

  gotos = labels = NULL;
}

static Obj *find_func(char *name) {
  Scope *sc = scope;
  while (sc->next)
    sc = sc->next;

  VarScope *sc2 = hashmap_get(&sc->vars, name);
  if (sc2 && sc2->var && sc2->var->is_function)
    return sc2->var;
  return NULL;
}

static void mark_live(Obj *var) {
  if (!var->is_function || var->is_live)
    return;
  var->is_live = true;

  for (int i = 0; i < var->refs.len; i++) {
    Obj *fn = find_func(var->refs.data[i]);
    if (fn)
      mark_live(fn);
  }
}

static Token *function(Token *tok, Type *basety, VarAttr *attr) {
  Type *ty = declarator(&tok, tok, basety);
  if (!ty->name)
    error_tok(ty->name_pos, "function name omitted");

  // asm declaration
  char *exact_name = NULL;
  Token *asm_tok = tok;
  if (consume(&tok, tok, "__asm__")) {
    tok = skip(tok, "(");
    if (tok->kind != TK_STR || tok->ty->base->kind != TY_CHAR)
      error_tok(tok, "symbol name required");
    exact_name = tok->str;
    tok = tok->next;
    tok = skip(tok, ")");
  }

  Obj *fn = new_gvar(get_ident(ty->name), ty);
  fn->is_function = true;
  fn->is_definition = !(consume(&tok, tok, ";") || consume(&tok, tok, ","));
  fn->is_static = attr->is_static || (attr->is_inline && !attr->is_extern);
  fn->is_inline = attr->is_inline;
  fn->is_root = !(fn->is_static && fn->is_inline);
  fn->exact_name = exact_name;

  if (!fn->is_definition)
    return tok;

  if (exact_name)
    error_tok(asm_tok, "could not apply exact symbol name");

  current_fn = fn;
  locals = NULL;
  enter_scope();
  create_param_lvars(ty->params);
  fn->params = locals;

  // .NET: Splitted between parameters and local variables.
  locals = NULL;

  tok = skip(tok, "{");

  // [https://www.sigbus.info/n1570#6.4.2.2p1] "__func__" is
  // automatically defined as a local variable containing the
  // current function name.
  push_scope("__func__")->var =
    new_string_literal(fn->name, array_of(ty_char, strlen(fn->name) + 1, tok));

  // [GNU] __FUNCTION__ is yet another name of __func__.
  push_scope("__FUNCTION__")->var =
    new_string_literal(fn->name, array_of(ty_char, strlen(fn->name) + 1, tok));

  fn->body = compound_stmt(&tok, tok);
  fn->locals = locals;
  leave_scope();
  resolve_goto_labels();
  return tok;
}

static Token *global_variable(Token *tok, Type *basety, VarAttr *attr) {
  bool first = true;

  while (!consume(&tok, tok, ";")) {
    if (!first)
      tok = skip(tok, ",");
    first = false;

    Type *ty = declarator(&tok, tok, basety);
    if (!ty->name)
      error_tok(ty->name_pos, "variable name omitted");

    // asm declaration
    char *exact_name = NULL;
    Token *asm_tok = tok;
    if (consume(&tok, tok, "__asm__")) {
      tok = skip(tok, "(");
      if (tok->kind != TK_STR || tok->ty->base->kind != TY_CHAR)
        error_tok(tok, "symbol name required");
      exact_name = tok->str;
      tok = tok->next;
      tok = skip(tok, ")");
    }

    Obj *var = new_gvar(get_ident(ty->name), ty);
    var->is_definition = !attr->is_extern;
    var->is_static = attr->is_static;
    var->is_tls = attr->is_tls;
    if (attr->align)
      var->align = attr->align;
    var->exact_name = exact_name;

    if (equal(tok, "=")) {
      if (exact_name)
        error_tok(asm_tok, "could not apply exact symbol name");
        
      gvar_initializer(&tok, tok->next, var);
    }
  }
  return tok;
}

// Lookahead tokens and returns true if a given token is a start
// of a function definition or declaration.
static bool is_function(Token *tok) {
  if (equal(tok, ";"))
    return false;

  Type dummy = {};
  Type *ty = declarator(&tok, tok, &dummy);
  return ty->kind == TY_FUNC;
}

// program = (typedef | function-definition | global-variable)*
Obj *parse(Token *tok) {
  globals = NULL;

  while (tok->kind != TK_EOF) {
    VarAttr attr = {};
    Type *basety = declspec(&tok, tok, &attr);

    Obj *global_ty_obj = calloc(1, sizeof(Obj));
    global_ty_obj->kind = OB_GLOBAL_TYPE;
    global_ty_obj->ty = basety;
    global_ty_obj->next = globals;
    globals = global_ty_obj;

    // Typedef
    if (attr.is_typedef) {
      tok = parse_typedef(tok, basety);
      continue;
    }

    // Function
    if (is_function(tok)) {
      tok = function(tok, basety, &attr);
      continue;
    }

    // Global variable
    tok = global_variable(tok, basety, &attr);
  }

  for (Obj *var = globals; var; var = var->next)
    if (var->is_root)
      mark_live(var);

  return globals;
}
