function fib(x)
    if x <= 1 then
        return x
    end

    return fib(x - 1) + fib(x - 2)
end

fib(35)
