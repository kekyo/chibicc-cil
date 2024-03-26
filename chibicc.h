#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <glob.h>
#include <libgen.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdnoreturn.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include <spawn.h>

#define MAX(x, y) ((x) < (y) ? (y) : (x))
#define MIN(x, y) ((x) < (y) ? (x) : (y))

#ifndef __GNUC__
# define __attribute__(x)
#endif

typedef struct Obj Obj;
typedef struct Token Token;
typedef struct VarScope VarScope;
typedef struct TagScope TagScope;
typedef struct Scope Scope;
typedef struct Type Type;
typedef struct Node Node;
typedef struct Member Member;
typedef struct EnumMember EnumMember;
typedef struct Hideset Hideset;

//
// strings.c
//

typedef struct {
  char **data;
  int capacity;
  int len;
} StringArray;

void strarray_push(StringArray *arr, char *s);
char *format(char *fmt, ...) __attribute__((format(printf, 1, 2)));

//
// tokenize.c
//

// Token
typedef enum {
  TK_IDENT,   // Identifiers
  TK_PUNCT,   // Punctuators
  TK_KEYWORD, // Keywords
  TK_STR,     // String literals
  TK_NUM,     // Numeric literals
  TK_PP_NUM,  // Preprocessing numbers
  TK_EOF,     // End-of-file markers
} TokenKind;

typedef struct {
  char *name;
  int file_no;
  char *contents;

  // For #line directive
  char *display_name;
  int line_delta;
} File;

// Token type
struct Token {
  TokenKind kind;   // Token kind
  Token *next;      // Next token
  int64_t val;      // If kind is TK_NUM, its value
  double fval;      // If kind is TK_NUM, its value
  char *loc;        // Token location
  int len;          // Token length
  Type *ty;         // Used if TK_NUM or TK_STR
  char *str;        // String literal contents including terminating '\0'
  
  File *file;       // Source location
  char *filename;   // Filename
  int line_no;      // Line number
  int line_delta;   // Line number
  int column_no;    // Column number
  bool at_bol;      // True if this token is at beginning of line
  bool has_space;   // True if this token follows a space character
  Hideset *hideset; // For macro expansion
  Token *origin;    // If this is expanded from a macro, the original token
};

noreturn void error(char *fmt, ...) __attribute__((format(printf, 1, 2)));
noreturn void error_at(char *loc, char *fmt, ...) __attribute__((format(printf, 2, 3)));
noreturn void error_tok(Token *tok, char *fmt, ...) __attribute__((format(printf, 2, 3)));
void warn_tok(Token *tok, char *fmt, ...) __attribute__((format(printf, 2, 3)));
char *get_string(Token *tok);
bool equal(Token *tok, char *op);
Token *skip(Token *tok, char *op);
bool consume(Token **rest, Token *tok, char *str);
void convert_pp_tokens(Token *tok);
File **get_input_files(void);
File *new_file(char *name, int file_no, char *contents);
Token *tokenize_string_literal(Token *tok, Type *basety);
Token *tokenize(File *file);
Token *tokenize_file(char *filename);

#define unreachable() \
  error("internal error at %s:%d", __FILE__, __LINE__)

//
// preprocess.c
//

void init_macros(void);
void define_macro(char *name, char *buf);
void undef_macro(char *name);
Token *preprocess(Token *tok);

//
// parse.c
//

// Scope for local variables, global variables, typedefs
// or enum constants
struct VarScope {
  VarScope *next;
  char *name;
  Obj *var;
  Type *type_def;
  Type *enum_ty;
  int enum_val;
  Scope *scope;
};

// Scope for struct, union or enum tags
struct TagScope {
  TagScope *next;
  char *name;
  Type *ty;
  Scope *scope;
};

// Represents a block scope.
struct Scope {
  Scope *next;

  // C has two block scopes; one is for variables/typedefs and
  // the other is for struct/union/enum tags.
  VarScope *vars;
  TagScope *tags;
};

typedef enum {
  OB_GLOBAL,
  OB_LOCAL,
  OB_PARAM,
  OB_GLOBAL_TYPE,
} ObjKind;

