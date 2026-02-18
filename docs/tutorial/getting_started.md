# Getting Started with Moxy

## Installation

### Quick install (macOS / Linux)

```sh
curl -fsSL https://github.com/wess/moxy/releases/latest/download/install.sh | sh
```

This auto-detects your OS and architecture, downloads the right binary, and installs to `/usr/local/bin`. Set `INSTALL_DIR` to change the location:

```sh
INSTALL_DIR=~/.local/bin curl -fsSL https://github.com/wess/moxy/releases/latest/download/install.sh | sh
```

### Package managers

**Debian / Ubuntu:**
```sh
curl -fsSLO https://github.com/wess/moxy/releases/latest/download/moxy_VERSION_amd64.deb
sudo dpkg -i moxy_*.deb
```

**Fedora / RHEL:**
```sh
curl -fsSLO https://github.com/wess/moxy/releases/latest/download/moxy-VERSION-1.x86_64.rpm
sudo rpm -i moxy-*.rpm
```

**Arch Linux:**
```sh
curl -fsSLO https://github.com/wess/moxy/releases/latest/download/PKGBUILD
makepkg -si
```

**asdf:**
```sh
asdf plugin add moxy https://github.com/wess/moxy.git tools/asdf
asdf install moxy latest
asdf set moxy latest
```

### Building from source

