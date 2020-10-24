"""
Monitors home energy use for the ELIQ Online service.

For more details about this platform, please refer to the documentation at
https://home-assistant.io/components/sensor.eliqonline/
"""
import logging
from urllib.error import URLError

from homeassistant.const import CONF_ACCESS_TOKEN, CONF_NAME, STATE_UNKNOWN
from homeassistant.helpers.entity import Entity

_LOGGER = logging.getLogger(__name__)

REQUIREMENTS = ['eliqonline==1.0.12']
DEFAULT_NAME = "ELIQ Online"
UNIT_OF_MEASUREMENT = "W"
ICON = "mdi:speedometer"
CONF_CHANNEL_ID = "channel_id"
SCAN_INTERVAL = 60


def setup_platform(hass, config, add_devices, discovery_info=None):
    """Setup the ELIQ Online sensor."""
    import eliqonline

    access_token = config.get(CONF_ACCESS_TOKEN)
    name = config.get(CONF_NAME, DEFAULT_NAME)
    channel_id = config.get(CONF_CHANNEL_ID)

    if access_token is None:
        _LOGGER.error(
            "Configuration Error: "
            "Please make sure you have configured your access token "
            "that can be aquired from https://my.eliq.se/user/settings/api")
        return False

    api = eliqonline.API(access_token)

    try:
        _LOGGER.debug("Probing for access to ELIQ Online API")
        api.get_data_now(channelid=channel_id)
    except URLError:
        _LOGGER.error("Could not access the ELIQ Online API. "
                      "Is the configuration valid?")
        return False

    add_devices([EliqSensor(api, channel_id, name)])


class EliqSensor(Entity):
    """Implementation of an ELIQ Online sensor."""

    def __init__(self, api, channel_id, name):
        """Initialize the sensor."""
        self._name = name
        self._state = STATE_UNKNOWN
        self._api = api
        self._channel_id = channel_id
        self.update()

    @property
    def name(self):
        """Return the name of the sensor."""
        return self._name

    @property
    def icon(self):
        """Return icon."""
        return ICON

    @property
    def unit_of_measurement(self):
        """Return the unit of measurement of this entity, if any."""
        return UNIT_OF_MEASUREMENT

    @property
    def state(self):
        """Return the state of the device."""
        return self._state

    def update(self):
        """Get the latest data."""
        try:
            response = self._api.get_data_now(channelid=self._channel_id)
            self._state = int(response.power)
            _LOGGER.debug("Updated power from server %d W", self._state)
        except URLError:
            _LOGGER.error("Could not connect to the ELIQ Online API")
