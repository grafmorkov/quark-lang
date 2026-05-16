# Quark

Quark is an experimental systems programming language focused on explicit state,
predictable semantics, and transparent memory management.

The compiler currently targets C as a backend.

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
Source -> AST -> IR -> semantic analysis -> C -> native binary
```

### Pipeline

1. Parse source code into AST
2. Generate intermediate representation
3. Run semantic validation passes
4. Generate C source code
5. Compile with a native C compiler

---

## Features

* explicit variable initialization
* mutable and immutable bindings
* transparent memory handling
* minimal hidden behavior
* portable C backend
* simple C interoperability
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

Generate C source:

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
| Semantic analysis    | in progress |
| IR                   | in progress |
| C backend            | legacy      |
| Optimizations        | planned     |
| FASM backend         | completed   |
| Self-hosted compiler | planned     |

---

## Roadmap

### Short-term

* improved diagnostics
* add modules and namespaces

### Long-term

* remove ir and make AST -> x86_64
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