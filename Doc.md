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

### if / else

```
if (x < 10) {
    std::io::print("small");
} else {
    std::io::print("big");
}
```

### while

```
while (x > 0) {
    x = x - 1;
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

### Precedence (lowest → highest)
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

### Operator Overloading

Struct types can overload binary operators by defining a free function:

```
func operator +(a: Point, b: Point) Point {
    @init mut r: Point;
    r.x = a.x + b.x;
    r.y = a.y + b.y;
    return r;
}
```

Supported overloadable operators:

| Token | Function name |
|-------|---------------|
| `+`   | `operator+`   |
| `-`   | `operator-`   |
| `*`   | `operator*`   |
| `/`   | `operator/`   |
| `==`  | `operator==`  |
| `!=`  | `operator!=`  |
| `<`   | `operator<`   |
| `<=`  | `operator<=`  |
| `>`   | `operator>`   |
| `>=`  | `operator>=`  |
| `&`   | `operator&`   |
| `\|`  | `operator\|`  |
| `&&`  | `operator&&`  |
| `\|\|` | `operator\|\|` |

Unary operators (`-`, `!`) can also be overloaded for struct types via `operator-` and `operator!`.

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

`extern` declares a function implemented in runtime assembly:

```
extern func print(text: str) void;
```
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
```alloc(T, size);``` or ```alloc(size)``` for void; 
However, you can allocate memory in the more familiar way via the "std/arena.qk" library:
```
load "std::arena";

func main() i32 {
    p: *void = std::arena::_create(4096);
    std::arena::_destroy(p);
    return 0;
}
```
---

## Standard Library 

### std::io

| Function         | Description                      |
|------------------|----------------------------------|
| print(text)      | print string to stdout           |
| println(text)    | print string + newline to stdout |
| eprint(text)     | print string to stderr           |
| print_char(c)    | print single byte to stdout      |
| read(buf, len)   | read from stdin into buffer      |
| read_char()      | read one byte from stdin         |
| open(path,flags,mode) | open file (returns handle)  |
| close(fd)        | close file handle                |
| write(fd,buf,len)| write to file                    |
| read_fd(fd,buf,len) | read from file               |
| seek(fd,offset,whence) | seek in file               |
| flush(fd)        | flush file buffers               |
| strlen(s)        | get string length                |
| exit(code)       | exit process                     |

### std::arena
| Function           | Description                      |
|--------------------|----------------------------------|
| _create(size: u64) | create new arena                 |
| _destroy(ptr: *void) | destroy allocated pointer      |
---

## Comments

```
// line comment

/*
   block comment
*/
```

---

## How to Build and Run

```
quark file.qk                     # generate out.S (assembly)
quark file.qk --build             # compile to out.exe
quark file.qk --run               # build and run
quark file.qk --emit-ir           # print intermediate representation
quark file.qk --emit-asm          # print generated assembly
quark file.qk --time              # print compilation time
quark file.qk --no-compile        # semantic analysis only
```

Requires: CMake 3.20+, flat assembler (fasm), C++20 compiler.

---

## Notes

- String literals support escape sequences: `\n`, `\t`, `\r`, `\\`, `\"`, `\'`, `\0`.
- Character literals use single quotes: `'a'`, `'\n'`, etc. `char` is an alias for `u8`.
- Number literals are decimal only (no hex, octal, or binary prefixes).
