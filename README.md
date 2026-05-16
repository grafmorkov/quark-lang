# Quark

Quark is an experimental systems programming language focused on explicit state,
predictable semantics, and transparent memory management.

The compiler currently targets fasm as a backend.

---

## Example

```qk
func main() void {
    value: int = 10;
    mut result: int = value + 2;

    if (value > 5) {
        result = 0;
    } else {
        result = 1;
    }
}
```

---

## Architecture

```text
Source -> AST -> semantic analysis -> tiny IR -> fasm -> native binary
```

### Pipeline

1. Parse source code into AST
2. Run semantic validation passes
3. Generate intermediate representation
4. Generate asm source code
5. Compile with a native C compiler

---

## Features

* minimal hidden behaviour
* tiny compiler(like tcc)
* explicit behaviour
* arena-based compiler memory management

---

## Build

### Requirements

* CMake 3.20+
* Fasm compiler

Clone the repository with submodules:

```bash
git clone --recursive https://github.com/grafmorkov/quark-lang.git
```

Build the compiler:

```bash
git clone https://github.com/grafmorkov/quark-lang.git
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
* add modules and namespaces

### Long-term

* remove ir and make AST -> x86_64 directly
* optimizations(maybe)
* self-hosting compiler

---

## Contributing

Contributions related to compilers, IR design, semantic analysis,
and systems programming are welcome.

---

## License
This project is under the [GPL-3.0](https://www.gnu.org/licenses/gpl-3.0.html) licence.

```