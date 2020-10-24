"""Tests for the HTTP API for the cloud component."""
import asyncio
from unittest.mock import patch, MagicMock

import pytest
from jose import jwt

from homeassistant.components.cloud import (
    DOMAIN, auth_api, iot)
from homeassistant.components.cloud.const import (
    PREF_ENABLE_GOOGLE, PREF_ENABLE_ALEXA, PREF_GOOGLE_ALLOW_UNLOCK)
from homeassistant.util import dt as dt_util

from tests.common import mock_coro

from . import mock_cloud, mock_cloud_prefs

GOOGLE_ACTIONS_SYNC_URL = 'https://api-test.hass.io/google_actions_sync'
SUBSCRIPTION_INFO_URL = 'https://api-test.hass.io/subscription_info'


@pytest.fixture()
def mock_auth():
    """Mock check token."""
    with patch('homeassistant.components.cloud.auth_api.check_token'):
        yield


@pytest.fixture(autouse=True)
def setup_api(hass):
    """Initialize HTTP API."""
    mock_cloud(hass, {
        'mode': 'development',
        'cognito_client_id': 'cognito_client_id',
        'user_pool_id': 'user_pool_id',
        'region': 'region',
        'relayer': 'relayer',
        'google_actions_sync_url': GOOGLE_ACTIONS_SYNC_URL,
        'subscription_info_url': SUBSCRIPTION_INFO_URL,
        'google_actions': {
            'filter': {
                'include_domains': 'light'
            }
        },
        'alexa': {
            'filter': {
                'include_entities': ['light.kitchen', 'switch.ac']
            }
        }
    })
    return mock_cloud_prefs(hass)


@pytest.fixture
def cloud_client(hass, hass_client):
    """Fixture that can fetch from the cloud client."""
    with patch('homeassistant.components.cloud.Cloud.write_user_info'):
        yield hass.loop.run_until_complete(hass_client())


@pytest.fixture
def mock_cognito():
    """Mock warrant."""
    with patch('homeassistant.components.cloud.auth_api._cognito') as mock_cog:
        yield mock_cog()


async def test_google_actions_sync(mock_cognito, cloud_client, aioclient_mock):
    """Test syncing Google Actions."""
    aioclient_mock.post(GOOGLE_ACTIONS_SYNC_URL)
    req = await cloud_client.post('/api/cloud/google_actions/sync')
    assert req.status == 200


async def test_google_actions_sync_fails(mock_cognito, cloud_client,
                                         aioclient_mock):
    """Test syncing Google Actions gone bad."""
    aioclient_mock.post(GOOGLE_ACTIONS_SYNC_URL, status=403)
    req = await cloud_client.post('/api/cloud/google_actions/sync')
    assert req.status == 403


@asyncio.coroutine
def test_login_view(hass, cloud_client, mock_cognito):
    """Test logging in."""
    mock_cognito.id_token = jwt.encode({
        'email': 'hello@home-assistant.io',
        'custom:sub-exp': '2018-01-03'
    }, 'test')
    mock_cognito.access_token = 'access_token'
    mock_cognito.refresh_token = 'refresh_token'

    with patch('homeassistant.components.cloud.iot.CloudIoT.'
               'connect') as mock_connect, \
            patch('homeassistant.components.cloud.auth_api._authenticate',
                  return_value=mock_cognito) as mock_auth:
        req = yield from cloud_client.post('/api/cloud/login', json={
            'email': 'my_username',
            'password': 'my_password'
        })

    assert req.status == 200
    result = yield from req.json()
    assert result == {'success': True}

    assert len(mock_connect.mock_calls) == 1

    assert len(mock_auth.mock_calls) == 1
    cloud, result_user, result_pass = mock_auth.mock_calls[0][1]
    assert result_user == 'my_username'
    assert result_pass == 'my_password'


@asyncio.coroutine
def test_login_view_invalid_json(cloud_client):
    """Try logging in with invalid JSON."""
    with patch('homeassistant.components.cloud.auth_api.login') as mock_login:
        req = yield from cloud_client.post('/api/cloud/login', data='Not JSON')
    assert req.status == 400
    assert len(mock_login.mock_calls) == 0


