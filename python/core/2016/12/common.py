"""Test the helper method for writing tests."""
import asyncio
import os
import sys
from datetime import timedelta
from unittest.mock import patch, MagicMock
from io import StringIO
import logging
import threading
from contextlib import contextmanager

from aiohttp import web

from homeassistant import core as ha, loader
from homeassistant.bootstrap import (
    setup_component, async_prepare_setup_component)
from homeassistant.helpers.entity import ToggleEntity
from homeassistant.util.unit_system import METRIC_SYSTEM
import homeassistant.util.dt as date_util
import homeassistant.util.yaml as yaml
from homeassistant.const import (
    STATE_ON, STATE_OFF, DEVICE_DEFAULT_NAME, EVENT_TIME_CHANGED,
    EVENT_STATE_CHANGED, EVENT_PLATFORM_DISCOVERED, ATTR_SERVICE,
    ATTR_DISCOVERED, SERVER_PORT)
from homeassistant.components import sun, mqtt
from homeassistant.components.http.auth import auth_middleware
from homeassistant.components.http.const import (
    KEY_USE_X_FORWARDED_FOR, KEY_BANS_ENABLED, KEY_TRUSTED_NETWORKS)

_TEST_INSTANCE_PORT = SERVER_PORT
_LOGGER = logging.getLogger(__name__)


def get_test_config_dir(*add_path):
    """Return a path to a test config dir."""
    return os.path.join(os.path.dirname(__file__), 'testing_config', *add_path)


def get_test_home_assistant():
    """Return a Home Assistant object pointing at test config directory."""
    if sys.platform == "win32":
        loop = asyncio.ProactorEventLoop()
    else:
        loop = asyncio.new_event_loop()

    hass = loop.run_until_complete(async_test_home_assistant(loop))

    # FIXME should not be a daemon. Means hass.stop() not called in teardown
    stop_event = threading.Event()

    def run_loop():
        """Run event loop."""
        # pylint: disable=protected-access
        loop._thread_ident = threading.get_ident()
        loop.run_forever()
        loop.close()
        stop_event.set()

    threading.Thread(name="LoopThread", target=run_loop, daemon=True).start()

    orig_start = hass.start
    orig_stop = hass.stop

    @patch.object(hass.loop, 'run_forever')
    @patch.object(hass.loop, 'close')
    def start_hass(*mocks):
        """Helper to start hass."""
        orig_start()
        hass.block_till_done()

    def stop_hass():
        """Stop hass."""
        orig_stop()
        stop_event.wait()

    hass.start = start_hass
    hass.stop = stop_hass

    return hass


# pylint: disable=protected-access
@asyncio.coroutine
def async_test_home_assistant(loop):
    """Return a Home Assistant object pointing at test config dir."""
    loop._thread_ident = threading.get_ident()

    hass = ha.HomeAssistant(loop)
    hass.async_track_tasks()

    hass.config.location_name = 'test home'
    hass.config.config_dir = get_test_config_dir()
    hass.config.latitude = 32.87336
    hass.config.longitude = -117.22743
    hass.config.elevation = 0
    hass.config.time_zone = date_util.get_time_zone('US/Pacific')
    hass.config.units = METRIC_SYSTEM
    hass.config.skip_pip = True

    if 'custom_components.test' not in loader.AVAILABLE_COMPONENTS:
        yield from loop.run_in_executor(None, loader.prepare, hass)

    hass.state = ha.CoreState.running

    # Mock async_start
    orig_start = hass.async_start

    @asyncio.coroutine
    def mock_async_start():
        """Start the mocking."""
        with patch.object(loop, 'add_signal_handler'), \
                patch('homeassistant.core._async_create_timer'):
            yield from orig_start()

    hass.async_start = mock_async_start

    return hass


def get_test_instance_port():
    """Return unused port for running test instance.

    The socket that holds the default port does not get released when we stop
    HA in a different test case. Until I have figured out what is going on,
    let's run each test on a different port.
    """
    global _TEST_INSTANCE_PORT
    _TEST_INSTANCE_PORT += 1
    return _TEST_INSTANCE_PORT


