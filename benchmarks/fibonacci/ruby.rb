def fib(x)
    if x <= 1 then return x end

    return fib(x - 2) + fib(x - 1)
end

fib(35)
