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
| Range iteration | `for i in 0..10 { ... }` | `for (int i = 0; i < 10; i++)` |
| Collection iteration | `for x in list { ... }` | Manual index loop |
| Pipe operator | `x \|> double_it() \|> add(1)` | `add(double_it(x), 1)` |
| Lambdas | `(int x) => x * 2` | Function pointer + separate definition |
| Async/Futures | `Future<int> f(int x) { return x*2; }` | pthread spawn + join |
| Await | `int val = await f(21);` | pthread_join + extract |
| ARC | `int[] nums = [1, 2, 3];` | Ref-counted heap alloc + auto release |
| Standard library | `#include "std/math.mxy"` | N/A |
| File includes | `#include "math.mxy"` | N/A (textual inlining) |

Everything else is standard C: `if`/`else`, `for`, `while`, `return`, structs, unions, typedefs, pointers, `switch`/`case`, `do`/`while`, `goto`, `sizeof`, all arithmetic/comparison/logical/bitwise operators, functions with typed parameters, recursion, global variables, and comments.

## Installation

### Quick install (macOS / Linux)

```sh
curl -fsSL https://github.com/wess/moxy/releases/latest/download/install.sh | sh
```

Or set a custom install directory:

```sh
INSTALL_DIR=~/.local/bin curl -fsSL https://github.com/wess/moxy/releases/latest/download/install.sh | sh
```

### Homebrew (macOS)

```sh
brew tap wess/moxy https://github.com/wess/moxy
brew install moxy
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
asdf plugin add moxy https://github.com/wess/moxy.git
asdf install moxy latest
asdf set --home moxy latest
```

### From source

Requires a C compiler and [goose](https://github.com/wess/goose):

```sh
git clone --recursive https://github.com/wess/moxy.git
cd moxy
goose build
```

The binary is at `./build/debug/moxy`.

### Platform support

| Platform | Architecture | Format |
|----------|-------------|--------|
| macOS | Intel (amd64) | tar.gz |
| macOS | Apple Silicon (arm64) | tar.gz |
| macOS | Intel + Apple Silicon | Homebrew |
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
transpile:
  <file.mxy>                 transpile to C on stdout
  run <file.mxy> [args]      transpile, compile, and execute
  build <file.mxy> [-o out]  transpile and compile to binary
  test [files...]            discover and run *_test.mxy files

project:
  new <name>                 create new project
  init                       initialize project in current directory
  build [--release] [-p member]  build project or workspace member
  run [--release] [-p member]    build and run project or member
  clean                      remove build directory
  install [--prefix PATH]    release build and install

packages:
  add <git-url> [opts]       add dependency (--name, --version)
  remove <name>              remove dependency
  update                     update all dependencies

tools:
  fmt [file.mxy] [--check]   format source files
  lint [file.mxy]            lint source files for issues
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

  // while loops
  int countdown = 3;
  while (countdown > 0) {
    print(countdown);
    countdown--;
  }
}
```

### For-In Loops

Iterate over ranges, lists, and maps without manual indexing:

```
void main() {
  // range: 0, 1, 2, 3, 4
  for i in 0..5 {
    print(i);
  }

  // list iteration
  int[] nums = [10, 20, 30];
  for n in nums {
    print(n);
  }

  // map iteration with key-value
  map[string,int] ages = {};
  ages.set("alice", 30);
  ages.set("bob", 25);
  for k, v in ages {
    print(k);
    print(v);
  }
}
```

`for x in 0..n` generates a standard C for loop. `for x in list` iterates over list elements. `for k, v in map` iterates over map key-value pairs.

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

### Lambdas

Pass inline functions as arguments:

```
int apply(int x, int fn(int)) {
  return fn(x);
}

void main() {
  // single expression
  int a = apply(5, (int x) => x * 2);
  print(a);    // 10

  // block body
  int b = apply(10, (int x) => {
    int result = x + 1;
    return result * 3;
  });
  print(b);    // 33
}
```

Lambdas are lifted to top-level C functions at compile time. They can be passed anywhere a function pointer is expected.

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

### Standard Library

Moxy ships with an embedded standard library. Import modules with `#include`:

```
#include "std/math.mxy"
#include "std/string.mxy"
#include "std/debug.mxy"

void main() {
  print(max_int(3, 7));              // 7
  print(clamp_int(15, 0, 10));       // 10
  print(str_contains("hello", "ell"));  // 1
  todo("not implemented yet");       // exits with message
}
```

