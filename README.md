# Moxy

Moxy is a lightweight superset of C — it's C with surgical improvements that eliminate boilerplate while producing clean, readable C output. Write modern, expressive code that transpiles to portable C11.

## Why Moxy?

C is fast, portable, and everywhere. But it's also verbose. You need `#include <stdio.h>` to print, `printf("%d\n", x)` to format, manual tagged unions to handle variants, and hand-rolled dynamic arrays for anything beyond stack allocation.

Moxy keeps everything good about C — same types, same control flow, same mental model — and removes the ceremony. The generated C is clean enough to read, debug, and maintain by hand if needed.

## What Moxy Adds to C

| Feature | Moxy | C Equivalent |
|---------|------|--------------|
| String type | `string name = "moxy";` | `const char* name = "moxy";` |
| Boolean type | `bool ok = true;` | `#include <stdbool.h>` + `bool ok = true;` |
| Auto-format print | `print(x);` | `printf("%d\n", x);` |
| Tagged enums | `enum Shape { Circle(float r) }` | Manual tag enum + union struct |
| Scoped construction | `Shape::Circle(3.14)` | Compound literal |
| Pattern matching | `match s { Shape::Circle(r) => ... }` | `switch` + tag + field access |
| Error handling | `Result<int> r = Ok(42);` | Manual result struct |
| Dynamic arrays | `int[] nums = [1, 2, 3];` | Manual malloc + realloc |
| Hash maps | `map[string,int] m = {};` | Manual implementation |
| File includes | `#include "math.mxy"` | N/A (textual inlining) |

Everything else is standard C: `if`/`else`, `for`, `while`, `return`, all arithmetic/comparison/logical operators, functions with typed parameters, recursion, global variables, and comments.

## Installation

### Quick install (macOS / Linux)

```sh
curl -fsSL https://github.com/wess/moxy/releases/latest/download/install.sh | sh
```

Or set a custom install directory:

```sh
INSTALL_DIR=~/.local/bin curl -fsSL https://github.com/wess/moxy/releases/latest/download/install.sh | sh
```

### Debian / Ubuntu

```sh
curl -fsSLO https://github.com/wess/moxy/releases/latest/download/moxy_VERSION_amd64.deb
sudo dpkg -i moxy_*.deb
```

### Fedora / RHEL

```sh
curl -fsSLO https://github.com/wess/moxy/releases/latest/download/moxy-VERSION-1.x86_64.rpm
sudo rpm -i moxy-*.rpm
```

### Arch Linux

A `PKGBUILD` is included in each release for use with `makepkg`:

```sh
curl -fsSLO https://github.com/wess/moxy/releases/latest/download/PKGBUILD
makepkg -si
```

### asdf

```sh
asdf plugin add moxy https://github.com/wess/moxy.git tools/asdf
asdf install moxy latest
asdf set moxy latest
```

### From source

