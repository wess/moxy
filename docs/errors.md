# Moxy Error Reference

Moxy produces Rust-style diagnostics with source snippets, caret pointers, and color-coded labels. Every error follows this format:

```
error: <message>
  --> file.mxy:12:5
   |
12 | some code here
   |     ^
  = help: <suggestion>
```

Errors are printed to stderr and cause the compiler to exit with code 1. Warnings are non-fatal but still printed with source context.

---

## Parse Errors

### `expected X, found Y`

The generic token-mismatch error. The parser expected one token kind but found another. Several specific cases produce tailored hints.

#### `expected ';', found ','`

**Hint:** `in match arms, wrap statements in braces: { statement; }`

**Cause:** Using a comma where a semicolon is needed, usually inside a match arm that contains a statement rather than a simple expression.

```mxy
// bad
match result {
    Ok(v) => print(v),   // comma after statement
}

// good
match result {
    Ok(v) => { print(v); }
}
```

#### `expected ';', found '}'`

**Hint:** `add ';' before '}'`

**Cause:** Missing semicolon at the end of the last statement before a closing brace.

```mxy
// bad
void main() {
    int x = 5
}

// good
void main() {
    int x = 5;
}
```

#### `expected ';'` (general)

**Hint:** `add ';' at end of statement`

**Cause:** A statement is missing its terminating semicolon.

```mxy
// bad
int x = 5
int y = 10;

// good
int x = 5;
int y = 10;
```

#### `expected '{', found '='`

**Hint:** `function bodies must be wrapped in { }`

**Cause:** Attempting to use `=` for a function body instead of braces. Moxy uses C-style function syntax.

```mxy
// bad
int add(int a, int b) = a + b;

// good
int add(int a, int b) {
    return a + b;
}
```

#### `expected ')', found ...`

**Hint:** `unclosed '(' -- add ')' to match`

**Cause:** An opening parenthesis has no matching closing parenthesis.

```mxy
// bad
print(x + y;

// good
print(x + y);
```

#### `expected ']', found ...`

**Hint:** `unclosed '[' -- add ']' to match`

**Cause:** An opening bracket has no matching closing bracket.

```mxy
// bad
int[] nums = [1, 2, 3;

// good
int[] nums = [1, 2, 3];
```

#### `expected '}', found ...`

**Hint:** `unclosed '{' -- add '}' to match`

**Cause:** An opening brace has no matching closing brace.

```mxy
// bad
void main() {
    print("hello")

// good
void main() {
    print("hello");
}
```

#### `expected '(', found identifier`

**Hint:** `expected '(' after function name`

**Cause:** A function call is missing its opening parenthesis.

```mxy
// bad
print "hello";

// good
print("hello");
```

---

### `unexpected X in expression`

The parser encountered a token that cannot begin or continue an expression. Several specific identifiers trigger helpful hints.

#### `unexpected identifier in expression` (when identifier is `str`)

**Hint:** `did you mean 'string'?`

**Cause:** Using `str` (Rust/Python style) instead of Moxy's `string` type.

```mxy
// bad
str name = "alice";

// good
string name = "alice";
```

#### `unexpected identifier in expression` (when identifier is `boolean`)

**Hint:** `did you mean 'bool'?`

**Cause:** Using `boolean` (Java style) instead of Moxy's `bool` type.

```mxy
// bad
boolean flag = true;

// good
bool flag = true;
```

#### `unexpected identifier in expression` (when identifier is `integer`)

**Hint:** `did you mean 'int'?`

**Cause:** Using `integer` instead of Moxy's `int` type.

```mxy
// bad
integer count = 0;

// good
int count = 0;
```

#### `unexpected identifier in expression` (when identifier is `println`)

**Hint:** `did you mean 'print'?`

**Cause:** Using `println` (Rust/Java style) instead of Moxy's `print`.

```mxy
// bad
println("hello");

// good
print("hello");
```

#### `unexpected identifier in expression` (when identifier is `fn`, `func`, or `function`)

**Hint:** `moxy uses C-style function syntax: int add(int a, int b) { ... }`

**Cause:** Attempting to declare a function using a keyword from another language.

```mxy
// bad
fn add(a, b) { return a + b; }
func add(a, b) { return a + b; }

// good
int add(int a, int b) {
    return a + b;
}
```

