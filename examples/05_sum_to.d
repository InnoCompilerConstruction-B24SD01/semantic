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
