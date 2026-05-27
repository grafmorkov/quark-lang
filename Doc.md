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

Integer literals are i32 by default. Float literals are f64 by default.

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

### Assignment
```
x = x + 1;
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

## Modules

Use `load` to import a module:

```
load "std/io.qk";
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
load "std/arena.qk";
p: *void = std::arena::_create(4096);
std::arena::_destroy(p);
```
---

## Standard Library 

### std/io.qk

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

### std/arena.qk
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

- String literals do not support escape sequences. `\n` is stored as two bytes (backslash and 'n').
- Number literals are decimal only (no hex, octal, or binary prefixes).
- No character literals (no single-quote syntax).