#### `unexpected identifier in expression` (when identifier is `let`, `var`, or `val`)

**Hint:** `moxy uses C-style declarations: int x = 42;`

**Cause:** Attempting to declare a variable using a keyword from another language (JavaScript, Swift, Kotlin, etc.).

```mxy
// bad
let x = 42;
var name = "alice";

// good
int x = 42;
string name = "alice";
```

#### `unexpected '=>' in expression`

**Hint:** `'=>' is used in match arms and lambda expressions`

**Cause:** A fat arrow appeared where the parser did not expect one, outside a match arm or lambda context.

#### `unexpected end of file in expression`

**Hint:** `unexpected end of file -- check for missing '}'`

**Cause:** The source ended while the parser was still inside an expression. Usually caused by an unclosed brace somewhere above.

---

### `unknown type 'str'`

**Hint:** `did you mean 'string'?`

**Cause:** Using `str` as a type name in a declaration. Moxy's string type is `string`.

```mxy
// bad
str name = "alice";

// good
string name = "alice";
```

### `unknown type 'boolean'`

**Hint:** `did you mean 'bool'?`

**Cause:** Using `boolean` as a type name. Moxy's boolean type is `bool`.

```mxy
// bad
boolean flag = true;

// good
bool flag = true;
```

### `unknown type 'integer'`

**Hint:** `did you mean 'int'?`

**Cause:** Using `integer` as a type name. Moxy's integer type is `int`.

```mxy
// bad
integer count = 0;

// good
int count = 0;
```

---

### `'let' is not a moxy keyword` / `'var' is not a moxy keyword` / `'val' is not a moxy keyword`

**Hint:** `moxy uses C-style declarations: int x = 42;`

**Cause:** These keywords from JavaScript, Swift, Kotlin, etc. are not valid in Moxy. Use C-style type-first declarations.

```mxy
// bad
let x = 42;
var y = "hello";
val z = true;

// good
int x = 42;
string y = "hello";
bool z = true;
```

### `'fn' is not a moxy keyword` / `'func' is not a moxy keyword` / `'function' is not a moxy keyword` / `'def' is not a moxy keyword`

**Hint:** `moxy uses C-style function syntax: int add(int a, int b) { ... }`

**Cause:** These function declaration keywords from Rust, Go, JavaScript, Python, etc. are not valid in Moxy. Use C-style return-type-first function declarations.

```mxy
// bad
fn greet() { print("hi"); }
func greet() { print("hi"); }
def greet() { print("hi"); }

// good
void greet() {
    print("hi");
}
```

---

### `Future<T> requires --enable-async flag`

**Hint:** `run with: moxy --enable-async ...`

**Cause:** The source uses `Future<T>` but the `--enable-async` flag was not passed. Async/futures support requires explicit opt-in.

```bash
# bad
moxy run myfile.mxy

# good
moxy --enable-async run myfile.mxy
```

### `'await' requires --enable-async flag`

**Hint:** `run with: moxy --enable-async ...`

**Cause:** The source uses `await` but the `--enable-async` flag was not passed.

```bash
# bad
moxy run myfile.mxy

# good
moxy --enable-async run myfile.mxy
```

### `expected function call after '|>'`

**Hint:** `pipe operator requires a function call on the right side`

**Cause:** The right-hand side of the pipe operator `|>` is not a function call or identifier that can be called.

```mxy
// bad
x |> 5;
x |> + 1;

// good
x |> double;
x |> add(1);
```

---

## Preprocessor Errors

### `moxy: cannot open 'X'`

**Cause:** The file passed to `moxy` does not exist or cannot be read.

```bash
$ moxy run missing.mxy
moxy: cannot open 'missing.mxy'
```

**Fix:** Check the file path. Make sure the file exists and has read permissions.

### `moxy: cannot find 'X' (checked disk and stdlib)`

**Cause:** An `#include "file.mxy"` directive references a `.mxy` file that was not found in the source directory or the embedded standard library.

```mxy
// bad
#include "nonexistent.mxy"
```

**Fix:** Verify the include path is correct. The preprocessor looks for `.mxy` includes relative to the including file's directory, then checks the embedded stdlib.

---

## Runtime / Build Errors

### `moxy: failed to create temp directory`

