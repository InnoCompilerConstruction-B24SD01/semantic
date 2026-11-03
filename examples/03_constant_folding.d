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
