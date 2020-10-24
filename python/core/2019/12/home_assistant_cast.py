"""Home Assistant Cast integration for Cast."""
from typing import Optional

from pychromecast.controllers.homeassistant import HomeAssistantController
import voluptuous as vol

from homeassistant import auth, config_entries, core
from homeassistant.const import ATTR_ENTITY_ID
from homeassistant.helpers import config_validation as cv, dispatcher

from .const import DOMAIN, SIGNAL_HASS_CAST_SHOW_VIEW

SERVICE_SHOW_VIEW = "show_lovelace_view"
ATTR_VIEW_PATH = "view_path"


async def async_setup_ha_cast(
    hass: core.HomeAssistant, entry: config_entries.ConfigEntry
):
    """Set up Home Assistant Cast."""
    user_id: Optional[str] = entry.data.get("user_id")
    user: Optional[auth.models.User] = None

    if user_id is not None:
        user = await hass.auth.async_get_user(user_id)

    if user is None:
        user = await hass.auth.async_create_system_user(
            "Home Assistant Cast", [auth.GROUP_ID_ADMIN]
        )
        hass.config_entries.async_update_entry(
            entry, data={**entry.data, "user_id": user.id}
        )

    if user.refresh_tokens:
        refresh_token: auth.models.RefreshToken = list(user.refresh_tokens.values())[0]
    else:
        refresh_token = await hass.auth.async_create_refresh_token(user)

    async def handle_show_view(call: core.ServiceCall):
        """Handle a Show View service call."""
        hass_url = hass.config.api.base_url

        # Home Assistant Cast only works with https urls. If user has no configured
        # base url, use their remote url.
        if not hass_url.lower().startswith("https://"):
            try:
                hass_url = hass.components.cloud.async_remote_ui_url()
            except hass.components.cloud.CloudNotAvailable:
                pass

        controller = HomeAssistantController(
            # If you are developing Home Assistant Cast, uncomment and set to your dev app id.
            # app_id="5FE44367",
            hass_url=hass_url,
            client_id=None,
            refresh_token=refresh_token.token,
        )

        dispatcher.async_dispatcher_send(
            hass,
            SIGNAL_HASS_CAST_SHOW_VIEW,
            controller,
            call.data[ATTR_ENTITY_ID],
            call.data[ATTR_VIEW_PATH],
        )

    hass.helpers.service.async_register_admin_service(
        DOMAIN,
        SERVICE_SHOW_VIEW,
        handle_show_view,
        vol.Schema({ATTR_ENTITY_ID: cv.entity_id, ATTR_VIEW_PATH: str}),
    )
