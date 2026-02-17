# Getting Started with Moxy

## Installation

### Prerequisites

- A C compiler (`cc`, `gcc`, or `clang`)
- [goose](https://github.com/nicebyte/goose) — C build tool

### Building from source

```sh
git clone <repo-url>
cd moxy
goose build
```

The transpiler binary is at `./build/debug/moxy`.

## Your First Program

Create `hello.mxy`:

```
void main() {
  print("hello, world!");
}
```

Transpile and run:

```sh
./build/debug/moxy hello.mxy > hello.c
cc -std=c11 -o hello hello.c
./hello
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

Along with the `list_int` struct and helper functions. The generated C is clean, readable, and compiles with any C11 compiler.

## Next Steps

- See [examples/features.mxy](../../examples/features.mxy) for a complete feature showcase
- Read the [Language Reference](../book/reference.md) for full syntax details
- Check the [Architecture](../book/architecture.md) to understand how the transpiler works
- Browse the [API Reference](../api/index.md) for the internal transpiler C API
