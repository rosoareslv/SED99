"""Tests for samsungtv Components."""
import asyncio
import unittest
from unittest.mock import call, patch, MagicMock
from subprocess import CalledProcessError

from asynctest import mock

import pytest

import tests.common
from homeassistant.components.media_player import SUPPORT_TURN_ON, \
    MEDIA_TYPE_CHANNEL, MEDIA_TYPE_URL
from homeassistant.components.media_player.samsungtv import setup_platform, \
    CONF_TIMEOUT, SamsungTVDevice, SUPPORT_SAMSUNGTV
from homeassistant.const import CONF_HOST, CONF_NAME, CONF_PORT, STATE_ON, \
    CONF_MAC, STATE_OFF
from tests.common import MockDependency
from homeassistant.util import dt as dt_util
from datetime import timedelta

WORKING_CONFIG = {
    CONF_HOST: 'fake',
    CONF_NAME: 'fake',
    CONF_PORT: 8001,
    CONF_TIMEOUT: 10,
    CONF_MAC: 'fake'
}

DISCOVERY_INFO = {
    'name': 'fake',
    'model_name': 'fake',
    'host': 'fake'
}


class AccessDenied(Exception):
    """Dummy Exception."""


class ConnectionClosed(Exception):
    """Dummy Exception."""


class UnhandledResponse(Exception):
    """Dummy Exception."""


