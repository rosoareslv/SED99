"""Handle MySensors messages."""
import logging

from homeassistant.core import callback
from homeassistant.helpers.dispatcher import async_dispatcher_send
from homeassistant.util import decorator

from .const import MYSENSORS_GATEWAY_READY, CHILD_CALLBACK, NODE_CALLBACK
from .device import get_mysensors_devices
from .helpers import discover_mysensors_platform, validate_child

_LOGGER = logging.getLogger(__name__)
HANDLERS = decorator.Registry()


@HANDLERS.register('presentation')
async def handle_presentation(hass, hass_config, msg):
    """Handle a mysensors presentation message."""
    # Handle both node and child presentation.
    from mysensors.const import SYSTEM_CHILD_ID
    if msg.child_id == SYSTEM_CHILD_ID:
        return
    _handle_child_update(hass, hass_config, msg)


@HANDLERS.register('set')
async def handle_set(hass, hass_config, msg):
    """Handle a mysensors set message."""
    _handle_child_update(hass, hass_config, msg)


@HANDLERS.register('internal')
async def handle_internal(hass, hass_config, msg):
    """Handle a mysensors internal message."""
    internal = msg.gateway.const.Internal(msg.sub_type)
    handler = HANDLERS.get(internal.name)
    if handler is None:
        return
    await handler(hass, hass_config, msg)


@HANDLERS.register('I_BATTERY_LEVEL')
async def handle_battery_level(hass, hass_config, msg):
    """Handle an internal battery level message."""
    _handle_node_update(hass, msg)


@HANDLERS.register('I_HEARTBEAT_RESPONSE')
async def handle_heartbeat(hass, hass_config, msg):
    """Handle an heartbeat."""
    _handle_node_update(hass, msg)


@HANDLERS.register('I_SKETCH_NAME')
async def handle_sketch_name(hass, hass_config, msg):
    """Handle an internal sketch name message."""
    _handle_node_update(hass, msg)


@HANDLERS.register('I_SKETCH_VERSION')
async def handle_sketch_version(hass, hass_config, msg):
    """Handle an internal sketch version message."""
    _handle_node_update(hass, msg)


@HANDLERS.register('I_GATEWAY_READY')
async def handle_gateway_ready(hass, hass_config, msg):
    """Handle an internal gateway ready message.

    Set asyncio future result if gateway is ready.
    """
    gateway_ready = hass.data.get(MYSENSORS_GATEWAY_READY.format(
        id(msg.gateway)))
    if gateway_ready is None or gateway_ready.cancelled():
        return
    gateway_ready.set_result(True)


@callback
def _handle_child_update(hass, hass_config, msg):
    """Handle a child update."""
    child = msg.gateway.sensors[msg.node_id].children[msg.child_id]
    signals = []

    # Update all platforms for the device via dispatcher.
    # Add/update entity if schema validates to true.
    validated = validate_child(msg.gateway, msg.node_id, child)
    for platform, dev_ids in validated.items():
        devices = get_mysensors_devices(hass, platform)
        new_dev_ids = []
        for dev_id in dev_ids:
            if dev_id in devices:
                signals.append(CHILD_CALLBACK.format(*dev_id))
            else:
                new_dev_ids.append(dev_id)
        if new_dev_ids:
            discover_mysensors_platform(
                hass, hass_config, platform, new_dev_ids)
    for signal in set(signals):
        # Only one signal per device is needed.
        # A device can have multiple platforms, ie multiple schemas.
        async_dispatcher_send(hass, signal)


@callback
def _handle_node_update(hass, msg):
    """Handle a node update."""
    signal = NODE_CALLBACK.format(id(msg.gateway), msg.node_id)
    async_dispatcher_send(hass, signal)
