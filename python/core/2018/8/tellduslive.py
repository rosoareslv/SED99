"""
Support for Tellstick Net/Telstick Live.

For more details about this platform, please refer to the documentation at
https://home-assistant.io/components/sensor.tellduslive/
"""
import logging

from homeassistant.components.tellduslive import TelldusLiveEntity
from homeassistant.const import (
    DEVICE_CLASS_HUMIDITY, DEVICE_CLASS_ILLUMINANCE, DEVICE_CLASS_TEMPERATURE,
    TEMP_CELSIUS)

_LOGGER = logging.getLogger(__name__)

SENSOR_TYPE_TEMPERATURE = 'temp'
SENSOR_TYPE_HUMIDITY = 'humidity'
SENSOR_TYPE_RAINRATE = 'rrate'
SENSOR_TYPE_RAINTOTAL = 'rtot'
SENSOR_TYPE_WINDDIRECTION = 'wdir'
SENSOR_TYPE_WINDAVERAGE = 'wavg'
SENSOR_TYPE_WINDGUST = 'wgust'
SENSOR_TYPE_UV = 'uv'
SENSOR_TYPE_WATT = 'watt'
SENSOR_TYPE_LUMINANCE = 'lum'
SENSOR_TYPE_DEW_POINT = 'dewp'
SENSOR_TYPE_BAROMETRIC_PRESSURE = 'barpress'

SENSOR_TYPES = {
    SENSOR_TYPE_TEMPERATURE: ['Temperature', TEMP_CELSIUS, None,
                              DEVICE_CLASS_TEMPERATURE],
    SENSOR_TYPE_HUMIDITY: ['Humidity', '%', None, DEVICE_CLASS_HUMIDITY],
    SENSOR_TYPE_RAINRATE: ['Rain rate', 'mm/h', 'mdi:water', None],
    SENSOR_TYPE_RAINTOTAL: ['Rain total', 'mm', 'mdi:water', None],
    SENSOR_TYPE_WINDDIRECTION: ['Wind direction', '', '', None],
    SENSOR_TYPE_WINDAVERAGE: ['Wind average', 'm/s', '', None],
    SENSOR_TYPE_WINDGUST: ['Wind gust', 'm/s', '', None],
    SENSOR_TYPE_UV: ['UV', 'UV', '', None],
    SENSOR_TYPE_WATT: ['Power', 'W', '', None],
    SENSOR_TYPE_LUMINANCE: ['Luminance', 'lx', None, DEVICE_CLASS_ILLUMINANCE],
    SENSOR_TYPE_DEW_POINT:
        ['Dew Point', TEMP_CELSIUS, None, DEVICE_CLASS_TEMPERATURE],
    SENSOR_TYPE_BAROMETRIC_PRESSURE: ['Barometric Pressure', 'kPa', '', None],
}


def setup_platform(hass, config, add_entities, discovery_info=None):
    """Set up the Tellstick sensors."""
    if discovery_info is None:
        return
    add_entities(TelldusLiveSensor(hass, sensor) for sensor in discovery_info)


class TelldusLiveSensor(TelldusLiveEntity):
    """Representation of a Telldus Live sensor."""

    @property
    def device_id(self):
        """Return id of the device."""
        return self._id[0]

    @property
    def _type(self):
        """Return the type of the sensor."""
        return self._id[1]

    @property
    def _value(self):
        """Return value of the sensor."""
        return self.device.value(*self._id[1:])

    @property
    def _value_as_temperature(self):
        """Return the value as temperature."""
        return round(float(self._value), 1)

    @property
    def _value_as_luminance(self):
        """Return the value as luminance."""
        return round(float(self._value), 1)

    @property
    def _value_as_humidity(self):
        """Return the value as humidity."""
        return int(round(float(self._value)))

    @property
    def name(self):
        """Return the name of the sensor."""
        return '{} {}'.format(
            super().name,
            self.quantity_name or '')

    @property
    def state(self):
        """Return the state of the sensor."""
        if not self.available:
            return None
        if self._type == SENSOR_TYPE_TEMPERATURE:
            return self._value_as_temperature
        if self._type == SENSOR_TYPE_HUMIDITY:
            return self._value_as_humidity
        if self._type == SENSOR_TYPE_LUMINANCE:
            return self._value_as_luminance
        return self._value

    @property
    def quantity_name(self):
        """Name of quantity."""
        return SENSOR_TYPES[self._type][0] \
            if self._type in SENSOR_TYPES else None

    @property
    def unit_of_measurement(self):
        """Return the unit of measurement."""
        return SENSOR_TYPES[self._type][1] \
            if self._type in SENSOR_TYPES else None

    @property
    def icon(self):
        """Return the icon."""
        return SENSOR_TYPES[self._type][2] \
            if self._type in SENSOR_TYPES else None

    @property
    def device_class(self):
        """Return the device class."""
        return SENSOR_TYPES[self._type][3] \
            if self._type in SENSOR_TYPES else None
