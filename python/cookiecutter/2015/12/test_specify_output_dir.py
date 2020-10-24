# -*- coding: utf-8 -*-

import pytest

from cookiecutter import main


@pytest.fixture
def context():
    """Fixture to return a valid context as known from a cookiecutter.json."""
    return {
        u'cookiecutter': {
            u'email': u'raphael@hackebrot.de',
            u'full_name': u'Raphael Pierzina',
            u'github_username': u'hackebrot',
            u'version': u'0.1.0',
        }
    }


@pytest.fixture
def output_dir(tmpdir):
    return str(tmpdir.mkdir('output'))


@pytest.fixture
def template(tmpdir):
    return str(tmpdir.mkdir('template'))


@pytest.fixture(autouse=True)
def mock_gen_context(mocker, context):
    mocker.patch('cookiecutter.main.generate_context', return_value=context)


@pytest.fixture(autouse=True)
def mock_prompt(mocker):
    mocker.patch('cookiecutter.main.prompt_for_config')


@pytest.fixture(autouse=True)
def mock_replay(mocker):
    mocker.patch('cookiecutter.main.dump')


def test_api_invocation(mocker, template, output_dir, context):
    mock_gen_files = mocker.patch('cookiecutter.main.generate_files')

    main.cookiecutter(template, output_dir=output_dir)

    mock_gen_files.assert_called_once_with(
        repo_dir=template,
        context=context,
        overwrite_if_exists=False,
        output_dir=output_dir
    )


def test_default_output_dir(mocker, template, context):
    mock_gen_files = mocker.patch('cookiecutter.main.generate_files')

    main.cookiecutter(template)

    mock_gen_files.assert_called_once_with(
        repo_dir=template,
        context=context,
        overwrite_if_exists=False,
        output_dir='.'
    )
