#include "chibicc.h"

static void gen_expr(Node *node) {
  switch (node->kind) {
  case ND_NUM:
    printf("    ldc.i4 %d\n", node->val);
    return;
  case ND_NEG:
    gen_expr(node->lhs);
    printf("    neg\n");
    return;
  }

  gen_expr(node->lhs);
  gen_expr(node->rhs);

  switch (node->kind) {
  case ND_ADD:
    printf("    add\n");
    return;
  case ND_SUB:
    printf("    sub\n");
    return;
  case ND_MUL:
    printf("    mul\n");
    return;
  case ND_DIV:
    printf("    div\n");
    return;
  case ND_EQ:
    printf("    ceq\n");
    return;
  case ND_NE:
    printf("    ceq\n");
    printf("    ldc.i4.0\n");
    printf("    ceq\n");
    return;
  case ND_LT:
    printf("    clt\n");
    return;
  case ND_LE:
    printf("    cgt\n");
    printf("    ldc.i4.0\n");
    printf("    ceq\n");
    return;
  }

  error("invalid expression");
}

static void gen_stmt(Node *node) {
  if (node->kind == ND_EXPR_STMT) {
    gen_expr(node->lhs);
    return;
  }

  error("invalid statement");
}

void codegen(Node *node) {
  printf(".assembly Test\n");
  printf("{\n");
  printf("}\n");
  printf(".class public auto ansi abstract sealed beforefieldinit Test.Program\n");
  printf("  extends [mscorlib]System.Object\n");
  printf("{\n");
  printf("  .method public hidebysig static int32 Main(string[] args) cil managed\n");
  printf("  {\n");
  printf("    .entrypoint\n");

  for (Node *n = node; n; n = n->next) {
    gen_stmt(n);
    if (n->next) {
      printf("    pop\n");
    }
  }
  printf("    ret\n");
  printf("  }\n");
  printf("}\n");
}