// Variable or function
struct Obj {
  Obj *next;
  char *name;    // Variable name
  Type *ty;      // Type
  Token *tok;    // representative token
  ObjKind kind;
  Node *align;   // alignment

  // Local variable
  int offset;

  // Global variable or function
  bool is_function;
  bool is_definition;
  bool is_static;

  // Global variable
  char *init_data;
  int init_data_size;
  Node *init_expr;

  // Function
  bool is_inline;
  Obj *params;
  Node *body;
  Obj *locals;

  // Static inline function
  bool is_live;
  bool is_root;
  StringArray refs;
};

// AST node
typedef enum {
  ND_NULL_EXPR, // Do nothing
  ND_ADD,       // +
  ND_SUB,       // -
  ND_MUL,       // *
  ND_DIV,       // /
  ND_NEG,       // unary -
  ND_MOD,       // %
  ND_BITAND,    // &
  ND_BITOR,     // |
  ND_BITXOR,    // ^
  ND_SHL,       // <<
  ND_SHR,       // >>
  ND_EQ,        // ==
  ND_NE,        // !=
  ND_LT,        // <
  ND_LE,        // <=
  ND_ASSIGN,    // =
  ND_COND,      // ?:
  ND_COMMA,     // ,
  ND_MEMBER,    // . (struct member access)
  ND_ADDR,      // unary &
  ND_DEREF,     // unary *
  ND_SIZEOF,    // sizeof
  ND_NOT,       // !
  ND_BITNOT,    // ~
  ND_LOGAND,    // &&
  ND_LOGOR,     // ||
  ND_RETURN,    // "return"
  ND_IF,        // "if"
  ND_FOR,       // "for" or "while"
  ND_DO,        // "do"
  ND_SWITCH,    // "switch"
  ND_CASE,      // "case"
  ND_BLOCK,     // { ... }
  ND_GOTO,      // "goto"
  ND_LABEL,     // Labeled statement
  ND_FUNCALL,   // Function call
  ND_EXPR_STMT, // Expression statement
  ND_STMT_EXPR, // Statement expression
  ND_VAR,       // Variable
  ND_NUM,       // Integer
  ND_CAST,      // Type cast
  ND_MEMZERO,   // Zero-clear a stack variable
  ND_ASM,       // "asm"
} NodeKind;

// AST node type
struct Node {
  NodeKind kind; // Node kind

  // Numeric literal
  int64_t val;
  double fval;

  Node *next;    // Next node
  Type *ty;      // Type, e.g. int or pointer to int
  Token *tok;    // Representative token
  
  bool is_reduced;

  Node *lhs;     // Left-hand side
  Node *rhs;     // Right-hand side

  // "if" or "for" statement
  Node *cond;
  Node *then;
  Node *els;
  Node *init;
  Node *inc;

  // "break" and "continue" labels
  char *brk_label;
  char *cont_label;
  bool is_resolved_cont;

  // Block or statement expression
  Node *body;

  // Struct member access
  Member *member;

  // Function call
  Type *func_ty;
  Node *args;
  char *cil_callsite;

  // Goto or labeled statement
  char *label;
  char *unique_label;
  Node *goto_next;
  bool is_resolved_label;

  // Switch-cases
  Node *case_next;
  Node *default_case;

  // "asm" string literal
  char *asm_str;

  // Variable
  Obj *var;

  // Sizeof
  Type *sizeof_ty;
};

Node *new_node(NodeKind kind, Token *tok);
Node *new_binary(NodeKind kind, Node *lhs, Node *rhs, Token *tok);
Node *new_unary(NodeKind kind, Node *expr, Token *tok);
Node *new_num(int64_t val, Token *tok);
Node *new_flonum(double fval, Type *ty, Token *tok);
Node *new_typed_num(int64_t val, Type *ty, Token *tok);
Node *new_sizeof(Type *ty, Token *tok);
Node *new_var_node(Obj *var, Token *tok);
Node *new_cast(Node *expr, Type *ty);
int64_t const_expr(Token **rest, Token *tok);
Obj *parse(Token *tok);

