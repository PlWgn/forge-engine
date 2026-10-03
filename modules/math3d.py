"""Small game-level math helpers; tuples match the native vector API."""
from math import sqrt

def add(a, b): return tuple(x + y for x, y in zip(a, b))
def mul(a, factor): return tuple(x * factor for x in a)
def normalize(a):
    length = sqrt(sum(x*x for x in a))
    return mul(a, 1 / length) if length else (0, 0, 0)
