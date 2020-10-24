"""
Support for Google Actions Smart Home Control.

For more details about this component, please refer to the documentation at
https://home-assistant.io/components/google_assistant/
"""
import asyncio
import logging

from aiohttp.hdrs import AUTHORIZATION
from aiohttp.web import Request, Response  # NOQA

from homeassistant.const import HTTP_UNAUTHORIZED

# Typing imports
# pylint: disable=using-constant-test,unused-import,ungrouped-imports
from homeassistant.components.http import HomeAssistantView
from homeassistant.core import HomeAssistant, callback  # NOQA
from homeassistant.helpers.entity import Entity  # NOQA

from .const import (
    GOOGLE_ASSISTANT_API_ENDPOINT,
    CONF_ACCESS_TOKEN,
    CONF_EXPOSE_BY_DEFAULT,
    CONF_EXPOSED_DOMAINS,
    ATTR_GOOGLE_ASSISTANT,
    CONF_AGENT_USER_ID
    )
from .smart_home import async_handle_message, Config

_LOGGER = logging.getLogger(__name__)


@callback
def async_register_http(hass, cfg):
    """Register HTTP views for Google Assistant."""
    access_token = cfg.get(CONF_ACCESS_TOKEN)
    expose_by_default = cfg.get(CONF_EXPOSE_BY_DEFAULT)
    exposed_domains = cfg.get(CONF_EXPOSED_DOMAINS)
    agent_user_id = cfg.get(CONF_AGENT_USER_ID)

    def is_exposed(entity) -> bool:
        """Determine if an entity should be exposed to Google Assistant."""
        if entity.attributes.get('view') is not None:
            # Ignore entities that are views
            return False

        domain = entity.domain.lower()
        explicit_expose = entity.attributes.get(ATTR_GOOGLE_ASSISTANT, None)

        domain_exposed_by_default = \
            expose_by_default and domain in exposed_domains

        # Expose an entity if the entity's domain is exposed by default and
        # the configuration doesn't explicitly exclude it from being
        # exposed, or if the entity is explicitly exposed
        is_default_exposed = \
            domain_exposed_by_default and explicit_expose is not False

        return is_default_exposed or explicit_expose

    gass_config = Config(is_exposed, agent_user_id)
    hass.http.register_view(
        GoogleAssistantView(access_token, gass_config))


class GoogleAssistantView(HomeAssistantView):
    """Handle Google Assistant requests."""

    url = GOOGLE_ASSISTANT_API_ENDPOINT
    name = 'api:google_assistant'
    requires_auth = False  # Uses access token from oauth flow

    def __init__(self, access_token, gass_config):
        """Initialize the Google Assistant request handler."""
        self.access_token = access_token
        self.gass_config = gass_config

    @asyncio.coroutine
    def post(self, request: Request) -> Response:
        """Handle Google Assistant requests."""
        auth = request.headers.get(AUTHORIZATION, None)
        if 'Bearer {}'.format(self.access_token) != auth:
            return self.json_message(
                "missing authorization", status_code=HTTP_UNAUTHORIZED)

        message = yield from request.json()  # type: dict
        result = yield from async_handle_message(
            request.app['hass'], self.gass_config, message)
        return self.json(result)
