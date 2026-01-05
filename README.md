XT
================================================================================
> **WARNING**: still under heavy development, many things may change in the future!

**XT** is a minimalistic FORTH programming language interpreter.
It is developed as a PET-project exploration of capabilities of stack-based languages.

There are two example files as of right now: basis.f (the standard library) and
quine.f.

## How to use
Run `xt basis.f`. It is specifying basis.f as an argument pre-loads it.
Notice that the interpreter is rather incapable without the standard library
(basis.f). Type in `words` to see
words that exist in the system. Use `$" <filename> include` to
include files, e.g. `$" quine.f" include`.

## How to build
Type `make` to build the language interpreter.