Prerequisites:
- A C compiler (`cc`, `gcc`, or `clang`)
- [goose](https://github.com/wess/goose) — C build tool

```sh
git clone https://github.com/wess/moxy.git
cd moxy
goose build
```

The transpiler binary is at `moxy`.

## Your First Program

Create `hello.mxy`:

```
void main() {
  print("hello, world!");
}
```

Run it:

```sh
moxy run hello.mxy
```

Or if you want to see the intermediate steps:

```sh
moxy hello.mxy > hello.c    # transpile to C
cc -std=c11 -o hello hello.c # compile
./hello                       # run
```

Output:

```
hello, world!
```

## Variables and Types

Moxy supports all standard C numeric types plus `string` and `bool`:

```
void main() {
  int age = 30;
  float height = 5.11;
  string name = "moxy";
  bool active = true;
  char initial = 'M';

  print(name);
  print(age);
  print(height);
  print(active);
  print(initial);
}
```

`print()` automatically picks the right format. No `printf` format strings needed.

## Arithmetic and Operators

Standard C operators work as expected:

```
void main() {
  int a = 10 + 5;      // 15
  int b = a * 2;        // 30
  bool big = (b > 20);  // true

  print(a);
  print(b);
  print(big);
}
```

Assignment operators: `+=`, `-=`, `*=`, `/=`
Increment/decrement: `++`, `--`

## Functions

Define functions with typed parameters and return values:

```
int square(int n) {
  return n * n;
}

int max(int a, int b) {
  if (a > b) {
    return a;
  }
  return b;
}

void main() {
  print(square(5));      // 25
  print(max(10, 20));    // 20
}
```

Functions can call each other in any order — forward declarations are generated automatically.

## Control Flow

### if / else

```
void main() {
  int score = 85;

  if (score >= 90) {
    print("A");
  } else if (score >= 80) {
    print("B");
  } else {
    print("C");
  }
}
```

### for loops

```
void main() {
  for (int i = 0; i < 5; i++) {
    print(i);
  }
}
```

### while loops

```
void main() {
  int i = 10;
  while (i > 0) {
    print(i);
    i -= 1;
  }
}
```

## Enums

### Simple enums

```
enum Direction {
  North,
  South,
  East,
  West
}

void main() {
  Direction d = Direction::North;

  match d {
    Direction::North => print("up"),
    Direction::South => print("down"),
    Direction::East => print("right"),
    Direction::West => print("left"),
  }
}
```

### Tagged enums

Variants can carry data:

```
enum Shape {
  Circle(float radius),
  Rect(int w, int h),
  Point
}

void main() {
  Shape s = Shape::Circle(5.0);

  match s {
    Shape::Circle(r) => print(r),
    Shape::Rect(w) => print(w),
    Shape::Point => print("point"),
  }
}
```

## Lists

Dynamic arrays with `T[]` syntax:

```
void main() {
  int[] numbers = [1, 2, 3];
  numbers.push(4);
  numbers.push(5);

  print(numbers.len);     // 5
  print(numbers[0]);      // 1
  print(numbers[4]);      // 5
}
```

## Result Type

Handle success and failure without exceptions:

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

## Maps

Key-value dictionaries:

```
void main() {
  map[string,int] scores = {};

  scores.set("alice", 95);
  scores.set("bob", 87);

  print(scores.get("alice"));  // 95
  print(scores.has("bob"));    // 1 (true)
  print(scores.len);           // 2
}
```

## Automatic Reference Counting (ARC)

By default, lists and maps are stack-allocated structs. With `--enable-arc`, they become heap-allocated objects with automatic reference counting. The compiler inserts `retain`/`release` calls at scope boundaries — your Moxy code stays the same:

```
void main() {
  int[] nums = [1, 2, 3];
  nums.push(4);
  print(nums.len);     // 4
  print(nums[0]);      // 1
}
```

```sh
moxy --enable-arc run arc_example.mxy
```

### Shared references

When you assign one list to another, they share the same backing memory:

```
void main() {
  int[] a = [1, 2, 3];
  int[] b = a;          // both point to the same list
  b.push(4);
  print(a.len);         // 4 — a sees the change too
}
```

### Passing to functions

ARC types are passed as pointers. The compiler retains on entry and releases on exit:

```
int list_sum(int[] nums) {
  int total = 0;
  for (int i = 0; i < nums.len; i++) {
    total += nums[i];
  }
  return total;
}

int[] make_list() {
  int[] result = [10, 20, 30];
  return result;    // ownership transfers to caller
}

void main() {
  int[] a = [1, 2, 3];
  print(list_sum(a));    // 6

  int[] b = make_list();
  print(b[1]);           // 20
}
```

### Nested scopes

ARC variables declared in if-blocks, loops, or match arms are released when that scope ends:

```
void main() {
  int[] outer = [1, 2];
  if (true) {
    int[] inner = [3, 4];
    print(inner.len);    // 2, released here
  }
  print(outer.len);      // 2, released at function exit
}
```

## Comments

```
// This is a line comment

/* This is a
   block comment */

void main() {
  // comments work anywhere
  print("hello");
}
```

## Includes

As your project grows, you can split code across multiple `.mxy` files using `#include`.

### Including Moxy files

Create a helper file, `helpers.mxy`:

```
int square(int n) {
  return n * n;
}

int cube(int n) {
  return n * n * n;
}
```

Include it from your main file:

```
#include "helpers.mxy"

void main() {
  print(square(5));   // 25
  print(cube(3));     // 27
}
```

The contents of `helpers.mxy` are spliced directly into the source before compilation. This works just like C's `#include` — functions defined in the included file are available as if they were written inline.

Includes are resolved relative to the including file's directory, and nested includes work too.

### Including C headers

You can also include C headers for use in the generated output:

```
#include <stdlib.h>
#include "mylib.h"
```

These are passed through directly to the generated C code. Moxy automatically avoids duplicating headers it already generates (like `<stdio.h>`).

## Pipe Operator

The pipe operator `|>` lets you chain function calls left-to-right, like Elixir or F#. The left side becomes the first argument to the function on the right:

```
int double_it(int x) {
  return x * 2;
}

int add(int a, int b) {
  return a + b;
}

void main() {
  // without pipes
  int result = add(double_it(5), 3);
  print(result);    // 13

  // same thing with pipes — reads left-to-right
  5 |> double_it() |> add(3) |> print()    // 13
}
```

Pipes are great for building data transformation pipelines. You can also omit the parentheses for single-argument functions:

```
void main() {
  10 |> double_it |> print()    // 20
}
```

## Async / Futures

Moxy supports concurrency via `Future<T>` and `await`, backed by pthreads. This feature requires the `--enable-async` flag.

### Spawning a future

A function returning `Future<T>` runs its body on a new thread:

```
Future<int> compute(int x) {
  return x * 2;
}

void main() {
  int result = await compute(21);
  print(result);    // 42
}
```

```sh
moxy --enable-async run compute.mxy
```

### Void futures

Use `Future<void>` for side-effecting work:

```
Future<void> do_work() {
  // does something
  return;
}

void main() {
  await do_work();
  print("done");
}
```

### Multiple futures

You can chain sequential awaits:

```
Future<int> double_it(int x) { return x * 2; }
Future<int> add_one(int x) { return x + 1; }

void main() {
  int a = await double_it(10);
  int b = await add_one(a);
  print(a);    // 20
  print(b);    // 21
}
```

### How it works

Under the hood, `Future<int> compute(int x)` generates:

1. An args struct to pass parameters across the thread boundary
2. A thread wrapper function that extracts args, runs the body, and returns the result via `void*`
3. A launcher function that mallocs args, calls `pthread_create`, and returns the future handle

`await` calls `pthread_join` and extracts the result.

### Limitations

- No nested await (`await f(await g())`)
- Await only in variable declarations or standalone statements
- No `Future<Result<T>>` nesting
- Every future must be awaited

## What Happens Under the Hood

When you write:

```
int[] nums = [1, 2, 3];
nums.push(4);
print(nums[0]);
```

Moxy generates:

```c
list_int nums = list_int_make((int[]){1, 2, 3}, 3);
list_int_push(&nums, 4);
printf("%d\n", nums.data[0]);
```

Along with the `list_int` struct and helper functions.

With `--enable-arc`, the same Moxy code generates ref-counted heap objects:

```c
list_int *nums = list_int_make((int[]){1, 2, 3}, 3);  // rc=1
list_int_push(nums, 4);       // pointer, no &
printf("%d\n", nums->data[0]); // -> not .
list_int_release(nums);        // auto-inserted at scope exit
```

The generated C is clean, readable, and compiles with any C11 compiler.

## Editor Setup

### Zed

Copy the bundled extension into your Zed extensions directory:

```sh
cp -r tools/editors/zed ~/.config/zed/extensions/moxy
```

This gives you syntax highlighting, auto-indent, bracket matching, and comment toggling for `.mxy` files.

## Next Steps

- See [examples/features.mxy](../../examples/features.mxy) for a complete feature showcase
- Read the [Language Reference](../book/reference.md) for full syntax details
- Check the [Architecture](../book/architecture.md) to understand how the transpiler works
- Browse the [API Reference](../api/index.md) for the internal transpiler C API
