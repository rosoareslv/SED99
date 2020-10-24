"""Test the condition helper."""
from unittest.mock import patch

from homeassistant.helpers import condition
from homeassistant.util import dt


async def test_and_condition(hass):
    """Test the 'and' condition."""
    test = await condition.async_from_config(
        hass,
        {
            "condition": "and",
            "conditions": [
                {
                    "condition": "state",
                    "entity_id": "sensor.temperature",
                    "state": "100",
                },
                {
                    "condition": "numeric_state",
                    "entity_id": "sensor.temperature",
                    "below": 110,
                },
            ],
        },
    )

    hass.states.async_set("sensor.temperature", 120)
    assert not test(hass)

    hass.states.async_set("sensor.temperature", 105)
    assert not test(hass)

    hass.states.async_set("sensor.temperature", 100)
    assert test(hass)


async def test_and_condition_with_template(hass):
    """Test the 'and' condition."""
    test = await condition.async_from_config(
        hass,
        {
            "condition": "and",
            "conditions": [
                {
                    "condition": "template",
                    "value_template": '{{ states.sensor.temperature.state == "100" }}',
                },
                {
                    "condition": "numeric_state",
                    "entity_id": "sensor.temperature",
                    "below": 110,
                },
            ],
        },
    )

    hass.states.async_set("sensor.temperature", 120)
    assert not test(hass)

    hass.states.async_set("sensor.temperature", 105)
    assert not test(hass)

    hass.states.async_set("sensor.temperature", 100)
    assert test(hass)


async def test_or_condition(hass):
    """Test the 'or' condition."""
    test = await condition.async_from_config(
        hass,
        {
            "condition": "or",
            "conditions": [
                {
                    "condition": "state",
                    "entity_id": "sensor.temperature",
                    "state": "100",
                },
                {
                    "condition": "numeric_state",
                    "entity_id": "sensor.temperature",
                    "below": 110,
                },
            ],
        },
    )

    hass.states.async_set("sensor.temperature", 120)
    assert not test(hass)

    hass.states.async_set("sensor.temperature", 105)
    assert test(hass)

    hass.states.async_set("sensor.temperature", 100)
    assert test(hass)


async def test_or_condition_with_template(hass):
    """Test the 'or' condition."""
    test = await condition.async_from_config(
        hass,
        {
            "condition": "or",
            "conditions": [
                {
                    "condition": "template",
                    "value_template": '{{ states.sensor.temperature.state == "100" }}',
                },
                {
                    "condition": "numeric_state",
                    "entity_id": "sensor.temperature",
                    "below": 110,
                },
            ],
        },
    )

    hass.states.async_set("sensor.temperature", 120)
    assert not test(hass)

    hass.states.async_set("sensor.temperature", 105)
    assert test(hass)

    hass.states.async_set("sensor.temperature", 100)
    assert test(hass)


async def test_time_window(hass):
    """Test time condition windows."""
    sixam = dt.parse_time("06:00:00")
    sixpm = dt.parse_time("18:00:00")

    with patch(
        "homeassistant.helpers.condition.dt_util.now",
        return_value=dt.now().replace(hour=3),
    ):
        assert not condition.time(after=sixam, before=sixpm)
        assert condition.time(after=sixpm, before=sixam)

    with patch(
        "homeassistant.helpers.condition.dt_util.now",
        return_value=dt.now().replace(hour=9),
    ):
        assert condition.time(after=sixam, before=sixpm)
        assert not condition.time(after=sixpm, before=sixam)

    with patch(
        "homeassistant.helpers.condition.dt_util.now",
        return_value=dt.now().replace(hour=15),
    ):
        assert condition.time(after=sixam, before=sixpm)
        assert not condition.time(after=sixpm, before=sixam)

    with patch(
        "homeassistant.helpers.condition.dt_util.now",
        return_value=dt.now().replace(hour=21),
    ):
        assert not condition.time(after=sixam, before=sixpm)
        assert condition.time(after=sixpm, before=sixam)


async def test_if_numeric_state_not_raise_on_unavailable(hass):
    """Test numeric_state doesn't raise on unavailable/unknown state."""
    test = await condition.async_from_config(
        hass,
        {"condition": "numeric_state", "entity_id": "sensor.temperature", "below": 42},
    )

    with patch("homeassistant.helpers.condition._LOGGER.warning") as logwarn:
        hass.states.async_set("sensor.temperature", "unavailable")
        assert not test(hass)
        assert len(logwarn.mock_calls) == 0

        hass.states.async_set("sensor.temperature", "unknown")
        assert not test(hass)
        assert len(logwarn.mock_calls) == 0
