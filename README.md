# CinC

A small C(11) compiler written in C.

## Features

- `int` and `void` types
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
- Uses GCC for assembling and linking

The implemented features still probably have bugs, and limitations, eg. switch value can only be an int literal. I will be working to fix those.

## Build and run

```sh
make
./build/cinc [options] <file1 file2 ...>
```

## Tests

Tests are taken from "Writing a C Compiler" test suite.
Tests that should fail have `fail` prefix.
Tests that should pass have their expected return code prefix.
