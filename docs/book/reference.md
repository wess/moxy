# Moxy Language Reference

## Includes

Moxy supports `#include` directives that work like C's preprocessor. There are two modes:

### Moxy file includes

```
#include "math.mxy"
```

Includes a `.mxy` file by textually inlining its contents before the source is lexed. The file path is resolved relative to the directory of the including file. Nested includes are supported — an included `.mxy` file can include other `.mxy` files.

### C header includes

```
#include <stdlib.h>
#include "mylib.h"
```

Non-`.mxy` includes are passed through to the generated C output. Both angle-bracket and quoted forms are supported. These appear at the top of the C output, before any auto-generated includes. Duplicates are automatically removed — if you include `<stdio.h>` and the codegen would also auto-generate it, only one copy appears.

### Examples

```
#include "helpers.mxy"    // contents spliced into source
#include <math.h>         // emits #include <math.h> in C output
#include "mylib.h"        // emits #include "mylib.h" in C output
```

## Primitive Types

| Moxy Type | C Type | Format | Example |
|-----------|--------|--------|---------|
| `int` | `int` | `%d` | `int x = 42;` |
| `float` | `float` | `%f` | `float pi = 3.14;` |
| `double` | `double` | `%f` | `double tau = 6.28;` |
| `char` | `char` | `%c` | `char c = 'A';` |
| `bool` | `bool` | `%d` | `bool ok = true;` |
| `long` | `long` | `%ld` | `long big = 999999;` |
| `short` | `short` | `%hd` | `short s = 72;` |
| `string` | `const char*` | `%s` | `string name = "moxy";` |

## Variables

```
type name = value;
```

Variables must be initialized at declaration. Reassignment uses `=`:

```
int x = 10;
x = 20;
x += 5;
```

## Operators

### Arithmetic

`+`, `-`, `*`, `/`, `%`

### Comparison

`==`, `!=`, `<`, `>`, `<=`, `>=`

### Logical

`&&`, `||`, `!`

### Assignment

`=`, `+=`, `-=`, `*=`, `/=`

### Increment/Decrement

`++`, `--` (postfix only)

### Pipe

`|>` — passes the left-hand value as the first argument to the right-hand function call.

Operator precedence (highest to lowest):

1. `*`, `/`, `%`
2. `+`, `-`
3. `<`, `>`, `<=`, `>=`
4. `==`, `!=`
5. `&&`
6. `||`
7. `|>` (pipe, lowest precedence)

## Functions

```
returnType name(type1 param1, type2 param2) {
  // body
  return value;
}
```

Functions can call other functions (forward declarations are generated automatically):

```
int add(int a, int b) {
  return a + b;
}

int double_it(int n) {
  return add(n, n);
}
```

The entry point is `void main()`:

```
void main() {
  print("hello");
}
```

## Pipe Operator

The pipe operator `|>` chains function calls left-to-right, passing the left-hand value as the first argument to the right-hand function:

```
expr |> func(args...)
```

Desugars to:

```
func(expr, args...)
```

### Examples

```
int double_it(int x) {
  return x * 2;
}

int add(int a, int b) {
  return a + b;
}

void main() {
  // simple pipe
  int result = 5 |> double_it();
  print(result);    // 10

  // chained pipes (left-associative)
  5 |> double_it() |> add(3) |> print()    // 13

  // bare function name without parens
  10 |> double_it |> print()    // 20
}
```

### How it works

- `a |> f()` becomes `f(a)`
- `a |> f(b, c)` becomes `f(a, b, c)`
- `a |> f() |> g()` becomes `g(f(a))`
- `a |> print()` desugars to `print(a)` with auto-format

Pipe has the lowest operator precedence, so `1 + 2 |> f()` pipes `3` into `f`.

The right-hand side must be a function call or bare function name. Bare names are called with the piped value as the only argument.

## Global Variables

Variables declared outside any function are global:

```
int max_retries = 3;
string version = "1.0";

void main() {
  print(version);
  print(max_retries);
}
```

Global variables follow the same `type name = value;` syntax as local variables and are emitted before function definitions in the C output.

## Print

`print(expr)` auto-detects the type and generates the correct `printf` format:

