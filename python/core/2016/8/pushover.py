"""
Pushover platform for notify component.

For more details about this platform, please refer to the documentation at
https://home-assistant.io/components/notify.pushover/
"""
import logging

import voluptuous as vol

from homeassistant.components.notify import (
    ATTR_TITLE, ATTR_TARGET, ATTR_DATA, BaseNotificationService)
from homeassistant.const import CONF_API_KEY
import homeassistant.helpers.config_validation as cv

REQUIREMENTS = ['python-pushover==0.2']
_LOGGER = logging.getLogger(__name__)


PLATFORM_SCHEMA = cv.PLATFORM_SCHEMA.extend({
    vol.Required('user_key'): cv.string,
    vol.Required(CONF_API_KEY): cv.string,
})


# pylint: disable=unused-variable
def get_service(hass, config):
    """Get the Pushover notification service."""
    from pushover import InitError

    try:
        return PushoverNotificationService(config['user_key'],
                                           config[CONF_API_KEY])
    except InitError:
        _LOGGER.error(
            'Wrong API key supplied. Get it at https://pushover.net')
        return None


# pylint: disable=too-few-public-methods
class PushoverNotificationService(BaseNotificationService):
    """Implement the notification service for Pushover."""

    def __init__(self, user_key, api_token):
        """Initialize the service."""
        from pushover import Client
        self._user_key = user_key
        self._api_token = api_token
        self.pushover = Client(
            self._user_key, api_token=self._api_token)

    def send_message(self, message='', **kwargs):
        """Send a message to a user."""
        from pushover import RequestError

        # Make a copy and use empty dict if necessary
        data = dict(kwargs.get(ATTR_DATA) or {})

        data['title'] = kwargs.get(ATTR_TITLE)

        target = kwargs.get(ATTR_TARGET)
        if target is not None:
            data['device'] = target

        try:
            self.pushover.send_message(message, **data)
        except ValueError as val_err:
            _LOGGER.error(str(val_err))
        except RequestError:
            _LOGGER.exception('Could not send pushover notification')
