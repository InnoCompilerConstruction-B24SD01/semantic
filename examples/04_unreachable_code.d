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
