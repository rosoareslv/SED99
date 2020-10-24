"""Various algorithms for helping identifying numbers and sequences."""
from __future__ import print_function, division

from sympy.utilities import public

from sympy.core.compatibility import range
from sympy.core import Function, Symbol
from sympy.core.numbers import Zero
from sympy import sympify, floor, sqrt, Rational
from mpmath import pslq, sqrt, mp

@public
def find_simple_recurrence_vector(v, maxcoeff=1024):
    """
    This function is used internally by other functions from the
    sympy.concrete.guess module. While most users may want to rather use the
    function find_simple_recurrence when looking for recurrence relations
    among rational numbers, the current function may still be useful when
    some post-processing has to be done.

    The function returns a vector of length n when a recurrence relation of
    order n is detected in the sequence of rational numbers v.

    If the returned vector has a length 1, then the returned value is always
    the list [0], which means that no relation has been found.

    While the functions is intended to be used with rational numbers, it should
    work for other kinds of real numbers except for some cases involving
    quadratic numbers; for that reason it should be used with some caution when
    the argument is not a list of rational numbers.

    Examples
    ========

    >>> from sympy.concrete.guess import find_simple_recurrence_vector
    >>> from sympy import fibonacci
    >>> find_simple_recurrence_vector([fibonacci(k) for k in range(12)])
    [1, -1, -1]

    See also
    ========

    See the function sympy.concrete.guess.find_simple_recurrence which is more
    user-friendly.

    """
    l = len(v)>>1

    previous = mp.prec # save current precision
    mp.prec = 128
    b = [sum(sqrt((l>>1)**2 + k)*v[-1-k-i] for k in range(l))
          for i in range(l)]
    p = pslq(b,
             maxcoeff = maxcoeff,
             maxsteps = 128 + 4*l)
    mp.prec = previous # restore current precision
    if p == None: return [0]

    first, last = 0, l-1
    while p[first]==0: first += 1
    while p[last]==0:
        last -= 1
        if first == last: return [0] # TODO: probably never occuring
    return p[first:last+1]

@public
def find_simple_recurrence(v, A=Function('a'), N=Symbol('n'), maxcoeff=1024):
    """
    Detects and returns a recurrence relation from a sequence of several integer
    (or rational) terms. The name of the function in the returned expression is
    'a' by default; the main variable is 'n' by default. The smallest index in
    the returned expression is always n (and never n-1, n-2, etc.).

    Examples
    ========

    >>> from sympy.concrete.guess import find_simple_recurrence
    >>> from sympy import fibonacci
    >>> find_simple_recurrence([fibonacci(k) for k in range(12)])
    -a(n) - a(n + 1) + a(n + 2)

    >>> from sympy import Function, Symbol
    >>> a = [1, 1, 1]
    >>> for k in range(15): a.append(5*a[-1]-3*a[-2]+8*a[-3])
    >>> find_simple_recurrence(a, A=Function('f'), N=Symbol('i'))
    -8*f(i) + 3*f(i + 1) - 5*f(i + 2) + f(i + 3)

    """
    p = find_simple_recurrence_vector(v, maxcoeff=maxcoeff)
    n = len(p)
    if n <= 1: return Zero()

    rel = Zero()
    for k in range(n):
        rel += A(N+n-1-k)*p[k]

    return rel


