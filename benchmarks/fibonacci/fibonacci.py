def fib(x):
    if x <= 1: return x

    return fib(x - 2) + fib(x - 1)

print(fib(35))
