"""
Support for Forecast.io weather service.

For more details about this platform, please refer to the documentation at
https://home-assistant.io/components/sensor.forecast/
"""
import logging
from datetime import timedelta
import voluptuous as vol
from requests.exceptions import ConnectionError as ConnectError, \
    HTTPError, Timeout

import homeassistant.helpers.config_validation as cv
from homeassistant.components.sensor import PLATFORM_SCHEMA
from homeassistant.const import (CONF_API_KEY, CONF_NAME,
                                 CONF_MONITORED_CONDITIONS)
from homeassistant.helpers.entity import Entity
from homeassistant.util import Throttle

REQUIREMENTS = ['python-forecastio==1.3.4']
_LOGGER = logging.getLogger(__name__)

# Sensor types are defined like so:
# Name, si unit, us unit, ca unit, uk unit, uk2 unit
SENSOR_TYPES = {
    'summary': ['Summary', None, None, None, None, None],
    'minutely_summary': ['Minutely Summary', None, None, None, None, None],
    'hourly_summary': ['Hourly Summary', None, None, None, None, None],
    'daily_summary': ['Daily Summary', None, None, None, None, None],
    'icon': ['Icon', None, None, None, None, None],
    'nearest_storm_distance': ['Nearest Storm Distance',
                               'km', 'm', 'km', 'km', 'm'],
    'nearest_storm_bearing': ['Nearest Storm Bearing',
                              '°', '°', '°', '°', '°'],
    'precip_type': ['Precip', None, None, None, None, None],
    'precip_intensity': ['Precip Intensity', 'mm', 'in', 'mm', 'mm', 'mm'],
    'precip_probability': ['Precip Probability', '%', '%', '%', '%', '%'],
    'temperature': ['Temperature', '°C', '°F', '°C', '°C', '°C'],
    'apparent_temperature': ['Apparent Temperature',
                             '°C', '°F', '°C', '°C', '°C'],
    'dew_point': ['Dew point', '°C', '°F', '°C', '°C', '°C'],
    'wind_speed': ['Wind Speed', 'm/s', 'mph', 'km/h', 'mph', 'mph'],
    'wind_bearing': ['Wind Bearing', '°', '°', '°', '°', '°'],
    'cloud_cover': ['Cloud Coverage', '%', '%', '%', '%', '%'],
    'humidity': ['Humidity', '%', '%', '%', '%', '%'],
    'pressure': ['Pressure', 'mbar', 'mbar', 'mbar', 'mbar', 'mbar'],
    'visibility': ['Visibility', 'km', 'm', 'km', 'km', 'm'],
    'ozone': ['Ozone', 'DU', 'DU', 'DU', 'DU', 'DU'],
    'apparent_temperature_max': ['Daily High Apparent Temperature',
                                 '°C', '°F', '°C', '°C', '°C'],
    'apparent_temperature_min': ['Daily Low Apparent Temperature',
                                 '°C', '°F', '°C', '°C', '°C'],
    'temperature_max': ['Daily High Temperature',
                        '°C', '°F', '°C', '°C', '°C'],
    'temperature_min': ['Daily Low Temperature',
                        '°C', '°F', '°C', '°C', '°C'],
    'precip_intensity_max': ['Daily Max Precip Intensity',
                             'mm', 'in', 'mm', 'mm', 'mm'],
}
DEFAULT_NAME = "Forecast.io"
CONF_UNITS = 'units'

PLATFORM_SCHEMA = PLATFORM_SCHEMA.extend({
    vol.Required(CONF_MONITORED_CONDITIONS):
        vol.All(cv.ensure_list, [vol.In(list(SENSOR_TYPES))]),
    vol.Required(CONF_API_KEY): cv.string,
    vol.Optional(CONF_NAME, default=DEFAULT_NAME): cv.string,
    vol.Optional(CONF_UNITS): vol.In(['auto', 'si', 'us', 'ca', 'uk', 'uk2'])
})


# Return cached results if last scan was less then this time ago.
MIN_TIME_BETWEEN_UPDATES = timedelta(seconds=120)


def setup_platform(hass, config, add_devices, discovery_info=None):
    """Setup the Forecast.io sensor."""
    # Validate the configuration
    if None in (hass.config.latitude, hass.config.longitude):
        _LOGGER.error("Latitude or longitude not set in Home Assistant config")
        return False

    if CONF_UNITS in config:
        units = config[CONF_UNITS]
    elif hass.config.units.is_metric:
        units = 'si'
    else:
        units = 'us'

    # Create a data fetcher to support all of the configured sensors. Then make
    # the first call to init the data and confirm we can connect.
    try:
        forecast_data = ForeCastData(
            config.get(CONF_API_KEY, None), hass.config.latitude,
            hass.config.longitude, units)
        forecast_data.update_currently()
    except ValueError as error:
        _LOGGER.error(error)
        return False

    name = config.get(CONF_NAME)

    # Initialize and add all of the sensors.
    sensors = []
    for variable in config[CONF_MONITORED_CONDITIONS]:
        sensors.append(ForeCastSensor(forecast_data, variable, name))

    add_devices(sensors)


