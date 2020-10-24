"""Generic Philips Hue component tests."""

import logging
import unittest
from unittest.mock import call, MagicMock, patch

from homeassistant.components import configurator, hue
from homeassistant.const import CONF_FILENAME, CONF_HOST
from homeassistant.setup import setup_component

from tests.common import (
    assert_setup_component, get_test_home_assistant, get_test_config_dir,
    MockDependency
)

_LOGGER = logging.getLogger(__name__)


class TestSetup(unittest.TestCase):
    """Test the Hue component."""

    def setUp(self):  # pylint: disable=invalid-name
        """Setup things to be run when tests are started."""
        self.hass = get_test_home_assistant()
        self.skip_teardown_stop = False

    def tearDown(self):
        """Stop everything that was started."""
        if not self.skip_teardown_stop:
            self.hass.stop()

    @MockDependency('phue')
    def test_setup_no_domain(self, mock_phue):
        """If it's not in the config we won't even try."""
        with assert_setup_component(0):
            self.assertTrue(setup_component(
                self.hass, hue.DOMAIN, {}))
            mock_phue.Bridge.assert_not_called()
            self.assertEquals({}, self.hass.data[hue.DOMAIN])

    @MockDependency('phue')
    def test_setup_no_host(self, mock_phue):
        """No host specified in any way."""
        with assert_setup_component(1):
            self.assertTrue(setup_component(
                self.hass, hue.DOMAIN, {hue.DOMAIN: {}}))
            mock_phue.Bridge.assert_not_called()
            self.assertEquals({}, self.hass.data[hue.DOMAIN])

    @MockDependency('phue')
    def test_setup_with_host(self, mock_phue):
        """Host specified in the config file."""
        mock_bridge = mock_phue.Bridge

        with assert_setup_component(1):
            with patch('homeassistant.helpers.discovery.load_platform') \
                    as mock_load:
                self.assertTrue(setup_component(
                    self.hass, hue.DOMAIN,
                    {hue.DOMAIN: {hue.CONF_BRIDGES: [
                        {CONF_HOST: 'localhost'}]}}))

                mock_bridge.assert_called_once_with(
                    'localhost',
                    config_file_path=get_test_config_dir(hue.PHUE_CONFIG_FILE))
                mock_load.assert_called_once_with(
                    self.hass, 'light', hue.DOMAIN,
                    {'bridge_id': '127.0.0.1'})

                self.assertTrue(hue.DOMAIN in self.hass.data)
                self.assertEquals(1, len(self.hass.data[hue.DOMAIN]))

    @MockDependency('phue')
    def test_setup_with_phue_conf(self, mock_phue):
        """No host in the config file, but one is cached in phue.conf."""
        mock_bridge = mock_phue.Bridge

        with assert_setup_component(1):
            with patch(
                    'homeassistant.components.hue._find_host_from_config',
                    return_value='localhost'):
                with patch('homeassistant.helpers.discovery.load_platform') \
                        as mock_load:
                    self.assertTrue(setup_component(
                        self.hass, hue.DOMAIN,
                        {hue.DOMAIN: {hue.CONF_BRIDGES: [
                            {CONF_FILENAME: 'phue.conf'}]}}))

                    mock_bridge.assert_called_once_with(
                        'localhost',
                        config_file_path=get_test_config_dir(
                            hue.PHUE_CONFIG_FILE))
                    mock_load.assert_called_once_with(
                        self.hass, 'light', hue.DOMAIN,
                        {'bridge_id': '127.0.0.1'})

                    self.assertTrue(hue.DOMAIN in self.hass.data)
                    self.assertEquals(1, len(self.hass.data[hue.DOMAIN]))

    @MockDependency('phue')
    def test_setup_with_multiple_hosts(self, mock_phue):
        """Multiple hosts specified in the config file."""
        mock_bridge = mock_phue.Bridge

        with assert_setup_component(1):
            with patch('homeassistant.helpers.discovery.load_platform') \
                    as mock_load:
                self.assertTrue(setup_component(
                    self.hass, hue.DOMAIN,
                    {hue.DOMAIN: {hue.CONF_BRIDGES: [
                        {CONF_HOST: 'localhost'},
                        {CONF_HOST: '192.168.0.1'}]}}))

                mock_bridge.assert_has_calls([
                    call(
                        'localhost',
                        config_file_path=get_test_config_dir(
                            hue.PHUE_CONFIG_FILE)),
                    call(
                        '192.168.0.1',
                        config_file_path=get_test_config_dir(
                            hue.PHUE_CONFIG_FILE))])
                mock_load.mock_bridge.assert_not_called()
                mock_load.assert_has_calls([
                    call(
                        self.hass, 'light', hue.DOMAIN,
                        {'bridge_id': '127.0.0.1'}),
                    call(
                        self.hass, 'light', hue.DOMAIN,
                        {'bridge_id': '192.168.0.1'}),
                ], any_order=True)

                self.assertTrue(hue.DOMAIN in self.hass.data)
                self.assertEquals(2, len(self.hass.data[hue.DOMAIN]))

    @MockDependency('phue')
    def test_bridge_discovered(self, mock_phue):
        """Bridge discovery."""
        mock_bridge = mock_phue.Bridge
        mock_service = MagicMock()
        discovery_info = {'host': '192.168.0.10', 'serial': 'foobar'}

        with patch('homeassistant.helpers.discovery.load_platform') \
                as mock_load:
            self.assertTrue(setup_component(
                self.hass, hue.DOMAIN, {}))
            hue.bridge_discovered(self.hass, mock_service, discovery_info)

            mock_bridge.assert_called_once_with(
                '192.168.0.10',
                config_file_path=get_test_config_dir('phue-foobar.conf'))
            mock_load.assert_called_once_with(
                self.hass, 'light', hue.DOMAIN,
                {'bridge_id': '192.168.0.10'})

            self.assertTrue(hue.DOMAIN in self.hass.data)
            self.assertEquals(1, len(self.hass.data[hue.DOMAIN]))

    @MockDependency('phue')
    def test_bridge_configure_and_discovered(self, mock_phue):
        """Bridge is in the config file, then we discover it."""
        mock_bridge = mock_phue.Bridge
        mock_service = MagicMock()
        discovery_info = {'host': '192.168.1.10', 'serial': 'foobar'}

        with assert_setup_component(1):
            with patch('homeassistant.helpers.discovery.load_platform') \
                    as mock_load:
                # First we set up the component from config
                self.assertTrue(setup_component(
                    self.hass, hue.DOMAIN,
                    {hue.DOMAIN: {hue.CONF_BRIDGES: [
                        {CONF_HOST: '192.168.1.10'}]}}))

                mock_bridge.assert_called_once_with(
                    '192.168.1.10',
                    config_file_path=get_test_config_dir(
                        hue.PHUE_CONFIG_FILE))
                calls_to_mock_load = [
                    call(
                        self.hass, 'light', hue.DOMAIN,
                        {'bridge_id': '192.168.1.10'}),
                ]
                mock_load.assert_has_calls(calls_to_mock_load)

                self.assertTrue(hue.DOMAIN in self.hass.data)
                self.assertEquals(1, len(self.hass.data[hue.DOMAIN]))

                # Then we discover the same bridge
                hue.bridge_discovered(self.hass, mock_service, discovery_info)

                # No additional calls
                mock_bridge.assert_called_once_with(
                    '192.168.1.10',
                    config_file_path=get_test_config_dir(
                        hue.PHUE_CONFIG_FILE))
                mock_load.assert_has_calls(calls_to_mock_load)

                # Still only one
                self.assertTrue(hue.DOMAIN in self.hass.data)
                self.assertEquals(1, len(self.hass.data[hue.DOMAIN]))


