"""
homeassistant.components.sensor.rpi_gpio
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
Allows to configure a binary state sensor using RPi GPIO.

For more details about this platform, please refer to the documentation at
https://home-assistant.io/components/sensor.rpi_gpio/
"""
# pylint: disable=import-error
import logging
from homeassistant.helpers.entity import Entity

from homeassistant.const import (DEVICE_DEFAULT_NAME,
                                 EVENT_HOMEASSISTANT_START,
                                 EVENT_HOMEASSISTANT_STOP)

DEFAULT_PULL_MODE = "UP"
DEFAULT_VALUE_HIGH = "HIGH"
DEFAULT_VALUE_LOW = "LOW"
DEFAULT_BOUNCETIME = 50

REQUIREMENTS = ['RPi.GPIO==0.5.11']
_LOGGER = logging.getLogger(__name__)


# pylint: disable=unused-argument
def setup_platform(hass, config, add_devices, discovery_info=None):
    """ Sets up the Raspberry PI GPIO ports. """
    import RPi.GPIO as GPIO
    GPIO.setmode(GPIO.BCM)

    sensors = []
    pull_mode = config.get('pull_mode', DEFAULT_PULL_MODE)
    value_high = config.get('value_high', DEFAULT_VALUE_HIGH)
    value_low = config.get('value_low', DEFAULT_VALUE_LOW)
    bouncetime = config.get('bouncetime', DEFAULT_BOUNCETIME)
    ports = config.get('ports')
    for port_num, port_name in ports.items():
        sensors.append(RPiGPIOSensor(
            port_name, port_num, pull_mode,
            value_high, value_low, bouncetime))
    add_devices(sensors)

    def cleanup_gpio(event):
        """ Stuff to do before stop home assistant. """
        # pylint: disable=no-member
        GPIO.cleanup()

    def prepare_gpio(event):
        """ Stuff to do when home assistant starts. """
        hass.bus.listen_once(EVENT_HOMEASSISTANT_STOP, cleanup_gpio)

    hass.bus.listen_once(EVENT_HOMEASSISTANT_START, prepare_gpio)


# pylint: disable=too-many-arguments, too-many-instance-attributes
class RPiGPIOSensor(Entity):
    """ Sets up the Raspberry PI GPIO ports. """
    def __init__(self, port_name, port_num, pull_mode,
                 value_high, value_low, bouncetime):
        # pylint: disable=no-member
        import RPi.GPIO as GPIO
        self._name = port_name or DEVICE_DEFAULT_NAME
        self._port = port_num
        self._pull = GPIO.PUD_DOWN if pull_mode == "DOWN" else GPIO.PUD_UP
        self._vhigh = value_high
        self._vlow = value_low
        self._bouncetime = bouncetime
        GPIO.setup(self._port, GPIO.IN, pull_up_down=self._pull)
        self._state = self._vhigh if GPIO.input(self._port) else self._vlow

        def edge_callback(channel):
            """ port changed state """
            # pylint: disable=no-member
            self._state = self._vhigh if GPIO.input(channel) else self._vlow
            self.update_ha_state()

        GPIO.add_event_detect(
            self._port,
            GPIO.BOTH,
            callback=edge_callback,
            bouncetime=self._bouncetime)

    @property
    def should_poll(self):
        """ No polling needed. """
        return False

    @property
    def name(self):
        """ The name of the sensor. """
        return self._name

    @property
    def state(self):
        """ Returns the state of the entity. """
        return self._state
