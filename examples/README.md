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

## Примеры оптимизации

- `03_constant_folding.d`: арифметические и логические константы до свёртки.
- `04_unreachable_code.d`: недостижимые команды после `return` и `exit`.
- `optimized/`: эквивалентные исходные программы после оптимизации.
- `*.optimized.sem`: ожидаемые деревья для режима `--optimize`.

Из корня репозитория:

```sh
./dsemantic examples/03_constant_folding.d
./dsemantic --optimize examples/03_constant_folding.d
./dsemantic examples/04_unreachable_code.d
./dsemantic --optimize examples/04_unreachable_code.d
```

Подробный разбор до/после и команды сборки находятся в [README](../README.md).

## Корректная программа и семантическая ошибка

[05_sum_to.d](05_sum_to.d) объявляет функцию суммы чисел от 1 до `limit`,
использует цикл `while` и возвращает результат. Вызов `sum_to(5)` соответствует
сумме `1 + 2 + 3 + 4 + 5 = 15`. Анализатор проверяет программу и выводит её
дерево, но не вычисляет и не печатает число 15.

```sh
./dsemantic examples/05_sum_to.d
./dsemantic --optimize examples/05_sum_to.d
```

[invalid_scope.d](invalid_scope.d) содержит ошибку области видимости:

```text
var calculate := func() is
    var total := 10
    return total
end
print total
```

`total` объявлена внутри функции и недоступна снаружи. Запуск:

```sh
./dsemantic examples/invalid_scope.d
```

Ожидается код завершения `1`, пустой stdout и диагностика в stderr:

```text
examples/invalid_scope.d: semantic error at 5:7: use of undeclared identifier 'total'
```

С `--optimize` программа отклоняется с той же ошибкой: семантическая проверка
выполняется до оптимизаций.
