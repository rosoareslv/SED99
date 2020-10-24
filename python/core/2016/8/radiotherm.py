"""
Support for Radio Thermostat wifi-enabled home thermostats.

For more details about this platform, please refer to the documentation at
https://home-assistant.io/components/climate.radiotherm/
"""
import datetime
import logging
from urllib.error import URLError

from homeassistant.components.climate import (
    STATE_AUTO, STATE_COOL, STATE_HEAT, STATE_IDLE, STATE_OFF,
    ClimateDevice)
from homeassistant.const import CONF_HOST, TEMP_FAHRENHEIT

REQUIREMENTS = ['radiotherm==1.2']
HOLD_TEMP = 'hold_temp'
_LOGGER = logging.getLogger(__name__)


def setup_platform(hass, config, add_devices, discovery_info=None):
    """Setup the Radio Thermostat."""
    import radiotherm

    hosts = []
    if CONF_HOST in config:
        hosts = config[CONF_HOST]
    else:
        hosts.append(radiotherm.discover.discover_address())

    if hosts is None:
        _LOGGER.error("No radiotherm thermostats detected.")
        return False

    hold_temp = config.get(HOLD_TEMP, False)
    tstats = []

    for host in hosts:
        try:
            tstat = radiotherm.get_thermostat(host)
            tstats.append(RadioThermostat(tstat, hold_temp))
        except (URLError, OSError):
            _LOGGER.exception("Unable to connect to Radio Thermostat: %s",
                              host)

    add_devices(tstats)


# pylint: disable=abstract-method
class RadioThermostat(ClimateDevice):
    """Representation of a Radio Thermostat."""

    def __init__(self, device, hold_temp):
        """Initialize the thermostat."""
        self.device = device
        self.set_time()
        self._target_temperature = None
        self._current_temperature = None
        self._current_operation = STATE_IDLE
        self._name = None
        self.hold_temp = hold_temp
        self.update()

    @property
    def name(self):
        """Return the name of the Radio Thermostat."""
        return self._name

    @property
    def unit_of_measurement(self):
        """Return the unit of measurement."""
        return TEMP_FAHRENHEIT

    @property
    def device_state_attributes(self):
        """Return the device specific state attributes."""
        return {
            "fan": self.device.fmode['human'],
            "mode": self.device.tmode['human']
        }

    @property
    def current_temperature(self):
        """Return the current temperature."""
        return self._current_temperature

    @property
    def current_operation(self):
        """Return the current operation. head, cool idle."""
        return self._current_operation

    @property
    def target_temperature(self):
        """Return the temperature we try to reach."""
        return self._target_temperature

    def update(self):
        """Update the data from the thermostat."""
        self._current_temperature = self.device.temp['raw']
        self._name = self.device.name['raw']
        if self.device.tmode['human'] == 'Cool':
            self._target_temperature = self.device.t_cool['raw']
            self._current_operation = STATE_COOL
        elif self.device.tmode['human'] == 'Heat':
            self._target_temperature = self.device.t_heat['raw']
            self._current_operation = STATE_HEAT
        else:
            self._current_operation = STATE_IDLE

    def set_temperature(self, temperature):
        """Set new target temperature."""
        if self._current_operation == STATE_COOL:
            self.device.t_cool = temperature
        elif self._current_operation == STATE_HEAT:
            self.device.t_heat = temperature
        if self.hold_temp:
            self.device.hold = 1
        else:
            self.device.hold = 0

    def set_time(self):
        """Set device time."""
        now = datetime.datetime.now()
        self.device.time = {'day': now.weekday(),
                            'hour': now.hour, 'minute': now.minute}

    def set_operation_mode(self, operation_mode):
        """Set operation mode (auto, cool, heat, off)."""
        if operation_mode == STATE_OFF:
            self.device.tmode = 0
        elif operation_mode == STATE_AUTO:
            self.device.tmode = 3
        elif operation_mode == STATE_COOL:
            self.device.t_cool = self._target_temperature
        elif operation_mode == STATE_HEAT:
            self.device.t_heat = self._target_temperature