class TestSamsungTv(unittest.TestCase):
    """Testing Samsungtv component."""

    @MockDependency('samsungctl')
    @MockDependency('wakeonlan')
    def setUp(self, samsung_mock, wol_mock):
        """Set up test environment."""
        self.hass = tests.common.get_test_home_assistant()
        self.hass.start()
        self.hass.block_till_done()
        self.device = SamsungTVDevice(**WORKING_CONFIG)
        self.device._exceptions_class = mock.Mock()
        self.device._exceptions_class.UnhandledResponse = UnhandledResponse
        self.device._exceptions_class.AccessDenied = AccessDenied
        self.device._exceptions_class.ConnectionClosed = ConnectionClosed

    def tearDown(self):
        """Tear down test data."""
        self.hass.stop()

    @MockDependency('samsungctl')
    @MockDependency('wakeonlan')
    def test_setup(self, samsung_mock, wol_mock):
        """Testing setup of platform."""
        with mock.patch(
                'homeassistant.components.media_player.samsungtv.socket'):
            add_entities = mock.Mock()
            setup_platform(
                self.hass, WORKING_CONFIG, add_entities)

    @MockDependency('samsungctl')
    @MockDependency('wakeonlan')
    def test_setup_discovery(self, samsung_mock, wol_mock):
        """Testing setup of platform with discovery."""
        with mock.patch(
                'homeassistant.components.media_player.samsungtv.socket'):
            add_entities = mock.Mock()
            setup_platform(self.hass, {}, add_entities,
                           discovery_info=DISCOVERY_INFO)

    @MockDependency('samsungctl')
    @MockDependency('wakeonlan')
    @mock.patch(
        'homeassistant.components.media_player.samsungtv._LOGGER.warning')
    def test_setup_none(self, samsung_mock, wol_mock, mocked_warn):
        """Testing setup of platform with no data."""
        with mock.patch(
                'homeassistant.components.media_player.samsungtv.socket'):
            add_entities = mock.Mock()
            setup_platform(self.hass, {}, add_entities,
                           discovery_info=None)
            mocked_warn.assert_called_once_with("Cannot determine device")
            add_entities.assert_not_called()

    @mock.patch(
        'homeassistant.components.media_player.samsungtv.subprocess.Popen'
    )
    def test_update_on(self, mocked_popen):
        """Testing update tv on."""
        ping = mock.Mock()
        mocked_popen.return_value = ping
        ping.returncode = 0
        self.device.update()
        self.assertEqual(STATE_ON, self.device._state)

    @mock.patch(
        'homeassistant.components.media_player.samsungtv.subprocess.Popen'
    )
    def test_update_off(self, mocked_popen):
        """Testing update tv off."""
        ping = mock.Mock()
        mocked_popen.return_value = ping
        ping.returncode = 1
        self.device.update()
        self.assertEqual(STATE_OFF, self.device._state)
        ping = mock.Mock()
        ping.communicate = mock.Mock(
            side_effect=CalledProcessError("BOOM", None))
        mocked_popen.return_value = ping
        self.device.update()
        self.assertEqual(STATE_OFF, self.device._state)

    def test_send_key(self):
        """Test for send key."""
        self.device.send_key('KEY_POWER')
        self.assertEqual(STATE_ON, self.device._state)

    def test_send_key_broken_pipe(self):
        """Testing broken pipe Exception."""
        _remote = mock.Mock()
        _remote.control = mock.Mock(
            side_effect=BrokenPipeError('Boom'))
        self.device.get_remote = mock.Mock(return_value=_remote)
        self.device.send_key('HELLO')
        self.assertIsNone(self.device._remote)
        self.assertEqual(STATE_ON, self.device._state)

    def test_send_key_connection_closed_retry_succeed(self):
        """Test retry on connection closed."""
        _remote = mock.Mock()
        _remote.control = mock.Mock(side_effect=[
            self.device._exceptions_class.ConnectionClosed('Boom'),
            mock.DEFAULT])
        self.device.get_remote = mock.Mock(return_value=_remote)
        command = 'HELLO'
        self.device.send_key(command)
        self.assertEqual(STATE_ON, self.device._state)
        # verify that _remote.control() get called twice because of retry logic
        expected = [mock.call(command),
                    mock.call(command)]
        self.assertEqual(expected, _remote.control.call_args_list)

    def test_send_key_unhandled_response(self):
        """Testing unhandled response exception."""
        _remote = mock.Mock()
        _remote.control = mock.Mock(
            side_effect=self.device._exceptions_class.UnhandledResponse('Boom')
        )
        self.device.get_remote = mock.Mock(return_value=_remote)
        self.device.send_key('HELLO')
        self.assertIsNone(self.device._remote)
        self.assertEqual(STATE_ON, self.device._state)

    def test_send_key_os_error(self):
        """Testing broken pipe Exception."""
        _remote = mock.Mock()
        _remote.control = mock.Mock(
            side_effect=OSError('Boom'))
        self.device.get_remote = mock.Mock(return_value=_remote)
        self.device.send_key('HELLO')
        self.assertIsNone(self.device._remote)
        self.assertEqual(STATE_OFF, self.device._state)

    def test_power_off_in_progress(self):
        """Test for power_off_in_progress."""
        self.assertFalse(self.device._power_off_in_progress())
        self.device._end_of_power_off = dt_util.utcnow() + timedelta(
            seconds=15)
        self.assertTrue(self.device._power_off_in_progress())

    def test_name(self):
        """Test for name property."""
        self.assertEqual('fake', self.device.name)

    def test_state(self):
        """Test for state property."""
        self.device._state = STATE_ON
        self.assertEqual(STATE_ON, self.device.state)
        self.device._state = STATE_OFF
        self.assertEqual(STATE_OFF, self.device.state)

    def test_is_volume_muted(self):
        """Test for is_volume_muted property."""
        self.device._muted = False
        self.assertFalse(self.device.is_volume_muted)
        self.device._muted = True
        self.assertTrue(self.device.is_volume_muted)

    def test_supported_features(self):
        """Test for supported_features property."""
        self.device._mac = None
        self.assertEqual(SUPPORT_SAMSUNGTV, self.device.supported_features)
        self.device._mac = "fake"
        self.assertEqual(
            SUPPORT_SAMSUNGTV | SUPPORT_TURN_ON,
            self.device.supported_features)

    def test_turn_off(self):
        """Test for turn_off."""
        self.device.send_key = mock.Mock()
        _remote = mock.Mock()
        _remote.close = mock.Mock()
        self.get_remote = mock.Mock(return_value=_remote)
        self.device._end_of_power_off = None
        self.device.turn_off()
        self.assertIsNotNone(self.device._end_of_power_off)
        self.device.send_key.assert_called_once_with('KEY_POWER')
        self.device.send_key = mock.Mock()
        self.device._config['method'] = 'legacy'
        self.device.turn_off()
        self.device.send_key.assert_called_once_with('KEY_POWEROFF')

    @mock.patch(
        'homeassistant.components.media_player.samsungtv._LOGGER.debug')
    def test_turn_off_os_error(self, mocked_debug):
        """Test for turn_off with OSError."""
        _remote = mock.Mock()
        _remote.close = mock.Mock(side_effect=OSError("BOOM"))
        self.device.get_remote = mock.Mock(return_value=_remote)
        self.device.turn_off()
        mocked_debug.assert_called_once_with("Could not establish connection.")

    def test_volume_up(self):
        """Test for volume_up."""
        self.device.send_key = mock.Mock()
        self.device.volume_up()
        self.device.send_key.assert_called_once_with("KEY_VOLUP")

    def test_volume_down(self):
        """Test for volume_down."""
        self.device.send_key = mock.Mock()
        self.device.volume_down()
        self.device.send_key.assert_called_once_with("KEY_VOLDOWN")

    def test_mute_volume(self):
        """Test for mute_volume."""
        self.device.send_key = mock.Mock()
        self.device.mute_volume(True)
        self.device.send_key.assert_called_once_with("KEY_MUTE")

    def test_media_play_pause(self):
        """Test for media_next_track."""
        self.device.send_key = mock.Mock()
        self.device._playing = False
        self.device.media_play_pause()
        self.device.send_key.assert_called_once_with("KEY_PLAY")
        self.assertTrue(self.device._playing)
        self.device.send_key = mock.Mock()
        self.device.media_play_pause()
        self.device.send_key.assert_called_once_with("KEY_PAUSE")
        self.assertFalse(self.device._playing)

    def test_media_play(self):
        """Test for media_play."""
        self.device.send_key = mock.Mock()
        self.device._playing = False
        self.device.media_play()
        self.device.send_key.assert_called_once_with("KEY_PLAY")
        self.assertTrue(self.device._playing)

    def test_media_pause(self):
        """Test for media_pause."""
        self.device.send_key = mock.Mock()
        self.device._playing = True
        self.device.media_pause()
        self.device.send_key.assert_called_once_with("KEY_PAUSE")
        self.assertFalse(self.device._playing)

    def test_media_next_track(self):
        """Test for media_next_track."""
        self.device.send_key = mock.Mock()
        self.device.media_next_track()
        self.device.send_key.assert_called_once_with("KEY_FF")

    def test_media_previous_track(self):
        """Test for media_previous_track."""
        self.device.send_key = mock.Mock()
        self.device.media_previous_track()
        self.device.send_key.assert_called_once_with("KEY_REWIND")

    def test_turn_on(self):
        """Test turn on."""
        self.device.send_key = mock.Mock()
        self.device._mac = None
        self.device.turn_on()
        self.device.send_key.assert_called_once_with('KEY_POWERON')
        self.device._wol.send_magic_packet = mock.Mock()
        self.device._mac = "fake"
        self.device.turn_on()
        self.device._wol.send_magic_packet.assert_called_once_with("fake")