@asyncio.coroutine
def test_login_view_invalid_schema(cloud_client):
    """Try logging in with invalid schema."""
    with patch('homeassistant.components.cloud.auth_api.login') as mock_login:
        req = yield from cloud_client.post('/api/cloud/login', json={
            'invalid': 'schema'
        })
    assert req.status == 400
    assert len(mock_login.mock_calls) == 0


@asyncio.coroutine
def test_login_view_request_timeout(cloud_client):
    """Test request timeout while trying to log in."""
    with patch('homeassistant.components.cloud.auth_api.login',
               side_effect=asyncio.TimeoutError):
        req = yield from cloud_client.post('/api/cloud/login', json={
            'email': 'my_username',
            'password': 'my_password'
        })

    assert req.status == 502


@asyncio.coroutine
def test_login_view_invalid_credentials(cloud_client):
    """Test logging in with invalid credentials."""
    with patch('homeassistant.components.cloud.auth_api.login',
               side_effect=auth_api.Unauthenticated):
        req = yield from cloud_client.post('/api/cloud/login', json={
            'email': 'my_username',
            'password': 'my_password'
        })

    assert req.status == 401


@asyncio.coroutine
def test_login_view_unknown_error(cloud_client):
    """Test unknown error while logging in."""
    with patch('homeassistant.components.cloud.auth_api.login',
               side_effect=auth_api.UnknownError):
        req = yield from cloud_client.post('/api/cloud/login', json={
            'email': 'my_username',
            'password': 'my_password'
        })

    assert req.status == 502


@asyncio.coroutine
def test_logout_view(hass, cloud_client):
    """Test logging out."""
    cloud = hass.data['cloud'] = MagicMock()
    cloud.logout.return_value = mock_coro()
    req = yield from cloud_client.post('/api/cloud/logout')
    assert req.status == 200
    data = yield from req.json()
    assert data == {'message': 'ok'}
    assert len(cloud.logout.mock_calls) == 1


@asyncio.coroutine
def test_logout_view_request_timeout(hass, cloud_client):
    """Test timeout while logging out."""
    cloud = hass.data['cloud'] = MagicMock()
    cloud.logout.side_effect = asyncio.TimeoutError
    req = yield from cloud_client.post('/api/cloud/logout')
    assert req.status == 502


@asyncio.coroutine
def test_logout_view_unknown_error(hass, cloud_client):
    """Test unknown error while logging out."""
    cloud = hass.data['cloud'] = MagicMock()
    cloud.logout.side_effect = auth_api.UnknownError
    req = yield from cloud_client.post('/api/cloud/logout')
    assert req.status == 502


@asyncio.coroutine
def test_register_view(mock_cognito, cloud_client):
    """Test logging out."""
    req = yield from cloud_client.post('/api/cloud/register', json={
        'email': 'hello@bla.com',
        'password': 'falcon42'
    })
    assert req.status == 200
    assert len(mock_cognito.register.mock_calls) == 1
    result_email, result_pass = mock_cognito.register.mock_calls[0][1]
    assert result_email == 'hello@bla.com'
    assert result_pass == 'falcon42'


@asyncio.coroutine
def test_register_view_bad_data(mock_cognito, cloud_client):
    """Test logging out."""
    req = yield from cloud_client.post('/api/cloud/register', json={
        'email': 'hello@bla.com',
        'not_password': 'falcon'
    })
    assert req.status == 400
    assert len(mock_cognito.logout.mock_calls) == 0


@asyncio.coroutine
def test_register_view_request_timeout(mock_cognito, cloud_client):
    """Test timeout while logging out."""
    mock_cognito.register.side_effect = asyncio.TimeoutError
    req = yield from cloud_client.post('/api/cloud/register', json={
        'email': 'hello@bla.com',
        'password': 'falcon42'
    })
    assert req.status == 502


