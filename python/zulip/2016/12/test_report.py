# -*- coding: utf-8 -*-
from __future__ import absolute_import
from __future__ import print_function

from typing import Any, Callable, Iterable, Tuple

from zerver.lib.test_classes import (
    ZulipTestCase,
)
from zerver.lib.utils import statsd

import mock
import ujson

def fix_params(raw_params):
    # type: (Dict[str, Any]) -> Dict[str, str]
    # A few of our few legacy endpoints need their
    # individual parameters serialized as JSON.
    return {k: ujson.dumps(v) for k, v in raw_params.items()}

class StatsMock(object):
    def __init__(self, settings):
        # type: (Callable) -> None
        self.settings = settings
        self.real_impl = statsd
        self.func_calls = [] # type: List[Tuple[str, Iterable[Any]]]

    def __getattr__(self, name):
        # type: (str) -> Callable
        def f(*args):
            # type: (*Any) -> None
            with self.settings(STATSD_HOST=''):
                getattr(self.real_impl, name)(*args)
            self.func_calls.append((name, args))

        return f

class TestReport(ZulipTestCase):
    def test_send_time(self):
        # type: () -> None
        email = 'hamlet@zulip.com'
        self.login(email)

        params = dict(
            time=5,
            received=6,
            displayed=7,
            locally_echoed='true',
            rendered_content_disparity='true',
        )

        stats_mock = StatsMock(self.settings)
        with mock.patch('zerver.views.report.statsd', wraps=stats_mock):
            result = self.client_post("/json/report_send_time", params)
        self.assert_json_success(result)

        expected_calls = [
            ('timing', ('endtoend.send_time.zulip_com', 5)),
            ('timing', ('endtoend.receive_time.zulip_com', 6)),
            ('timing', ('endtoend.displayed_time.zulip_com', 7)),
            ('incr', ('locally_echoed',)),
            ('incr', ('render_disparity',)),
        ]
        self.assertEqual(stats_mock.func_calls, expected_calls)

    def test_narrow_time(self):
        # type: () -> None
        email = 'hamlet@zulip.com'
        self.login(email)

        params = dict(
            initial_core=5,
            initial_free=6,
            network=7,
        )

        stats_mock = StatsMock(self.settings)
        with mock.patch('zerver.views.report.statsd', wraps=stats_mock):
            result = self.client_post("/json/report_narrow_time", params)
        self.assert_json_success(result)

        expected_calls = [
            ('timing', ('narrow.initial_core.zulip_com', 5)),
            ('timing', ('narrow.initial_free.zulip_com', 6)),
            ('timing', ('narrow.network.zulip_com', 7)),
        ]
        self.assertEqual(stats_mock.func_calls, expected_calls)

    def test_unnarrow_time(self):
        # type: () -> None
        email = 'hamlet@zulip.com'
        self.login(email)

        params = dict(
            initial_core=5,
            initial_free=6,
        )

        stats_mock = StatsMock(self.settings)
        with mock.patch('zerver.views.report.statsd', wraps=stats_mock):
            result = self.client_post("/json/report_unnarrow_time", params)
        self.assert_json_success(result)

        expected_calls = [
            ('timing', ('unnarrow.initial_core.zulip_com', 5)),
            ('timing', ('unnarrow.initial_free.zulip_com', 6)),
        ]
        self.assertEqual(stats_mock.func_calls, expected_calls)

    def test_report_error(self):
        # type: () -> None
        email = 'hamlet@zulip.com'
        self.login(email)

        params = fix_params(dict(
            message='hello',
            stacktrace='trace',
            ui_message=True,
            user_agent='agent',
            href='href',
            log='log',
            more_info=dict(foo='bar'),
        ))

        publish_mock = mock.patch('zerver.views.report.queue_json_publish')
        subprocess_mock = mock.patch(
            'zerver.views.report.subprocess.check_output',
            side_effect=KeyError('foo')
        )
        with publish_mock as m, subprocess_mock:
            result = self.client_post("/json/report_error", params)
        self.assert_json_success(result)

        report = m.call_args[0][1]['report']
        for k in set(params) - set(['ui_message', 'more_info']):
            self.assertEqual(report[k], params[k])

        self.assertEqual(report['more_info'], dict(foo='bar'))
        self.assertEqual(report['user_email'], email)

        with self.settings(ERROR_REPORTING=False):
            result = self.client_post("/json/report_error", params)
        self.assert_json_success(result)
