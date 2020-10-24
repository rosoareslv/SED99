"""The tests for the Script component."""
# pylint: disable=protected-access
from datetime import timedelta
from unittest import mock
import unittest

import jinja2
import voluptuous as vol
import pytest

from homeassistant import exceptions
from homeassistant.core import Context, callback
# Otherwise can't test just this file (import order issue)
import homeassistant.components  # noqa
import homeassistant.util.dt as dt_util
from homeassistant.helpers import script, config_validation as cv

from tests.common import fire_time_changed, get_test_home_assistant


ENTITY_ID = 'script.test'


class TestScriptHelper(unittest.TestCase):
    """Test the Script component."""

    # pylint: disable=invalid-name
    def setUp(self):
        """Set up things to be run when tests are started."""
        self.hass = get_test_home_assistant()

    # pylint: disable=invalid-name
    def tearDown(self):
        """Stop down everything that was started."""
        self.hass.stop()

    def test_firing_event(self):
        """Test the firing of events."""
        event = 'test_event'
        context = Context()
        calls = []

        @callback
        def record_event(event):
            """Add recorded event to set."""
            calls.append(event)

        self.hass.bus.listen(event, record_event)

        script_obj = script.Script(self.hass, cv.SCRIPT_SCHEMA({
            'event': event,
            'event_data': {
                'hello': 'world'
            }
        }))

        script_obj.run(context=context)

        self.hass.block_till_done()

        assert len(calls) == 1
        assert calls[0].context is context
        assert calls[0].data.get('hello') == 'world'
        assert not script_obj.can_cancel

    def test_firing_event_template(self):
        """Test the firing of events."""
        event = 'test_event'
        context = Context()
        calls = []

        @callback
        def record_event(event):
            """Add recorded event to set."""
            calls.append(event)

        self.hass.bus.listen(event, record_event)

        script_obj = script.Script(self.hass, cv.SCRIPT_SCHEMA({
            'event': event,
            'event_data_template': {
                'dict': {
                   1: '{{ is_world }}',
                   2: '{{ is_world }}{{ is_world }}',
                   3: '{{ is_world }}{{ is_world }}{{ is_world }}',
                },
                'list': [
                    '{{ is_world }}', '{{ is_world }}{{ is_world }}'
                ]
            }
        }))

        script_obj.run({'is_world': 'yes'}, context=context)

        self.hass.block_till_done()

        assert len(calls) == 1
        assert calls[0].context is context
        assert calls[0].data == {
            'dict': {
                1: 'yes',
                2: 'yesyes',
                3: 'yesyesyes',
            },
            'list': ['yes', 'yesyes']
        }
        assert not script_obj.can_cancel

    def test_calling_service(self):
        """Test the calling of a service."""
        calls = []
        context = Context()

        @callback
        def record_call(service):
            """Add recorded event to set."""
            calls.append(service)

        self.hass.services.register('test', 'script', record_call)

        script.call_from_config(self.hass, {
            'service': 'test.script',
            'data': {
                'hello': 'world'
            }
        }, context=context)

        self.hass.block_till_done()

        assert len(calls) == 1
        assert calls[0].context is context
        assert calls[0].data.get('hello') == 'world'

    def test_calling_service_template(self):
        """Test the calling of a service."""
        calls = []
        context = Context()

        @callback
        def record_call(service):
            """Add recorded event to set."""
            calls.append(service)

        self.hass.services.register('test', 'script', record_call)

        script.call_from_config(self.hass, {
            'service_template': """
                {% if True %}
                    test.script
                {% else %}
                    test.not_script
                {% endif %}""",
            'data_template': {
                'hello': """
                    {% if is_world == 'yes' %}
                        world
                    {% else %}
                        not world
                    {% endif %}
                """
            }
        }, {'is_world': 'yes'}, context=context)

        self.hass.block_till_done()

        assert len(calls) == 1
        assert calls[0].context is context
        assert calls[0].data.get('hello') == 'world'

    def test_delay(self):
        """Test the delay."""
        event = 'test_event'
        events = []
        context = Context()
        delay_alias = 'delay step'

        @callback
        def record_event(event):
            """Add recorded event to set."""
            events.append(event)

        self.hass.bus.listen(event, record_event)

        script_obj = script.Script(self.hass, cv.SCRIPT_SCHEMA([
            {'event': event},
            {'delay': {'seconds': 5}, 'alias': delay_alias},
            {'event': event}]))

        script_obj.run(context=context)
        self.hass.block_till_done()

        assert script_obj.is_running
        assert script_obj.can_cancel
        assert script_obj.last_action == delay_alias
        assert len(events) == 1

        future = dt_util.utcnow() + timedelta(seconds=5)
        fire_time_changed(self.hass, future)
        self.hass.block_till_done()

        assert not script_obj.is_running
        assert len(events) == 2
        assert events[0].context is context
        assert events[1].context is context

    def test_delay_template(self):
        """Test the delay as a template."""
        event = 'test_event'
        events = []
        delay_alias = 'delay step'

        @callback
        def record_event(event):
            """Add recorded event to set."""
            events.append(event)

        self.hass.bus.listen(event, record_event)

        script_obj = script.Script(self.hass, cv.SCRIPT_SCHEMA([
            {'event': event},
            {'delay': '00:00:{{ 5 }}', 'alias': delay_alias},
            {'event': event}]))

        script_obj.run()
        self.hass.block_till_done()

        assert script_obj.is_running
        assert script_obj.can_cancel
        assert script_obj.last_action == delay_alias
        assert len(events) == 1

        future = dt_util.utcnow() + timedelta(seconds=5)
        fire_time_changed(self.hass, future)
        self.hass.block_till_done()

        assert not script_obj.is_running
        assert len(events) == 2

    def test_delay_invalid_template(self):
        """Test the delay as a template that fails."""
        event = 'test_event'
        events = []

        @callback
        def record_event(event):
            """Add recorded event to set."""
            events.append(event)

        self.hass.bus.listen(event, record_event)

        script_obj = script.Script(self.hass, cv.SCRIPT_SCHEMA([
            {'event': event},
            {'delay': '{{ invalid_delay }}'},
            {'delay': {'seconds': 5}},
            {'event': event}]))

        with mock.patch.object(script, '_LOGGER') as mock_logger:
            script_obj.run()
            self.hass.block_till_done()
            assert mock_logger.error.called

        assert not script_obj.is_running
        assert len(events) == 1

    def test_delay_complex_template(self):
        """Test the delay with a working complex template."""
        event = 'test_event'
        events = []
        delay_alias = 'delay step'

        @callback
        def record_event(event):
            """Add recorded event to set."""
            events.append(event)

        self.hass.bus.listen(event, record_event)

        script_obj = script.Script(self.hass, cv.SCRIPT_SCHEMA([
            {'event': event},
            {'delay': {
                'seconds': '{{ 5 }}'},
             'alias': delay_alias},
            {'event': event}]))

        script_obj.run()
        self.hass.block_till_done()

        assert script_obj.is_running
        assert script_obj.can_cancel
        assert script_obj.last_action == delay_alias
        assert len(events) == 1

        future = dt_util.utcnow() + timedelta(seconds=5)
        fire_time_changed(self.hass, future)
        self.hass.block_till_done()

        assert not script_obj.is_running
        assert len(events) == 2

    def test_delay_complex_invalid_template(self):
        """Test the delay with a complex template that fails."""
        event = 'test_event'
        events = []

        @callback
        def record_event(event):
            """Add recorded event to set."""
            events.append(event)

        self.hass.bus.listen(event, record_event)

        script_obj = script.Script(self.hass, cv.SCRIPT_SCHEMA([
            {'event': event},
            {'delay': {
                 'seconds': '{{ invalid_delay }}'
            }},
            {'delay': {
                'seconds': '{{ 5 }}'
            }},
            {'event': event}]))

        with mock.patch.object(script, '_LOGGER') as mock_logger:
            script_obj.run()
            self.hass.block_till_done()
            assert mock_logger.error.called

        assert not script_obj.is_running
        assert len(events) == 1

    def test_cancel_while_delay(self):
        """Test the cancelling while the delay is present."""
        event = 'test_event'
        events = []

        @callback
        def record_event(event):
            """Add recorded event to set."""
            events.append(event)

        self.hass.bus.listen(event, record_event)

        script_obj = script.Script(self.hass, cv.SCRIPT_SCHEMA([
            {'delay': {'seconds': 5}},
            {'event': event}]))

        script_obj.run()
        self.hass.block_till_done()

        assert script_obj.is_running
        assert len(events) == 0

        script_obj.stop()

        assert not script_obj.is_running

        # Make sure the script is really stopped.
        future = dt_util.utcnow() + timedelta(seconds=5)
        fire_time_changed(self.hass, future)
        self.hass.block_till_done()

        assert not script_obj.is_running
        assert len(events) == 0

    def test_wait_template(self):
        """Test the wait template."""
        event = 'test_event'
        events = []
        context = Context()
        wait_alias = 'wait step'

        @callback
        def record_event(event):
            """Add recorded event to set."""
            events.append(event)

        self.hass.bus.listen(event, record_event)

        self.hass.states.set('switch.test', 'on')

        script_obj = script.Script(self.hass, cv.SCRIPT_SCHEMA([
            {'event': event},
            {'wait_template': "{{states.switch.test.state == 'off'}}",
             'alias': wait_alias},
            {'event': event}]))

        script_obj.run(context=context)
        self.hass.block_till_done()

        assert script_obj.is_running
        assert script_obj.can_cancel
        assert script_obj.last_action == wait_alias
        assert len(events) == 1

        self.hass.states.set('switch.test', 'off')
        self.hass.block_till_done()

        assert not script_obj.is_running
        assert len(events) == 2
        assert events[0].context is context
        assert events[1].context is context

    def test_wait_template_cancel(self):
        """Test the wait template cancel action."""
        event = 'test_event'
        events = []
        wait_alias = 'wait step'

        @callback
        def record_event(event):
            """Add recorded event to set."""
            events.append(event)

        self.hass.bus.listen(event, record_event)

        self.hass.states.set('switch.test', 'on')

        script_obj = script.Script(self.hass, cv.SCRIPT_SCHEMA([
            {'event': event},
            {'wait_template': "{{states.switch.test.state == 'off'}}",
             'alias': wait_alias},
            {'event': event}]))

        script_obj.run()
        self.hass.block_till_done()

        assert script_obj.is_running
        assert script_obj.can_cancel
        assert script_obj.last_action == wait_alias
        assert len(events) == 1

        script_obj.stop()

        assert not script_obj.is_running
        assert len(events) == 1

        self.hass.states.set('switch.test', 'off')
        self.hass.block_till_done()

        assert not script_obj.is_running
        assert len(events) == 1

    def test_wait_template_not_schedule(self):
        """Test the wait template with correct condition."""
        event = 'test_event'
        events = []

        @callback
        def record_event(event):
            """Add recorded event to set."""
            events.append(event)

        self.hass.bus.listen(event, record_event)

        self.hass.states.set('switch.test', 'on')

        script_obj = script.Script(self.hass, cv.SCRIPT_SCHEMA([
            {'event': event},
            {'wait_template': "{{states.switch.test.state == 'on'}}"},
            {'event': event}]))

        script_obj.run()
        self.hass.block_till_done()

        assert not script_obj.is_running
        assert script_obj.can_cancel
        assert len(events) == 2

    def test_wait_template_timeout_halt(self):
        """Test the wait template, halt on timeout."""
        event = 'test_event'
        events = []
        wait_alias = 'wait step'

        @callback
        def record_event(event):
            """Add recorded event to set."""
            events.append(event)

        self.hass.bus.listen(event, record_event)

        self.hass.states.set('switch.test', 'on')

        script_obj = script.Script(self.hass, cv.SCRIPT_SCHEMA([
            {'event': event},
            {
                'wait_template': "{{states.switch.test.state == 'off'}}",
                'continue_on_timeout': False,
                'timeout': 5,
                'alias': wait_alias
            },
            {'event': event}]))

        script_obj.run()
        self.hass.block_till_done()

        assert script_obj.is_running
        assert script_obj.can_cancel
        assert script_obj.last_action == wait_alias
        assert len(events) == 1

        future = dt_util.utcnow() + timedelta(seconds=5)
        fire_time_changed(self.hass, future)
        self.hass.block_till_done()

        assert not script_obj.is_running
        assert len(events) == 1

    def test_wait_template_timeout_continue(self):
        """Test the wait template with continuing the script."""
        event = 'test_event'
        events = []
        wait_alias = 'wait step'

        @callback
        def record_event(event):
            """Add recorded event to set."""
            events.append(event)

        self.hass.bus.listen(event, record_event)

        self.hass.states.set('switch.test', 'on')

        script_obj = script.Script(self.hass, cv.SCRIPT_SCHEMA([
            {'event': event},
            {
                'wait_template': "{{states.switch.test.state == 'off'}}",
                'timeout': 5,
                'continue_on_timeout': True,
                'alias': wait_alias
            },
            {'event': event}]))

        script_obj.run()
        self.hass.block_till_done()

        assert script_obj.is_running
        assert script_obj.can_cancel
        assert script_obj.last_action == wait_alias
        assert len(events) == 1

        future = dt_util.utcnow() + timedelta(seconds=5)
        fire_time_changed(self.hass, future)
        self.hass.block_till_done()

        assert not script_obj.is_running
        assert len(events) == 2

    def test_wait_template_timeout_default(self):
        """Test the wait template with default contiune."""
        event = 'test_event'
        events = []
        wait_alias = 'wait step'

        @callback
        def record_event(event):
            """Add recorded event to set."""
            events.append(event)

        self.hass.bus.listen(event, record_event)

        self.hass.states.set('switch.test', 'on')

        script_obj = script.Script(self.hass, cv.SCRIPT_SCHEMA([
            {'event': event},
            {
                'wait_template': "{{states.switch.test.state == 'off'}}",
                'timeout': 5,
                'alias': wait_alias
            },
            {'event': event}]))

        script_obj.run()
        self.hass.block_till_done()

        assert script_obj.is_running
        assert script_obj.can_cancel
        assert script_obj.last_action == wait_alias
        assert len(events) == 1

        future = dt_util.utcnow() + timedelta(seconds=5)
        fire_time_changed(self.hass, future)
        self.hass.block_till_done()

        assert not script_obj.is_running
        assert len(events) == 2

    def test_wait_template_variables(self):
        """Test the wait template with variables."""
        event = 'test_event'
        events = []
        wait_alias = 'wait step'

        @callback
        def record_event(event):
            """Add recorded event to set."""
            events.append(event)

        self.hass.bus.listen(event, record_event)

        self.hass.states.set('switch.test', 'on')

        script_obj = script.Script(self.hass, cv.SCRIPT_SCHEMA([
            {'event': event},
            {'wait_template': "{{is_state(data, 'off')}}",
             'alias': wait_alias},
            {'event': event}]))

        script_obj.run({
            'data': 'switch.test'
        })
        self.hass.block_till_done()

        assert script_obj.is_running
        assert script_obj.can_cancel
        assert script_obj.last_action == wait_alias
        assert len(events) == 1

        self.hass.states.set('switch.test', 'off')
        self.hass.block_till_done()

        assert not script_obj.is_running
        assert len(events) == 2

    def test_passing_variables_to_script(self):
        """Test if we can pass variables to script."""
        calls = []

        @callback
        def record_call(service):
            """Add recorded event to set."""
            calls.append(service)

        self.hass.services.register('test', 'script', record_call)

        script_obj = script.Script(self.hass, cv.SCRIPT_SCHEMA([
            {
                'service': 'test.script',
                'data_template': {
                    'hello': '{{ greeting }}',
                },
            },
            {'delay': '{{ delay_period }}'},
            {
                'service': 'test.script',
                'data_template': {
                    'hello': '{{ greeting2 }}',
                },
            }]))

        script_obj.run({
            'greeting': 'world',
            'greeting2': 'universe',
            'delay_period': '00:00:05'
        })

        self.hass.block_till_done()

        assert script_obj.is_running
        assert len(calls) == 1
        assert calls[-1].data['hello'] == 'world'

        future = dt_util.utcnow() + timedelta(seconds=5)
        fire_time_changed(self.hass, future)
        self.hass.block_till_done()

        assert not script_obj.is_running
        assert len(calls) == 2
        assert calls[-1].data['hello'] == 'universe'

    def test_condition(self):
        """Test if we can use conditions in a script."""
        event = 'test_event'
        events = []

        @callback
        def record_event(event):
            """Add recorded event to set."""
            events.append(event)

        self.hass.bus.listen(event, record_event)

        self.hass.states.set('test.entity', 'hello')

        script_obj = script.Script(self.hass, cv.SCRIPT_SCHEMA([
            {'event': event},
            {
                'condition': 'template',
                'value_template': '{{ states.test.entity.state == "hello" }}',
            },
            {'event': event},
        ]))

        script_obj.run()
        self.hass.block_till_done()
        assert len(events) == 2

        self.hass.states.set('test.entity', 'goodbye')

        script_obj.run()
        self.hass.block_till_done()
        assert len(events) == 3

    @mock.patch('homeassistant.helpers.script.condition.async_from_config')
    def test_condition_created_once(self, async_from_config):
        """Test that the conditions do not get created multiple times."""
        event = 'test_event'
        events = []

        @callback
        def record_event(event):
            """Add recorded event to set."""
            events.append(event)

        self.hass.bus.listen(event, record_event)

        self.hass.states.set('test.entity', 'hello')

        script_obj = script.Script(self.hass, cv.SCRIPT_SCHEMA([
            {'event': event},
            {
                'condition': 'template',
                'value_template': '{{ states.test.entity.state == "hello" }}',
            },
            {'event': event},
        ]))

        script_obj.run()
        script_obj.run()
        self.hass.block_till_done()
        assert async_from_config.call_count == 1
        assert len(script_obj._config_cache) == 1

    def test_all_conditions_cached(self):
        """Test that multiple conditions get cached."""
        event = 'test_event'
        events = []

        @callback
        def record_event(event):
            """Add recorded event to set."""
            events.append(event)

        self.hass.bus.listen(event, record_event)

        self.hass.states.set('test.entity', 'hello')

        script_obj = script.Script(self.hass, cv.SCRIPT_SCHEMA([
            {'event': event},
            {
                'condition': 'template',
                'value_template': '{{ states.test.entity.state == "hello" }}',
            },
            {
                'condition': 'template',
                'value_template': '{{ states.test.entity.state != "hello" }}',
            },
            {'event': event},
        ]))

        script_obj.run()
        self.hass.block_till_done()
        assert len(script_obj._config_cache) == 2

    def test_last_triggered(self):
        """Test the last_triggered."""
        event = 'test_event'

        script_obj = script.Script(self.hass, cv.SCRIPT_SCHEMA([
            {'event': event},
            {'delay': {'seconds': 5}},
            {'event': event}]))

        assert script_obj.last_triggered is None

        time = dt_util.utcnow()
        with mock.patch('homeassistant.helpers.script.date_util.utcnow',
                        return_value=time):
            script_obj.run()
            self.hass.block_till_done()

        assert script_obj.last_triggered == time