@asyncio.coroutine
def test_register_view_unknown_error(mock_cognito, cloud_client):
    """Test unknown error while logging out."""
    mock_cognito.register.side_effect = auth_api.UnknownError
    req = yield from cloud_client.post('/api/cloud/register', json={
        'email': 'hello@bla.com',
        'password': 'falcon42'
    })
    assert req.status == 502


@asyncio.coroutine
def test_forgot_password_view(mock_cognito, cloud_client):
    """Test logging out."""
    req = yield from cloud_client.post('/api/cloud/forgot_password', json={
        'email': 'hello@bla.com',
    })
    assert req.status == 200
    assert len(mock_cognito.initiate_forgot_password.mock_calls) == 1


@asyncio.coroutine
def test_forgot_password_view_bad_data(mock_cognito, cloud_client):
    """Test logging out."""
    req = yield from cloud_client.post('/api/cloud/forgot_password', json={
        'not_email': 'hello@bla.com',
    })
    assert req.status == 400
    assert len(mock_cognito.initiate_forgot_password.mock_calls) == 0


@asyncio.coroutine
def test_forgot_password_view_request_timeout(mock_cognito, cloud_client):
    """Test timeout while logging out."""
    mock_cognito.initiate_forgot_password.side_effect = asyncio.TimeoutError
    req = yield from cloud_client.post('/api/cloud/forgot_password', json={
        'email': 'hello@bla.com',
    })
    assert req.status == 502


@asyncio.coroutine
def test_forgot_password_view_unknown_error(mock_cognito, cloud_client):
    """Test unknown error while logging out."""
    mock_cognito.initiate_forgot_password.side_effect = auth_api.UnknownError
    req = yield from cloud_client.post('/api/cloud/forgot_password', json={
        'email': 'hello@bla.com',
    })
    assert req.status == 502


@asyncio.coroutine
def test_resend_confirm_view(mock_cognito, cloud_client):
    """Test logging out."""
    req = yield from cloud_client.post('/api/cloud/resend_confirm', json={
        'email': 'hello@bla.com',
    })
    assert req.status == 200
    assert len(mock_cognito.client.resend_confirmation_code.mock_calls) == 1


@asyncio.coroutine
def test_resend_confirm_view_bad_data(mock_cognito, cloud_client):
    """Test logging out."""
    req = yield from cloud_client.post('/api/cloud/resend_confirm', json={
        'not_email': 'hello@bla.com',
    })
    assert req.status == 400
    assert len(mock_cognito.client.resend_confirmation_code.mock_calls) == 0


@asyncio.coroutine
def test_resend_confirm_view_request_timeout(mock_cognito, cloud_client):
    """Test timeout while logging out."""
    mock_cognito.client.resend_confirmation_code.side_effect = \
        asyncio.TimeoutError
    req = yield from cloud_client.post('/api/cloud/resend_confirm', json={
        'email': 'hello@bla.com',
    })
    assert req.status == 502


@asyncio.coroutine
def test_resend_confirm_view_unknown_error(mock_cognito, cloud_client):
    """Test unknown error while logging out."""
    mock_cognito.client.resend_confirmation_code.side_effect = \
        auth_api.UnknownError
    req = yield from cloud_client.post('/api/cloud/resend_confirm', json={
        'email': 'hello@bla.com',
    })
    assert req.status == 502


