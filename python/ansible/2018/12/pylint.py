"""Sanity test using pylint."""
from __future__ import absolute_import, print_function

import collections
import json
import os
import datetime

from lib.sanity import (
    SanitySingleVersion,
    SanityMessage,
    SanityFailure,
    SanitySuccess,
    SanitySkipped,
)

from lib.util import (
    SubprocessError,
    run_command,
    display,
    read_lines_without_comments,
    ConfigParser,
)

from lib.executor import (
    SUPPORTED_PYTHON_VERSIONS,
)

from lib.ansible_util import (
    ansible_environment,
)

from lib.config import (
    SanityConfig,
)

from lib.test import (
    calculate_confidence,
    calculate_best_confidence,
)

PYLINT_SKIP_PATH = 'test/sanity/pylint/skip.txt'
PYLINT_IGNORE_PATH = 'test/sanity/pylint/ignore.txt'

UNSUPPORTED_PYTHON_VERSIONS = (
    '2.6',
    '2.7',
)


class PylintTest(SanitySingleVersion):
    """Sanity test using pylint."""
    def __init__(self):
        super(PylintTest, self).__init__()

        self.plugin_dir = 'test/sanity/pylint/plugins'
        self.plugin_names = sorted(p[0] for p in [os.path.splitext(p) for p in os.listdir(self.plugin_dir)] if p[1] == '.py' and p[0] != '__init__')

    def test(self, args, targets):
        """
        :type args: SanityConfig
        :type targets: SanityTargets
        :rtype: TestResult
        """
        if args.python_version in UNSUPPORTED_PYTHON_VERSIONS:
            display.warning('Skipping pylint on unsupported Python version %s.' % args.python_version)
            return SanitySkipped(self.name)

        skip_paths = read_lines_without_comments(PYLINT_SKIP_PATH)

        invalid_ignores = []

        supported_versions = set(SUPPORTED_PYTHON_VERSIONS) - set(UNSUPPORTED_PYTHON_VERSIONS)
        supported_versions = set([v.split('.')[0] for v in supported_versions]) | supported_versions

        ignore_entries = read_lines_without_comments(PYLINT_IGNORE_PATH)
        ignore = collections.defaultdict(dict)
        line = 0

        for ignore_entry in ignore_entries:
            line += 1

            if not ignore_entry:
                continue

            if ' ' not in ignore_entry:
                invalid_ignores.append((line, 'Invalid syntax'))
                continue

            path, code = ignore_entry.split(' ', 1)

            if not os.path.exists(path):
                invalid_ignores.append((line, 'Remove "%s" since it does not exist' % path))
                continue

            if ' ' in code:
                code, version = code.split(' ', 1)

                if version not in supported_versions:
                    invalid_ignores.append((line, 'Invalid version: %s' % version))
                    continue

                if version not in (args.python_version, args.python_version.split('.')[0]):
                    continue  # ignore version specific entries for other versions

            ignore[path][code] = line

        skip_paths_set = set(skip_paths)

        paths = sorted(i.path for i in targets.include if (os.path.splitext(i.path)[1] == '.py' or i.path.startswith('bin/')) and i.path not in skip_paths_set)

        module_paths = [p.split(os.path.sep) for p in paths if p.startswith('lib/ansible/modules/')]
        module_dirs = sorted(set([p[3] for p in module_paths if len(p) > 4]))

        contexts = []
        remaining_paths = set(paths)

        def add_context(available_paths, context_name, context_filter):
            """
            :type available_paths: set[str]
            :type context_name: str
            :type context_filter: (str) -> bool
            """
            filtered_paths = set(p for p in available_paths if context_filter(p))
            contexts.append((context_name, sorted(filtered_paths)))
            available_paths -= filtered_paths

        def filter_path(path_filter=None):
            """
            :type path_filter: str
            :rtype: (str) -> bool
            """
            def context_filter(path_to_filter):
                """
                :type path_to_filter: str
                :rtype: bool
                """
                return path_to_filter.startswith(path_filter)

            return context_filter

        add_context(remaining_paths, 'ansible-test', filter_path('test/runner/'))
        add_context(remaining_paths, 'units', filter_path('test/units/'))
        add_context(remaining_paths, 'test', filter_path('test/'))
        add_context(remaining_paths, 'hacking', filter_path('hacking/'))

        for module_dir in module_dirs:
            add_context(remaining_paths, 'modules/%s' % module_dir, filter_path('lib/ansible/modules/%s/' % module_dir))

        add_context(remaining_paths, 'modules', filter_path('lib/ansible/modules/'))
        add_context(remaining_paths, 'module_utils', filter_path('lib/ansible/module_utils/'))
        add_context(remaining_paths, 'ansible', lambda p: True)

        messages = []
        context_times = []

        test_start = datetime.datetime.utcnow()

        for context, context_paths in sorted(contexts):
            if not context_paths:
                continue

            context_start = datetime.datetime.utcnow()
            messages += self.pylint(args, context, context_paths)
            context_end = datetime.datetime.utcnow()

            context_times.append('%s: %d (%s)' % (context, len(context_paths), context_end - context_start))

        test_end = datetime.datetime.utcnow()

        for context_time in context_times:
            display.info(context_time, verbosity=4)

        display.info('total: %d (%s)' % (len(paths), test_end - test_start), verbosity=4)

        errors = [SanityMessage(
            message=m['message'].replace('\n', ' '),
            path=m['path'],
            line=int(m['line']),
            column=int(m['column']),
            level=m['type'],
            code=m['symbol'],
        ) for m in messages]

        if args.explain:
            return SanitySuccess(self.name)

        line = 0

        filtered = []

        for error in errors:
            if error.code in ignore[error.path]:
                ignore[error.path][error.code] = None  # error ignored, clear line number of ignore entry to track usage
            else:
                filtered.append(error)  # error not ignored

        errors = filtered

        for invalid_ignore in invalid_ignores:
            errors.append(SanityMessage(
                code='A201',
                message=invalid_ignore[1],
                path=PYLINT_IGNORE_PATH,
                line=invalid_ignore[0],
                column=1,
                confidence=calculate_confidence(PYLINT_IGNORE_PATH, line, args.metadata) if args.metadata.changes else None,
            ))

        for path in skip_paths:
            line += 1

            if not path:
                continue

            if not os.path.exists(path):
                # Keep files out of the list which no longer exist in the repo.
                errors.append(SanityMessage(
                    code='A101',
                    message='Remove "%s" since it does not exist' % path,
                    path=PYLINT_SKIP_PATH,
                    line=line,
                    column=1,
                    confidence=calculate_best_confidence(((PYLINT_SKIP_PATH, line), (path, 0)), args.metadata) if args.metadata.changes else None,
                ))

        for path in paths:
            if path not in ignore:
                continue

            for code in ignore[path]:
                line = ignore[path][code]

                if not line:
                    continue

                errors.append(SanityMessage(
                    code='A102',
                    message='Remove since "%s" passes "%s" pylint test' % (path, code),
                    path=PYLINT_IGNORE_PATH,
                    line=line,
                    column=1,
                    confidence=calculate_best_confidence(((PYLINT_IGNORE_PATH, line), (path, 0)), args.metadata) if args.metadata.changes else None,
                ))

        if errors:
            return SanityFailure(self.name, messages=errors)

        return SanitySuccess(self.name)

    def pylint(self, args, context, paths):
        """
        :type args: SanityConfig
        :type context: str
        :type paths: list[str]
        :rtype: list[dict[str, str]]
        """
        rcfile = 'test/sanity/pylint/config/%s' % context.split('/')[0]

        if not os.path.exists(rcfile):
            rcfile = 'test/sanity/pylint/config/default'

        parser = ConfigParser()
        parser.read(rcfile)

        if parser.has_section('ansible-test'):
            config = dict(parser.items('ansible-test'))
        else:
            config = dict()

        disable_plugins = set(i.strip() for i in config.get('disable-plugins', '').split(',') if i)
        load_plugins = set(self.plugin_names) - disable_plugins

        cmd = [
            args.python_executable,
            '-m', 'pylint',
            '--jobs', '0',
            '--reports', 'n',
            '--max-line-length', '160',
            '--rcfile', rcfile,
            '--output-format', 'json',
            '--load-plugins', ','.join(load_plugins),
        ] + paths

        env = ansible_environment(args)
        env['PYTHONPATH'] += '%s%s' % (os.path.pathsep, self.plugin_dir)

        if paths:
            display.info('Checking %d file(s) in context "%s" with config: %s' % (len(paths), context, rcfile), verbosity=1)

            try:
                stdout, stderr = run_command(args, cmd, env=env, capture=True)
                status = 0
            except SubprocessError as ex:
                stdout = ex.stdout
                stderr = ex.stderr
                status = ex.status

            if stderr or status >= 32:
                raise SubprocessError(cmd=cmd, status=status, stderr=stderr, stdout=stdout)
        else:
            stdout = None

        if not args.explain and stdout:
            messages = json.loads(stdout)
        else:
            messages = []

        return messages
