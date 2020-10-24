#!/usr/bin/env python
"""
Python script for building documentation.

To build the docs you must have all optional dependencies for pandas
installed. See the installation instructions for a list of these.

Usage
-----
    $ python make.py clean
    $ python make.py html
    $ python make.py latex
"""
import importlib
import sys
import os
import shutil
import subprocess
import argparse
import webbrowser


DOC_PATH = os.path.dirname(os.path.abspath(__file__))
SOURCE_PATH = os.path.join(DOC_PATH, 'source')
BUILD_PATH = os.path.join(DOC_PATH, 'build')
BUILD_DIRS = ['doctrees', 'html', 'latex', 'plots', '_static', '_templates']


class DocBuilder:
    """
    Class to wrap the different commands of this script.

    All public methods of this class can be called as parameters of the
    script.
    """
    def __init__(self, num_jobs=0, include_api=True, single_doc=None,
                 verbosity=0, warnings_are_errors=False):
        self.num_jobs = num_jobs
        self.verbosity = verbosity
        self.warnings_are_errors = warnings_are_errors

        if single_doc:
            single_doc = self._process_single_doc(single_doc)
            include_api = False
            os.environ['SPHINX_PATTERN'] = single_doc
        elif not include_api:
            os.environ['SPHINX_PATTERN'] = '-api'

        self.single_doc_html = None
        if single_doc and single_doc.endswith('.rst'):
            self.single_doc_html = os.path.splitext(single_doc)[0] + '.html'
        elif single_doc:
            self.single_doc_html = 'api/generated/pandas.{}.html'.format(
                single_doc)

    def _process_single_doc(self, single_doc):
        """
        Make sure the provided value for --single is a path to an existing
        .rst/.ipynb file, or a pandas object that can be imported.

        For example, categorial.rst or pandas.DataFrame.head. For the latter,
        return the corresponding file path
        (e.g. generated/pandas.DataFrame.head.rst).
        """
        base_name, extension = os.path.splitext(single_doc)
        if extension in ('.rst', '.ipynb'):
            if os.path.exists(os.path.join(SOURCE_PATH, single_doc)):
                return single_doc
            else:
                raise FileNotFoundError('File {} not found'.format(single_doc))

        elif single_doc.startswith('pandas.'):
            try:
                obj = pandas  # noqa: F821
                for name in single_doc.split('.'):
                    obj = getattr(obj, name)
            except AttributeError:
                raise ImportError('Could not import {}'.format(single_doc))
            else:
                return single_doc[len('pandas.'):]
        else:
            raise ValueError(('--single={} not understood. Value should be a '
                              'valid path to a .rst or .ipynb file, or a '
                              'valid pandas object (e.g. categorical.rst or '
                              'pandas.DataFrame.head)').format(single_doc))

    @staticmethod
    def _run_os(*args):
        """
        Execute a command as a OS terminal.

        Parameters
        ----------
        *args : list of str
            Command and parameters to be executed

        Examples
        --------
        >>> DocBuilder()._run_os('python', '--version')
        """
        subprocess.check_call(args, stdout=sys.stdout, stderr=sys.stderr)

    def _sphinx_build(self, kind):
        """
        Call sphinx to build documentation.

        Attribute `num_jobs` from the class is used.

        Parameters
        ----------
        kind : {'html', 'latex'}

        Examples
        --------
        >>> DocBuilder(num_jobs=4)._sphinx_build('html')
        """
        if kind not in ('html', 'latex'):
            raise ValueError('kind must be html or latex, '
                             'not {}'.format(kind))

        self.clean()

        cmd = ['sphinx-build', '-b', kind]
        if self.num_jobs:
            cmd += ['-j', str(self.num_jobs)]
        if self.warnings_are_errors:
            cmd += ['-W', '--keep-going']
        if self.verbosity:
            cmd.append('-{}'.format('v' * self.verbosity))
        cmd += ['-d', os.path.join(BUILD_PATH, 'doctrees'),
                SOURCE_PATH, os.path.join(BUILD_PATH, kind)]
        return subprocess.call(cmd)

    def _open_browser(self, single_doc_html):
        """
        Open a browser tab showing single
        """
        url = os.path.join('file://', DOC_PATH, 'build', 'html',
                           single_doc_html)
        webbrowser.open(url, new=2)

    def html(self):
        """
        Build HTML documentation.
        """
        ret_code = self._sphinx_build('html')
        zip_fname = os.path.join(BUILD_PATH, 'html', 'pandas.zip')
        if os.path.exists(zip_fname):
            os.remove(zip_fname)

        if self.single_doc_html is not None:
            self._open_browser(self.single_doc_html)
        return ret_code

    def latex(self, force=False):
        """
        Build PDF documentation.
        """
        if sys.platform == 'win32':
            sys.stderr.write('latex build has not been tested on windows\n')
        else:
            ret_code = self._sphinx_build('latex')
            os.chdir(os.path.join(BUILD_PATH, 'latex'))
            if force:
                for i in range(3):
                    self._run_os('pdflatex',
                                 '-interaction=nonstopmode',
                                 'pandas.tex')
                raise SystemExit('You should check the file '
                                 '"build/latex/pandas.pdf" for problems.')
            else:
                self._run_os('make')
            return ret_code

    def latex_forced(self):
        """
        Build PDF documentation with retries to find missing references.
        """
        return self.latex(force=True)

    @staticmethod
    def clean():
        """
        Clean documentation generated files.
        """
        shutil.rmtree(BUILD_PATH, ignore_errors=True)
        shutil.rmtree(os.path.join(SOURCE_PATH, 'api', 'generated'),
                      ignore_errors=True)

    def zip_html(self):
        """
        Compress HTML documentation into a zip file.
        """
        zip_fname = os.path.join(BUILD_PATH, 'html', 'pandas.zip')
        if os.path.exists(zip_fname):
            os.remove(zip_fname)
        dirname = os.path.join(BUILD_PATH, 'html')
        fnames = os.listdir(dirname)
        os.chdir(dirname)
        self._run_os('zip',
                     zip_fname,
                     '-r',
                     '-q',
                     *fnames)


