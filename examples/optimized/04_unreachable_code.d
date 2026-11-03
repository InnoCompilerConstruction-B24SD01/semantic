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