def mock_service(hass, domain, service):
    """Setup a fake service.

    Return a list that logs all calls to fake service.
    """
    calls = []

    # pylint: disable=redefined-outer-name
    @ha.callback
    def mock_service(call):
        """"Mocked service call."""
        calls.append(call)

    # pylint: disable=unnecessary-lambda
    hass.services.register(domain, service, mock_service)

    return calls


def fire_mqtt_message(hass, topic, payload, qos=0):
    """Fire the MQTT message."""
    hass.bus.fire(mqtt.EVENT_MQTT_MESSAGE_RECEIVED, {
        mqtt.ATTR_TOPIC: topic,
        mqtt.ATTR_PAYLOAD: payload,
        mqtt.ATTR_QOS: qos,
    })


def fire_time_changed(hass, time):
    """Fire a time changes event."""
    hass.bus.fire(EVENT_TIME_CHANGED, {'now': time})


def fire_service_discovered(hass, service, info):
    """Fire the MQTT message."""
    hass.bus.fire(EVENT_PLATFORM_DISCOVERED, {
        ATTR_SERVICE: service,
        ATTR_DISCOVERED: info
    })


def ensure_sun_risen(hass):
    """Trigger sun to rise if below horizon."""
    if sun.is_on(hass):
        return
    fire_time_changed(hass, sun.next_rising_utc(hass) + timedelta(seconds=10))


def ensure_sun_set(hass):
    """Trigger sun to set if above horizon."""
    if not sun.is_on(hass):
        return
    fire_time_changed(hass, sun.next_setting_utc(hass) + timedelta(seconds=10))


def load_fixture(filename):
    """Helper to load a fixture."""
    path = os.path.join(os.path.dirname(__file__), 'fixtures', filename)
    with open(path) as fptr:
        return fptr.read()


def mock_state_change_event(hass, new_state, old_state=None):
    """Mock state change envent."""
    event_data = {
        'entity_id': new_state.entity_id,
        'new_state': new_state,
    }

    if old_state:
        event_data['old_state'] = old_state

    hass.bus.fire(EVENT_STATE_CHANGED, event_data)


def mock_http_component(hass):
    """Mock the HTTP component."""
    hass.http = MagicMock()
    hass.config.components.append('http')
    hass.http.views = {}

    def mock_register_view(view):
        """Store registered view."""
        if isinstance(view, type):
            # Instantiate the view, if needed
            view = view()

        hass.http.views[view.name] = view

    hass.http.register_view = mock_register_view


def mock_http_component_app(hass, api_password=None):
    """Create an aiohttp.web.Application instance for testing."""
    hass.http = MagicMock(api_password=api_password)
    app = web.Application(middlewares=[auth_middleware], loop=hass.loop)
    app['hass'] = hass
    app[KEY_USE_X_FORWARDED_FOR] = False
    app[KEY_BANS_ENABLED] = False
    app[KEY_TRUSTED_NETWORKS] = []
    return app


def mock_mqtt_component(hass):
    """Mock the MQTT component."""
    with patch('homeassistant.components.mqtt.MQTT') as mock_mqtt:
        setup_component(hass, mqtt.DOMAIN, {
            mqtt.DOMAIN: {
                mqtt.CONF_BROKER: 'mock-broker',
            }
        })
        return mock_mqtt


class MockModule(object):
    """Representation of a fake module."""

    # pylint: disable=invalid-name
    def __init__(self, domain=None, dependencies=None, setup=None,
                 requirements=None, config_schema=None, platform_schema=None,
                 async_setup=None):
        """Initialize the mock module."""
        self.DOMAIN = domain
        self.DEPENDENCIES = dependencies or []
        self.REQUIREMENTS = requirements or []
        self._setup = setup

        if config_schema is not None:
            self.CONFIG_SCHEMA = config_schema

        if platform_schema is not None:
            self.PLATFORM_SCHEMA = platform_schema

        if async_setup is not None:
            self.async_setup = async_setup

    def setup(self, hass, config):
        """Setup the component.

        We always define this mock because MagicMock setups will be seen by the
        executor as a coroutine, raising an exception.
        """
        if self._setup is not None:
            return self._setup(hass, config)
        return True


