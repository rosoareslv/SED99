#!/usr/bin/env python3
"""Home Assistant setup script."""
import os
from setuptools import setup, find_packages

from homeassistant.const import __version__

PROJECT_NAME = 'Home Assistant'
PROJECT_PACKAGE_NAME = 'homeassistant'
PROJECT_LICENSE = 'Apache License 2.0'
PROJECT_AUTHOR = 'The Home Assistant Authors'
PROJECT_COPYRIGHT = ' 2013-2017, {}'.format(PROJECT_AUTHOR)
PROJECT_URL = 'https://home-assistant.io/'
PROJECT_EMAIL = 'hello@home-assistant.io'
PROJECT_DESCRIPTION = ('Open-source home automation platform '
                       'running on Python 3.')
PROJECT_LONG_DESCRIPTION = ('Home Assistant is an open-source '
                            'home automation platform running on Python 3. '
                            'Track and control all devices at home and '
                            'automate control. '
                            'Installation in less than a minute.')
PROJECT_CLASSIFIERS = [
    'Development Status :: 4 - Beta',
    'Intended Audience :: End Users/Desktop',
    'Intended Audience :: Developers',
    'License :: OSI Approved :: Apache Software License',
    'Operating System :: OS Independent',
    'Programming Language :: Python :: 3.4',
    'Programming Language :: Python :: 3.5',
    'Programming Language :: Python :: 3.6',
    'Topic :: Home Automation'
]

PROJECT_GITHUB_USERNAME = 'home-assistant'
PROJECT_GITHUB_REPOSITORY = 'home-assistant'

PYPI_URL = 'https://pypi.python.org/pypi/{}'.format(PROJECT_PACKAGE_NAME)
GITHUB_PATH = '{}/{}'.format(
    PROJECT_GITHUB_USERNAME, PROJECT_GITHUB_REPOSITORY)
GITHUB_URL = 'https://github.com/{}'.format(GITHUB_PATH)


HERE = os.path.abspath(os.path.dirname(__file__))
DOWNLOAD_URL = '{}/archive/{}.zip'.format(GITHUB_URL, __version__)

PACKAGES = find_packages(exclude=['tests', 'tests.*'])

REQUIRES = [
    'requests==2.18.4',
    'pyyaml>=3.11,<4',
    'pytz>=2017.02',
    'pip>=8.0.3',
    'jinja2>=2.9.6',
    'voluptuous==0.10.5',
    'typing>=3,<4',
    'aiohttp==2.3.7',   # If updated, check if yarl also needs an update!
    'yarl==0.16.0',
    'async_timeout==2.0.0',
    'chardet==3.0.4',
    'astral==1.4',
    'certifi>=2017.4.17',
]

setup(
    name=PROJECT_PACKAGE_NAME,
    version=__version__,
    license=PROJECT_LICENSE,
    url=PROJECT_URL,
    download_url=DOWNLOAD_URL,
    author=PROJECT_AUTHOR,
    author_email=PROJECT_EMAIL,
    description=PROJECT_DESCRIPTION,
    packages=PACKAGES,
    include_package_data=True,
    zip_safe=False,
    platforms='any',
    install_requires=REQUIRES,
    test_suite='tests',
    keywords=['home', 'automation'],
    entry_points={
        'console_scripts': [
            'hass = homeassistant.__main__:main'
        ]
    },
    classifiers=PROJECT_CLASSIFIERS,
)