//
// type.c
//

typedef enum {
  TY_VOID,
  TY_BOOL,
  TY_CHAR,
  TY_SHORT,
  TY_INT,
  TY_LONG,
  TY_NINT,
  TY_FLOAT,
  TY_DOUBLE,
  TY_ENUM,
  TY_PTR,
  TY_FUNC,
  TY_ARRAY,
  TY_STRUCT,
  TY_UNION,
  TY_VA_LIST,
} TypeKind;

struct Type {
  TypeKind kind;
  Node *size;         // sizeof() value
  Node *align;        // alignment
  Node *origin_size;
  bool is_unsigned;   // unsigned or signed
  Token *tok;
  Type *origin;       // for type compatibility check

  // Pointer-to or array-of type. We intentionally use the same member
  // to represent pointer/array duality in C.
  //
  // In many contexts in which a pointer is expected, we examine this
  // member instead of "kind" member to determine whether a type is a
  // pointer or not. That means in many contexts "array of T" is
  // naturally handled as if it were "pointer to T", as required by
  // the C spec.
  Type *base;

  // Declaration
  Token *name;
  Token *name_pos;
  Token *typedef_name;

  // Array
  int array_len;

  // Enum/Struct/Union
  TagScope *tag_scope;

  // Enum
  EnumMember *enum_members;

  // Struct
  Member *members;
  bool is_flexible;

  // Function type
  Type *return_ty;
  Type *params;
  bool is_variadic;
  Type *next;

  // CIL specific
  bool is_public;
  char *cil_name;
};

// Enum member
struct EnumMember {
  EnumMember *next;
  Token *name;
  int val;
};

// Struct member
struct Member {
  Member *next;
  Type *ty;
  Token *tok; // for error message
  Token *name;
  int idx;
  Node *align;
  Node *offset;
  Node *origin_offset;

  // Bitfield
  bool is_bitfield;
  int bit_width;
  Node *bit_offset;
};

extern Type *ty_void;
extern Type *ty_bool;

extern Type *ty_char;
extern Type *ty_short;
extern Type *ty_int;
extern Type *ty_long;

extern Type *ty_uchar;
extern Type *ty_ushort;
extern Type *ty_uint;
extern Type *ty_ulong;

extern Type *ty_nint;
extern Type *ty_nuint;

extern Type *ty_float;
extern Type *ty_double;

extern Type *ty_va_list;

typedef enum MemoryModel {
  AnyCPU,
  M32,
  M64,
} MemoryModel;

extern MemoryModel mem_model;

void init_type_system(MemoryModel mm);

bool is_integer(Type *ty);
bool is_flonum(Type *ty);
bool is_numeric(Type *ty);
bool is_compatible(Type *t1, Type *t2);
Type *copy_type(Type *ty);
Type *pointer_to(Type *base, Token *tok);
Type *func_type(Type *return_ty, Token *tok);
Type *array_of(Type *base, int size, Token *tok);
Type *enum_type(Token *tok);
Type *struct_type(Token *tok);
void add_type(Node *node);

//
// utils.c
//

int calculate_size(Type *ty);
Node *align_to_node(Node *n, Node *align);
char *to_cil_typename(Type *ty);
void pretty_print_node(Node *node);
void pretty_print_obj(Obj *obj);
void verify_type(Type *ty);
void verify_node(Node *node);
bool equals_type(Type *lhs, Type *rhs);
bool equals_node(Node *lhs, Node *rhs);
Node *reduce_node(Node *node);
int64_t get_by_integer(Node *node);

//
// unicode.c
//

int encode_utf8(char *buf, uint32_t c);
uint32_t decode_utf8(char **new_pos, char *p);
bool is_ident1(uint32_t c);
bool is_ident2(uint32_t c);
int display_width(char *p, int len);

//
// codegen.c
//

void codegen(Obj *prog, FILE *out);

//
// main.c
//

bool file_exists(char *path);

extern StringArray include_paths;
extern char *base_file;
