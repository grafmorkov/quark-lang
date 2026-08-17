## 1. Contributing to Quant

Quant is an open-source project, and contributions are welcome.

You can contribute to:

- compiler;
- standard library;
- documentation;
- tests;
- tools;
- build system.

You can work on the frontend, backend, semantic analysis, or other parts of the compiler. You can also suggest different design approaches.

The main long-term goal is to make Quant self-hosted. Because of this, contributions of many kinds can be useful.

---

## 2. Development Setup

### Requirements

- CMake 3.20+
- C++20 compiler
- Git

### Build

```bash
git clone https://github.com/quant-lang/quant.git
cd quant

cmake -B build
cmake --build build # You can also use -DCMAKE_BUILD_TYPE=Release for optimizations
````

### Run

```bash
./qu file.qu -o program
```

### Tests

```bash
./scripts/check_tests.sh
# or
./scripts/check_tests.ps1
```

---

## 3. Project Structure

```text
src/frontend/   Lexer, Parser, AST
src/semantic/   Semantic analysis
src/ir/         IR generation
src/backend/    Native backend
src/modules/    Module loading
src/support/    Type system and shared compiler utilities
src/utils/      Utility code
include/        Header files
std/            Standard library
scripts/        Build and test scripts
tests/          Tests
AI_CONTEXT.md   AI context for LLMs
```

---

## 4. Development Workflow

Before starting work:

1. Read the existing implementation.
2. Find which part of the compiler needs to be changed.
3. Check existing tests and documentation.
4. Make the smallest change needed for the task.

After making changes:

1. Build the project.
2. Run the tests.
3. Check the changed behavior.
4. Review the diff.
5. Update the documentation if the change affects user-visible behavior.
6. Update `AI_CONTEXT.md` when useful, especially when compiler semantics or important invariants change.

Do not mix unrelated changes into the same pull request.

---

## 5. Compiler Development

Quant uses the following compiler pipeline:

```text
Loading Modules
  ↓
Source
  ↓
Lexer
  ↓
Parser
  ↓
AST
  ↓
Semantic Analysis
  ↓
IR
  ↓
Native Backend
  ↓
ELF / PE32+
```

When changing the compiler, keep the existing architecture and compiler invariants in mind.

### Main Principles

* Keep the architecture simple.
* Do not add abstractions without a clear reason.
* Read the existing implementation before changing it.
* Fix problems at the compiler stage where they actually belong.
* Do not change unrelated parts of the compiler.
* Preserve existing behavior unless changing the language semantics is the goal.

---

## 6. Changing the Language

When a change affects Quant syntax or semantics:

1. Check the current compiler behavior.
2. Make sure the change fits the existing type system and language semantics.
3. Add tests.
4. Update `Doc.md`.
5. Update `AI_CONTEXT.md` when needed.

Do not automatically copy features or behavior from other languages into Quant.

For example, Rust, C++, Zig, or another language having a feature does not mean that Quant should have the same behavior.

---

## 7. Testing

Every change should be tested.

New features should include tests for the new behavior.

Bug fixes should normally include a regression test that reproduces the bug.

Depending on the change, tests may cover:

* lexer;
* parser;
* semantic analysis;
* type checking;
* generics;
* pointers/references;
* regions;
* attributes;
* IR;
* native backend;
* standard library.

You can also use LLMs to generate tests.

## NOTE

> Some tests are expected to fail. These tests are used to verify compiler error reporting and are part of the `error_testing` system.
>
> If you create a test that is expected to produce a compiler error, add the `_err` suffix to its filename.
>
> Example:
>
> `attr_unknown_err.qu`

---

## 8. Code Style

Follow the existing style of the project.

### General Rules

* Use clear names.
* Do not add unnecessary abstractions.
* Do not make simple code more complicated without a reason.
* Do not make large refactors together with a feature or bug fix.
* Follow the existing project structure.
* Comments should explain important decisions, not obvious code.

---

## 9. Documentation

Documentation should be updated together with language changes.

### `Doc.md`

Update `Doc.md` when changing:

* syntax;
* semantics;
* types;
* attributes;
* standard library APIs;
* compiler behavior visible to users.

### `AI_CONTEXT.md`

`AI_CONTEXT.md` is used as context for AI tools working with Quant.

Update it when changing:

* important compiler invariants;
* language semantics;
* compiler architecture;
* rules that AI tools may misunderstand;
* known common mistakes.

---

## 10. Working with AI

AI tools are allowed and can be used for:

* code analysis;
* finding bugs;
* generating tests;
* writing documentation;
* architecture analysis;
* finding edge cases;
* implementation suggestions;
* code review;
* code generation.

When using AI with Quant, use `AI_CONTEXT.md` as the main context for the language semantics and compiler architecture.

### AI-generated Code

AI-generated code must be reviewed by a human before it is added to the project.

Contributors must understand the code they submit and should be able to explain:

* what it does;
* why it is needed;
* why it works;
* how it fits into the existing architecture;
* which tests verify its behavior.

AI-generated code should be:

* reviewed;
* compiled;
* tested;
* checked in the diff;
* manually checked for edge cases when needed.

### Vibe Coding

Uncontrolled AI code generation without understanding, testing, and code review is not allowed.

Do not:

* make architectural decisions only because an AI suggested them;
* add large amounts of unreviewed AI-generated code;
* create a pull request with code you do not understand;
* treat successful compilation as enough testing.

AI is a development tool, not a replacement for understanding, testing, or code review.

---

## 11. Pull Requests

Before creating a pull request, make sure that:

* the project builds;
* tests pass;
* new behavior is tested;
* documentation is updated when needed;
* there are no unrelated changes.

A pull request should contain:

### What Changed

[Briefly describe what changed.]

### Why

[Explain the problem or reason for the change.]

### What Was Changed

[Lexer / Parser / Semantic / IR / Backend / Stdlib / Docs / Tests.]

### Tests

[Describe which tests were added or run.]

---

## 12. Commit Messages

Use the following format:

```text
<type>: <short description>
```

Examples:

```text
core: add enum parsing
fix: fix struct return
feat: add new operator: %
docs: update reference semantics
```

You can also suggest and use other commit types when appropriate.

---

## 13. Reporting Bugs

When reporting a bug, include:

* Quant version or commit;
* operating system;
* a minimal example that reproduces the problem;
* expected behavior;
* actual behavior;
* compiler error output;
* generated IR or assembly when relevant.

Smaller reproducible examples are better.

---

## 14. Feature Requests

When proposing a new language feature, describe:

* what problem the feature solves;
* the proposed behavior;
* an example of Quant code;
* why the existing language features are not enough;
* possible effects on language semantics;
* possible changes to the compiler pipeline.

Do not add a feature only because another language already has it.

---

## 15. License

Quant is licensed under the GPL-3.0 license.
