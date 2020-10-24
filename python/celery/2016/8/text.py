# -*- coding: utf-8 -*-
"""Text formatting utilities."""
from __future__ import absolute_import, unicode_literals

import re

from collections import Callable
from functools import partial
from textwrap import fill

from pprint import pformat

from celery.five import string_t

__all__ = [
    'abbr', 'abbrtask', 'dedent', 'dedent_initial',
    'ensure_newlines', 'ensure_sep',
    'fill_paragraphs', 'indent', 'join',
    'pluralize', 'pretty', 'str_to_list', 'simple_format', 'truncate',
]

UNKNOWN_SIMPLE_FORMAT_KEY = """
Unknown format %{0} in string {1!r}.
Possible causes: Did you forget to escape the expand sign (use '%%{0!r}'),
or did you escape and the value was expanded twice? (%%N -> %N -> %hostname)?
""".strip()

RE_FORMAT = re.compile(r'%(\w)')


def str_to_list(s):
    if isinstance(s, string_t):
        return s.split(',')
    return s


def dedent_initial(s, n=4):
    return s[n:] if s[:n] == ' ' * n else s


def dedent(s, n=4, sep='\n'):
    return sep.join(dedent_initial(l) for l in s.splitlines())


def fill_paragraphs(s, width, sep='\n'):
    return sep.join(fill(p, width) for p in s.split(sep))


def join(l, sep='\n'):
    return sep.join(v for v in l if v)


def ensure_sep(sep, s, n=2):
    return s + sep * (n - s.count(sep))
ensure_newlines = partial(ensure_sep, '\n')


def abbr(S, max, ellipsis='...'):
    if S is None:
        return '???'
    if len(S) > max:
        return ellipsis and (S[:max - len(ellipsis)] + ellipsis) or S[:max]
    return S


def abbrtask(S, max):
    if S is None:
        return '???'
    if len(S) > max:
        module, _, cls = S.rpartition('.')
        module = abbr(module, max - len(cls) - 3, False)
        return module + '[.]' + cls
    return S


def indent(t, indent=0, sep='\n'):
    """Indent text."""
    return sep.join(' ' * indent + p for p in t.split(sep))


def truncate(s, maxlen=128, suffix='...'):
    """Truncates text to a maximum number of characters."""
    if maxlen and len(s) >= maxlen:
        return s[:maxlen].rsplit(' ', 1)[0] + suffix
    return s


def truncate_bytes(s, maxlen=128, suffix=b'...'):
    if maxlen and len(s) >= maxlen:
        return s[:maxlen].rsplit(b' ', 1)[0] + suffix
    return s


def pluralize(n, text, suffix='s'):
    if n != 1:
        return text + suffix
    return text


def pretty(value, width=80, nl_width=80, sep='\n', **kw):
    if isinstance(value, dict):
        return '{{{0} {1}'.format(sep, pformat(value, 4, nl_width)[1:])
    elif isinstance(value, tuple):
        return '{0}{1}{2}'.format(
            sep, ' ' * 4, pformat(value, width=nl_width, **kw),
        )
    else:
        return pformat(value, width=width, **kw)


def match_case(s, other):
    return s.upper() if other.isupper() else s.lower()


def simple_format(s, keys, pattern=RE_FORMAT, expand=r'\1'):
    if s:
        keys.setdefault('%', '%')

        def resolve(match):
            key = match.expand(expand)
            try:
                resolver = keys[key]
            except KeyError:
                raise ValueError(UNKNOWN_SIMPLE_FORMAT_KEY.format(key, s))
            if isinstance(resolver, Callable):
                return resolver()
            return resolver

        return pattern.sub(resolve, s)
    return s
