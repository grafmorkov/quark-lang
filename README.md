# Quant

Quant is an experimental systems programming language focused on explicit state,
predictable semantics, and transparent memory management.

The compiler uses a native backend (IR -> x86-64 machine code) on both platforms:
ELF executables on Linux, PE32+ executables on Windows. No external assembler is needed.

> Note: Quant is still in development and not everything is done

---

## Example

```qu
load "std::io";
using std::io;

@entry i32 main() {
    i32 value = 10;
    mut i32 result = value + 2;

    if (value > 5) {
        result = 0;
    } else {
        result = 1;
    }
    str msg = result as str; // casting
    println(msg);
    return 0;
}
```
## Documentation

The documentation is in **Doc.md** file

---

## Architecture

```text
Load modules -> AST -> semantic analysis -> IR -> native binary
```

### Pipeline

0. Load all modules
1. Parse source code into AST
2. Run semantic validation passes
3. Generate intermediate representation
4. Generate machine code (native backend: ELF on Linux, PE32+ on Windows)

---

## Features

* minimal hidden behaviour
* attributes
* explicit behaviour
* arena-based compiler memory management
* stdlib written in pure Quant (io, format, heap, arena, vector, string)
* Windows stdlib via `@import` (WinAPI), no assembly runtime

---

## Build

### Requirements

* CMake 3.20+
* C++20 compiler

Build the compiler:

```bash
git clone https://github.com/quant-lang/quanta.git
cd quanta

cmake -B build
cmake --build build
```

---

## Usage

Compile to a native executable:

```bash
./qu file.qu -o program
```

Other options: `--emit-ir`, `--emit-asm`, `--no-compile`, `--time`.

---

## Project Status

| Component            | Status      |
| -------------------- | ----------- |
| Lexer                | completed   |
| Parser               | completed   |
| AST                  | completed   |
| Semantic analysis    | completed   |
| IR                   | completed   |
| Native backend       | completed (Linux & Windows) |
| Optimizations        | planned     |
| Self-hosted compiler | planned     |

---

## Roadmap

### Short-term

* add more attributes
* improve stdlib

### Long-term

* optimizations
* self-hosting compiler

---

## Contributing

Contributions related to compilers and systems programming are welcome.

---

## License
This project is under the [GPL-3.0](https://www.gnu.org/licenses/gpl-3.0.html) licence.

```