class MockPlatform(object):
    """Provide a fake platform."""

    # pylint: disable=invalid-name
    def __init__(self, setup_platform=None, dependencies=None,
                 platform_schema=None):
        """Initialize the platform."""
        self.DEPENDENCIES = dependencies or []
        self._setup_platform = setup_platform

        if platform_schema is not None:
            self.PLATFORM_SCHEMA = platform_schema

    def setup_platform(self, hass, config, add_devices, discovery_info=None):
        """Setup the platform."""
        if self._setup_platform is not None:
            self._setup_platform(hass, config, add_devices, discovery_info)


class MockToggleDevice(ToggleEntity):
    """Provide a mock toggle device."""

    def __init__(self, name, state):
        """Initialize the mock device."""
        self._name = name or DEVICE_DEFAULT_NAME
        self._state = state
        self.calls = []

    @property
    def name(self):
        """Return the name of the device if any."""
        self.calls.append(('name', {}))
        return self._name

    @property
    def state(self):
        """Return the name of the device if any."""
        self.calls.append(('state', {}))
        return self._state

    @property
    def is_on(self):
        """Return true if device is on."""
        self.calls.append(('is_on', {}))
        return self._state == STATE_ON

    def turn_on(self, **kwargs):
        """Turn the device on."""
        self.calls.append(('turn_on', kwargs))
        self._state = STATE_ON

    def turn_off(self, **kwargs):
        """Turn the device off."""
        self.calls.append(('turn_off', kwargs))
        self._state = STATE_OFF

    def last_call(self, method=None):
        """Return the last call."""
        if not self.calls:
            return None
        elif method is None:
            return self.calls[-1]
        else:
            try:
                return next(call for call in reversed(self.calls)
                            if call[0] == method)
            except StopIteration:
                return None


def patch_yaml_files(files_dict, endswith=True):
    """Patch load_yaml with a dictionary of yaml files."""
    # match using endswith, start search with longest string
    matchlist = sorted(list(files_dict.keys()), key=len) if endswith else []

    def mock_open_f(fname, **_):
        """Mock open() in the yaml module, used by load_yaml."""
        # Return the mocked file on full match
        if fname in files_dict:
            _LOGGER.debug('patch_yaml_files match %s', fname)
            res = StringIO(files_dict[fname])
            setattr(res, 'name', fname)
            return res

        # Match using endswith
        for ends in matchlist:
            if fname.endswith(ends):
                _LOGGER.debug('patch_yaml_files end match %s: %s', ends, fname)
                res = StringIO(files_dict[ends])
                setattr(res, 'name', fname)
                return res

        # Fallback for hass.components (i.e. services.yaml)
        if 'homeassistant/components' in fname:
            _LOGGER.debug('patch_yaml_files using real file: %s', fname)
            return open(fname, encoding='utf-8')

        # Not found
        raise FileNotFoundError('File not found: {}'.format(fname))

    return patch.object(yaml, 'open', mock_open_f, create=True)


def mock_coro(return_value=None):
    """Helper method to return a coro that returns a value."""
    @asyncio.coroutine
    def coro():
        """Fake coroutine."""
        return return_value

    return coro


@contextmanager
def assert_setup_component(count, domain=None):
    """Collect valid configuration from setup_component.

    - count: The amount of valid platforms that should be setup
    - domain: The domain to count is optional. It can be automatically
              determined most of the time

    Use as a context manager aroung bootstrap.setup_component
        with assert_setup_component(0) as result_config:
            setup_component(hass, start_config, domain)
            # using result_config is optional
    """
    config = {}

    @asyncio.coroutine
    def mock_psc(hass, config_input, domain):
        """Mock the prepare_setup_component to capture config."""
        res = yield from async_prepare_setup_component(
            hass, config_input, domain)
        config[domain] = None if res is None else res.get(domain)
        _LOGGER.debug('Configuration for %s, Validated: %s, Original %s',
                      domain, config[domain], config_input.get(domain))
        return res

    assert isinstance(config, dict)
    with patch('homeassistant.bootstrap.async_prepare_setup_component',
               mock_psc):
        yield config

    if domain is None:
        assert len(config) == 1, ('assert_setup_component requires DOMAIN: {}'
                                  .format(list(config.keys())))
        domain = list(config.keys())[0]

    res = config.get(domain)
    res_len = 0 if res is None else len(res)
    assert res_len == count, 'setup_component failed, expected {} got {}: {}' \
        .format(count, res_len, res)
