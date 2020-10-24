# -*- coding: utf-8 -*-

from jinja2.exceptions import UndefinedError

from cookiecutter import exceptions


def test_undefined_variable_to_str():
    undefined_var_error = exceptions.UndefinedVariableInTemplate(
        'Beautiful is better than ugly',
        UndefinedError('Errors should never pass silently'),
        {'cookiecutter': {'foo': 'bar'}}
    )

    expected_str = (
        "Beautiful is better than ugly. "
        "Error message: Errors should never pass silently. "
        "Context: {'cookiecutter': {'foo': 'bar'}}"
    )

    assert str(undefined_var_error) == expected_str