Requires a C compiler and [goose](https://github.com/wess/goose):

```sh
git clone https://github.com/wess/moxy.git
cd moxy
goose build
```

The binary is at `./build/debug/moxy`.

### Platform support

| Platform | Architecture | Format |
|----------|-------------|--------|
| macOS | Intel (amd64) | tar.gz |
| macOS | Apple Silicon (arm64) | tar.gz |
| Linux | amd64 | tar.gz |
| Linux | arm64 | tar.gz |
| Linux | amd64 (static/musl) | tar.gz |
| Debian/Ubuntu | amd64, arm64 | .deb |
| Fedora/RHEL | x86_64 | .rpm |
| Arch Linux | x86_64, aarch64 | PKGBUILD |

All release assets include SHA-256 checksums in `checksums.txt`.

## Quick Start

### Transpile and run

```sh
moxy examples/features.mxy > out.c
cc -std=c11 -o out out.c
./out
```

## Examples

### Hello World

```
void main() {
  print("hello, world!");
}
```

### Variables and Types

```
void main() {
  int age = 30;
  float height = 5.11;
  string name = "moxy";
  bool active = true;
  char initial = 'M';
  long population = 8000000;
  double pi = 3.14159265;
  short temp = 72;

  print(name);
  print(age);
  print(height);
  print(active);
}
```

`print()` automatically picks the right format string for each type. No `printf` format specifiers needed.

### Functions

```
int add(int a, int b) {
  return a + b;
}

int factorial(int n) {
  if (n <= 1) {
    return 1;
  }
  return n * factorial(n - 1);
}

void main() {
  print(add(3, 4));       // 7
  print(factorial(5));     // 120
}
```

Functions can call each other in any order — forward declarations are generated automatically.

### Control Flow

```
void main() {
  int x = 15;

  // if / else if / else
  if (x > 20) {
    print("big");
  } else if (x > 10) {
    print("medium");
  } else {
    print("small");
  }

  // for loops
  for (int i = 0; i < 5; i++) {
    print(i);
  }

  // for with custom step
  for (int k = 0; k < 10; k += 3) {
    print(k);
  }

  // while loops
  int countdown = 3;
  while (countdown > 0) {
    print(countdown);
    countdown--;
  }

  // assignment operators
  int val = 10;
  val += 5;
  val -= 3;
  val *= 2;
  val /= 6;
}
```

### Enums and Pattern Matching

```
// Simple enum
enum Color {
  Red,
  Green,
  Blue
}

// Tagged enum (algebraic data type)
enum Shape {
  Circle(float radius),
  Rect(int w, int h),
  None
}

void main() {
  Shape s = Shape::Circle(3.14);

  match s {
    Shape::Circle(r) => print(r),
    Shape::Rect(w) => print(w),
    Shape::None => print("nothing"),
  }

  Color c = Color::Red;

  match c {
    Color::Red => print("red"),
    Color::Green => print("green"),
    Color::Blue => print("blue"),
  }
}
```

Tagged enum variants can carry typed fields. The `match` binding captures the first field of each variant.

### Dynamic Arrays

```
void main() {
  int[] nums = [10, 20, 30];
  nums.push(40);

  print(nums.len);    // 4
  print(nums[0]);     // 10
  print(nums[3]);     // 40

  string[] words = ["hello", "world"];
  words.push("moxy");
  print(words[2]);    // moxy
  print(words.len);   // 3
}
```

Lists support `push`, `len`, and index access. They grow automatically.

### Result Type

```
void main() {
  Result<int> ok = Ok(42);
  Result<int> fail = Err("not found");

  match ok {
    Ok(v) => print(v),
    Err(e) => print(e),
  }

  match fail {
    Ok(v) => print(v),
    Err(e) => print(e),
  }
}
```

Built-in error handling without exceptions. `Result<T>` wraps a success value of type `T` or an error string.

### Hash Maps

```
void main() {
  map[string,int] ages = {};

  ages.set("alice", 30);
  ages.set("bob", 25);

  print(ages.get("alice"));   // 30
  print(ages.has("bob"));     // 1 (true)
  print(ages.len);            // 2
}
```

Maps support `set`, `get`, `has`, and `len`. String keys use `strcmp`, numeric keys use `==`.

### Includes

Split code across multiple files:

```
// math.mxy
int square(int x) {
  return x * x;
}
```

```
// main.mxy
#include "math.mxy"
#include <stdlib.h>

void main() {
  print(square(5));    // 25
}
```

- `#include "file.mxy"` — textually inlines the `.mxy` file before compilation (recursive)
- `#include <header.h>` — passes through to C output
- `#include "header.h"` — passes through to C output

Duplicate C headers are automatically deduplicated against auto-generated ones.

### Global Variables

```
int max_retries = 3;
string version = "1.0";

void main() {
  print(version);
  print(max_retries);
}
```

### Comments

```
// line comment

/* block
   comment */
```

## What the Output Looks Like

Moxy generates clean, readable C. For example:

```
int[] nums = [1, 2, 3];
nums.push(4);
print(nums[0]);
```

Becomes:

```c
list_int nums = list_int_make((int[]){1, 2, 3}, 3);
list_int_push(&nums, 4);
printf("%d\n", nums.data[0]);
```

Along with the `list_int` struct and helper functions. The output compiles with any C11 compiler.

## Documentation

| Document | Description |
|----------|-------------|
| [Language Reference](docs/book/reference.md) | Complete type, syntax, and operator reference |
| [Tutorial](docs/tutorial/getting_started.md) | Step-by-step introduction to every feature |
| [Architecture](docs/book/architecture.md) | How the transpiler pipeline works |
| [API Reference](docs/api/index.md) | Internal C API for each transpiler module |
| [Examples](examples/) | `.mxy` source files demonstrating all features |

## Editor Support

### Zed

Copy the extension into your Zed extensions directory:

```sh
cp -r tools/editors/zed ~/.config/zed/extensions/moxy
```

Provides syntax highlighting, auto-indent, bracket matching, and comment toggling for `.mxy` files.

## Project Structure

```
src/
  token.h        — token kinds
  lexer.h/c      — tokenizer
  ast.h/c        — AST node definitions
  parser.h/c     — recursive descent parser
  codegen.h/c    — C code generator with monomorphization
  main.c         — CLI entry point and preprocessor
tools/
  asdf/          — asdf version manager plugin
  editors/zed/   — Zed editor extension
examples/
  features.mxy   — comprehensive feature showcase
  math.mxy       — helper functions (included by features.mxy)
docs/
  book/          — language reference and architecture
  tutorial/      — getting started guide
  api/           — transpiler API reference
```

## How It Works

Moxy is a five-stage transpiler written in ~2,300 lines of C:

```
.mxy source → Preprocessor → Lexer → Parser → AST → Codegen → .c output
```

1. **Preprocessor** scans for `#include` directives, inlining `.mxy` files and collecting C headers
2. **Lexer** tokenizes the source into a flat token array
3. **Parser** builds an AST using recursive descent with Pratt-style expression parsing
4. **Codegen** walks the AST in two passes — first collecting generic type instantiations, then emitting C with monomorphized type definitions, forward declarations, and function bodies

See [Architecture](docs/book/architecture.md) for the full technical breakdown.

## License

MIT