@pytest.fixture
def samsung_mock():
    """Mock samsungctl."""
    with patch.dict('sys.modules', {
        'samsungctl': MagicMock(),
    }):
        yield


async def test_play_media(hass, samsung_mock):
    """Test for play_media."""
    asyncio_sleep = asyncio.sleep
    sleeps = []

    async def sleep(duration, loop):
        sleeps.append(duration)
        await asyncio_sleep(0, loop=loop)

    with patch('asyncio.sleep', new=sleep):
        device = SamsungTVDevice(**WORKING_CONFIG)
        device.hass = hass

        device.send_key = mock.Mock()
        await device.async_play_media(MEDIA_TYPE_CHANNEL, "576")

        exp = [call("KEY_5"), call("KEY_7"), call("KEY_6")]
        assert device.send_key.call_args_list == exp
        assert len(sleeps) == 3


async def test_play_media_invalid_type(hass, samsung_mock):
    """Test for play_media with invalid media type."""
    url = "https://example.com"
    device = SamsungTVDevice(**WORKING_CONFIG)
    device.send_key = mock.Mock()
    await device.async_play_media(MEDIA_TYPE_URL, url)
    assert device.send_key.call_count == 0


async def test_play_media_channel_as_string(hass, samsung_mock):
    """Test for play_media with invalid channel as string."""
    url = "https://example.com"
    device = SamsungTVDevice(**WORKING_CONFIG)
    device.send_key = mock.Mock()
    await device.async_play_media(MEDIA_TYPE_CHANNEL, url)
    assert device.send_key.call_count == 0


async def test_play_media_channel_as_non_positive(hass, samsung_mock):
    """Test for play_media with invalid channel as non positive integer."""
    device = SamsungTVDevice(**WORKING_CONFIG)
    device.send_key = mock.Mock()
    await device.async_play_media(MEDIA_TYPE_CHANNEL, "-4")
    assert device.send_key.call_count == 0
