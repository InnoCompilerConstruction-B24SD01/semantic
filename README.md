# Примеры оптимизаций и ошибок

## 1. Свёртка констант

До — [03_constant_folding.d](examples/03_constant_folding.d):

```text
var total := (5 + 3) * 2
var smaller := 3 < 5

loop
    var a := 1
    var total := 2
    exit
end

var answer := func() is
    var a := 1
    return 42
end
print answer()

print total, smaller, not false, 10 - 4
```

После — [optimized/03_constant_folding.d](examples/optimized/03_constant_folding.d):

```text
var total := 16
var smaller := true

loop
    var a := 1
    var total := 2
    exit
end

var answer := func() is
    var a := 1
    return 42
end
print answer()

print total, smaller, true, 6
```

Оптимизатор заменяет выражения из констант готовыми литералами:
`(5 + 3) * 2` → `16`, `3 < 5` → `true`, `not false` → `true`, `10 - 4` → `6`.
Имена `total` и `smaller` в `print` сохраняются.

Реализация — [src/optimizer.cpp](src/optimizer.cpp):

- `fold_constants()`, строка 72 — проверяет операнды и сворачивает операции над константами.
- `arithmetic()`, строка 42 — вычисляет целочисленные `+`, `-`, `*` с защитой от переполнения.
- `replace_literal()`, строка 31 — заменяет узел операции литералом и удаляет его дочерние узлы.

Показать дерево до оптимизации (команда работает из любой папки):

```sh
/Users/ivan-filatov/vscode_projects/mage_arena/semantic/dsemantic /Users/ivan-filatov/vscode_projects/mage_arena/semantic/examples/03_constant_folding.d
```

Показать дерево после оптимизации:

```sh
/Users/ivan-filatov/vscode_projects/mage_arena/semantic/dsemantic --optimize /Users/ivan-filatov/vscode_projects/mage_arena/semantic/examples/03_constant_folding.d
```

Вместо узлов операций появятся `IntegerLiteral` и `BooleanLiteral`.
Команда выводит дерево программы; исходный код выше показывает смысл преобразования.

## 2. Удаление недостижимого кода

До — [04_unreachable_code.d](examples/04_unreachable_code.d):

```text
var answer := func() is
    print 'before return'
    return 42
    print 'after return'
end
print answer()

loop
    print 'before exit'
    exit
    print 'after exit'
end
print 'done'
```

После — [optimized/04_unreachable_code.d](examples/optimized/04_unreachable_code.d):

```text
var answer := func() is
    print 'before return'
    return 42
end
print answer()

loop
    print 'before exit'
    exit
end
print 'done'
```

После `return` функция завершается, поэтому `print 'after return'` недостижим.
После `exit` выполнение выходит из цикла, поэтому `print 'after exit'` тоже
недостижим. Эти команды удаляются из дерева. Вызов `answer()` и команда
`print 'done'` остаются: они находятся за пределами соответствующих блоков.

Показать дерево до оптимизации:

```sh
/Users/ivan-filatov/vscode_projects/mage_arena/semantic/dsemantic /Users/ivan-filatov/vscode_projects/mage_arena/semantic/examples/04_unreachable_code.d
```

Показать дерево после оптимизации:

```sh
/Users/ivan-filatov/vscode_projects/mage_arena/semantic/dsemantic --optimize /Users/ivan-filatov/vscode_projects/mage_arena/semantic/examples/04_unreachable_code.d
```

Во втором выводе исчезнут узлы печати `after return` и `after exit`.

Реализация — `optimize_node()` в [src/optimizer.cpp](src/optimizer.cpp), строка 123.
При обнаружении прямого `ReturnStatement` или `ExitStatement` внутри `Block`
вызов `node.children.resize(i + 1)` на строке 131 удаляет все следующие команды
этого блока. Команды внешнего блока не затрагиваются.

Обе оптимизации запускает `Optimizer::optimize()` в том же файле, строка 141:
сначала проверяет всю программу через `SemanticAnalyzer`, затем копирует AST
и вызывает `optimize_node()`. Обход идёт от детей к родителю, поэтому вложенные
константные выражения сворачиваются последовательно.

## 3. Ошибка: необъявленная переменная

[invalid_undeclared.d](examples/invalid_undeclared.d):

```text
var known := 1
print unknown
```

Объявлена переменная `known`, но в `print` используется другое имя — `unknown`.
Анализатор не находит его объявления.

Запуск:

```sh
/Users/ivan-filatov/vscode_projects/mage_arena/semantic/dsemantic /Users/ivan-filatov/vscode_projects/mage_arena/semantic/examples/invalid_undeclared.d
```

Диагностика после пути к файлу:

```text
semantic error at 2:7: use of undeclared identifier 'unknown'
```

Код завершения — `1`; дерево не выводится.

## 4. Ошибка: переменная вне области видимости

[invalid_scope.d](examples/invalid_scope.d):

```text
var calculate := func() is
    var total := 10
    return total
end
print total
```

`total` объявлена внутри функции `calculate`. В `return total` она доступна,
но в `print total` после функции — уже нет.

Запуск:

```sh
/Users/ivan-filatov/vscode_projects/mage_arena/semantic/dsemantic /Users/ivan-filatov/vscode_projects/mage_arena/semantic/examples/invalid_scope.d
```

Диагностика после пути к файлу:

```text
semantic error at 5:7: use of undeclared identifier 'total'
```

Код завершения — `1`; дерево не выводится. С оптимизациями ошибка сохраняется,
потому что вся программа проверяется до изменения AST:

```sh
/Users/ivan-filatov/vscode_projects/mage_arena/semantic/dsemantic --optimize /Users/ivan-filatov/vscode_projects/mage_arena/semantic/examples/invalid_scope.d
```

## 5. Корректная программа: сумма чисел

[05_sum_to.d](examples/05_sum_to.d):

```text
var sum_to := func(limit) is
    var total := 0
    var i := 1
    while i <= limit loop
        total := total + i
        i := i + 1
    end
    return total
end

print sum_to(5)
```

Функция `sum_to` суммирует целые числа от `1` до `limit` включительно.
Для вызова `sum_to(5)` сумма равна `15`. Пример демонстрирует параметр функции,
локальные переменные, цикл `while`, присваивание, `return` и вызов функции.
Все имена объявлены до использования, а `return` находится внутри функции.

Полная команда запуска из любой папки:

```sh
/Users/ivan-filatov/vscode_projects/mage_arena/semantic/dsemantic /Users/ivan-filatov/vscode_projects/mage_arena/semantic/examples/05_sum_to.d
```

Ожидается код завершения `0` и дерево программы с типами и ID объявлений.
Анализатор не выполняет программу, поэтому число `15` не печатается.
