"""Class to hold all light accessories."""
import logging

from homeassistant.components.light import (
    ATTR_HS_COLOR, ATTR_COLOR_TEMP, ATTR_BRIGHTNESS, ATTR_MIN_MIREDS,
    ATTR_MAX_MIREDS, SUPPORT_COLOR, SUPPORT_COLOR_TEMP, SUPPORT_BRIGHTNESS)
from homeassistant.const import ATTR_SUPPORTED_FEATURES, STATE_ON, STATE_OFF

from . import TYPES
from .accessories import (
    HomeAccessory, add_preload_service, debounce, setup_char)
from .const import (
    CATEGORY_LIGHT, SERV_LIGHTBULB, CHAR_COLOR_TEMPERATURE,
    CHAR_BRIGHTNESS, CHAR_HUE, CHAR_ON, CHAR_SATURATION)

_LOGGER = logging.getLogger(__name__)

RGB_COLOR = 'rgb_color'


@TYPES.register('Light')
class Light(HomeAccessory):
    """Generate a Light accessory for a light entity.

    Currently supports: state, brightness, color temperature, rgb_color.
    """

    def __init__(self, *args, config):
        """Initialize a new Light accessory object."""
        super().__init__(*args, category=CATEGORY_LIGHT)
        self._flag = {CHAR_ON: False, CHAR_BRIGHTNESS: False,
                      CHAR_HUE: False, CHAR_SATURATION: False,
                      CHAR_COLOR_TEMPERATURE: False, RGB_COLOR: False}
        self._state = 0

        self.chars = []
        self._features = self.hass.states.get(self.entity_id) \
            .attributes.get(ATTR_SUPPORTED_FEATURES)
        if self._features & SUPPORT_BRIGHTNESS:
            self.chars.append(CHAR_BRIGHTNESS)
        if self._features & SUPPORT_COLOR_TEMP:
            self.chars.append(CHAR_COLOR_TEMPERATURE)
        if self._features & SUPPORT_COLOR:
            self.chars.append(CHAR_HUE)
            self.chars.append(CHAR_SATURATION)
            self._hue = None
            self._saturation = None

        serv_light = add_preload_service(self, SERV_LIGHTBULB, self.chars)
        self.char_on = setup_char(
            CHAR_ON, serv_light, value=self._state, callback=self.set_state)

        if CHAR_BRIGHTNESS in self.chars:
            self.char_brightness = setup_char(
                CHAR_BRIGHTNESS, serv_light, value=0,
                callback=self.set_brightness)
        if CHAR_COLOR_TEMPERATURE in self.chars:
            min_mireds = self.hass.states.get(self.entity_id) \
                .attributes.get(ATTR_MIN_MIREDS, 153)
            max_mireds = self.hass.states.get(self.entity_id) \
                .attributes.get(ATTR_MAX_MIREDS, 500)
            self.char_color_temperature = setup_char(
                CHAR_COLOR_TEMPERATURE, serv_light, value=min_mireds,
                properties={'minValue': min_mireds, 'maxValue': max_mireds},
                callback=self.set_color_temperature)
        if CHAR_HUE in self.chars:
            self.char_hue = setup_char(
                CHAR_HUE, serv_light, value=0, callback=self.set_hue)
        if CHAR_SATURATION in self.chars:
            self.char_saturation = setup_char(
                CHAR_SATURATION, serv_light, value=75,
                callback=self.set_saturation)

    def set_state(self, value):
        """Set state if call came from HomeKit."""
        if self._state == value:
            return

        _LOGGER.debug('%s: Set state to %d', self.entity_id, value)
        self._flag[CHAR_ON] = True

        if value == 1:
            self.hass.components.light.turn_on(self.entity_id)
        elif value == 0:
            self.hass.components.light.turn_off(self.entity_id)

    @debounce
    def set_brightness(self, value):
        """Set brightness if call came from HomeKit."""
        _LOGGER.debug('%s: Set brightness to %d', self.entity_id, value)
        self._flag[CHAR_BRIGHTNESS] = True
        if value != 0:
            self.hass.components.light.turn_on(
                self.entity_id, brightness_pct=value)
        else:
            self.hass.components.light.turn_off(self.entity_id)

    def set_color_temperature(self, value):
        """Set color temperature if call came from HomeKit."""
        _LOGGER.debug('%s: Set color temp to %s', self.entity_id, value)
        self._flag[CHAR_COLOR_TEMPERATURE] = True
        self.hass.components.light.turn_on(self.entity_id, color_temp=value)

    def set_saturation(self, value):
        """Set saturation if call came from HomeKit."""
        _LOGGER.debug('%s: Set saturation to %d', self.entity_id, value)
        self._flag[CHAR_SATURATION] = True
        self._saturation = value
        self.set_color()

    def set_hue(self, value):
        """Set hue if call came from HomeKit."""
        _LOGGER.debug('%s: Set hue to %d', self.entity_id, value)
        self._flag[CHAR_HUE] = True
        self._hue = value
        self.set_color()

    def set_color(self):
        """Set color if call came from HomeKit."""
        # Handle Color
        if self._features & SUPPORT_COLOR and self._flag[CHAR_HUE] and \
                self._flag[CHAR_SATURATION]:
            color = (self._hue, self._saturation)
            _LOGGER.debug('%s: Set hs_color to %s', self.entity_id, color)
            self._flag.update({
                CHAR_HUE: False, CHAR_SATURATION: False, RGB_COLOR: True})
            self.hass.components.light.turn_on(
                self.entity_id, hs_color=color)

    def update_state(self, new_state):
        """Update light after state change."""
        # Handle State
        state = new_state.state
        if state in (STATE_ON, STATE_OFF):
            self._state = 1 if state == STATE_ON else 0
            if not self._flag[CHAR_ON] and self.char_on.value != self._state:
                self.char_on.set_value(self._state)
            self._flag[CHAR_ON] = False

        # Handle Brightness
        if CHAR_BRIGHTNESS in self.chars:
            brightness = new_state.attributes.get(ATTR_BRIGHTNESS)
            if not self._flag[CHAR_BRIGHTNESS] and isinstance(brightness, int):
                brightness = round(brightness / 255 * 100, 0)
                if self.char_brightness.value != brightness:
                    self.char_brightness.set_value(brightness)
            self._flag[CHAR_BRIGHTNESS] = False

        # Handle color temperature
        if CHAR_COLOR_TEMPERATURE in self.chars:
            color_temperature = new_state.attributes.get(ATTR_COLOR_TEMP)
            if not self._flag[CHAR_COLOR_TEMPERATURE] \
                and isinstance(color_temperature, int) and \
                    self.char_color_temperature.value != color_temperature:
                self.char_color_temperature.set_value(color_temperature)
            self._flag[CHAR_COLOR_TEMPERATURE] = False

        # Handle Color
        if CHAR_SATURATION in self.chars and CHAR_HUE in self.chars:
            hue, saturation = new_state.attributes.get(
                ATTR_HS_COLOR, (None, None))
            if not self._flag[RGB_COLOR] and (
                    hue != self._hue or saturation != self._saturation) and \
                    isinstance(hue, (int, float)) and \
                    isinstance(saturation, (int, float)):
                self.char_hue.set_value(hue)
                self.char_saturation.set_value(saturation)
                self._hue, self._saturation = (hue, saturation)
            self._flag[RGB_COLOR] = False