class TestHueBridge(unittest.TestCase):
    """Test the HueBridge class."""

    def setUp(self):  # pylint: disable=invalid-name
        """Setup things to be run when tests are started."""
        self.hass = get_test_home_assistant()
        self.hass.data[hue.DOMAIN] = {}
        self.skip_teardown_stop = False

    def tearDown(self):
        """Stop everything that was started."""
        if not self.skip_teardown_stop:
            self.hass.stop()

    @MockDependency('phue')
    def test_setup_bridge_connection_refused(self, mock_phue):
        """Test a registration failed with a connection refused exception."""
        mock_bridge = mock_phue.Bridge
        mock_bridge.side_effect = ConnectionRefusedError()

        bridge = hue.HueBridge('localhost', self.hass, hue.PHUE_CONFIG_FILE)
        bridge.setup()
        self.assertFalse(bridge.configured)
        self.assertTrue(bridge.config_request_id is None)

        mock_bridge.assert_called_once_with(
            'localhost',
            config_file_path=get_test_config_dir(hue.PHUE_CONFIG_FILE))

    @MockDependency('phue')
    def test_setup_bridge_registration_exception(self, mock_phue):
        """Test a registration failed with an exception."""
        mock_bridge = mock_phue.Bridge
        mock_phue.PhueRegistrationException = Exception
        mock_bridge.side_effect = mock_phue.PhueRegistrationException(1, 2)

        bridge = hue.HueBridge('localhost', self.hass, hue.PHUE_CONFIG_FILE)
        bridge.setup()
        self.assertFalse(bridge.configured)
        self.assertFalse(bridge.config_request_id is None)
        self.assertTrue(isinstance(bridge.config_request_id, str))

        mock_bridge.assert_called_once_with(
            'localhost',
            config_file_path=get_test_config_dir(hue.PHUE_CONFIG_FILE))

    @MockDependency('phue')
    def test_setup_bridge_registration_succeeds(self, mock_phue):
        """Test a registration success sequence."""
        mock_bridge = mock_phue.Bridge
        mock_phue.PhueRegistrationException = Exception
        mock_bridge.side_effect = [
            # First call, raise because not registered
            mock_phue.PhueRegistrationException(1, 2),
            # Second call, registration is done
            None,
        ]

        bridge = hue.HueBridge('localhost', self.hass, hue.PHUE_CONFIG_FILE)
        bridge.setup()
        self.assertFalse(bridge.configured)
        self.assertFalse(bridge.config_request_id is None)

        # Simulate the user confirming the registration
        self.hass.services.call(
            configurator.DOMAIN, configurator.SERVICE_CONFIGURE,
            {configurator.ATTR_CONFIGURE_ID: bridge.config_request_id})

        self.hass.block_till_done()
        self.assertTrue(bridge.configured)
        self.assertTrue(bridge.config_request_id is None)

        # We should see a total of two identical calls
        args = call(
            'localhost',
            config_file_path=get_test_config_dir(hue.PHUE_CONFIG_FILE))
        mock_bridge.assert_has_calls([args, args])

        # Make sure the request is done
        self.assertEqual(1, len(self.hass.states.all()))
        self.assertEqual('configured', self.hass.states.all()[0].state)

    @MockDependency('phue')
    def test_setup_bridge_registration_fails(self, mock_phue):
        """
        Test a registration failure sequence.

        This may happen when we start the registration process, the user
        responds to the request but the bridge has become unreachable.
        """
        mock_bridge = mock_phue.Bridge
        mock_phue.PhueRegistrationException = Exception
        mock_bridge.side_effect = [
            # First call, raise because not registered
            mock_phue.PhueRegistrationException(1, 2),
            # Second call, the bridge has gone away
            ConnectionRefusedError(),
        ]

        bridge = hue.HueBridge('localhost', self.hass, hue.PHUE_CONFIG_FILE)
        bridge.setup()
        self.assertFalse(bridge.configured)
        self.assertFalse(bridge.config_request_id is None)

        # Simulate the user confirming the registration
        self.hass.services.call(
            configurator.DOMAIN, configurator.SERVICE_CONFIGURE,
            {configurator.ATTR_CONFIGURE_ID: bridge.config_request_id})

        self.hass.block_till_done()
        self.assertFalse(bridge.configured)
        self.assertFalse(bridge.config_request_id is None)

        # We should see a total of two identical calls
        args = call(
            'localhost',
            config_file_path=get_test_config_dir(hue.PHUE_CONFIG_FILE))
        mock_bridge.assert_has_calls([args, args])

        # The request should still be pending
        self.assertEqual(1, len(self.hass.states.all()))
        self.assertEqual('configure', self.hass.states.all()[0].state)

    @MockDependency('phue')
    def test_setup_bridge_registration_retry(self, mock_phue):
        """
        Test a registration retry sequence.

        This may happen when we start the registration process, the user
        responds to the request but we fail to confirm it with the bridge.
        """
        mock_bridge = mock_phue.Bridge
        mock_phue.PhueRegistrationException = Exception
        mock_bridge.side_effect = [
            # First call, raise because not registered
            mock_phue.PhueRegistrationException(1, 2),
            # Second call, for whatever reason authentication fails
            mock_phue.PhueRegistrationException(1, 2),
        ]

        bridge = hue.HueBridge('localhost', self.hass, hue.PHUE_CONFIG_FILE)
        bridge.setup()
        self.assertFalse(bridge.configured)
        self.assertFalse(bridge.config_request_id is None)

        # Simulate the user confirming the registration
        self.hass.services.call(
            configurator.DOMAIN, configurator.SERVICE_CONFIGURE,
            {configurator.ATTR_CONFIGURE_ID: bridge.config_request_id})

        self.hass.block_till_done()
        self.assertFalse(bridge.configured)
        self.assertFalse(bridge.config_request_id is None)

        # We should see a total of two identical calls
        args = call(
            'localhost',
            config_file_path=get_test_config_dir(hue.PHUE_CONFIG_FILE))
        mock_bridge.assert_has_calls([args, args])

        # Make sure the request is done
        self.assertEqual(1, len(self.hass.states.all()))
        self.assertEqual('configure', self.hass.states.all()[0].state)
        self.assertEqual(
            'Failed to register, please try again.',
            self.hass.states.all()[0].attributes.get(configurator.ATTR_ERRORS))

    @MockDependency('phue')
    def test_hue_activate_scene(self, mock_phue):
        """Test the hue_activate_scene service."""
        with patch('homeassistant.helpers.discovery.load_platform'):
            bridge = hue.HueBridge('localhost', self.hass,
                                   hue.PHUE_CONFIG_FILE)
            bridge.setup()

            # No args
            self.hass.services.call(hue.DOMAIN, hue.SERVICE_HUE_SCENE,
                                    blocking=True)
            bridge.bridge.run_scene.assert_not_called()

            # Only one arg
            self.hass.services.call(
                hue.DOMAIN, hue.SERVICE_HUE_SCENE,
                {hue.ATTR_GROUP_NAME: 'group'},
                blocking=True)
            bridge.bridge.run_scene.assert_not_called()

            self.hass.services.call(
                hue.DOMAIN, hue.SERVICE_HUE_SCENE,
                {hue.ATTR_SCENE_NAME: 'scene'},
                blocking=True)
            bridge.bridge.run_scene.assert_not_called()

            # Both required args
            self.hass.services.call(
                hue.DOMAIN, hue.SERVICE_HUE_SCENE,
                {hue.ATTR_GROUP_NAME: 'group', hue.ATTR_SCENE_NAME: 'scene'},
                blocking=True)
            bridge.bridge.run_scene.assert_called_once_with('group', 'scene')
