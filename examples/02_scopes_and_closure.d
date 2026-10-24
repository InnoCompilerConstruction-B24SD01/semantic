var base := 10
var makeAdder := func(amount) => func(value) => value + amount + base
print makeAdder(5)(3)