async def test_websocket_status(hass, hass_ws_client, mock_cloud_fixture):
    """Test querying the status."""
    hass.data[DOMAIN].id_token = jwt.encode({
        'email': 'hello@home-assistant.io',
        'custom:sub-exp': '2018-01-03'
    }, 'test')
    hass.data[DOMAIN].iot.state = iot.STATE_CONNECTED
    client = await hass_ws_client(hass)

    with patch.dict(
        'homeassistant.components.google_assistant.smart_home.'
        'DOMAIN_TO_GOOGLE_TYPES', {'light': None}, clear=True
    ), patch.dict('homeassistant.components.alexa.smart_home.ENTITY_ADAPTERS',
                  {'switch': None}, clear=True):
        await client.send_json({
            'id': 5,
            'type': 'cloud/status'
        })
        response = await client.receive_json()
    assert response['result'] == {
        'logged_in': True,
        'email': 'hello@home-assistant.io',
        'cloud': 'connected',
        'prefs': mock_cloud_fixture,
        'alexa_entities': {
            'include_domains': [],
            'include_entities': ['light.kitchen', 'switch.ac'],
            'exclude_domains': [],
            'exclude_entities': [],
        },
        'alexa_domains': ['switch'],
        'google_entities': {
            'include_domains': ['light'],
            'include_entities': [],
            'exclude_domains': [],
            'exclude_entities': [],
        },
        'google_domains': ['light'],
    }


async def test_websocket_status_not_logged_in(hass, hass_ws_client):
    """Test querying the status."""
    client = await hass_ws_client(hass)
    await client.send_json({
        'id': 5,
        'type': 'cloud/status'
    })
    response = await client.receive_json()
    assert response['result'] == {
        'logged_in': False,
        'cloud': 'disconnected'
    }


async def test_websocket_subscription_reconnect(
        hass, hass_ws_client, aioclient_mock, mock_auth):
    """Test querying the status and connecting because valid account."""
    aioclient_mock.get(SUBSCRIPTION_INFO_URL, json={'provider': 'stripe'})
    hass.data[DOMAIN].id_token = jwt.encode({
        'email': 'hello@home-assistant.io',
        'custom:sub-exp': dt_util.utcnow().date().isoformat()
    }, 'test')
    client = await hass_ws_client(hass)

    with patch(
        'homeassistant.components.cloud.auth_api.renew_access_token'
    ) as mock_renew, patch(
        'homeassistant.components.cloud.iot.CloudIoT.connect'
    ) as mock_connect:
        await client.send_json({
            'id': 5,
            'type': 'cloud/subscription'
        })
        response = await client.receive_json()

    assert response['result'] == {
        'provider': 'stripe'
    }
    assert len(mock_renew.mock_calls) == 1
    assert len(mock_connect.mock_calls) == 1


async def test_websocket_subscription_no_reconnect_if_connected(
        hass, hass_ws_client, aioclient_mock, mock_auth):
    """Test querying the status and not reconnecting because still expired."""
    aioclient_mock.get(SUBSCRIPTION_INFO_URL, json={'provider': 'stripe'})
    hass.data[DOMAIN].iot.state = iot.STATE_CONNECTED
    hass.data[DOMAIN].id_token = jwt.encode({
        'email': 'hello@home-assistant.io',
        'custom:sub-exp': dt_util.utcnow().date().isoformat()
    }, 'test')
    client = await hass_ws_client(hass)

    with patch(
        'homeassistant.components.cloud.auth_api.renew_access_token'
    ) as mock_renew, patch(
        'homeassistant.components.cloud.iot.CloudIoT.connect'
    ) as mock_connect:
        await client.send_json({
            'id': 5,
            'type': 'cloud/subscription'
        })
        response = await client.receive_json()

    assert response['result'] == {
        'provider': 'stripe'
    }
    assert len(mock_renew.mock_calls) == 0
    assert len(mock_connect.mock_calls) == 0


async def test_websocket_subscription_no_reconnect_if_expired(
        hass, hass_ws_client, aioclient_mock, mock_auth):
    """Test querying the status and not reconnecting because still expired."""
    aioclient_mock.get(SUBSCRIPTION_INFO_URL, json={'provider': 'stripe'})
    hass.data[DOMAIN].id_token = jwt.encode({
        'email': 'hello@home-assistant.io',
        'custom:sub-exp': '2018-01-03'
    }, 'test')
    client = await hass_ws_client(hass)

    with patch(
        'homeassistant.components.cloud.auth_api.renew_access_token'
    ) as mock_renew, patch(
        'homeassistant.components.cloud.iot.CloudIoT.connect'
    ) as mock_connect:
        await client.send_json({
            'id': 5,
            'type': 'cloud/subscription'
        })
        response = await client.receive_json()

    assert response['result'] == {
        'provider': 'stripe'
    }
    assert len(mock_renew.mock_calls) == 1
    assert len(mock_connect.mock_calls) == 1


