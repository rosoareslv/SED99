"""Support for light effects for the LIFX light platform."""
import logging
import asyncio
import random
from os import path

import voluptuous as vol

from homeassistant.components.light import (
    DOMAIN, ATTR_BRIGHTNESS, ATTR_COLOR_NAME, ATTR_RGB_COLOR, ATTR_EFFECT)
from homeassistant.config import load_yaml_config_file
from homeassistant.const import (ATTR_ENTITY_ID)
from homeassistant.helpers.service import extract_entity_ids
import homeassistant.helpers.config_validation as cv

_LOGGER = logging.getLogger(__name__)

SERVICE_EFFECT_BREATHE = 'lifx_effect_breathe'
SERVICE_EFFECT_PULSE = 'lifx_effect_pulse'
SERVICE_EFFECT_COLORLOOP = 'lifx_effect_colorloop'
SERVICE_EFFECT_STOP = 'lifx_effect_stop'

ATTR_POWER_ON = 'power_on'
ATTR_PERIOD = 'period'
ATTR_CYCLES = 'cycles'
ATTR_SPREAD = 'spread'
ATTR_CHANGE = 'change'

# aiolifx waveform modes
WAVEFORM_SINE = 1
WAVEFORM_PULSE = 4

LIFX_EFFECT_SCHEMA = vol.Schema({
    vol.Optional(ATTR_ENTITY_ID): cv.entity_ids,
    vol.Optional(ATTR_POWER_ON, default=True): cv.boolean,
})

LIFX_EFFECT_BREATHE_SCHEMA = LIFX_EFFECT_SCHEMA.extend({
    ATTR_BRIGHTNESS: vol.All(vol.Coerce(int), vol.Clamp(min=0, max=255)),
    ATTR_COLOR_NAME: cv.string,
    ATTR_RGB_COLOR: vol.All(vol.ExactSequence((cv.byte, cv.byte, cv.byte)),
                            vol.Coerce(tuple)),
    vol.Optional(ATTR_PERIOD, default=1.0): vol.All(vol.Coerce(float),
                                                    vol.Range(min=0.05)),
    vol.Optional(ATTR_CYCLES, default=1.0): vol.All(vol.Coerce(float),
                                                    vol.Range(min=1)),
})

LIFX_EFFECT_PULSE_SCHEMA = LIFX_EFFECT_BREATHE_SCHEMA

LIFX_EFFECT_COLORLOOP_SCHEMA = LIFX_EFFECT_SCHEMA.extend({
    ATTR_BRIGHTNESS: vol.All(vol.Coerce(int), vol.Clamp(min=0, max=255)),
    vol.Optional(ATTR_PERIOD, default=60): vol.All(vol.Coerce(float),
                                                   vol.Clamp(min=1)),
    vol.Optional(ATTR_CHANGE, default=20): vol.All(vol.Coerce(float),
                                                   vol.Clamp(min=0, max=360)),
    vol.Optional(ATTR_SPREAD, default=30): vol.All(vol.Coerce(float),
                                                   vol.Clamp(min=0, max=360)),
})

LIFX_EFFECT_STOP_SCHEMA = vol.Schema({
    vol.Optional(ATTR_ENTITY_ID): cv.entity_ids,
    vol.Optional(ATTR_POWER_ON, default=False): cv.boolean,
})


def setup(hass, lifx_manager):
    """Register the LIFX effects as hass service calls."""
    @asyncio.coroutine
    def async_service_handle(service):
        """Internal func for applying a service."""
        entity_ids = extract_entity_ids(hass, service)
        if entity_ids:
            devices = [entity for entity in lifx_manager.entities.values()
                       if entity.entity_id in entity_ids]
        else:
            devices = list(lifx_manager.entities.values())

        if devices:
            yield from start_effect(hass, devices,
                                    service.service, **service.data)

    descriptions = load_yaml_config_file(
        path.join(path.dirname(__file__), 'services.yaml'))

    hass.services.async_register(
        DOMAIN, SERVICE_EFFECT_BREATHE, async_service_handle,
        descriptions.get(SERVICE_EFFECT_BREATHE),
        schema=LIFX_EFFECT_BREATHE_SCHEMA)

    hass.services.async_register(
        DOMAIN, SERVICE_EFFECT_PULSE, async_service_handle,
        descriptions.get(SERVICE_EFFECT_PULSE),
        schema=LIFX_EFFECT_PULSE_SCHEMA)

    hass.services.async_register(
        DOMAIN, SERVICE_EFFECT_COLORLOOP, async_service_handle,
        descriptions.get(SERVICE_EFFECT_COLORLOOP),
        schema=LIFX_EFFECT_COLORLOOP_SCHEMA)

    hass.services.async_register(
        DOMAIN, SERVICE_EFFECT_STOP, async_service_handle,
        descriptions.get(SERVICE_EFFECT_STOP),
        schema=LIFX_EFFECT_STOP_SCHEMA)


