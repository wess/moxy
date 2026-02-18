# Moxy Transpiler API Reference

Internal C API documentation for each module in the Moxy transpiler. These are the public interfaces exposed by each header file. All modules live in `src/`.

## token.h

Defines the token types and token struct produced by the lexer.

### TokenKind

Enum of all token types recognized by the lexer.

**Keywords** (25 tokens):

| Token | Keyword |
|-------|---------|
| `TOK_INT_KW` | `int` |
| `TOK_FLOAT_KW` | `float` |
| `TOK_DOUBLE_KW` | `double` |
| `TOK_CHAR_KW` | `char` |
| `TOK_BOOL_KW` | `bool` |
| `TOK_LONG_KW` | `long` |
| `TOK_SHORT_KW` | `short` |
| `TOK_STRING_KW` | `string` |
| `TOK_VOID_KW` | `void` |
| `TOK_ENUM_KW` | `enum` |
| `TOK_MATCH_KW` | `match` |
| `TOK_IF_KW` | `if` |
| `TOK_ELSE_KW` | `else` |
| `TOK_FOR_KW` | `for` |
| `TOK_WHILE_KW` | `while` |
| `TOK_RETURN_KW` | `return` |
| `TOK_RESULT_KW` | `Result` |
| `TOK_MAP_KW` | `map` |
| `TOK_OK_KW` | `Ok` |
| `TOK_ERR_KW` | `Err` |
| `TOK_TRUE_KW` | `true` |
| `TOK_FALSE_KW` | `false` |
| `TOK_NULL_KW` | `null` |
| `TOK_FUTURE_KW` | `Future` |
| `TOK_AWAIT_KW` | `await` |

**Literals** (5 tokens):

| Token | Description |
|-------|-------------|
| `TOK_IDENT` | Identifier (variable/function names) |
| `TOK_INTLIT` | Integer literal |
| `TOK_FLOATLIT` | Floating-point literal |
| `TOK_STRLIT` | String literal (content without quotes) |
| `TOK_CHARLIT` | Character literal (content without quotes) |

**Delimiters** (6 tokens):

`TOK_LBRACE`, `TOK_RBRACE`, `TOK_LPAREN`, `TOK_RPAREN`, `TOK_LBRACKET`, `TOK_RBRACKET`

**Operators** (20 tokens):

| Token | Symbol | Token | Symbol |
|-------|--------|-------|--------|
| `TOK_PLUS` | `+` | `TOK_MINUS` | `-` |
| `TOK_STAR` | `*` | `TOK_SLASH` | `/` |
| `TOK_PERCENT` | `%` | `TOK_BANG` | `!` |
| `TOK_EQEQ` | `==` | `TOK_NEQ` | `!=` |
| `TOK_LT` | `<` | `TOK_GT` | `>` |
| `TOK_LTEQ` | `<=` | `TOK_GTEQ` | `>=` |
| `TOK_AND` | `&&` | `TOK_OR` | `\|\|` |
| `TOK_PLUSEQ` | `+=` | `TOK_MINUSEQ` | `-=` |
| `TOK_STAREQ` | `*=` | `TOK_SLASHEQ` | `/=` |
| `TOK_PLUSPLUS` | `++` | `TOK_MINUSMINUS` | `--` |

**Punctuation and special** (5 tokens):

| Token | Symbol |
|-------|--------|
| `TOK_DOT` | `.` |
| `TOK_COMMA` | `,` |
| `TOK_SEMI` | `;` |
| `TOK_EQ` | `=` |
| `TOK_COLONCOLON` | `::` |
| `TOK_FATARROW` | `=>` |
| `TOK_EOF` | End of file |

### Token

```c
typedef struct {
    TokenKind kind;
    char text[256];
    int line;
    int col;
} Token;
```

| Field | Description |
|-------|-------------|
| `kind` | The type of token |
| `text` | Raw text content (identifier name, literal value, keyword text) |
| `line` | 1-based line number in source |
| `col` | 1-based column number in source |

---

## lexer.h

Tokenizer that converts source text into a flat token array.

### Lexer

```c
typedef struct {
    const char *src;
    int pos;
    int line;
    int col;
} Lexer;
```

State for the tokenizer. Tracks position in the source string and current line/column for error reporting.

### lexer_init

