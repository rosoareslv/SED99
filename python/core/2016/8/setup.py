#!/usr/bin/env python3
import os
from setuptools import setup, find_packages
from homeassistant.const import __version__

PACKAGE_NAME = 'homeassistant'
HERE = os.path.abspath(os.path.dirname(__file__))
DOWNLOAD_URL = ('https://github.com/home-assistant/home-assistant/archive/'
                '{}.zip'.format(__version__))

PACKAGES = find_packages(exclude=['tests', 'tests.*'])

REQUIRES = [
    'requests>=2,<3',
    'pyyaml>=3.11,<4',
    'pytz>=2016.6.1',
    'pip>=7.0.0',
    'jinja2>=2.8',
    'voluptuous==0.9.2',
    'typing>=3,<4',
]

setup(
    name=PACKAGE_NAME,
    version=__version__,
    license='MIT License',
    url='https://home-assistant.io/',
    download_url=DOWNLOAD_URL,
    author='Home Assistant',
    author_email='hello@home-assistant.io',
    description='Open-source home automation platform running on Python 3.',
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
    classifiers=[
        'Intended Audience :: End Users/Desktop',
        'Intended Audience :: Developers',
        'License :: OSI Approved :: MIT License',
        'Operating System :: OS Independent',
        'Programming Language :: Python :: 3.4',
        'Topic :: Home Automation'
    ],
)