# pylint: disable=too-few-public-methods
class ForeCastSensor(Entity):
    """Implementation of a Forecast.io sensor."""

    def __init__(self, forecast_data, sensor_type, name):
        """Initialize the sensor."""
        self.client_name = name
        self._name = SENSOR_TYPES[sensor_type][0]
        self.forecast_data = forecast_data
        self.type = sensor_type
        self._state = None
        self._unit_of_measurement = None

        self.update()

    @property
    def name(self):
        """Return the name of the sensor."""
        return '{} {}'.format(self.client_name, self._name)

    @property
    def state(self):
        """Return the state of the sensor."""
        return self._state

    @property
    def unit_of_measurement(self):
        """Return the unit of measurement of this entity, if any."""
        return self._unit_of_measurement

    @property
    def unit_system(self):
        """Return the unit system of this entity."""
        return self.forecast_data.unit_system

    def update_unit_of_measurement(self):
        """Update units based on unit system."""
        unit_index = {
            'si': 1,
            'us': 2,
            'ca': 3,
            'uk': 4,
            'uk2': 5
        }.get(self.unit_system, 1)
        self._unit_of_measurement = SENSOR_TYPES[self.type][unit_index]

    def update(self):
        """Get the latest data from Forecast.io and updates the states."""
        # Call the API for new forecast data. Each sensor will re-trigger this
        # same exact call, but thats fine. We cache results for a short period
        # of time to prevent hitting API limits. Note that forecast.io will
        # charge users for too many calls in 1 day, so take care when updating.
        self.forecast_data.update()
        self.update_unit_of_measurement()

        if self.type == 'minutely_summary':
            self.forecast_data.update_minutely()
            minutely = self.forecast_data.data_minutely
            self._state = getattr(minutely, 'summary', '')
        elif self.type == 'hourly_summary':
            self.forecast_data.update_hourly()
            hourly = self.forecast_data.data_hourly
            self._state = getattr(hourly, 'summary', '')
        elif self.type in ['daily_summary',
                           'temperature_min', 'temperature_max',
                           'apparent_temperature_min',
                           'apparent_temperature_max',
                           'precip_intensity_max']:
            self.forecast_data.update_daily()
            daily = self.forecast_data.data_daily
            if self.type == 'daily_summary':
                self._state = getattr(daily, 'summary', '')
            else:
                if hasattr(daily, 'data'):
                    self._state = self.get_state(daily.data[0])
                else:
                    self._state = 0
        else:
            self.forecast_data.update_currently()
            currently = self.forecast_data.data_currently
            self._state = self.get_state(currently)

    def get_state(self, data):
        """
        Helper function that returns a new state based on the type.

        If the sensor type is unknown, the current state is returned.
        """
        lookup_type = convert_to_camel(self.type)
        state = getattr(data, lookup_type, 0)

        # Some state data needs to be rounded to whole values or converted to
        # percentages
        if self.type in ['precip_probability', 'cloud_cover', 'humidity']:
            return round(state * 100, 1)
        elif (self.type in ['dew_point', 'temperature', 'apparent_temperature',
                            'temperature_min', 'temperature_max',
                            'apparent_temperature_min',
                            'apparent_temperature_max',
                            'pressure', 'ozone']):
            return round(state, 1)
        return state


def convert_to_camel(data):
    """
    Convert snake case (foo_bar_bat) to camel case (fooBarBat).

    This is not pythonic, but needed for certain situations
    """
    components = data.split('_')
    return components[0] + "".join(x.title() for x in components[1:])


class ForeCastData(object):
    """Gets the latest data from Forecast.io."""

    # pylint: disable=too-many-instance-attributes

    def __init__(self, api_key, latitude, longitude, units):
        """Initialize the data object."""
        self._api_key = api_key
        self.latitude = latitude
        self.longitude = longitude
        self.units = units

        self.data = None
        self.unit_system = None
        self.data_currently = None
        self.data_minutely = None
        self.data_hourly = None
        self.data_daily = None

        self.update()

    @Throttle(MIN_TIME_BETWEEN_UPDATES)
    def update(self):
        """Get the latest data from Forecast.io."""
        import forecastio

        try:
            self.data = forecastio.load_forecast(self._api_key,
                                                 self.latitude,
                                                 self.longitude,
                                                 units=self.units)
        except (ConnectError, HTTPError, Timeout, ValueError) as error:
            raise ValueError("Unable to init Forecast.io. - %s", error)
        self.unit_system = self.data.json['flags']['units']

    @Throttle(MIN_TIME_BETWEEN_UPDATES)
    def update_currently(self):
        """Update currently data."""
        self.data_currently = self.data.currently()

    @Throttle(MIN_TIME_BETWEEN_UPDATES)
    def update_minutely(self):
        """Update minutely data."""
        self.data_minutely = self.data.minutely()

    @Throttle(MIN_TIME_BETWEEN_UPDATES)
    def update_hourly(self):
        """Update hourly data."""
        self.data_hourly = self.data.hourly()

    @Throttle(MIN_TIME_BETWEEN_UPDATES)
    def update_daily(self):
        """Update daily data."""
        self.data_daily = self.data.daily()