@public
def rationalize(x, maxcoeff=10000):
    """
    Helps identifying a rational number from a float (or mpmath.mpf) value by
    using a continued fraction. The algorithm stops as soon as a large partial
    quotient is detected (greater than 10000 by default).

    Examples
    ========

    >>> from sympy.concrete.guess import rationalize
    >>> from mpmath import cos, pi
    >>> rationalize(cos(pi/3))
    1/2

    >>> from mpmath import mpf
    >>> rationalize(mpf("0.333333333333333"))
    1/3

    While the function is rather intended to help 'identifying' rational
    values, it may be used in some cases for approximating real numbers.
    (Though other functions may be more relevant in that case.)

    >>> rationalize(pi, maxcoeff = 250)
    355/113

    See also
    ========
    Several other methods can approximate a real number as a rational, like:

      * fractions.Fraction.from_decimal
      * fractions.Fraction.from_float
      * mpmath.identify
      * mpmath.pslq by using the following syntax: mpmath.pslq([x, 1])
      * mpmath.findpoly by using the following syntax: mpmath.findpoly(x, 1)
      * sympy.simplify.nsimplify (which is a more general function)

    The main difference between the current function and all these variants is
    that control focuses on magnitude of partial quotients here rather than on
    global precision of the approximation. If the real is "known to be" a
    rational number, the current function should be able to detect it correctly
    with the default settings even when denominator is great (unless its
    expansion contains unusually big partial quotients) which may occur
    when studying sequences of increasing numbers. If the user cares more
    on getting simple fractions, other methods may be more convenient.

    """
    p0, p1 = 0, 1
    q0, q1 = 1, 0
    a = floor(x)
    while a < maxcoeff or q1==0:
        p = a*p1 + p0
        q = a*q1 + q0
        p0, p1 = p1, p
        q0, q1 = q1, q
        if x==a: break
        x = 1/(x-a)
        a = floor(x)
    return sympify(p) / q


@public
def guess_generating_function_rational(v, X=Symbol('x'), maxcoeff=1024):
    """
    Tries to "guess" a rational generating function for a sequence of rational
    numbers v.

    Examples
    ========

    >>> from sympy.concrete.guess import guess_generating_function_rational
    >>> from sympy import fibonacci
    >>> l = [fibonacci(k) for k in range(5,15)]
    >>> guess_generating_function_rational(l)
    (3*x + 5)/(-x**2 - x + 1)

    """
    #   a) compute the denominator as q
    q = find_simple_recurrence_vector(v, maxcoeff=maxcoeff)
    n = len(q)
    if n <= 1: return None
    #   b) compute the numerator as p
    p = [sum(v[i-k]*q[k] for k in range(min(i+1, n)))
            for i in range(len(v))] # TODO: maybe better with:  len(v)>>1
    return (sum(p[k]*X**k for k in range(len(p)))
            / sum(q[k]*X**k for k in range(n)))


@public
def guess_generating_function(v, X=Symbol('x'), maxcoeff=1024, maxsqrtn=2):
    """
    Tries to "guess" a generating function for a sequence of rational numbers v.
    Only a few patterns are implemented yet.

    Examples
    ========

    >>> from sympy.concrete.guess import guess_generating_function as ggf
    >>> from sympy import fibonacci
    >>> ggf([fibonacci(k) for k in range(5, 15)])
    (3*x + 5)/(-x**2 - x + 1)

    >>> from sympy import sympify
    >>> l = sympify("[3/2, 11/2, 0, -121/2, -363/2, 121]")
    >>> ggf(l)
    (x + 3/2)/(11*x**2 - 3*x + 1)

    N-th root of a rational function can also be detected (below is an example
    coming from the sequence A108626 from http://oeis.org ).
    The greatest n-th root to be tested is specified as maxsqrtn (default 2).

    >>> ggf([1, 2, 5, 14, 41, 124, 383, 1200, 3799, 12122, 38919])
    sqrt(1/(x**4 + 2*x**2 - 4*x + 1))

    References
    ==========
    "Concrete Mathematics", R.L. Graham, D.E. Knuth, O. Patashnik

    """
    # Perform some convolutions of the sequence with itself
    t = [1 if k==0 else 0 for k in range(len(v))]
    for d in range(max(1, maxsqrtn)):
      t = [sum(t[n-i]*v[i] for i in range(n+1)) for n in range(len(v))]
      g = guess_generating_function_rational(t, X=X, maxcoeff=maxcoeff)
      if g: return g**Rational(1, d+1)
