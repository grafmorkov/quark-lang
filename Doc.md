# Quark Language

## Types

| Type | Size | Description        |
|------|------|--------------------|
| void | 0    | no value           |
| bool | 1    | boolean            |
| i8   | 1    | signed byte        |
| i16  | 2    | signed short       |
| i32  | 4    | signed int         |
| i64  | 8    | signed long        |
| u8   | 1    | unsigned byte      |
| u16  | 2    | unsigned short     |
| u32  | 4    | unsigned int       |
| u64  | 8    | unsigned long      |
| f32  | 4    | float              |
| f64  | 8    | double             |
| str  | 8    | string (pointer)   |
| *T   | 8    | pointer to T       |
| &T   | 8    | reference to T     |

Integer literals are i32 by default. Float literals are f64 by default. Boolean literals `true` and `false` are `bool`.

---

## Variables

```
a: i32 = 10;            // immutable, must be initialized
mut b: str = "hello";   // mutable
c: f64 = 3.14;
d: f64 = 1.5e2;         // scientific notation
```

Variables must have an explicit type annotation.

---

## Functions

```
func add(x: i32, y: i32) i32 {
    return x + y;
}

func greet(name: str) {
    std::io::print("hello ");
    std::io::print(name);
}

func main() i32 {
    return 0;
}
```

- Return type is required unless the function returns void (then it can be omitted).
- Parameters can be mutable: `func foo(mut x: i32)`.
- Function must have at least one return statement if return type is not void.

---

## Control Flow

### if / else if / else

```
if (x < 10) {
    std::io::print("small");
}
else if(x == 10){
  std::io::print("medium");
}
else {
    std::io::print("big");
}
```

### while

```
while (x > 0) {
    x = x - 1;
}
```

### break / continue

`break` immediately exits the nearest enclosing `while` loop or `switch`.

`continue` skips the rest of the current loop iteration and starts the next one:

```qk
while (i < 10) {
    i = i + 1;

    if (i == 5) {
        break;      // stop the loop
    }

    if (i == 3) {
        continue;   // skip sum += i
    }

    sum = sum + i;
}
```

Notes:

- `break` can only be used inside a `while` loop or `switch`.
- `continue` can only be used inside a `while` loop.
- If a `switch` is inside a `while`, `break` leaves only the `switch`, while `continue` starts the next iteration of the enclosing loop.

### switch / case / default

`switch` selects one branch based on the value of an expression:

```qk
switch (x) {
    case 1: {
        std::io::print("one");
    }

    case 2: {
        std::io::print("two");
    }

    default: {
        std::io::print("other");
    }
}
```

Multiple `case` labels may share the same body, just like in C:

```qk
switch (x) {
    case 1:
    case 2: {
        std::io::print("one or two");
    }

    default: {
        std::io::print("other");
    }
}
```

Rules:

- The `switch` expression must have type `bool`, `char`, or an integer type.
- `case` values must be compile-time constants (literals, enum variants, or `const` variables).
- Duplicate `case` values are a compile-time error.
- `default` is optional. If no case matches and there is no `default`, execution simply continues after the `switch`.
- A `switch` is considered terminating only if every possible branch (`case` and `default`) terminates (for example, by `return`).

### References

References (`&T`) are non-owning aliases to existing variables. They have the same size as pointers (8 bytes) but provide compile-time safety.

```
x: i32 = 42;
r: &i32 = &x;     // take reference to x
std::io::print(r as str);
```

References can be reassigned to point to different variables:

```
a: i32 = 10;
b: i32 = 20;
r: &i32 = &a;
r = &b;            // reassign reference to point to b
```

References can be passed to functions that accept reference parameters:

```
func increment(r: &i32) void {
    r = r + 1;     // modifies the original variable
}

func main() i32 {
    mut x: i32 = 10;
    increment(&x);
    return 0;
}
```

References are automatically dereferenced when accessing fields or indexing:

```
struct Point { x: i32; y: i32; }

func main() i32 {
    p: Point;
    p.x = 10;
    p.y = 10;
    r: &Point = &p;
    return r.x + r.y;   // field access through reference
}
```

References are automatically dereferenced for pointer dereference via index:

```
region {
    p: *i32 = alloc(i32, 1);
    p[0] = 42;
    r: &*i32 = &p;      // reference to pointer
    v: i32 = r[0];      // auto-deref, then index
}
```

---

## Operators

### Arithmetic
```
+   -   *   /
```

### Comparison (result is i32, 0 or 1)
```
==   !=   <   <=   >   >=
```

### Bitwise
```
&    (bitwise AND)
|    (bitwise OR)
```

### Logical (result is i32, 0 or 1)
```
&&   (logical AND - both operands non-zero -> 1)
||   (logical OR  - at least one non-zero -> 1)
```

### Compound Assignment
```
+=   -=   *=   /=   &=   |=
```
Each desugars to `x = x op y` at parse time.

### Precedence (lowest -> highest)
```
=   +=   -=   *=   /=   &=   |=
||
&&
|
&
==   !=
<   <=   >   >=
+   -
*   /
```