def main():
    cmds = [method for method in dir(DocBuilder) if not method.startswith('_')]

    argparser = argparse.ArgumentParser(
        description='pandas documentation builder',
        epilog='Commands: {}'.format(','.join(cmds)))
    argparser.add_argument('command',
                           nargs='?',
                           default='html',
                           help='command to run: {}'.format(', '.join(cmds)))
    argparser.add_argument('--num-jobs',
                           type=int,
                           default=0,
                           help='number of jobs used by sphinx-build')
    argparser.add_argument('--no-api',
                           default=False,
                           help='ommit api and autosummary',
                           action='store_true')
    argparser.add_argument('--single',
                           metavar='FILENAME',
                           type=str,
                           default=None,
                           help=('filename of section or method name to '
                                 'compile, e.g. "indexing", "DataFrame.join"'))
    argparser.add_argument('--python-path',
                           type=str,
                           default=os.path.dirname(DOC_PATH),
                           help='path')
    argparser.add_argument('-v', action='count', dest='verbosity', default=0,
                           help=('increase verbosity (can be repeated), '
                                 'passed to the sphinx build command'))
    argparser.add_argument('--warnings-are-errors', '-W',
                           action='store_true',
                           help='fail if warnings are raised')
    args = argparser.parse_args()

    if args.command not in cmds:
        raise ValueError('Unknown command {}. Available options: {}'.format(
            args.command, ', '.join(cmds)))

    # Below we update both os.environ and sys.path. The former is used by
    # external libraries (namely Sphinx) to compile this module and resolve
    # the import of `python_path` correctly. The latter is used to resolve
    # the import within the module, injecting it into the global namespace
    os.environ['PYTHONPATH'] = args.python_path
    sys.path.append(args.python_path)
    globals()['pandas'] = importlib.import_module('pandas')

    # Set the matplotlib backend to the non-interactive Agg backend for all
    # child processes.
    os.environ['MPLBACKEND'] = 'module://matplotlib.backends.backend_agg'

    builder = DocBuilder(args.num_jobs, not args.no_api, args.single,
                         args.verbosity, args.warnings_are_errors)
    return getattr(builder, args.command)()


if __name__ == '__main__':
    sys.exit(main())
