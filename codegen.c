#include "chibicc.h"

static Function *current_prog;
static Function *current_fn;

static const char *to_typename(Type *ty) {
  return "int32";
}

static void gen_expr(Node *node);

static int count(void) {
  static int i = 1;
  return i++;
}

// Compute the absolute address of a given node.
// It's an error if a given node does not reside in memory.
static void gen_addr(Node *node) {
  switch (node->kind) {
  case ND_VAR:
    printf("    ldloca %d\n", node->var->offset);
    return;
  case ND_DEREF:
    gen_expr(node->lhs);
    return;
  }

  error_tok(node->tok, "not an lvalue");
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
  case ND_DEREF:
    gen_expr(node->lhs);
    printf("    ldind.i4\n");
    return;
  case ND_ADDR:
    gen_addr(node->lhs);
    return;
  case ND_ASSIGN:
    gen_addr(node->lhs);
    printf("    dup\n");
    gen_expr(node->rhs);
    printf("    stind.i4\n");
    printf("    ldind.i4\n");
    return;
  case ND_FUNCALL:
    for (Node *arg = node->args; arg; arg = arg->next) {
      gen_expr(arg);
    }
    Function *fn;
    for (fn = current_prog; fn; fn = fn->next) {
      if (strcmp(fn->name, node->funcname) == 0)
        break;
    }
    if (fn)
      printf("    call int32 [Test]C::_%s(", node->funcname);
    else
      printf("    call int32 [tmp2]C::_%s(", node->funcname);
    for (Node *arg = node->args; arg; arg = arg->next) {
      if (arg->next) {
        printf("%s,", to_typename(arg->ty));
      } else {
        printf("%s", to_typename(arg->ty));
      }
    }
    printf(")\n");
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

  error_tok(node->tok, "invalid expression");
}

static void gen_stmt(Node *node) {
  switch (node->kind) {
  case ND_IF: {
    int c = count();
    gen_expr(node->cond);
    printf("    brfalse _L_else_%d\n", c);
    gen_stmt(node->then);
    printf("    pop\n");
    printf("    br _L_end_%d\n", c);
    printf("_L_else_%d:\n", c);
    if (node->els) {
      gen_stmt(node->els);
      printf("    pop\n");
    }
    printf("_L_end_%d:\n", c);
    return;
  }
  case ND_FOR: {
    int c = count();
    if (node->init)
      gen_stmt(node->init);
    printf("_L_begin_%d:\n", c);
    if (node->cond) {
      gen_expr(node->cond);
      printf("    brfalse _L_end_%d\n", c);
    }
    gen_stmt(node->then);
    if (node->inc) {
      gen_expr(node->inc);
      printf("    pop\n");
    }
    printf("    br _L_begin_%d\n", c);
    printf("_L_end_%d:\n", c);
    return;
  }
  case ND_BLOCK:
    for (Node *n = node->body; n; n = n->next)
      gen_stmt(n);
    return;
  case ND_RETURN:
    gen_expr(node->lhs);
    printf("    br _L_return\n");
    return;
  case ND_EXPR_STMT:
    gen_expr(node->lhs);
    printf("    pop\n");
    return;
  }

  error_tok(node->tok, "invalid statement");
}

// Assign offsets to local variables.
static void assign_lvar_offsets(Function *prog) {
  for (Function *fn = prog; fn; fn = fn->next) {
    int offset = 0;
    for (Obj *var = fn->locals; var; var = var->next) {
      var->offset = offset;
      offset++;
    }
    fn->stack_size = offset;
  }
}

void codegen(Function *prog) {
  current_prog = prog;

  assign_lvar_offsets(prog);

  printf(".assembly Test\n");
  printf("{\n");
  printf("}\n");

  printf(".class public auto ansi abstract sealed beforefieldinit C\n");
  printf("  extends [mscorlib]System.Object\n");
  printf("{\n");

  for (Function *fn = prog; fn; fn = fn->next) {
    printf("  .method public hidebysig static int32 _%s() cil managed\n", fn->name);
    printf("  {\n");
    if (strcmp(fn->name, "main") == 0)
      printf("    .entrypoint\n");
    printf("    .maxstack 10\n");
    if (fn->stack_size >= 1) {
      printf("    .locals init (\n");
      int i;
      for (i = 0; i < fn->stack_size; i++) {
        printf("      [%d] int32,\n", i);
      }
      printf("      [%d] int32\n", i);
      printf("    )\n");
    }

    gen_stmt(fn->body);

    printf("_L_return:\n");
    printf("    ret\n");
    printf("  }\n");
  }

  printf("}\n");
}
