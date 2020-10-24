#!/usr/bin/env python

import os
import re
import subprocess


def main():
    base_dir = os.getcwd() + os.sep
    docs_dir = os.path.abspath('docs/docsite')
    cmd = ['make', 'singlehtmldocs']

    sphinx = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, cwd=docs_dir)
    stdout, stderr = sphinx.communicate()

    if sphinx.returncode != 0:
        raise subprocess.CalledProcessError(sphinx.returncode, cmd, output=stdout, stderr=stderr)

    with open('docs/docsite/rst_warnings', 'r') as warnings_fd:
        output = warnings_fd.read().strip()
        lines = output.splitlines()

    known_warnings = {
        'block-quote-missing-blank-line': r'^Block quote ends without a blank line; unexpected unindent.$',
        'literal-block-lex-error': r'^Could not lex literal_block as "[^"]*". Highlighting skipped.$',
        'duplicate-label': r'^duplicate label ',
        'undefined-label': r'undefined label: ',
        'unknown-document': r'unknown document: ',
        'toc-tree-missing-document': r'toctree contains reference to nonexisting document ',
        'reference-target-not-found': r'[^ ]* reference target not found: ',
        'not-in-toc-tree': r"document isn't included in any toctree$",
        'unexpected-indentation': r'^Unexpected indentation.$',
        'definition-list-missing-blank-line': r'^Definition list ends without a blank line; unexpected unindent.$',
        'explicit-markup-missing-blank-line': r'Explicit markup ends without a blank line; unexpected unindent.$',
        'toc-tree-glob-pattern-no-match': r"^toctree glob pattern '[^']*' didn't match any documents$",
        'unknown-interpreted-text-role': '^Unknown interpreted text role "[^"]*".$',
    }

    ignore_codes = [
        'literal-block-lex-error',
        'reference-target-not-found',
        'not-in-toc-tree',
    ]

    used_ignore_codes = set()

    for line in lines:
        match = re.search('^(?P<path>[^:]+):((?P<line>[0-9]+):)?((?P<column>[0-9]+):)? (?P<level>WARNING|ERROR): (?P<message>.*)$', line)

        if not match:
            path = 'docs/docsite/rst/index.rst'
            lineno = 0
            column = 0
            code = 'unknown'
            message = line

            # surface unknown lines while filtering out known lines to avoid excessive output
            print('%s:%d:%d: %s: %s' % (path, lineno, column, code, message))
            continue

        path = match.group('path')
        lineno = int(match.group('line') or 0)
        column = int(match.group('column') or 0)
        level = match.group('level').lower()
        message = match.group('message')

        path = os.path.abspath(path)

        if path.startswith(base_dir):
            path = path[len(base_dir):]

        if path.startswith('rst/'):
            path = 'docs/docsite/' + path  # fix up paths reported relative to `docs/docsite/`

        if level == 'warning':
            code = 'warning'

            for label, pattern in known_warnings.items():
                if re.search(pattern, message):
                    code = label
                    break
        else:
            code = 'error'

        if code == 'not-in-toc-tree' and path.startswith('docs/docsite/rst/modules/'):
            continue  # modules are not expected to be in the toc tree

        if code in ignore_codes:
            used_ignore_codes.add(code)
            continue  # ignore these codes

        print('%s:%d:%d: %s: %s' % (path, lineno, column, code, message))

    unused_ignore_codes = set(ignore_codes) - used_ignore_codes

    for code in unused_ignore_codes:
        print('test/sanity/code-smell/docs-build.py:0:0: remove `%s` from the `ignore_codes` list as it is no longer needed' % code)


if __name__ == '__main__':
    main()
