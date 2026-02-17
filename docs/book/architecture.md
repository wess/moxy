# Moxy Architecture

Moxy is a source-to-source transpiler with five stages:

```
.mxy source → Preprocessor → Lexer → Parser → AST → Codegen → .c output
```

## Pipeline

### 0. Preprocessor (`main.c`)

Before any lexing occurs, the source text is scanned line-by-line for `#include` directives. This is a textual operation — no tokens or AST are involved.

**Moxy includes** (`#include "file.mxy"`) read the referenced file relative to the including file's directory, recursively preprocess it, and splice the contents inline. This works like C's `#include` — the included code becomes part of the source as if it were written directly in the file.

**C header includes** (`#include <stdio.h>` or `#include "mylib.h"`) are extracted from the source and registered with the code generator via `codegen_add_include()`. They are not passed to the lexer.

All other lines pass through unchanged. The result is a single preprocessed string that the lexer receives.

### 1. Lexer (`lexer.c`)

Converts preprocessed source text into a flat array of tokens. Handles:

- Keywords: `int`, `float`, `string`, `bool`, `enum`, `match`, `if`, `else`, `for`, `while`, `return`, `Result`, `map`, `Ok`, `Err`, `true`, `false`, `null`, and type keywords (`double`, `char`, `long`, `short`, `void`)
- Literals: integers, floats, strings, chars
- Operators: arithmetic, comparison, logical, assignment, increment/decrement
- Punctuation: braces, parens, brackets, dots, commas, semicolons
- Two-char tokens: `::`, `=>`, `==`, `!=`, `<=`, `>=`, `&&`, `||`, `+=`, `-=`, `*=`, `/=`, `++`, `--`
- Comments: `//` line and `/* */` block (skipped as whitespace)

The lexer produces tokens into a fixed-size array of 4096 tokens, terminated by `TOK_EOF`.

### 2. Parser (`parser.c`)

Recursive descent parser that builds an AST from the token array. Key design decisions:

**Expression parsing** uses precedence climbing (Pratt-style). Precedence levels:

1. `||` (lowest)
2. `&&`
3. `==`, `!=`
4. `<`, `>`, `<=`, `>=`
5. `+`, `-`
6. `*`, `/`, `%` (highest)

Postfix operations (`.field`, `.method()`, `[index]`, `++/--`) bind tighter than binary operators.

**Type parsing** handles compound types: `Result<T>`, `map[K,V]`, `T[]`, and simple type keywords. Uses backtracking to distinguish variable declarations from expressions (both can start with an identifier).

**Statement parsing** tries in order: `print`, `match`, `if`, `while`, `for`, `return`, variable declaration, then falls back to expression/assignment.

**Top-level parsing** accepts three kinds of declarations: enum definitions, function definitions, and global variable declarations. Forward declarations for functions are generated automatically by codegen.

### 3. AST (`ast.h`)

Tagged union nodes. Every node is heap-allocated via `node_new()`. Key node kinds:

- **Declarations**: `NODE_PROGRAM`, `NODE_VAR_DECL`, `NODE_ENUM_DECL`, `NODE_FUNC_DECL`
- **Statements**: `NODE_PRINT_STMT`, `NODE_MATCH_STMT`, `NODE_IF_STMT`, `NODE_WHILE_STMT`, `NODE_FOR_STMT`, `NODE_RETURN_STMT`, `NODE_ASSIGN`, `NODE_EXPR_STMT`, `NODE_BLOCK`
- **Expressions**: literals, identifiers, binary ops, unary ops, function calls, method calls, field access, index access, enum constructors, Ok/Err, list literals, parenthesized expressions, empty initializers

### 4. Codegen (`codegen.c`)

Two-pass code generation:

**Pass 1: Type collection** — walks the AST to discover all generic type instantiations (`int[]`, `Result<int>`, `map[string,int]`, etc.).

**Pass 2: Emission** — generates C in this order:

1. User-specified `#include` directives (deduplicated against auto-generated ones)
2. Auto-generated `#include` directives (`stdio.h`, `stdbool.h`, and `stdlib.h`/`string.h` if needed)
3. User-defined enum type definitions
4. Monomorphized generic types (list, Result, map structs + helper functions)
5. Forward declarations for user-defined functions
6. Global variables
7. Function definitions

**Type monomorphization**: Generic types stamp out specialized C code:

| Moxy type | C type | Generated helpers |
|-----------|--------|-------------------|
| `int[]` | `list_int` | `list_int_make()`, `list_int_push()` |
| `Result<int>` | `Result_int` | Tag enum + tagged struct |
| `map[string,int]` | `map_string_int` | `_make()`, `_set()`, `_get()`, `_has()` |

**Type inference**: A symbol table (`Sym syms[256]`) tracks variable names to Moxy types. This enables:

- Correct `printf` format specifiers for `print()`
- Method call translation (`nums.push(4)` → `list_int_push(&nums, 4)`)
- Field/index type resolution for nested expressions

**Include deduplication**: When the user specifies `#include <stdlib.h>` via source-level `#include` and the codegen would also auto-generate it (because lists or maps are used), only one copy is emitted.

## Build System

Moxy is built with [goose](https://github.com/wess/goose), a Cargo-inspired C build tool. The `goose.yaml` config:

```yaml
project:
  name: "moxy"
  version: "0.1.0"
build:
  cc: "cc"
  cflags: "-Wall -Wextra -std=c11"
  includes:
    - "src"
```

Goose auto-discovers `.c` files in `src/`, resolves headers, and compiles everything. Output goes to `build/debug/moxy`.

## File Map

| File | Lines | Purpose |
|------|-------|---------|
| `token.h` | ~80 | Token kind enum and Token struct |
| `lexer.h` | ~16 | Lexer state and API |
| `lexer.c` | ~187 | Tokenizer with comment support |
| `ast.h` | ~106 | AST node definitions (tagged union) |
| `ast.c` | ~9 | `node_new()` allocator |
| `parser.h` | ~9 | Parser API |
| `parser.c` | ~721 | Recursive descent parser |
| `codegen.h` | ~9 | Codegen API |
| `codegen.c` | ~965 | C code generator with monomorphization |
| `main.c` | ~171 | CLI entry point and source preprocessor |

Total: ~2,272 lines of C.