---

## Casts

### Value conversion (`as`)

Converts between numeric types. Also converts numbers to string.

```
a: i64 = 42 as i64;       // i32 -> i64
b: i8  = 1000 as i8;      // i32 -> i8  (truncation)
c: f64 = 10 as f64;       // i32 -> f64
d: i32 = 3.9 as i32;      // f64 -> i32 (truncates to 3)
s: str = 42 as str;       // number -> string
t: str = 3.14 as str;     // float -> string
```

### Bit reinterpretation (`as!`)

Reinterprets the bytes of a value as a different type. Source and target must have the same size.

```
x: u32 = -1 as i32 as! u32;   // reinterpret i32 bytes as u32
```

---

## Structs

```
struct Point {
    x: i32;
    y: i32;
}

func main() i32 {
    p: Point = Point{ x: 10, y: 20 };
    return p.x + p.y;
}
```

Fields can be mutable:
```
struct Counter {
    mut val: i32;
}
```

---

## Enums

C-style enums with automatically assigned `i32` values starting from 0.
```
enum Color {
    Red,    // 0
    Green,  // 1
    Blue,   // 2
};
```

Variants can be accessed directly by name or qualified:

```
x: i32 = Red;          // 0
y: i32 = Color::Green; // 1
```

Enums support attributes like `@public` and `@private`:

```
@public enum Status {
    Ok,
    Error,
};
```

---

## Generics

Structs can have type parameters with `<>`:

```
struct Box<T> {
    value: T;
};
```

When you use a generic struct, pass the type argument in `<>`:

```
@init mut b: Box<i32>;
b.value = 42;
```

Multiple type parameters are also supported:

```
struct Pair<A, B> {
    first: A;
    second: B;
};
```

Generics are compiled lazily: the concrete struct (like `Box$i32`) is created only when you first access a field.

### Generic Functions

Functions can also have type parameters:

```
func identity<T>(x: T) T {
    return x;
}
```

Pass the type argument when calling:

```
identity<i32>(42);
```

Multiple type parameters work too:

```
func swap<A, B>(a: A, b: B) A {
    return a;
}
```

Generic function bodies are compiled separately for each set of type arguments you actually use (monomorphization). For example, `identity<i32>` and `identity<bool>` become two independent functions.

---

## Modules

Use `load` to import a module:

```
load "std::io";
```

Functions in other files are accessed via namespace:

```
std::io::print("hello");
std::io::exit(0);
```

`extern` declares a function implemented outside of Quark (system calls, native runtime, or another module):

```
extern func print(text: str) void;
```

Functions marked with `@syscall(N)` are lowered to the Linux syscall with number N:

```
@syscall(1) extern func sys_write(fd: i64, buf: str, len: i64) i64;
```

The standard library (`std::io`, `std::heap`, `std::arena`, `std::string`, ...) is written in pure Quark: on Linux on top of syscalls, on Windows on top of `@import` (WinAPI).

### `using`

Imports all symbols from a namespace into the current scope:

```
load "std::io";
using std::io;

func main() {
    print("hello\n");   // instead of std::io::print
}
```

Symbols are imported by value (copied) - visibility (`@public`/`@private`) is still enforced at the use-site based on the original module

---

## Attributes

Attributes annotate declarations with extra semantics. Syntax: `@name` before a declaration.

If the attribute takes arguments: `@name(expr1, expr2)`.

Supported attributes:

| Attribute  | Targets                              | Args | Description                                          |
|------------|--------------------------------------|------|------------------------------------------------------|
| `@entry`   | function                             | 0    | Mark function as program entry point                 |
| `@init`    | variable                             | 0    | Suppress uninitialized variable check                |
| `@guard`   | variable / field                      | 1    | Compile-time check: validate condition on use        |
| `@public`  | function / variable / field / struct | 0    | Make symbol visible outside the module               |
| `@private` | function / variable / field / struct | 0    | Hide symbol from other modules                       |
| `@hide`    | module                               | 0    | Make all module symbols private by default           |
| `@syscall` | function (extern only)               | 1    | Lower function to Linux syscall `N`                  |
| `@export`  | function                             | 1    | Use `name` as the function symbol in the object file |
| `@import`  | function (extern only)               | 1-2  | Import a function from a Windows DLL                 |

### `@entry`

The function with `@entry` is the program entry point (replaces `main` by name).

```
@entry func start() i32 {
    return 0;
}
```

If no `@entry` is found, the linker falls back to a function named `main`.

### `@init`

Declares a variable without an initializer, promising the compiler it will be initialized before use.

```
@init x: i32;
foo(x);                  // no "uninitialized variable" error
```

Without `@init`, an immutable variable without a value is a compile error.

### `@guard`

Validates a condition at compile time when the value is used (passed to a function or when a struct field is accessed). If the condition evaluates to `false`, the compiler reports a `guard failed` error.

The guard condition must be a compile-time constant expression (literals, const variables, and comparisons between them).

