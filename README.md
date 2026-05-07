# Quark

**Quark** is an experimental programming language focused on predictable behavior, explicit state, and simple systems-level programming.

It generates **C** source files, which are compiled into a native binary, with any C compiler, such as Clang

Quark is not a C extension. It defines its own rules and uses C only as a backend

---

## Why Quark

Quark is made for developers who values:

* predictable behavior of their code
* no magic or hidden state
* explicit hangling of memory
* clear semantic of their language
* easy C/C++ interop without C’s unsafe parts

---

## Philosophy

Quark tries to remove surprises from systems programming.

Whatever is going on in your program, it should be:

* written down in the code
* visible in the compiler
* understandable for the developer

No hidden behavior. No guessing.

---

## Quick Example

```qk
func main() void {
    x: int = 10;
    mut y: int = x + 2;

    if (x > 5) {
        y = 0;
    } else {
        y = 1;
    }
}
```
---
## Requirements

Clone the repository with submodules:

```bash
git clone --recursive https://github.com/grafmorkov/quark-lang.git
```
You also need:

**Clang (or another C compiler)**
**CMake 3.20+**
---

## Build(Compilation)

```bash
./quark example.qk --build
```

This will:

1. Parse the code
2. Build abstract syntax tree(AST)
3. Generate IR
4. Check rules (types, variables, state)
5. Generate C code
6. Compile it with Clang

---

## Architecture

```
Source → AST → IR → checks → C → native binary
```

* AST - source code structure
* IR - internal representation with applied rules
* checks - validation phase (no incorrect state allowed)
* C - final output target

---

## Language rules

Some things in Quark are strictly defined:

* variables have to be explicitly initialized
* no undefined behavior
* no hidden memory allocation and usage
* all modifications should be explicit
* no runtime environment acting under the hood

Program being compilable guarantees that it is valid Quark program.

---

## Memory

* no garbage collection
* memory is allocated explicitly by the programmer
* there are no hidden memory behaviors

Everything concerning memory usage is visible.

---

## CLI commands

```bash
./quark file.qk            # generates C code
./quark file.qk --build    # compiles it to executable
./quark file.qk --run
```

---

## Example

```qk
func main() void {
    x: int = 10;
    mut y: int = x + 2;

    if (x > 5) {
        y = 0;
    } else {
        y = 1;
    }
}
```
---

# Design goals

* Predictable programs
* Simple rules
* No runtime mysteries
* Easy code reasoning
* C compatibility with generated code

---

## Project status

| Component   | Progress  |
| ----------  | --------- |
| Lexer       | completed |
| Parser      | completed |
| Abstract syntax tree | completed |
| Intermediate representation | ongoing |
| Semantic checks | ongoing |
| Code generation | completed |
| Optimizations | planned |

---
## TODO

### Core language features

- [x] Structs
  - [x] basic struct definition
  - [x] field parsing
  - [x] struct in AST
  - [x] struct checking in semantic
  - [ ] struct methods (optional future)

- [x] Attributes
  - [x] AST representation for attributes(currently demo representation)
  - [ ] parser support (@attr style)
  - [ ] attach to:
    - [ ] structs
    - [ ] functions
    - [ ] variables
  - [ ] semantic validation pass

---

### Memory

- [x] Arena Allocator
  - [x] bump allocator implementation
  - [x] scoped lifetime (reset / pop state)
  - [x] integration with AST allocation
  - [x] replace raw new/delete in parser

---

### Standard / ecosystem libs

- [ ] stream lib
  - [ ] basic output stream
  - [ ] input stream abstraction
  - [ ] error stream
  - [ ] formatting utilities (printf-like or safe format API)

- [ ] opt lib
  - [ ] optional type (like std::optional)
  - [ ] has_value / unwrap API
  - [ ] error-safe unwrap patterns
  - [ ] integration with parser & semantic errors
---

## Future roadmap

### Short-term (P0)
- Better error messages
- Variable validation

### Mid-term (P1)
- IR improvements
- Modules
- Std

### Long-term (P2)
- LLVM/WASM backend
- Self-hosted compiler

---

## Building

```sh
git clone https://github.com/grafmorkov/Quark.git
cd Quark

mkdir build
cd build

cmake ..
cmake --build .
```

---

## Contributing

If you are into compilers, IR design and systems programming, please consider contributing.

---

## License

GPL-3.0

[https://www.gnu.org/licenses/gpl-3.0.html](https://www.gnu.org/licenses/gpl-3.0.html)

---

## Notice

Quark is not intended to be a universal programming language.

It targets systems programming.