```c
void lexer_init(Lexer *l, const char *src);
```

Initialize a lexer with a null-terminated source string. Sets position to the start and line/col to 1.

### lexer_next

```c
Token lexer_next(Lexer *l);
```

Consume and return the next token from the source. Skips whitespace and comments (`//` line comments and `/* */` block comments). Returns `TOK_EOF` at end of input.

**Usage:**

```c
Lexer lexer;
lexer_init(&lexer, source);

Token tokens[4096];
int n = 0;
for (;;) {
    tokens[n] = lexer_next(&lexer);
    if (tokens[n].kind == TOK_EOF) { n++; break; }
    n++;
}
```

---

## ast.h

AST node definitions using a tagged union pattern. Every node is heap-allocated.

### NodeKind

Enum of all AST node types (34 kinds):

**Declarations:**

| Kind | Description |
|------|-------------|
| `NODE_PROGRAM` | Top-level program container |
| `NODE_VAR_DECL` | Variable declaration with type, name, and initializer |
| `NODE_ENUM_DECL` | Enum type definition with variants |
| `NODE_FUNC_DECL` | Function definition with params and body |

**Statements:**

| Kind | Description |
|------|-------------|
| `NODE_PRINT_STMT` | `print(expr)` statement |
| `NODE_MATCH_STMT` | `match` with arms |
| `NODE_EXPR_STMT` | Expression used as statement |
| `NODE_IF_STMT` | `if`/`else if`/`else` chain |
| `NODE_WHILE_STMT` | `while` loop |
| `NODE_FOR_STMT` | `for` loop with init/cond/step |
| `NODE_RETURN_STMT` | `return` with optional value |
| `NODE_BLOCK` | Block of statements (used in `if`/`match` bodies) |
| `NODE_ASSIGN` | Assignment with operator (`=`, `+=`, etc.) |

**Expressions:**

| Kind | Description |
|------|-------------|
| `NODE_EXPR_IDENT` | Variable reference |
| `NODE_EXPR_INTLIT` | Integer literal |
| `NODE_EXPR_FLOATLIT` | Float literal |
| `NODE_EXPR_STRLIT` | String literal |
| `NODE_EXPR_CHARLIT` | Character literal |
| `NODE_EXPR_BOOLLIT` | Boolean literal |
| `NODE_EXPR_NULL` | `null` literal |
| `NODE_EXPR_ENUM_INIT` | Enum variant constructor (`Shape::Circle(3.14)`) |
| `NODE_EXPR_LIST_LIT` | List literal (`[1, 2, 3]`) |
| `NODE_EXPR_OK` | `Ok(value)` expression |
| `NODE_EXPR_ERR` | `Err(value)` expression |
| `NODE_EXPR_METHOD` | Method call (`target.name(args)`) |
| `NODE_EXPR_FIELD` | Field access (`target.name`) |
| `NODE_EXPR_INDEX` | Index access (`target[idx]`) |
| `NODE_EXPR_EMPTY` | Empty initializer (`{}` for maps) |
| `NODE_EXPR_CALL` | Function call (`name(args)`) |
| `NODE_EXPR_BINOP` | Binary operation (`left op right`) |
| `NODE_EXPR_UNARY` | Unary operation (`op operand` or `operand op`) |
| `NODE_EXPR_PAREN` | Parenthesized expression |
| `NODE_EXPR_AWAIT` | `await` expression (unwraps `Future<T>`) |

### Supporting types

```c
typedef struct { char name[64]; char type[64]; } Field;
typedef struct { char name[64]; Field fields[8]; int nfields; } Variant;
typedef struct { char enum_name[64]; char variant[64]; char binding[64]; } Pattern;
typedef struct { Pattern pattern; Node *body; } MatchArm;
typedef struct { char type[64]; char name[64]; } Param;
```

| Type | Purpose |
|------|---------|
| `Field` | Named typed field in an enum variant |
| `Variant` | Enum variant with up to 8 fields |
| `Pattern` | Match arm pattern binding an enum variant |
| `MatchArm` | Pattern + body pair in a match statement |
| `Param` | Function parameter with type and name |

### Node

Tagged union struct. The `kind` field determines which union member is active. See `ast.h` for the full union definition. Key capacity limits:

| Container | Limit |
|-----------|-------|
| Program declarations | 64 |
| Function body statements | 256 |
| Function parameters | 16 |
| Enum variants | 16 |
| Variant fields | 8 |
| Match arms | 16 |
| List literal items | 64 |
| Call/method arguments | 8-16 |

### node_new

```c
Node *node_new(NodeKind kind);
```

Allocate and zero-initialize a new AST node with the given kind. Uses `calloc` internally.

---

## parser.h

Recursive descent parser that converts a token array into an AST.

### parse

```c
Node *parse(Token *tokens, int ntokens);
```

Parse a flat array of tokens (ending with `TOK_EOF`) into a `NODE_PROGRAM` AST node. The program node contains top-level declarations: enum definitions, function definitions, and global variable declarations.

**Expression parsing** uses precedence climbing (Pratt-style) with six levels, lowest to highest:

1. `||`
2. `&&`
3. `==`, `!=`
4. `<`, `>`, `<=`, `>=`
5. `+`, `-`
6. `*`, `/`, `%`

Postfix operations (`.field`, `.method()`, `[index]`, `++`, `--`) bind tighter than all binary operators.

**Type parsing** handles: simple types (`int`, `string`, etc.), list types (`T[]`), result types (`Result<T>`), future types (`Future<T>`, gated behind `--enable-async`), and map types (`map[K,V]`). Uses backtracking to distinguish variable declarations from expression statements.

**Await parsing**: `await` is parsed as a prefix expression that captures the next postfix expression. Gated behind `--enable-async`.

---

## codegen.h

C code generator that converts an AST into a C source string.

### codegen

```c
const char *codegen(Node *program);
```

Generate a complete C source file from a `NODE_PROGRAM` AST. Returns a pointer to a static internal buffer (128 KB max). The output includes all necessary `#include` directives, type definitions, forward declarations, and function bodies.

**Emission order:**

1. User-specified `#include` directives (added via `codegen_add_include`)
2. Auto-generated includes (`stdio.h`, `stdbool.h`, plus `stdlib.h`/`string.h` if generics are used, `pthread.h` if futures are used)
3. Enum type definitions
4. Monomorphized generic type definitions and helper functions (lists, results, maps, futures)
5. Forward declarations for user-defined functions
6. Global variable definitions
7. Function definitions

Auto-generated includes are deduplicated against user-specified includes.

### codegen_add_include

```c
void codegen_add_include(const char *line);
```

Register a raw C `#include` directive to be emitted at the top of the generated output. The `line` should be a complete directive string like `#include <stdlib.h>` or `#include "mylib.h"`. Duplicate lines are ignored. Up to 64 user includes are supported.

Called by the preprocessor in `main.c` when a non-`.mxy` include is encountered.

---

## flags.h

Global feature flags shared across the transpiler pipeline.

### moxy_async_enabled

```c
extern int moxy_async_enabled;
```

When set to `1`, enables `Future<T>` type parsing, `await` expression parsing, and async code generation. Set by `main.c` when `--enable-async` is passed on the command line. Default is `0`.

---

## main.c

CLI entry point and source preprocessor. Not exposed via a header.

### Pipeline

```
read_file(path) → preprocess(src, path) → lexer → parser → codegen → stdout
```

### Preprocessor

The `preprocess` function (static in `main.c`) scans source text line-by-line before lexing:

- `#include "file.mxy"` — reads the file relative to the source file's directory, recursively preprocesses it, and splices the contents inline
- `#include "header.h"` — passes the directive to `codegen_add_include()` for C output
- `#include <header.h>` — same as above, angle-bracket form
- All other lines pass through unchanged

### CLI usage

```
moxy [--enable-async] <file.mxy>
moxy [--enable-async] run <file.mxy> [args]
moxy [--enable-async] build <file.mxy> [-o out]
moxy test [files...]
moxy fmt [file.mxy] [--check]
moxy lint [file.mxy]
```

Reads the `.mxy` file, preprocesses it, lexes, parses, generates C, and prints the C source to stdout. The `run` command also compiles and executes; `build` compiles to a binary. The `--enable-async` flag sets `moxy_async_enabled` and appends `-lpthread` to the compiler flags. The `test` command auto-detects async test files (containing `Future<` or `await `) and links pthreads automatically.
