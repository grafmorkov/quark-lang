# Quark

Quark is an experimental systems programming language focused on explicit state,
predictable semantics, and transparent memory management.

The compiler currently targets fasm as a backend.

> Note: Quark is still in development and not everything is done

---

## Example

```qk
load "std::io";

@entry func main() {
    value: i32 = 10;
    mut result: i32 = value + 2;

    if (value > 5) {
        result = 0;
    } else {
        result = 1;
    }
    msg: str = result as str; // casting
    std::io::print(msg);
    return 0;
}
```
## Documentation

The documentation is in **Doc.md** file

---

## Architecture

```text
Load modules -> AST -> semantic analysis -> IR -> fasm -> native binary
```

### Pipeline

0. Load all modules
1. Parse source code into AST
2. Run semantic validation passes
3. Generate intermediate representation
4. Generate asm source code
5. Compile with fasm

---

## Features

* minimal hidden behaviour
* attributes
* explicit behaviour
* arena-based compiler memory management

---

## Build

### Requirements

* CMake 3.20+
* Fasm compiler(copy the fasm.exe/fasm into the fasm/ folder)

Clone the repository with submodules:

```bash
git clone --recursive https://github.com/grafmorkov/quark-lang.git
```

Build the compiler:

```bash
git clone --recursive https://github.com/grafmorkov/quark-lang.git
cd quark-lang

mkdir build
cd build

cmake ..
cmake --build .
```

---

## Usage

Generate asm source:

```bash
./quark file.qk
```

Build executable:

```bash
./quark file.qk --build
```

Build and run:

```bash
./quark file.qk --run
```

---

## Project Status

| Component            | Status      |
| -------------------- | ----------- |
| Lexer                | completed   |
| Parser               | completed   |
| AST                  | completed   |
| Semantic analysis    | completed   |
| IR                   | completed   |
| FASM backend         | completed   |
| Optimizations        | planned     |
| Self-hosted compiler | planned     |

---

## Roadmap

### Short-term

* improved diagnostics
* add generic types

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