| Module | Functions |
|--------|-----------|
| `std/math.mxy` | `abs_int`, `min_int`, `max_int`, `clamp_int` |
| `std/string.mxy` | `str_len`, `str_eq`, `str_contains`, `str_starts_with`, `str_ends_with` |
| `std/io.mxy` | `eprintln`, `readln` |
| `std/debug.mxy` | `panic`, `todo`, `unreachable` |
| `std/test.mxy` | `assert_eq_int`, `assert_eq_str`, `assert_true`, `assert_false` |

Standard library modules are embedded in the binary — no external files needed at runtime.

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

## Project Mode

For multi-file projects, Moxy uses a `moxy.yaml` config file (powered by [goose](https://github.com/wess/goose)):

```sh
moxy new myapp        # scaffold a new project
cd myapp
moxy run              # build and run from moxy.yaml
moxy build --release  # optimized build
moxy install          # install to /usr/local/bin
```

Projects support dependencies from git:

```sh
moxy add https://github.com/user/lib.git --name lib --version v1.0
moxy remove lib
moxy update           # update all dependencies
```

Async and ARC flags are auto-detected from the source when running in project mode.

### Workspaces

For multi-project repositories, Moxy supports Cargo-style workspaces. A root `moxy.yaml` lists member directories, each with their own `moxy.yaml`:

```yaml
# root moxy.yaml
workspace:
  members:
    - "mylib"
    - "myapp"
```

Members can be libraries or binaries:

```yaml
# mylib/moxy.yaml
project:
  name: "mylib"
  type: "lib"
```

```yaml
# myapp/moxy.yaml
project:
  name: "myapp"
dependencies:
  mylib:
    path: "../mylib"
```

Build and run workspace members:

```sh
moxy build                # build all members in dependency order
moxy build -p mylib       # build just one member
moxy run                  # build all, run the binary member
moxy run -p myapp         # build and run a specific member
moxy build --release      # release build of entire workspace
```

Libraries produce static archives (`build/lib/lib{name}.a`). Binaries that depend on workspace libraries automatically get the right include paths and link flags. Members are built in topological order based on their dependencies.

## Formatter and Linter

```sh
moxy fmt              # format all .mxy files recursively
moxy fmt file.mxy     # format a single file
moxy fmt --check      # check formatting without modifying

moxy lint             # lint all .mxy files recursively
moxy lint file.mxy    # lint a single file
```

The formatter reads settings from `moxyfmt.yaml` if present. The linter checks for unused variables, empty blocks, and shadowed variables.

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
  mxyconf.h/c    — moxyfmt.yaml config parser
  mxystdlib.h/c  — embedded standard library (auto-generated)
  main.c         — CLI entry point, preprocessor, project mode
std/
  math.mxy       — abs, min, max, clamp
  string.mxy     — length, equality, contains, starts/ends with
  io.mxy         — eprintln, readln
  debug.mxy      — panic, todo, unreachable
  test.mxy       — typed assertions
tests/
  *_test.mxy     — test suite
tools/
  asdf/          — asdf version manager plugin
  editors/zed/   — Zed editor extension
  moxylsp/       — Moxy language server (LSP)
examples/
  features.mxy   — comprehensive feature showcase
  lambda.mxy     — lambda / closure examples
  ccompat.mxy    — C compatibility (structs, pointers, switch, bitwise)
  async.mxy      — async/futures (requires --enable-async)
  arc.mxy        — ARC example (requires --enable-arc)
  stdlib.mxy     — standard library usage
  math.mxy       — helper functions (included by features.mxy)
docs/
  book/          — language reference and architecture
  tutorial/      — getting started guide
  api/           — transpiler API reference
```

## How It Works

Moxy is a five-stage transpiler written in ~6,200 lines of C:

```
.mxy source → Preprocessor → Lexer → Parser → AST → Codegen → .c output
```

1. **Preprocessor** scans for `#include` directives, inlining `.mxy` files (with stdlib fallback) and collecting C headers
2. **Lexer** tokenizes the source into a flat token array
3. **Parser** builds an AST using recursive descent with Pratt-style expression parsing
4. **Codegen** walks the AST in two passes — first collecting generic type instantiations and lambdas, then emitting C with monomorphized type definitions, forward declarations, and function bodies

See [Architecture](docs/book/architecture.md) for the full technical breakdown.

## License

MIT