```
print(42);         // printf("%d\n", 42);
print(3.14);       // printf("%f\n", 3.14);
print("hello");    // printf("%s\n", "hello");
print('A');        // printf("%c\n", 'A');
print(true);       // printf("%d\n", true);
```

Works with variables, expressions, method calls, and field access.

## Assert

`assert(expr)` checks a condition at runtime and exits with an error if it fails:

```
assert(x == 42);
assert(nums.len > 0);
assert(ok);
```

On failure, prints `FAIL: assert at line N` to stderr and exits with code 1. Use in `*_test.mxy` files with `moxy test`.

## Control Flow

### if / else if / else

```
if (condition) {
  // ...
} else if (other) {
  // ...
} else {
  // ...
}
```

### while

```
while (condition) {
  // ...
}
```

### for

```
for (int i = 0; i < 10; i++) {
  // ...
}
```

The for loop supports variable declarations in the init clause, any expression as the condition, and assignment or postfix expressions as the step.

## Enums

### Simple enums

```
enum Color {
  Red,
  Green,
  Blue
}
```

### Tagged enums (algebraic data types)

```
enum Shape {
  Circle(float radius),
  Rect(int w, int h),
  None
}
```

### Construction

```
Color c = Color::Red;
Shape s = Shape::Circle(3.14);
Shape r = Shape::Rect(10, 20);
Shape n = Shape::None;
```

### Pattern Matching

```
match s {
  Shape::Circle(r) => print(r),
  Shape::Rect(w) => print(w),
  Shape::None => print("nothing"),
}
```

The binding variable in a pattern captures the first field of the variant.

## Lists

Dynamic arrays with type `T[]`:

```
int[] nums = [10, 20, 30];
```

### Operations

| Operation | Description |
|-----------|-------------|
| `nums.push(val)` | Append element |
| `nums.len` | Get length |
| `nums[i]` | Index access |

Lists automatically grow when pushing beyond capacity.

## Result Type

Built-in error handling with `Result<T>`:

```
Result<int> ok = Ok(42);
Result<int> fail = Err("something went wrong");
```

### Matching Results

```
match ok {
  Ok(v) => print(v),
  Err(e) => print(e),
}
```

## Map Type

Key-value dictionaries with `map[K,V]`:

```
map[string,int] ages = {};
```

### Operations

| Operation | Description |
|-----------|-------------|
| `ages.set(key, val)` | Insert or update |
| `ages.get(key)` | Retrieve value |
| `ages.has(key)` | Check existence |
| `ages.len` | Get entry count |

String keys use `strcmp` for comparison. Numeric keys use `==`.

## Comments

```
// single line comment

/* multi-line
   comment */
```

## Null

The `null` keyword emits `NULL` in C. Useful when interfacing with C patterns or initializing pointers:

```
string name = null;
```

Transpiles to:

```c
const char* name = NULL;
```

## What Transpiles to What

| Moxy | C Output |
|------|----------|
| `string name = "hi";` | `const char* name = "hi";` |
| `print(x);` | `printf("%d\n", x);` |
| `Color::Red` | `(Color){ .tag = Color_Red }` |
| `Shape::Circle(3.14)` | `(Shape){ .tag = Shape_Circle, .Circle = { .radius = 3.14 } }` |
| `match x { ... }` | `switch (x.tag) { case ...: { ... break; } }` |
| `int[] nums = [1,2,3];` | `list_int nums = list_int_make((int[]){1,2,3}, 3);` |
| `nums.push(4);` | `list_int_push(&nums, 4);` |
| `nums[0]` | `nums.data[0]` |
| `Ok(42)` | `(Result_int){ .tag = Result_int_Ok, .ok = 42 }` |
| `map[string,int] m = {};` | `map_string_int m = map_string_int_make();` |
| `#include "math.mxy"` | Contents inlined before lexing |
| `#include <math.h>` | `#include <math.h>` (passed through) |
| `null` | `NULL` |
| `assert(x == 1);` | `if (!(x == 1)) { fprintf(stderr, "FAIL: ..."); exit(1); }` |
| `x \|> f(y)` | `f(x, y)` |
| `x \|> f() \|> g()` | `g(f(x))` |
| `x \|> print()` | `printf("%d\n", x);` |
