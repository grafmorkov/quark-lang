# Contributing to Quant

Contributions are welcome. You can contribute to the compiler, standard library, tests, documentation, tools, or build system.

## Development Rules

* Read the existing implementation before changing it.
* Keep changes small and focused.
* Preserve the existing architecture and compiler invariants.
* Avoid unnecessary abstractions and unrelated refactors.
* Add tests for new behavior and bug fixes.
* Update documentation when user-visible behavior changes.
* Update `AI_CONTEXT.md` when compiler semantics, architecture, or important invariants change.

Compiler pipeline:

```text
Modules → Lexer → Parser → AST → Semantic → IR → Backend → ELF / PE32+
```

## Language Changes

For syntax or semantic changes:

1. Add the implementation.
2. Add tests.
3. Update `Doc.md`.
4. Update `AI_CONTEXT.md` when necessary.

Do not copy features or behavior from other languages without considering whether they fit Quant.

## Testing

New features should include tests.

Bug fixes should normally include a regression test.

Tests ending in `_err.qu` are expected to fail compilation and are used for error testing.

## AI

AI tools may be used for development, testing, documentation, analysis, and code generation.

AI-generated code must be understood, reviewed, built, and tested before submission.

Use `AI_CONTEXT.md` as the main context for AI tools working with Quant.

## Pull Requests

Before submitting a PR:

* build the project;
* run tests;
* add tests for new behavior;
* update documentation when needed;
* avoid unrelated changes.

Describe what changed, why, and how it was tested.

## Commits

Use:

```text
<type>: <short description>
```

Examples:

```text
feat: add enum parsing
fix: fix struct return
docs: update reference semantics
```

## Bug Reports

Include:

* Quant version or commit;
* minimal reproduction;
* expected behavior;
* actual behavior;
* compiler output when relevant.

## Feature Requests

Describe the problem, proposed behavior, example code, and why existing features are insufficient.

Do not add features solely because another language has them.
