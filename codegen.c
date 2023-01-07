#include "chibicc.h"

// Compute the absolute address of a given node.
// It's an error if a given node does not reside in memory.
static void gen_addr(Node *node) {
  if (node->kind == ND_VAR) {
    printf("    ldloca %d\n", node->var->offset);
    return;
  }

  error("not an lvalue");
}

static void gen_expr(Node *node) {
  switch (node->kind) {
  case ND_NUM:
    printf("    ldc.i4 %d\n", node->val);
    return;
  case ND_NEG:
    gen_expr(node->lhs);
    printf("    neg\n");
    return;
  case ND_VAR:
    gen_addr(node);
    printf("    ldind.i4\n");
    return;
  case ND_ASSIGN:
    gen_addr(node->lhs);
    printf("    dup\n");
    gen_expr(node->rhs);
    printf("    stind.i4\n");
    printf("    ldind.i4\n");
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
  switch (node->kind) {
  case ND_RETURN:
    gen_expr(node->lhs);
    printf("    br _L_return\n");
    return;
  case ND_EXPR_STMT:
    gen_expr(node->lhs);
    return;
  }

  error("invalid statement");
}

// Assign offsets to local variables.
static void assign_lvar_offsets(Function *prog) {
  int offset = 0;
  for (Obj *var = prog->locals; var; var = var->next) {
    var->offset = offset;
    offset++;
  }
  prog->stack_size = offset;
}

void codegen(Function *prog) {
  assign_lvar_offsets(prog);

  printf(".assembly Test\n");
  printf("{\n");
  printf("}\n");
  printf(".class public auto ansi abstract sealed beforefieldinit Test.Program\n");
  printf("  extends [mscorlib]System.Object\n");
  printf("{\n");
  printf("  .method public hidebysig static int32 Main(string[] args) cil managed\n");
  printf("  {\n");
  printf("    .entrypoint\n");
  if (prog->stack_size >= 1) {
    printf("    .locals init (\n");
    int i;
    for (i = 0; i < prog->stack_size; i++) {
      printf("      [%d] int32,\n", i);
    }
    printf("      [%d] int32\n", i);
    printf("    )\n");
  }

  for (Node *n = prog->body; n; n = n->next) {
    gen_stmt(n);
    if (n->next) {
      printf("    pop\n");
    }
  }
  printf("_L_return:\n");
  printf("    ret\n");
  printf("  }\n");
  printf("}\n");
}