@asyncio.coroutine
def start_effect(hass, devices, service, **data):
    """Start a light effect."""
    tasks = []
    for light in devices:
        tasks.append(hass.async_add_job(light.stop_effect()))
    yield from asyncio.wait(tasks, loop=hass.loop)

    if service in SERVICE_EFFECT_BREATHE:
        effect = LIFXEffectBreathe(hass, devices)
    elif service in SERVICE_EFFECT_PULSE:
        effect = LIFXEffectPulse(hass, devices)
    elif service == SERVICE_EFFECT_COLORLOOP:
        effect = LIFXEffectColorloop(hass, devices)
    elif service == SERVICE_EFFECT_STOP:
        effect = LIFXEffectStop(hass, devices)

    hass.async_add_job(effect.async_perform(**data))


@asyncio.coroutine
def default_effect(light, **kwargs):
    """Start an effect with default parameters."""
    service = kwargs[ATTR_EFFECT]
    data = {
        ATTR_ENTITY_ID: light.entity_id,
    }
    if service in (SERVICE_EFFECT_BREATHE, SERVICE_EFFECT_PULSE):
        data[ATTR_RGB_COLOR] = [
            random.randint(1, 127),
            random.randint(1, 127),
            random.randint(1, 127),
        ]
        data[ATTR_BRIGHTNESS] = 255
    yield from light.hass.services.async_call(DOMAIN, service, data)


def effect_list():
    """Return the list of supported effects."""
    return [
        SERVICE_EFFECT_COLORLOOP,
        SERVICE_EFFECT_BREATHE,
        SERVICE_EFFECT_PULSE,
        SERVICE_EFFECT_STOP,
    ]


class LIFXEffectData(object):
    """Structure describing a running effect."""

    def __init__(self, effect, power, color):
        """Initialize data structure."""
        self.effect = effect
        self.power = power
        self.color = color


class LIFXEffect(object):
    """Representation of a light effect running on a number of lights."""

    def __init__(self, hass, lights):
        """Initialize the effect."""
        self.hass = hass
        self.lights = lights

    @asyncio.coroutine
    def async_perform(self, **kwargs):
        """Do common setup and play the effect."""
        yield from self.async_setup(**kwargs)
        yield from self.async_play(**kwargs)

    @asyncio.coroutine
    def async_setup(self, **kwargs):
        """Prepare all lights for the effect."""
        for light in self.lights:
            yield from light.refresh_state()
            if not light.device:
                self.lights.remove(light)
            else:
                light.effect_data = LIFXEffectData(
                    self, light.is_on, light.device.color)

                # Temporarily turn on power for the effect to be visible
                if kwargs[ATTR_POWER_ON] and not light.is_on:
                    hsbk = self.from_poweroff_hsbk(light, **kwargs)
                    light.device.set_color(hsbk)
                    light.device.set_power(True)

    # pylint: disable=no-self-use
    @asyncio.coroutine
    def async_play(self, **kwargs):
        """Play the effect."""
        yield None

    @asyncio.coroutine
    def async_restore(self, light):
        """Restore to the original state (if we are still running)."""
        if light.effect_data:
            if light.effect_data.effect == self:
                if light.device and not light.effect_data.power:
                    light.device.set_power(False)
                    yield from asyncio.sleep(0.5)
                if light.device:
                    light.device.set_color(light.effect_data.color)
                    yield from asyncio.sleep(0.5)
                light.effect_data = None
            self.lights.remove(light)

    def from_poweroff_hsbk(self, light, **kwargs):
        """The initial color when starting from a powered off state."""
        return None


