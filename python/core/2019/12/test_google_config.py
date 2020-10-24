"""Test the Cloud Google Config."""
from unittest.mock import Mock, patch

from homeassistant.components.cloud import GACTIONS_SCHEMA
from homeassistant.components.cloud.google_config import CloudGoogleConfig
from homeassistant.components.google_assistant import helpers as ga_helpers
from homeassistant.helpers.entity_registry import EVENT_ENTITY_REGISTRY_UPDATED
from homeassistant.util.dt import utcnow

from tests.common import async_fire_time_changed, mock_coro


async def test_google_update_report_state(hass, cloud_prefs):
    """Test Google config responds to updating preference."""
    config = CloudGoogleConfig(
        hass,
        GACTIONS_SCHEMA({}),
        "mock-user-id",
        cloud_prefs,
        Mock(claims={"cognito:username": "abcdefghjkl"}),
    )
    await config.async_initialize()
    await config.async_connect_agent_user("mock-user-id")

    with patch.object(
        config, "async_sync_entities", side_effect=mock_coro
    ) as mock_sync, patch(
        "homeassistant.components.google_assistant.report_state.async_enable_report_state"
    ) as mock_report_state:
        await cloud_prefs.async_update(google_report_state=True)
        await hass.async_block_till_done()

    assert len(mock_sync.mock_calls) == 1
    assert len(mock_report_state.mock_calls) == 1


async def test_sync_entities(aioclient_mock, hass, cloud_prefs):
    """Test sync devices."""
    aioclient_mock.post("http://example.com", status=404)
    config = CloudGoogleConfig(
        hass,
        GACTIONS_SCHEMA({}),
        "mock-user-id",
        cloud_prefs,
        Mock(
            google_actions_sync_url="http://example.com",
            auth=Mock(async_check_token=Mock(side_effect=mock_coro)),
        ),
    )

    assert await config.async_sync_entities("user") == 404


async def test_google_update_expose_trigger_sync(hass, cloud_prefs):
    """Test Google config responds to updating exposed entities."""
    config = CloudGoogleConfig(
        hass,
        GACTIONS_SCHEMA({}),
        "mock-user-id",
        cloud_prefs,
        Mock(claims={"cognito:username": "abcdefghjkl"}),
    )
    await config.async_initialize()
    await config.async_connect_agent_user("mock-user-id")

    with patch.object(
        config, "async_sync_entities", side_effect=mock_coro
    ) as mock_sync, patch.object(ga_helpers, "SYNC_DELAY", 0):
        await cloud_prefs.async_update_google_entity_config(
            entity_id="light.kitchen", should_expose=True
        )
        await hass.async_block_till_done()
        async_fire_time_changed(hass, utcnow())
        await hass.async_block_till_done()

    assert len(mock_sync.mock_calls) == 1

    with patch.object(
        config, "async_sync_entities", side_effect=mock_coro
    ) as mock_sync, patch.object(ga_helpers, "SYNC_DELAY", 0):
        await cloud_prefs.async_update_google_entity_config(
            entity_id="light.kitchen", should_expose=False
        )
        await cloud_prefs.async_update_google_entity_config(
            entity_id="binary_sensor.door", should_expose=True
        )
        await cloud_prefs.async_update_google_entity_config(
            entity_id="sensor.temp", should_expose=True
        )
        await hass.async_block_till_done()
        async_fire_time_changed(hass, utcnow())
        await hass.async_block_till_done()

    assert len(mock_sync.mock_calls) == 1


async def test_google_entity_registry_sync(hass, mock_cloud_login, cloud_prefs):
    """Test Google config responds to entity registry."""
    config = CloudGoogleConfig(
        hass, GACTIONS_SCHEMA({}), "mock-user-id", cloud_prefs, hass.data["cloud"]
    )
    await config.async_initialize()
    await config.async_connect_agent_user("mock-user-id")

    with patch.object(
        config, "async_sync_entities", side_effect=mock_coro
    ) as mock_sync, patch.object(ga_helpers, "SYNC_DELAY", 0):
        hass.bus.async_fire(
            EVENT_ENTITY_REGISTRY_UPDATED,
            {"action": "create", "entity_id": "light.kitchen"},
        )
        await hass.async_block_till_done()

    assert len(mock_sync.mock_calls) == 1

    with patch.object(
        config, "async_sync_entities", side_effect=mock_coro
    ) as mock_sync, patch.object(ga_helpers, "SYNC_DELAY", 0):
        hass.bus.async_fire(
            EVENT_ENTITY_REGISTRY_UPDATED,
            {"action": "remove", "entity_id": "light.kitchen"},
        )
        await hass.async_block_till_done()

    assert len(mock_sync.mock_calls) == 1

    with patch.object(
        config, "async_sync_entities", side_effect=mock_coro
    ) as mock_sync, patch.object(ga_helpers, "SYNC_DELAY", 0):
        hass.bus.async_fire(
            EVENT_ENTITY_REGISTRY_UPDATED,
            {
                "action": "update",
                "entity_id": "light.kitchen",
                "changes": ["entity_id"],
            },
        )
        await hass.async_block_till_done()

    assert len(mock_sync.mock_calls) == 1
