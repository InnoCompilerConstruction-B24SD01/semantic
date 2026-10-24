# Semantic analyzer examples

The `.d` files without the `invalid_` prefix are valid Project D programs. Their
matching `.sem` files contain the exact annotated syntax tree printed by
`dsemantic`. Compare one with:

```sh
./dsemantic examples/01_types_and_bindings.d |
    diff -u examples/01_types_and_bindings.sem -
```

`invalid_undeclared.d` demonstrates a semantic error: the file is lexically and
syntactically valid, but `unknown` has no visible declaration.
