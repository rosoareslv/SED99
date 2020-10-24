#!/usr/bin/env python
# -*- coding: utf-8 -*-

"""
test_get_config
---------------

Tests formerly known from a unittest residing in test_config.py named
TestGetConfig.test_get_config
TestGetConfig.test_get_config_does_not_exist
TestGetConfig.test_invalid_config
TestGetConfigWithDefaults.test_get_config_with_defaults
"""

import os
import pytest

from cookiecutter import config
from cookiecutter.exceptions import (
    ConfigDoesNotExistException, InvalidConfiguration
)


def test_get_config():
    """
    Opening and reading config file
    """
    conf = config.get_config('tests/test-config/valid-config.yaml')
    expected_conf = {
        'cookiecutters_dir': '/home/example/some-path-to-templates',
        'default_context': {
            'full_name': 'Firstname Lastname',
            'email': 'firstname.lastname@gmail.com',
            'github_username': 'example'
        }
    }
    assert conf == expected_conf


def test_get_config_does_not_exist():
    """
    Check that `exceptions.ConfigDoesNotExistException` is raised when
    attempting to get a non-existent config file.
    """
    with pytest.raises(ConfigDoesNotExistException):
        config.get_config('tests/test-config/this-does-not-exist.yaml')


def test_invalid_config():
    """
    An invalid config file should raise an `InvalidConfiguration` exception.
    """
    with pytest.raises(InvalidConfiguration):
        config.get_config('tests/test-config/invalid-config.yaml')


def test_get_config_with_defaults():
    """
    A config file that overrides 1 of 2 defaults
    """
    conf = config.get_config('tests/test-config/valid-partial-config.yaml')
    default_cookiecutters_dir = os.path.expanduser('~/.cookiecutters/')
    expected_conf = {
        'cookiecutters_dir': default_cookiecutters_dir,
        'default_context': {
            'full_name': 'Firstname Lastname',
            'email': 'firstname.lastname@gmail.com',
            'github_username': 'example'
        }
    }
    assert conf == expected_conf
