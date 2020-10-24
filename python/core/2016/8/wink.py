"""
Support for Wink sensors.

For more details about this platform, please refer to the documentation at
at https://home-assistant.io/components/sensor.wink/
"""
import logging

from homeassistant.const import (CONF_ACCESS_TOKEN, STATE_CLOSED,
                                 STATE_OPEN, TEMP_CELSIUS)
from homeassistant.helpers.entity import Entity
from homeassistant.components.wink import WinkDevice
from homeassistant.loader import get_component

REQUIREMENTS = ['python-wink==0.7.13', 'pubnub==3.8.2']

SENSOR_TYPES = ['temperature', 'humidity']


def setup_platform(hass, config, add_devices, discovery_info=None):
    """Setup the Wink platform."""
    import pywink

    if discovery_info is None:
        token = config.get(CONF_ACCESS_TOKEN)

        if token is None:
            logging.getLogger(__name__).error(
                "Missing wink access_token. "
                "Get one at https://winkbearertoken.appspot.com/")
            return

        pywink.set_bearer_token(token)

    for sensor in pywink.get_sensors():
        if sensor.capability() in SENSOR_TYPES:
            add_devices([WinkSensorDevice(sensor)])

    add_devices(WinkEggMinder(eggtray) for eggtray in pywink.get_eggtrays())


class WinkSensorDevice(WinkDevice, Entity):
    """Representation of a Wink sensor."""

    def __init__(self, wink):
        """Initialize the Wink device."""
        super().__init__(wink)
        wink = get_component('wink')
        self.capability = self.wink.capability()
        if self.wink.UNIT == "°":
            self._unit_of_measurement = TEMP_CELSIUS
        else:
            self._unit_of_measurement = self.wink.UNIT

    @property
    def state(self):
        """Return the state."""
        if self.capability == "humidity":
            return round(self.wink.humidity_percentage())
        elif self.capability == "temperature":
            return round(self.wink.temperature_float(), 1)
        else:
            return STATE_OPEN if self.is_open else STATE_CLOSED

    @property
    def unit_of_measurement(self):
        """Return the unit of measurement of this entity, if any."""
        return self._unit_of_measurement

    @property
    def is_open(self):
        """Return true if door is open."""
        return self.wink.state()


class WinkEggMinder(WinkDevice, Entity):
    """Representation of a Wink Egg Minder."""

    def __init__(self, wink):
        """Initialize the sensor."""
        WinkDevice.__init__(self, wink)

    @property
    def state(self):
        """Return the state."""
        return self.wink.state()
