"""Support for Xiaomi Mi Air Quality Monitor (PM2.5)."""
from miio import AirQualityMonitor, Device, DeviceException
import voluptuous as vol

from homeassistant.components.air_quality import (
    _LOGGER,
    PLATFORM_SCHEMA,
    AirQualityEntity,
)
from homeassistant.const import CONF_HOST, CONF_NAME, CONF_TOKEN
from homeassistant.exceptions import PlatformNotReady
import homeassistant.helpers.config_validation as cv

DEFAULT_NAME = "Xiaomi Miio Air Quality Monitor"

ATTR_CO2E = "carbon_dioxide_equivalent"
ATTR_TVOC = "total_volatile_organic_compounds"

PLATFORM_SCHEMA = PLATFORM_SCHEMA.extend(
    {
        vol.Required(CONF_HOST): cv.string,
        vol.Required(CONF_TOKEN): vol.All(cv.string, vol.Length(min=32, max=32)),
        vol.Optional(CONF_NAME, default=DEFAULT_NAME): cv.string,
    }
)

PROP_TO_ATTR = {
    "carbon_dioxide_equivalent": ATTR_CO2E,
    "total_volatile_organic_compounds": ATTR_TVOC,
}


async def async_setup_platform(hass, config, async_add_entities, discovery_info=None):
    """Set up the sensor from config."""

    host = config[CONF_HOST]
    token = config[CONF_TOKEN]
    name = config[CONF_NAME]

    _LOGGER.info("Initializing with host %s (token %s...)", host, token[:5])

    miio_device = Device(host, token)

    try:
        device_info = await hass.async_add_executor_job(miio_device.info)
    except DeviceException:
        raise PlatformNotReady

    model = device_info.model
    unique_id = f"{model}-{device_info.mac_address}"
    _LOGGER.debug(
        "%s %s %s detected",
        model,
        device_info.firmware_version,
        device_info.hardware_version,
    )
    device = AirMonitorB1(name, AirQualityMonitor(host, token, model=model), unique_id)

    async_add_entities([device], update_before_add=True)


class AirMonitorB1(AirQualityEntity):
    """Air Quality class for Xiaomi cgllc.airmonitor.b1 device."""

    def __init__(self, name, device, unique_id):
        """Initialize the entity."""
        self._name = name
        self._device = device
        self._unique_id = unique_id
        self._icon = "mdi:cloud"
        self._unit_of_measurement = "μg/m3"
        self._carbon_dioxide_equivalent = None
        self._particulate_matter_2_5 = None
        self._total_volatile_organic_compounds = None

    async def async_update(self):
        """Fetch state from the miio device."""

        try:
            state = await self.hass.async_add_executor_job(self._device.status)
            _LOGGER.debug("Got new state: %s", state)

            self._carbon_dioxide_equivalent = state.co2e
            self._particulate_matter_2_5 = round(state.pm25, 1)
            self._total_volatile_organic_compounds = round(state.tvoc, 3)

        except DeviceException as ex:
            _LOGGER.error("Got exception while fetching the state: %s", ex)

    @property
    def name(self):
        """Return the name of this entity, if any."""
        return self._name

    @property
    def icon(self):
        """Return the icon to use for device if any."""
        return self._icon

    @property
    def unique_id(self):
        """Return the unique ID."""
        return self._unique_id

    @property
    def carbon_dioxide_equivalent(self):
        """Return the CO2e (carbon dioxide equivalent) level."""
        return self._carbon_dioxide_equivalent

    @property
    def particulate_matter_2_5(self):
        """Return the particulate matter 2.5 level."""
        return self._particulate_matter_2_5

    @property
    def total_volatile_organic_compounds(self):
        """Return the total volatile organic compounds."""
        return self._total_volatile_organic_compounds

    @property
    def device_state_attributes(self):
        """Return the state attributes."""
        data = {}

        for prop, attr in PROP_TO_ATTR.items():
            value = getattr(self, prop)
            if value is not None:
                data[attr] = value

        return data

    @property
    def unit_of_measurement(self):
        """Return the unit of measurement."""
        return self._unit_of_measurement
