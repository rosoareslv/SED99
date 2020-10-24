"""Tests for the yandex transport platform."""

import json

import pytest

import homeassistant.components.sensor as sensor
from homeassistant.const import CONF_NAME
import homeassistant.util.dt as dt_util

from tests.common import (
    MockDependency,
    assert_setup_component,
    async_setup_component,
    load_fixture,
)

REPLY = json.loads(load_fixture("yandex_transport_reply.json"))


@pytest.fixture
def mock_requester():
    """Create a mock ya_ma module and YandexMapsRequester."""
    with MockDependency("ya_ma") as ya_ma:
        instance = ya_ma.YandexMapsRequester.return_value
        instance.get_stop_info.return_value = REPLY
        yield instance


STOP_ID = 9639579
ROUTES = ["194", "т36", "т47", "м10"]
NAME = "test_name"
TEST_CONFIG = {
    "sensor": {
        "platform": "yandex_transport",
        "stop_id": 9639579,
        "routes": ROUTES,
        "name": NAME,
    }
}

FILTERED_ATTRS = {
    "т36": ["16:10", "16:17", "16:26"],
    "т47": ["16:09", "16:10"],
    "м10": ["16:12", "16:20"],
    "stop_name": "7-й автобусный парк",
    "attribution": "Data provided by maps.yandex.ru",
}

RESULT_STATE = dt_util.utc_from_timestamp(1570972183).isoformat(timespec="seconds")


async def assert_setup_sensor(hass, config, count=1):
    """Set up the sensor and assert it's been created."""
    with assert_setup_component(count):
        assert await async_setup_component(hass, sensor.DOMAIN, config)


async def test_setup_platform_valid_config(hass, mock_requester):
    """Test that sensor is set up properly with valid config."""
    await assert_setup_sensor(hass, TEST_CONFIG)


async def test_setup_platform_invalid_config(hass, mock_requester):
    """Check an invalid configuration."""
    await assert_setup_sensor(
        hass, {"sensor": {"platform": "yandex_transport", "stopid": 1234}}, count=0
    )


async def test_name(hass, mock_requester):
    """Return the name if set in the configuration."""
    await assert_setup_sensor(hass, TEST_CONFIG)
    state = hass.states.get("sensor.test_name")
    assert state.name == TEST_CONFIG["sensor"][CONF_NAME]


async def test_state(hass, mock_requester):
    """Return the contents of _state."""
    await assert_setup_sensor(hass, TEST_CONFIG)
    state = hass.states.get("sensor.test_name")
    assert state.state == RESULT_STATE


async def test_filtered_attributes(hass, mock_requester):
    """Return the contents of attributes."""
    await assert_setup_sensor(hass, TEST_CONFIG)
    state = hass.states.get("sensor.test_name")
    state_attrs = {key: state.attributes[key] for key in FILTERED_ATTRS}
    assert state_attrs == FILTERED_ATTRS
