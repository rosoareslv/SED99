#!/usr/bin/env python
# -*- coding: utf-8 -*-

"""
cookiecutter.cli
-----------------

Main `cookiecutter` CLI.
"""

import os
import sys
import logging
import json

import click

from cookiecutter import __version__
from cookiecutter.config import USER_CONFIG_PATH
from cookiecutter.main import cookiecutter
from cookiecutter.exceptions import (
    OutputDirExistsException,
    InvalidModeException,
    FailedHookException,
    UndefinedVariableInTemplate
)

logger = logging.getLogger(__name__)


def version_msg():
    python_version = sys.version[:3]
    location = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    message = u'Cookiecutter %(version)s from {} (Python {})'
    return message.format(location, python_version)


@click.command(context_settings=dict(help_option_names=[u'-h', u'--help']))
@click.version_option(__version__, u'-V', u'--version', message=version_msg())
@click.argument(u'template')
@click.option(
    u'--no-input', is_flag=True,
    help=u'Do not prompt for parameters and only use cookiecutter.json '
         u'file content',
)
@click.option(
    u'-c', u'--checkout',
    help=u'branch, tag or commit to checkout after git clone',
)
@click.option(
    '-v', '--verbose',
    is_flag=True, help='Print debug information', default=False
)
@click.option(
    u'--replay', is_flag=True,
    help=u'Do not prompt for parameters and only use information entered '
         u'previously',
)
@click.option(
    u'-f', u'--overwrite-if-exists', is_flag=True,
    help=u'Overwrite the contents of the output directory if it already exists'
)
@click.option(
    u'-o', u'--output-dir', default='.', type=click.Path(),
    help=u'Where to output the generated project dir into'
)
@click.option(
    u'--config-file', type=click.Path(), default=USER_CONFIG_PATH,
    help=u'User configuration file'
)
@click.option(
    u'--default-config', is_flag=True,
    help=u'Do not load a config file. Use the defaults instead'
)
def main(template, no_input, checkout, verbose, replay, overwrite_if_exists,
         output_dir, config_file, default_config):
    """Create a project from a Cookiecutter project template (TEMPLATE)."""
    if verbose:
        logging.basicConfig(
            format=u'%(levelname)s %(filename)s: %(message)s',
            level=logging.DEBUG
        )
    else:
        # Log info and above to console
        logging.basicConfig(
            format=u'%(levelname)s: %(message)s',
            level=logging.INFO
        )

    try:
        # If you _need_ to support a local template in a directory
        # called 'help', use a qualified path to the directory.
        if template == u'help':
            click.echo(click.get_current_context().get_help())
            sys.exit(0)

        user_config = None if default_config else config_file

        cookiecutter(
            template, checkout, no_input,
            replay=replay,
            overwrite_if_exists=overwrite_if_exists,
            output_dir=output_dir,
            config_file=user_config
        )
    except (OutputDirExistsException,
            InvalidModeException,
            FailedHookException) as e:
        click.echo(e)
        sys.exit(1)
    except UndefinedVariableInTemplate as undefined_err:
        click.echo('{}'.format(undefined_err.message))
        click.echo('Error message: {}'.format(undefined_err.error.message))

        context_str = json.dumps(
            undefined_err.context,
            indent=4,
            sort_keys=True
        )
        click.echo('Context: {}'.format(context_str))
        sys.exit(1)


if __name__ == "__main__":
    main()