async def test_propagate_error_service_not_found(hass):
    """Test that a script aborts when a service is not found."""
    events = []

    @callback
    def record_event(event):
        events.append(event)

    hass.bus.async_listen('test_event', record_event)

    script_obj = script.Script(hass, cv.SCRIPT_SCHEMA([
        {'service': 'test.script'},
        {'event': 'test_event'}]))

    with pytest.raises(exceptions.ServiceNotFound):
        await script_obj.async_run()

    assert len(events) == 0
    assert script_obj._cur == -1


async def test_propagate_error_invalid_service_data(hass):
    """Test that a script aborts when we send invalid service data."""
    events = []

    @callback
    def record_event(event):
        events.append(event)

    hass.bus.async_listen('test_event', record_event)

    calls = []

    @callback
    def record_call(service):
        """Add recorded event to set."""
        calls.append(service)

    hass.services.async_register('test', 'script', record_call,
                                 schema=vol.Schema({'text': str}))

    script_obj = script.Script(hass, cv.SCRIPT_SCHEMA([
        {'service': 'test.script', 'data': {'text': 1}},
        {'event': 'test_event'}]))

    with pytest.raises(vol.Invalid):
        await script_obj.async_run()

    assert len(events) == 0
    assert len(calls) == 0
    assert script_obj._cur == -1


