"""The tests for the demo stt component."""
import pytest

from homeassistant.components import stt
from homeassistant.setup import async_setup_component


@pytest.fixture(autouse=True)
def setup_comp(hass):
    """Set up demo component."""
    hass.loop.run_until_complete(
        async_setup_component(hass, stt.DOMAIN, {"stt": {"platform": "demo"}})
    )


async def test_demo_settings(hass_client):
    """Test retrieve settings from demo provider."""
    client = await hass_client()

    response = await client.get("/api/stt/demo")
    response_data = await response.json()

    assert response.status == 200
    assert response_data == {
        "languages": ["en", "de"],
        "bit_rates": [16],
        "sample_rates": [16000, 44100],
        "formats": ["wav"],
        "codecs": ["pcm"],
        "channels": [2],
    }


async def test_demo_speech_no_metadata(hass_client):
    """Test retrieve settings from demo provider."""
    client = await hass_client()

    response = await client.post("/api/stt/demo", data=b"Test")
    assert response.status == 400


async def test_demo_speech_wrong_metadata(hass_client):
    """Test retrieve settings from demo provider."""
    client = await hass_client()

    response = await client.post(
        "/api/stt/demo",
        headers={
            "X-Speech-Content": "format=wav; codec=pcm; sample_rate=8000; bit_rate=16; channel=1; language=de"
        },
        data=b"Test",
    )
    assert response.status == 415


async def test_demo_speech(hass_client):
    """Test retrieve settings from demo provider."""
    client = await hass_client()

    response = await client.post(
        "/api/stt/demo",
        headers={
            "X-Speech-Content": "format=wav; codec=pcm; sample_rate=16000; bit_rate=16; channel=2; language=de"
        },
        data=b"Test",
    )
    response_data = await response.json()

    assert response.status == 200
    assert response_data == {"text": "Turn the Kitchen Lights on", "result": "success"}