async def test_websocket_subscription_fail(hass, hass_ws_client,
                                           aioclient_mock, mock_auth):
    """Test querying the status."""
    aioclient_mock.get(SUBSCRIPTION_INFO_URL, status=500)
    hass.data[DOMAIN].id_token = jwt.encode({
        'email': 'hello@home-assistant.io',
        'custom:sub-exp': '2018-01-03'
    }, 'test')
    client = await hass_ws_client(hass)
    await client.send_json({
        'id': 5,
        'type': 'cloud/subscription'
    })
    response = await client.receive_json()

    assert not response['success']
    assert response['error']['code'] == 'request_failed'


async def test_websocket_subscription_not_logged_in(hass, hass_ws_client):
    """Test querying the status."""
    client = await hass_ws_client(hass)
    with patch('homeassistant.components.cloud.Cloud.fetch_subscription_info',
               return_value=mock_coro({'return': 'value'})):
        await client.send_json({
            'id': 5,
            'type': 'cloud/subscription'
        })
        response = await client.receive_json()

    assert not response['success']
    assert response['error']['code'] == 'not_logged_in'


async def test_websocket_update_preferences(hass, hass_ws_client,
                                            aioclient_mock, setup_api):
    """Test updating preference."""
    assert setup_api[PREF_ENABLE_GOOGLE]
    assert setup_api[PREF_ENABLE_ALEXA]
    assert setup_api[PREF_GOOGLE_ALLOW_UNLOCK]
    hass.data[DOMAIN].id_token = jwt.encode({
        'email': 'hello@home-assistant.io',
        'custom:sub-exp': '2018-01-03'
    }, 'test')
    client = await hass_ws_client(hass)
    await client.send_json({
        'id': 5,
        'type': 'cloud/update_prefs',
        'alexa_enabled': False,
        'google_enabled': False,
        'google_allow_unlock': False,
    })
    response = await client.receive_json()

    assert response['success']
    assert not setup_api[PREF_ENABLE_GOOGLE]
    assert not setup_api[PREF_ENABLE_ALEXA]
    assert not setup_api[PREF_GOOGLE_ALLOW_UNLOCK]


async def test_enabling_webhook(hass, hass_ws_client, setup_api):
    """Test we call right code to enable webhooks."""
    hass.data[DOMAIN].id_token = jwt.encode({
        'email': 'hello@home-assistant.io',
        'custom:sub-exp': '2018-01-03'
    }, 'test')
    client = await hass_ws_client(hass)
    with patch('homeassistant.components.cloud.cloudhooks.Cloudhooks'
               '.async_create', return_value=mock_coro()) as mock_enable:
        await client.send_json({
            'id': 5,
            'type': 'cloud/cloudhook/create',
            'webhook_id': 'mock-webhook-id',
        })
        response = await client.receive_json()
    assert response['success']

    assert len(mock_enable.mock_calls) == 1
    assert mock_enable.mock_calls[0][1][0] == 'mock-webhook-id'


async def test_disabling_webhook(hass, hass_ws_client, setup_api):
    """Test we call right code to disable webhooks."""
    hass.data[DOMAIN].id_token = jwt.encode({
        'email': 'hello@home-assistant.io',
        'custom:sub-exp': '2018-01-03'
    }, 'test')
    client = await hass_ws_client(hass)
    with patch('homeassistant.components.cloud.cloudhooks.Cloudhooks'
               '.async_delete', return_value=mock_coro()) as mock_disable:
        await client.send_json({
            'id': 5,
            'type': 'cloud/cloudhook/delete',
            'webhook_id': 'mock-webhook-id',
        })
        response = await client.receive_json()
    assert response['success']

    assert len(mock_disable.mock_calls) == 1
    assert mock_disable.mock_calls[0][1][0] == 'mock-webhook-id'
