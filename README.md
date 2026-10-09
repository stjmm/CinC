# CinC

A small C(11) compiler written in C.

## Features

- `int`, `long` and `void` types
- Statements
    - if/else
    - for/while/dowhile
    - switch/default/case
    - break/continue
- Operations
    - Arithmetic
    - Bitwise
    - Logical
- Storage classes (static/extern/auto)
- Functions and function calls
- Error reporting from parser and sema
- `-c` `-S` `-o` flags
- `--lex` `--parse` flags for debug tokens and ast printing
- Uses GCC for assembling and linking

The implemented features still probably have bugs, and limitations, eg. switch value can only be an int literal. I will be working to fix those.

## Internals

- An on demand lexer
- A recursive descent parser with Pratt expression parsing
- Semantic analysis to get it as close to C11
- Three address code IR
- X86 emission

## Build and run

```bash
make
./build/cinc [options] <file1 file2 ...>
```

## Tests

Tests are taken from "Writing a C Compiler" test suite.
Tests that should fail have `fail` prefix.
Tests that should pass have their expected return code prefix.

To run tests
```bash
make test # runs all tests
./tests/test_runner.sh [-c -v] # -c 3 specifies chapter, -v shows errors
```