class LIFXEffectBreathe(LIFXEffect):
    """Representation of a breathe effect."""

    def __init__(self, hass, lights):
        """Initialize the breathe effect."""
        super(LIFXEffectBreathe, self).__init__(hass, lights)
        self.name = SERVICE_EFFECT_BREATHE
        self.waveform = WAVEFORM_SINE

    @asyncio.coroutine
    def async_play(self, **kwargs):
        """Play the effect on all lights."""
        for light in self.lights:
            self.hass.async_add_job(self.async_light_play(light, **kwargs))

    @asyncio.coroutine
    def async_light_play(self, light, **kwargs):
        """Play a light effect on the bulb."""
        period = kwargs[ATTR_PERIOD]
        cycles = kwargs[ATTR_CYCLES]
        hsbk, _ = light.find_hsbk(**kwargs)

        # Start the effect
        args = {
            'transient': 1,
            'color': hsbk,
            'period': int(period*1000),
            'cycles': cycles,
            'duty_cycle': 0,
            'waveform': self.waveform,
        }
        light.device.set_waveform(args)

        # Wait for completion and restore the initial state
        yield from asyncio.sleep(period*cycles)
        yield from self.async_restore(light)

    def from_poweroff_hsbk(self, light, **kwargs):
        """Initial color is the target color, but no brightness."""
        hsbk, _ = light.find_hsbk(**kwargs)
        return [hsbk[0], hsbk[1], 0, hsbk[2]]


class LIFXEffectPulse(LIFXEffectBreathe):
    """Representation of a pulse effect."""

    def __init__(self, hass, lights):
        """Initialize the pulse effect."""
        super(LIFXEffectPulse, self).__init__(hass, lights)
        self.name = SERVICE_EFFECT_PULSE
        self.waveform = WAVEFORM_PULSE


class LIFXEffectColorloop(LIFXEffect):
    """Representation of a colorloop effect."""

    def __init__(self, hass, lights):
        """Initialize the colorloop effect."""
        super(LIFXEffectColorloop, self).__init__(hass, lights)
        self.name = SERVICE_EFFECT_COLORLOOP

    @asyncio.coroutine
    def async_play(self, **kwargs):
        """Play the effect on all lights."""
        period = kwargs[ATTR_PERIOD]
        spread = kwargs[ATTR_SPREAD]
        change = kwargs[ATTR_CHANGE]
        direction = 1 if random.randint(0, 1) else -1

        # Random start
        hue = random.randint(0, 359)

        while self.lights:
            hue = (hue + direction*change) % 360

            random.shuffle(self.lights)
            lhue = hue

            transition = int(1000 * random.uniform(period/2, period))
            for light in self.lights:
                if spread > 0:
                    transition = int(1000 * random.uniform(period/2, period))

                if ATTR_BRIGHTNESS in kwargs:
                    brightness = int(65535/255*kwargs[ATTR_BRIGHTNESS])
                else:
                    brightness = light.effect_data.color[2]

                hsbk = [
                    int(65535/359*lhue),
                    int(random.uniform(0.8, 1.0)*65535),
                    brightness,
                    4000,
                ]
                light.device.set_color(hsbk, None, transition)

                # Adjust the next light so the full spread is used
                if len(self.lights) > 1:
                    lhue = (lhue + spread/(len(self.lights)-1)) % 360

            yield from asyncio.sleep(period)

    def from_poweroff_hsbk(self, light, **kwargs):
        """Start from a random hue."""
        return [random.randint(0, 65535), 65535, 0, 4000]


class LIFXEffectStop(LIFXEffect):
    """A no-op effect, but starting it will stop an existing effect."""

    def __init__(self, hass, lights):
        """Initialize the stop effect."""
        super(LIFXEffectStop, self).__init__(hass, lights)
        self.name = SERVICE_EFFECT_STOP

    @asyncio.coroutine
    def async_perform(self, **kwargs):
        """Do nothing."""
        yield None