async def test_propagate_error_service_exception(hass):
    """Test that a script aborts when a service throws an exception."""
    events = []

    @callback
    def record_event(event):
        events.append(event)

    hass.bus.async_listen('test_event', record_event)

    calls = []

    @callback
    def record_call(service):
        """Add recorded event to set."""
        raise ValueError("BROKEN")

    hass.services.async_register('test', 'script', record_call)

    script_obj = script.Script(hass, cv.SCRIPT_SCHEMA([
        {'service': 'test.script'},
        {'event': 'test_event'}]))

    with pytest.raises(ValueError):
        await script_obj.async_run()

    assert len(events) == 0
    assert len(calls) == 0
    assert script_obj._cur == -1


def test_log_exception():
    """Test logged output."""
    script_obj = script.Script(None, cv.SCRIPT_SCHEMA([
        {'service': 'test.script'},
        {'event': 'test_event'}]))
    script_obj._exception_step = 1

    for exc, msg in (
        (vol.Invalid("Invalid number"), 'Invalid data'),
        (exceptions.TemplateError(jinja2.TemplateError('Unclosed bracket')),
         'Error rendering template'),
        (exceptions.Unauthorized(), 'Unauthorized'),
        (exceptions.ServiceNotFound('light', 'turn_on'), 'Service not found'),
        (ValueError("Cannot parse JSON"), 'Unknown error'),
    ):
        logger = mock.Mock()
        script_obj.async_log_exception(logger, 'Test error', exc)

        assert len(logger.mock_calls) == 1
        p_format, p_msg_base, p_error_desc, p_action_type, p_step, p_error = \
            logger.mock_calls[0][1]

        assert p_error_desc == msg
        assert p_action_type == script.ACTION_FIRE_EVENT
        assert p_step == 2
        if isinstance(exc, ValueError):
            assert p_error == ""
        else:
            assert p_error == str(exc)