**Cause:** The `run` or `build` command could not create a temporary directory under `/tmp/` for compilation. This is typically a permissions or disk space issue.

### `moxy: failed to write temp file`

**Cause:** The transpiled C code could not be written to the temp directory. Check disk space and `/tmp/` permissions.

### `moxy: cannot write 'X'`

**Cause:** The `transpile_to_file` step could not open the output `.c` file for writing. This occurs during project-mode builds when writing to `build/gen/`.

### `moxy: exec failed`

**Cause:** After successful compilation, the `run` command could not execute the compiled binary via `execv`. This is a system-level failure (permissions, missing dynamic libraries, etc.).

### C compiler errors

When Moxy invokes `cc` (or the `CC` environment variable) to compile the generated C code, any errors from the C compiler are passed through directly to stderr. These are **not** Moxy errors. They appear as standard C compiler diagnostics and usually indicate a bug in the transpiler or an unsupported construct.

**Example:**

```
out.c:14:5: error: implicit declaration of function 'unknown_func'
```

**Fix:** Check whether the function or type is defined. For C standard library functions, add the appropriate `#include <header.h>` directive. If the error is from valid Moxy code, it may be a transpiler bug.

---

## Lint Warnings

Lint warnings are produced by `moxy lint` and are non-fatal. They use the same source-snippet format as errors but with a yellow `warning` label.

### `unused variable 'X'`

**Cause:** A variable was declared but never referenced in the scope. Variables whose names begin with `_` are exempt from this check.

```mxy
// triggers warning
void main() {
    int x = 5;    // warning: unused variable 'x'
    print("hi");
}

// suppressed (underscore prefix)
void main() {
    int _x = 5;   // no warning
    print("hi");
}
```

**Config:** Controlled by `lint.unused_vars` in `moxyfmt.yaml`. Default: `true`.

### `variable 'X' shadows outer declaration`

**Cause:** A variable in an inner scope has the same name as a variable in an outer scope, which can cause confusion.

```mxy
// triggers warning
void main() {
    int x = 5;
    if (true) {
        int x = 10;   // warning: variable 'x' shadows outer declaration
    }
}
```

**Fix:** Rename the inner variable to avoid ambiguity.

**Config:** Controlled by `lint.shadow_vars` in `moxyfmt.yaml`. Default: `true`.

### `empty X body`

Where X is one of: `if`, `while`, `for`, `for-in`.

**Cause:** A control flow block has no statements inside it.

```mxy
// triggers warning
void main() {
    if (true) {
        // warning: empty if body
    }
}
```

**Fix:** Add the intended body, or remove the empty block.

**Config:** Controlled by `lint.empty_blocks` in `moxyfmt.yaml`. Default: `true`.

---

## Assert Failures

### `FAIL: assert at line N`

**Cause:** A runtime `assert()` call evaluated to false. This is a runtime error, not a compile-time error. The program prints this message to stderr and exits with code 1.

```mxy
void main() {
    assert(1 == 2);   // FAIL: assert at line 2
}
```

**Fix:** The asserted condition was false. Check the logic or values involved.

---

## CLI Usage Errors

### `moxy: unknown command 'X'`

**Cause:** An unrecognized command was passed to `moxy`. Run `moxy --help` for the list of valid commands.

### `moxy: no test files found`

**Hint:** `name test files with _test.mxy suffix (e.g. math_test.mxy)`

**Cause:** The `test` command found no files matching the `*_test.mxy` pattern.

### `moxy: no .mxy files found`

**Cause:** The `fmt` or `lint` command found no `.mxy` files in the current directory tree.

---

## Configuration

Lint rules are configured in `moxyfmt.yaml`, which Moxy searches for in the current directory and the source file's directory.

```yaml
lint:
  unused_vars: true
  empty_blocks: true
  shadow_vars: true
```

| Rule | Default | Description |
|------|---------|-------------|
| `unused_vars` | `true` | Warn on declared but unused variables (prefix with `_` to suppress) |
| `empty_blocks` | `true` | Warn on empty `if`, `while`, `for`, and `for-in` bodies |
| `shadow_vars` | `true` | Warn when an inner variable shadows an outer one |

Set any rule to `false` to disable it:

```yaml
lint:
  unused_vars: false
  shadow_vars: false
```