```
count: i32 = 1;
@guard(count > 0) mut value: i32;

value = 10;               // ok (writes are not guarded)

func work(x: i32) {}
work(value);              // passes: count > 0 is true at compile time
```

If the condition is false, compilation fails:

```
count: i32 = 0;
@guard(count > 0) mut value: i32;

func work(x: i32) {}
work(value);              // error: guard failed for 'value' in call to 'work'
```

Guard also works on struct fields:

```
struct X {
    @guard(true) data: i32;
};
```

### `@private`

Restricts access to the declaring module. Other modules cannot call or reference the symbol.

```
@private func helper() i32 {
    return 42;
}

func public_fn() i32 {
    return helper();     // ok - same module
}
```

Access from another module produces: `Cannot access private symbol`.

### `@public`

Explicitly marks a symbol as accessible from other modules. Useful inside `@hide` modules.

### `@hide`

Applied to a module (place `@hide` on any top-level declaration). Makes all symbols in the module private by default; only symbols with `@public` remain accessible from outside.

```
@hide func internal() i32 { return 1; }
@public func api() i32    { return 2; }
```

### `@syscall`

Declares an `extern` function implemented as a raw Linux syscall. The argument is the syscall number. (Windows uses `@import` instead.)

```
@syscall(1) extern func sys_write(fd: i64, buf: str, len: i64) i64;
@syscall(60) extern func sys_exit(code: i64) void;
```

The generated wrapper follows the System V calling convention: arguments arrive in
`rdi, rsi, rdx, rcx, r8, r9` (up to 6), then `rcx` is moved to `r10`, the syscall
number is loaded into `rax`, and `syscall` is executed. The result is returned in `rax`.

```
extern func print(text: str) void;   // error: @syscall requires extern
@syscall("1") extern func bad() i64; // error: argument must be an integer literal
```

### `@export`

Assigns a fixed symbol name to a function in the generated object file (instead of the
compiler-generated internal name `fn_<id>__<name>`). Useful for calling Quark functions
from native code or for stable symbols during debugging.

```
@export("my_func") func do_work() i32 {
    return 42;
}
```

### `@import`

Declares an `extern` function implemented by a Windows DLL. The call is emitted
through the PE import table (IAT) by the native backend. Windows only.

The first argument is the DLL name. An optional second argument is the exported
symbol name; when omitted, the Quark function name is used as the export name.

```
@import("kernel32.dll", "ExitProcess") extern func sys_exit_process(uExitCode: u32) void;
@import("kernel32.dll") extern func GetTickCount() u32;   // imports GetTickCount
```

On Linux, use `@syscall` instead.

```
@import extern func bad() i64;          // error: @import requires a DLL name
extern func also_bad() i64;             // error: @import requires extern
```

---

## Regions & Pointers. Arrays
Quark has region memory system. Pointers can only be declared in ```region{}```. If a region dies, all pointers are destroyed.
```
region r {
    p: *void = alloc(32);
}
```
Arrays are also supported and can (only) be declared in region:
```
region r {
    p: *i32 = alloc(i32, 10);
    p[0] = 42;
    p[1] = p[0] + 1;
    std::io::print(p[0] as str);
    std::io::print_char(32 as i8);
    std::io::print(p[1] as str);
    std::io::print_char(10 as i8);
}
```
```alloc(T, count);``` — typed allocation, returns `*T` where count is number of elements.
```alloc(size);``` — untyped allocation, returns `*void` where size is in bytes.

Nested regions are also supported:
```
region outer {
    a: *i32 = alloc(i32, 1);
    region inner {
        b: *i32 = alloc(i32, 1);
    }
}
```

---

## Comments

```
// line comment

/*
   block comment
*/
```

---

## `sizeof`

Compile-time type size query:

```
sz: u64 = sizeof(i32);   // 4
sz: u64 = sizeof(i64);   // 8
sz: u64 = sizeof(u8);    // 1
sz: u64 = sizeof(*T);    // 8  (pointer)
sz: u64 = sizeof(&T);    // 8  (reference)
```

In generic functions, `sizeof(T)` substitutes the concrete type at compile time:

```
func foo<T>() void {
   x: u64 = sizeof(T);
}

func main() {
    foo<i64>();    // x = 8
    foo<i32>();    // x = 4
}
```

---

## How to Build and Run

```
quark file.qk -o output          # compile to native binary (ELF/PE32+)
quark file.qk --emit-ir          # print intermediate representation
quark file.qk --emit-asm         # print generated assembly
quark file.qk --time             # print compilation time
quark file.qk --no-compile       # semantic analysis only
```

The native backend generates machine code directly (x86-64): ELF on Linux, PE32+ on Windows. No external assembler is required.

Requires: CMake 3.20+, C++20 compiler.

---

## Notes

- String literals support escape sequences: `\n`, `\t`, `\r`, `\\`, `\"`, `\'`, `\0`.
- Character literals use single quotes: `'a'`, `'\n'`, etc. `char` is an alias for `u8`.
- Number literals are decimal only (no hex, octal, or binary prefixes).
