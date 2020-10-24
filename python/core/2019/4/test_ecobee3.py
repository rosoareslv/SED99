"""
Regression tests for Ecobee 3.

https://github.com/home-assistant/home-assistant/issues/15336
"""

from unittest import mock

from homekit import AccessoryDisconnectedError

from homeassistant.components.climate.const import (
    SUPPORT_TARGET_TEMPERATURE, SUPPORT_TARGET_HUMIDITY,
    SUPPORT_OPERATION_MODE)
from tests.components.homekit_controller.common import (
    FakePairing, device_config_changed, setup_accessories_from_file,
    setup_test_accessories, Helper
)


async def test_ecobee3_setup(hass):
    """Test that a Ecbobee 3 can be correctly setup in HA."""
    accessories = await setup_accessories_from_file(hass, 'ecobee3.json')
    pairing = await setup_test_accessories(hass, accessories)

    entity_registry = await hass.helpers.entity_registry.async_get_registry()

    climate = entity_registry.async_get('climate.homew')
    assert climate.unique_id == 'homekit-123456789012-16'

    climate_helper = Helper(hass, 'climate.homew', pairing, accessories[0])
    climate_state = await climate_helper.poll_and_get_state()
    assert climate_state.attributes['friendly_name'] == 'HomeW'
    assert climate_state.attributes['supported_features'] == (
        SUPPORT_TARGET_TEMPERATURE | SUPPORT_TARGET_HUMIDITY |
        SUPPORT_OPERATION_MODE
    )

    occ1 = entity_registry.async_get('binary_sensor.kitchen')
    assert occ1.unique_id == 'homekit-AB1C-56'

    occ1_helper = Helper(
        hass, 'binary_sensor.kitchen', pairing, accessories[0])
    occ1_state = await occ1_helper.poll_and_get_state()
    assert occ1_state.attributes['friendly_name'] == 'Kitchen'

    occ2 = entity_registry.async_get('binary_sensor.porch')
    assert occ2.unique_id == 'homekit-AB2C-56'

    occ3 = entity_registry.async_get('binary_sensor.basement')
    assert occ3.unique_id == 'homekit-AB3C-56'


async def test_ecobee3_setup_from_cache(hass, hass_storage):
    """Test that Ecbobee can be correctly setup from its cached entity map."""
    accessories = await setup_accessories_from_file(hass, 'ecobee3.json')

    hass_storage['homekit_controller-entity-map'] = {
        'version': 1,
        'data': {
            'pairings': {
                '00:00:00:00:00:00': {
                    'config_num': 1,
                    'accessories': [
                        a.to_accessory_and_service_list() for a in accessories
                    ],
                }
            }
        }
    }

    await setup_test_accessories(hass, accessories)

    entity_registry = await hass.helpers.entity_registry.async_get_registry()

    climate = entity_registry.async_get('climate.homew')
    assert climate.unique_id == 'homekit-123456789012-16'

    occ1 = entity_registry.async_get('binary_sensor.kitchen')
    assert occ1.unique_id == 'homekit-AB1C-56'

    occ2 = entity_registry.async_get('binary_sensor.porch')
    assert occ2.unique_id == 'homekit-AB2C-56'

    occ3 = entity_registry.async_get('binary_sensor.basement')
    assert occ3.unique_id == 'homekit-AB3C-56'


async def test_ecobee3_setup_connection_failure(hass):
    """Test that Ecbobee can be correctly setup from its cached entity map."""
    accessories = await setup_accessories_from_file(hass, 'ecobee3.json')

    entity_registry = await hass.helpers.entity_registry.async_get_registry()

    # Test that the connection fails during initial setup.
    # No entities should be created.
    list_accessories = 'list_accessories_and_characteristics'
    with mock.patch.object(FakePairing, list_accessories) as laac:
        laac.side_effect = AccessoryDisconnectedError('Connection failed')
        await setup_test_accessories(hass, accessories)

    climate = entity_registry.async_get('climate.homew')
    assert climate is None

    # When a regular discovery event happens it should trigger another scan
    # which should cause our entities to be added.
    await device_config_changed(hass, accessories)

    climate = entity_registry.async_get('climate.homew')
    assert climate.unique_id == 'homekit-123456789012-16'

    occ1 = entity_registry.async_get('binary_sensor.kitchen')
    assert occ1.unique_id == 'homekit-AB1C-56'

    occ2 = entity_registry.async_get('binary_sensor.porch')
    assert occ2.unique_id == 'homekit-AB2C-56'

    occ3 = entity_registry.async_get('binary_sensor.basement')
    assert occ3.unique_id == 'homekit-AB3C-56'


async def test_ecobee3_add_sensors_at_runtime(hass):
    """Test that new sensors are automatically added."""
    entity_registry = await hass.helpers.entity_registry.async_get_registry()

    # Set up a base Ecobee 3 with no additional sensors.
    # There shouldn't be any entities but climate visible.
    accessories = await setup_accessories_from_file(
        hass, 'ecobee3_no_sensors.json')
    await setup_test_accessories(hass, accessories)

    climate = entity_registry.async_get('climate.homew')
    assert climate.unique_id == 'homekit-123456789012-16'

    occ1 = entity_registry.async_get('binary_sensor.kitchen')
    assert occ1 is None

    occ2 = entity_registry.async_get('binary_sensor.porch')
    assert occ2 is None

    occ3 = entity_registry.async_get('binary_sensor.basement')
    assert occ3 is None

    # Now added 3 new sensors at runtime - sensors should appear and climate
    # shouldn't be duplicated.
    accessories = await setup_accessories_from_file(hass, 'ecobee3.json')
    await device_config_changed(hass, accessories)

    occ1 = entity_registry.async_get('binary_sensor.kitchen')
    assert occ1.unique_id == 'homekit-AB1C-56'

    occ2 = entity_registry.async_get('binary_sensor.porch')
    assert occ2.unique_id == 'homekit-AB2C-56'

    occ3 = entity_registry.async_get('binary_sensor.basement')
    assert occ3.unique_id == 'homekit-AB3C-56'
