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
| Pipe operator | `x \|> double_it() \|> add(1)` | `add(double_it(x), 1)` |
| Async/Futures | `Future<int> f(int x) { return x*2; }` | pthread spawn + join |
| Await | `int val = await f(21);` | pthread_join + extract |
| ARC | `int[] nums = [1, 2, 3];` | Ref-counted heap alloc + auto release |
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

```sh
moxy run hello.mxy
```

That's it — one command to transpile, compile, and execute.

### Commands

```
moxy run <file.mxy> [args]      transpile, compile, and execute
moxy build <file.mxy> [-o out]  transpile and compile to binary
moxy test [files...]            discover and run *_test.mxy files
moxy fmt [file.mxy] [--check]   format source (in-place or check-only)
moxy lint [file.mxy]            lint source for issues
moxy <file.mxy>                 transpile to C on stdout
```

**Flags:**

- `--enable-async` — enables `Future<T>` and `await` support (links `-lpthread`). Place before the command: `moxy --enable-async run file.mxy`
- `--enable-arc` — enables automatic reference counting for lists and maps. Heap-allocates collections with a refcount and inserts `retain`/`release` calls at scope boundaries. Place before the command: `moxy --enable-arc run file.mxy`

`run` passes extra arguments through to the compiled program. `build` produces a binary (defaults to the source filename without `.mxy`). `test` discovers `*_test.mxy` files recursively or runs specific files you pass (async tests are auto-detected and linked with pthreads; ARC tests with `arc` in the filename are auto-detected). `fmt` formats source files in-place (or checks with `--check`). `lint` checks for unused variables, empty blocks, and shadowed variables. Both `fmt` and `lint` discover `.mxy` files recursively when no file is given, and read settings from `moxyfmt.yaml` if present. All commands respect `CC` and `CFLAGS` environment variables.

### Testing

Name test files with a `_test.mxy` suffix and use `assert`:

```
// math_test.mxy
int square(int n) {
  return n * n;
}

void main() {
  assert(square(5) == 25);
  assert(square(0) == 0);
  assert(square(-3) == 9);
}
```

```sh
moxy test
```

```
  test tests/math_test.mxy ... ok (42ms)

  1 passed, 0 failed (1 total) in 0.0s
```

`assert(expr)` prints the source line number and exits with code 1 on failure.

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

### Pipe Operator

Chain function calls left-to-right with `|>`:

```
int double_it(int x) {
  return x * 2;
}

int add(int a, int b) {
  return a + b;
}

void main() {
  int result = 5 |> double_it();
  print(result);    // 10

  // chained pipes
  5 |> double_it() |> add(3) |> print()    // 13

  // bare function name (no parens) also works
  10 |> double_it |> print()    // 20
}
```

The pipe operator passes the left-hand value as the first argument to the right-hand function. `a |> f(b)` becomes `f(a, b)`. Pipes are left-associative, so `a |> f() |> g()` becomes `g(f(a))`.

### Async / Futures

Run concurrent work with `Future<T>` and `await` (requires `--enable-async`):

```
Future<int> compute(int x) {
  return x * 2;
}

void main() {
  int val = await compute(21);
  print(val);    // 42
}
```

Functions returning `Future<T>` automatically spawn a pthread. `await` joins the thread and extracts the result. Works with any type:

```
Future<string> greet(string name) {
  return name;
}

Future<void> do_work() {
  return;
}
```

```sh
moxy --enable-async run async.mxy
```

### Automatic Reference Counting (ARC)

With `--enable-arc`, lists and maps become heap-allocated, reference-counted objects. The compiler inserts `retain`/`release` calls at scope boundaries and assignments. When the refcount hits zero, the object is freed. Your Moxy source code stays the same:

```
void main() {
  int[] nums = [1, 2, 3];
  nums.push(4);
  print(nums.len);    // 4

  int[] alias = nums;  // rc=2, shared reference
  alias.push(5);
  print(nums.len);     // 5 (same object)
}
```

```sh
moxy --enable-arc run arc.mxy
```

ARC-managed types are passed as pointers and automatically retained/released when entering and leaving functions, if-blocks, loops, and match arms. Returning an ARC value transfers ownership — the returned object is not released.

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

Along with the `list_int` struct and helper functions. Pipes desugar at compile time:

```
5 |> double_it() |> add(3)
```

Becomes:

```c
add(double_it(5), 3)
```

With `--enable-arc`, the same list code generates ref-counted heap objects:

```
int[] nums = [1, 2, 3];
nums.push(4);
print(nums[0]);
```

Becomes:

```c
list_int *nums = list_int_make((int[]){1, 2, 3}, 3);  // rc=1
list_int_push(nums, 4);       // pointer, no &
printf("%d\n", nums->data[0]); // -> not .
list_int_release(nums);        // auto-inserted at scope exit
```

Async functions spawn pthreads:

```
Future<int> compute(int x) { return x * 2; }
int val = await compute(21);
```

Becomes:

```c
typedef struct { pthread_t thread; int result; int started; } Future_int;

// ... args struct, thread wrapper, launcher ...

Future_int _aw0 = compute(21);
void *_aw0_ret;
pthread_join(_aw0.thread, &_aw0_ret);
int val = *(int *)_aw0_ret;
free(_aw0_ret);
```

The output compiles with any C11 compiler.

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
  flags.h/c      — global feature flags (--enable-async, --enable-arc)
  diag.h/c       — error and warning diagnostics
  fmt.h/c        — source formatter
  lint.h/c       — static analysis linter
  yaml.h/c       — moxyfmt.yaml config parser
  main.c         — CLI entry point and preprocessor
tests/
  *_test.mxy     — test suite (types, lists, maps, enums, functions, control flow, async, arc)
tools/
  asdf/          — asdf version manager plugin
  editors/zed/   — Zed editor extension
examples/
  features.mxy   — comprehensive feature showcase
  async.mxy      — async/futures example (requires --enable-async)
  arc.mxy        — ARC example (requires --enable-arc)
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
