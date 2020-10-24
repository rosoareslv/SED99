"""
Connects to LCN platform.

For more details about this platform, please refer to the documentation at
https://home-assistant.io/components/lcn/
"""

import logging
import re

import voluptuous as vol

from homeassistant.const import (
    CONF_ADDRESS, CONF_HOST, CONF_LIGHTS, CONF_NAME, CONF_PASSWORD, CONF_PORT,
    CONF_USERNAME)
import homeassistant.helpers.config_validation as cv
from homeassistant.helpers.discovery import async_load_platform
from homeassistant.helpers.entity import Entity

REQUIREMENTS = ['pypck==0.5.9']

_LOGGER = logging.getLogger(__name__)

DOMAIN = 'lcn'
DATA_LCN = 'lcn'
DEFAULT_NAME = 'pchk'

CONF_SK_NUM_TRIES = 'sk_num_tries'
CONF_DIM_MODE = 'dim_mode'
CONF_OUTPUT = 'output'
CONF_TRANSITION = 'transition'
CONF_DIMMABLE = 'dimmable'
CONF_CONNECTIONS = 'connections'

DIM_MODES = ['steps50', 'steps200']
OUTPUT_PORTS = ['output1', 'output2', 'output3', 'output4']

# Regex for address validation
PATTERN_ADDRESS = re.compile('^((?P<conn_id>\\w+)\\.)?s?(?P<seg_id>\\d+)'
                             '\\.(?P<type>m|g)?(?P<id>\\d+)$')


def has_unique_connection_names(connections):
    """Validate that all connection names are unique.

    Use 'pchk' as default connection_name (or add a numeric suffix if
    pchk' is already in use.
    """
    for suffix, connection in enumerate(connections):
        connection_name = connection.get(CONF_NAME)
        if connection_name is None:
            if suffix == 0:
                connection[CONF_NAME] = DEFAULT_NAME
            else:
                connection[CONF_NAME] = '{}{:d}'.format(DEFAULT_NAME, suffix)

    schema = vol.Schema(vol.Unique())
    schema([connection.get(CONF_NAME) for connection in connections])
    return connections


def is_address(value):
    """Validate the given address string.

    Examples for S000M005 at myhome:
        myhome.s000.m005
        myhome.s0.m5
        myhome.0.5    ("m" is implicit if missing)

    Examples for s000g011
        myhome.0.g11
        myhome.s0.g11
    """
    matcher = PATTERN_ADDRESS.match(value)
    if matcher:
        is_group = (matcher.group('type') == 'g')
        addr = (int(matcher.group('seg_id')),
                int(matcher.group('id')),
                is_group)
        conn_id = matcher.group('conn_id')
        return addr, conn_id
    raise vol.error.Invalid('Not a valid address string.')


LIGHTS_SCHEMA = vol.Schema({
    vol.Required(CONF_NAME): cv.string,
    vol.Required(CONF_ADDRESS): is_address,
    vol.Required(CONF_OUTPUT): vol.All(vol.In(OUTPUT_PORTS), vol.Upper),
    vol.Optional(CONF_DIMMABLE, default=False): vol.Coerce(bool),
    vol.Optional(CONF_TRANSITION, default=0):
        vol.All(vol.Coerce(float), vol.Range(min=0., max=486.),
                lambda value: value * 1000),
})

CONNECTION_SCHEMA = vol.Schema({
    vol.Required(CONF_HOST): cv.string,
    vol.Required(CONF_PORT): cv.port,
    vol.Required(CONF_USERNAME): cv.string,
    vol.Required(CONF_PASSWORD): cv.string,
    vol.Optional(CONF_SK_NUM_TRIES, default=3): cv.positive_int,
    vol.Optional(CONF_DIM_MODE, default='steps50'): vol.All(vol.In(DIM_MODES),
                                                            vol.Upper),
    vol.Optional(CONF_NAME): cv.string
})

CONFIG_SCHEMA = vol.Schema({
    DOMAIN: vol.Schema({
        vol.Required(CONF_CONNECTIONS): vol.All(
            cv.ensure_list, has_unique_connection_names, [CONNECTION_SCHEMA]),
        vol.Required(CONF_LIGHTS): vol.All(cv.ensure_list, [LIGHTS_SCHEMA])
    })
}, extra=vol.ALLOW_EXTRA)


def get_connection(connections, connection_id=None):
    """Return the connection object from list."""
    if connection_id is None:
        connection = connections[0]
    else:
        for connection in connections:
            if connection.connection_id == connection_id:
                break
        else:
            raise ValueError('Unknown connection_id.')
    return connection


async def async_setup(hass, config):
    """Set up the LCN component."""
    import pypck
    from pypck.connection import PchkConnectionManager

    hass.data[DATA_LCN] = {}

    conf_connections = config[DOMAIN][CONF_CONNECTIONS]
    connections = []
    for conf_connection in conf_connections:
        connection_name = conf_connection.get(CONF_NAME)

        settings = {'SK_NUM_TRIES': conf_connection[CONF_SK_NUM_TRIES],
                    'DIM_MODE': pypck.lcn_defs.OutputPortDimMode[
                        conf_connection[CONF_DIM_MODE]]}

        connection = PchkConnectionManager(hass.loop,
                                           conf_connection[CONF_HOST],
                                           conf_connection[CONF_PORT],
                                           conf_connection[CONF_USERNAME],
                                           conf_connection[CONF_PASSWORD],
                                           settings=settings,
                                           connection_id=connection_name)

        try:
            # establish connection to PCHK server
            await hass.async_create_task(connection.async_connect(timeout=15))
            connections.append(connection)
            _LOGGER.info('LCN connected to "%s"', connection_name)
        except TimeoutError:
            _LOGGER.error('Connection to PCHK server "%s" failed.',
                          connection_name)
            return False

    hass.data[DATA_LCN][CONF_CONNECTIONS] = connections

    hass.async_create_task(
        async_load_platform(hass, 'light', DOMAIN,
                            config[DOMAIN][CONF_LIGHTS], config))

    return True


class LcnDevice(Entity):
    """Parent class for all devices associated with the LCN component."""

    def __init__(self, config, address_connection):
        """Initialize the LCN device."""
        import pypck
        self.pypck = pypck
        self.config = config
        self.address_connection = address_connection
        self._name = config[CONF_NAME]

    @property
    def should_poll(self) -> bool:
        """Lcn device entity pushes its state to HA."""
        return False

    async def async_added_to_hass(self) -> None:
        """Run when entity about to be added to hass."""
        self.address_connection.register_for_inputs(
            self.input_received)

    @property
    def name(self):
        """Return the name of the device."""
        return self._name

    def input_received(self, input_obj):
        """Set state/value when LCN input object (command) is received."""
        raise NotImplementedError('Pure virtual function.')
