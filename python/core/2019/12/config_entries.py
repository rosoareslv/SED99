"""Manage config entries in Home Assistant."""
import asyncio
import functools
import logging
from typing import Any, Callable, Dict, List, Optional, Set, Union, cast
import uuid
import weakref

import attr

from homeassistant import data_entry_flow, loader
from homeassistant.core import HomeAssistant, callback
from homeassistant.exceptions import ConfigEntryNotReady, HomeAssistantError
from homeassistant.helpers import entity_registry
from homeassistant.helpers.event import Event
from homeassistant.setup import async_process_deps_reqs, async_setup_component
from homeassistant.util.decorator import Registry

_LOGGER = logging.getLogger(__name__)
_UNDEF: dict = {}

SOURCE_DISCOVERY = "discovery"
SOURCE_IMPORT = "import"
SOURCE_SSDP = "ssdp"
SOURCE_USER = "user"
SOURCE_ZEROCONF = "zeroconf"

# If a user wants to hide a discovery from the UI they can "Ignore" it. The config_entries/ignore_flow
# websocket command creates a config entry with this source and while it exists normal discoveries
# with the same unique id are ignored.
SOURCE_IGNORE = "ignore"

# This is used when a user uses the "Stop Ignoring" button in the UI (the
# config_entries/ignore_flow websocket command). It's triggered after the "ignore" config entry has
# been removed and unloaded.
SOURCE_UNIGNORE = "unignore"

HANDLERS = Registry()

STORAGE_KEY = "core.config_entries"
STORAGE_VERSION = 1

# Deprecated since 0.73
PATH_CONFIG = ".config_entries.json"

SAVE_DELAY = 1

# The config entry has been set up successfully
ENTRY_STATE_LOADED = "loaded"
# There was an error while trying to set up this config entry
ENTRY_STATE_SETUP_ERROR = "setup_error"
# There was an error while trying to migrate the config entry to a new version
ENTRY_STATE_MIGRATION_ERROR = "migration_error"
# The config entry was not ready to be set up yet, but might be later
ENTRY_STATE_SETUP_RETRY = "setup_retry"
# The config entry has not been loaded
ENTRY_STATE_NOT_LOADED = "not_loaded"
# An error occurred when trying to unload the entry
ENTRY_STATE_FAILED_UNLOAD = "failed_unload"

UNRECOVERABLE_STATES = (ENTRY_STATE_MIGRATION_ERROR, ENTRY_STATE_FAILED_UNLOAD)

DISCOVERY_NOTIFICATION_ID = "config_entry_discovery"
DISCOVERY_SOURCES = (SOURCE_SSDP, SOURCE_ZEROCONF, SOURCE_DISCOVERY, SOURCE_IMPORT)

EVENT_FLOW_DISCOVERED = "config_entry_discovered"

CONN_CLASS_CLOUD_PUSH = "cloud_push"
CONN_CLASS_CLOUD_POLL = "cloud_poll"
CONN_CLASS_LOCAL_PUSH = "local_push"
CONN_CLASS_LOCAL_POLL = "local_poll"
CONN_CLASS_ASSUMED = "assumed"
CONN_CLASS_UNKNOWN = "unknown"


class ConfigError(HomeAssistantError):
    """Error while configuring an account."""


class UnknownEntry(ConfigError):
    """Unknown entry specified."""


class OperationNotAllowed(ConfigError):
    """Raised when a config entry operation is not allowed."""


class ConfigEntry:
    """Hold a configuration entry."""

    __slots__ = (
        "entry_id",
        "version",
        "domain",
        "title",
        "data",
        "options",
        "unique_id",
        "system_options",
        "source",
        "connection_class",
        "state",
        "_setup_lock",
        "update_listeners",
        "_async_cancel_retry_setup",
    )

    def __init__(
        self,
        version: int,
        domain: str,
        title: str,
        data: dict,
        source: str,
        connection_class: str,
        system_options: dict,
        options: Optional[dict] = None,
        unique_id: Optional[str] = None,
        entry_id: Optional[str] = None,
        state: str = ENTRY_STATE_NOT_LOADED,
    ) -> None:
        """Initialize a config entry."""
        # Unique id of the config entry
        self.entry_id = entry_id or uuid.uuid4().hex

        # Version of the configuration.
        self.version = version

        # Domain the configuration belongs to
        self.domain = domain

        # Title of the configuration
        self.title = title

        # Config data
        self.data = data

        # Entry options
        self.options = options or {}

        # Entry system options
        self.system_options = SystemOptions(**system_options)

        # Source of the configuration (user, discovery, cloud)
        self.source = source

        # Connection class
        self.connection_class = connection_class

        # State of the entry (LOADED, NOT_LOADED)
        self.state = state

        # Unique ID of this entry.
        self.unique_id = unique_id

        # Listeners to call on update
        self.update_listeners: List = []

        # Function to cancel a scheduled retry
        self._async_cancel_retry_setup: Optional[Callable[[], Any]] = None

    async def async_setup(
        self,
        hass: HomeAssistant,
        *,
        integration: Optional[loader.Integration] = None,
        tries: int = 0,
    ) -> None:
        """Set up an entry."""
        if self.source == SOURCE_IGNORE:
            return

        if integration is None:
            integration = await loader.async_get_integration(hass, self.domain)

        try:
            component = integration.get_component()
        except ImportError as err:
            _LOGGER.error(
                "Error importing integration %s to set up %s config entry: %s",
                integration.domain,
                self.domain,
                err,
            )
            if self.domain == integration.domain:
                self.state = ENTRY_STATE_SETUP_ERROR
            return

        if self.domain == integration.domain:
            try:
                integration.get_platform("config_flow")
            except ImportError as err:
                _LOGGER.error(
                    "Error importing platform config_flow from integration %s to set up %s config entry: %s",
                    integration.domain,
                    self.domain,
                    err,
                )
                self.state = ENTRY_STATE_SETUP_ERROR
                return

            # Perform migration
            if not await self.async_migrate(hass):
                self.state = ENTRY_STATE_MIGRATION_ERROR
                return

        try:
            result = await component.async_setup_entry(  # type: ignore
                hass, self
            )

            if not isinstance(result, bool):
                _LOGGER.error(
                    "%s.async_setup_entry did not return boolean", integration.domain
                )
                result = False
        except ConfigEntryNotReady:
            self.state = ENTRY_STATE_SETUP_RETRY
            wait_time = 2 ** min(tries, 4) * 5
            tries += 1
            _LOGGER.warning(
                "Config entry for %s not ready yet. Retrying in %d seconds.",
                self.domain,
                wait_time,
            )

            async def setup_again(now: Any) -> None:
                """Run setup again."""
                self._async_cancel_retry_setup = None
                await self.async_setup(hass, integration=integration, tries=tries)

            self._async_cancel_retry_setup = hass.helpers.event.async_call_later(
                wait_time, setup_again
            )
            return
        except Exception:  # pylint: disable=broad-except
            _LOGGER.exception(
                "Error setting up entry %s for %s", self.title, integration.domain
            )
            result = False

        # Only store setup result as state if it was not forwarded.
        if self.domain != integration.domain:
            return

        if result:
            self.state = ENTRY_STATE_LOADED
        else:
            self.state = ENTRY_STATE_SETUP_ERROR

    async def async_unload(
        self, hass: HomeAssistant, *, integration: Optional[loader.Integration] = None
    ) -> bool:
        """Unload an entry.

        Returns if unload is possible and was successful.
        """
        if self.source == SOURCE_IGNORE:
            self.state = ENTRY_STATE_NOT_LOADED
            return True

        if integration is None:
            integration = await loader.async_get_integration(hass, self.domain)

        component = integration.get_component()

        if integration.domain == self.domain:
            if self.state in UNRECOVERABLE_STATES:
                return False

            if self.state != ENTRY_STATE_LOADED:
                if self._async_cancel_retry_setup is not None:
                    self._async_cancel_retry_setup()
                    self._async_cancel_retry_setup = None

                self.state = ENTRY_STATE_NOT_LOADED
                return True

        supports_unload = hasattr(component, "async_unload_entry")

        if not supports_unload:
            if integration.domain == self.domain:
                self.state = ENTRY_STATE_FAILED_UNLOAD
            return False

        try:
            result = await component.async_unload_entry(  # type: ignore
                hass, self
            )

            assert isinstance(result, bool)

            # Only adjust state if we unloaded the component
            if result and integration.domain == self.domain:
                self.state = ENTRY_STATE_NOT_LOADED

            return result
        except Exception:  # pylint: disable=broad-except
            _LOGGER.exception(
                "Error unloading entry %s for %s", self.title, integration.domain
            )
            if integration.domain == self.domain:
                self.state = ENTRY_STATE_FAILED_UNLOAD
            return False

    async def async_remove(self, hass: HomeAssistant) -> None:
        """Invoke remove callback on component."""
        if self.source == SOURCE_IGNORE:
            return

        integration = await loader.async_get_integration(hass, self.domain)
        component = integration.get_component()
        if not hasattr(component, "async_remove_entry"):
            return
        try:
            await component.async_remove_entry(  # type: ignore
                hass, self
            )
        except Exception:  # pylint: disable=broad-except
            _LOGGER.exception(
                "Error calling entry remove callback %s for %s",
                self.title,
                integration.domain,
            )

    async def async_migrate(self, hass: HomeAssistant) -> bool:
        """Migrate an entry.

        Returns True if config entry is up-to-date or has been migrated.
        """
        handler = HANDLERS.get(self.domain)
        if handler is None:
            _LOGGER.error(
                "Flow handler not found for entry %s for %s", self.title, self.domain
            )
            return False
        # Handler may be a partial
        while isinstance(handler, functools.partial):
            handler = handler.func

        if self.version == handler.VERSION:
            return True

        integration = await loader.async_get_integration(hass, self.domain)
        component = integration.get_component()
        supports_migrate = hasattr(component, "async_migrate_entry")
        if not supports_migrate:
            _LOGGER.error(
                "Migration handler not found for entry %s for %s",
                self.title,
                self.domain,
            )
            return False

        try:
            result = await component.async_migrate_entry(  # type: ignore
                hass, self
            )
            if not isinstance(result, bool):
                _LOGGER.error(
                    "%s.async_migrate_entry did not return boolean", self.domain
                )
                return False
            if result:
                # pylint: disable=protected-access
                hass.config_entries._async_schedule_save()
            return result
        except Exception:  # pylint: disable=broad-except
            _LOGGER.exception(
                "Error migrating entry %s for %s", self.title, self.domain
            )
            return False

    def add_update_listener(self, listener: Callable) -> Callable:
        """Listen for when entry is updated.

        Listener: Callback function(hass, entry)

        Returns function to unlisten.
        """
        weak_listener = weakref.ref(listener)
        self.update_listeners.append(weak_listener)

        return lambda: self.update_listeners.remove(weak_listener)

    def as_dict(self) -> Dict[str, Any]:
        """Return dictionary version of this entry."""
        return {
            "entry_id": self.entry_id,
            "version": self.version,
            "domain": self.domain,
            "title": self.title,
            "data": self.data,
            "options": self.options,
            "system_options": self.system_options.as_dict(),
            "source": self.source,
            "connection_class": self.connection_class,
            "unique_id": self.unique_id,
        }


class ConfigEntries:
    """Manage the configuration entries.

    An instance of this object is available via `hass.config_entries`.
    """

    def __init__(self, hass: HomeAssistant, hass_config: dict) -> None:
        """Initialize the entry manager."""
        self.hass = hass
        self.flow = data_entry_flow.FlowManager(
            hass, self._async_create_flow, self._async_finish_flow
        )
        self.options = OptionsFlowManager(hass)
        self._hass_config = hass_config
        self._entries: List[ConfigEntry] = []
        self._store = hass.helpers.storage.Store(STORAGE_VERSION, STORAGE_KEY)
        EntityRegistryDisabledHandler(hass).async_setup()

    @callback
    def async_domains(self) -> List[str]:
        """Return domains for which we have entries."""
        seen: Set[str] = set()
        result = []

        for entry in self._entries:
            if entry.domain not in seen:
                seen.add(entry.domain)
                result.append(entry.domain)

        return result

    @callback
    def async_get_entry(self, entry_id: str) -> Optional[ConfigEntry]:
        """Return entry with matching entry_id."""
        for entry in self._entries:
            if entry_id == entry.entry_id:
                return entry
        return None

    @callback
    def async_entries(self, domain: Optional[str] = None) -> List[ConfigEntry]:
        """Return all entries or entries for a specific domain."""
        if domain is None:
            return list(self._entries)
        return [entry for entry in self._entries if entry.domain == domain]

    async def async_remove(self, entry_id: str) -> Dict[str, Any]:
        """Remove an entry."""
        entry = self.async_get_entry(entry_id)

        if entry is None:
            raise UnknownEntry

        if entry.state in UNRECOVERABLE_STATES:
            unload_success = entry.state != ENTRY_STATE_FAILED_UNLOAD
        else:
            unload_success = await self.async_unload(entry_id)

        await entry.async_remove(self.hass)

        self._entries.remove(entry)
        self._async_schedule_save()

        dev_reg, ent_reg = await asyncio.gather(
            self.hass.helpers.device_registry.async_get_registry(),
            self.hass.helpers.entity_registry.async_get_registry(),
        )

        dev_reg.async_clear_config_entry(entry_id)
        ent_reg.async_clear_config_entry(entry_id)

        # After we have fully removed an "ignore" config entry we can try and rediscover it so that a
        # user is able to immediately start configuring it. We do this by starting a new flow with
        # the 'unignore' step. If the integration doesn't implement async_step_unignore then
        # this will be a no-op.
        if entry.source == SOURCE_IGNORE:
            self.hass.async_create_task(
                self.hass.config_entries.flow.async_init(
                    entry.domain,
                    context={"source": SOURCE_UNIGNORE},
                    data={"unique_id": entry.unique_id},
                )
            )

        return {"require_restart": not unload_success}

    async def async_initialize(self) -> None:
        """Initialize config entry config."""
        # Migrating for config entries stored before 0.73
        config = await self.hass.helpers.storage.async_migrator(
            self.hass.config.path(PATH_CONFIG),
            self._store,
            old_conf_migrate_func=_old_conf_migrator,
        )

        if config is None:
            self._entries = []
            return

        self._entries = [
            ConfigEntry(
                version=entry["version"],
                domain=entry["domain"],
                entry_id=entry["entry_id"],
                data=entry["data"],
                source=entry["source"],
                title=entry["title"],
                # New in 0.79
                connection_class=entry.get("connection_class", CONN_CLASS_UNKNOWN),
                # New in 0.89
                options=entry.get("options"),
                # New in 0.98
                system_options=entry.get("system_options", {}),
                # New in 0.104
                unique_id=entry.get("unique_id"),
            )
            for entry in config["entries"]
        ]

    async def async_setup(self, entry_id: str) -> bool:
        """Set up a config entry.

        Return True if entry has been successfully loaded.
        """
        entry = self.async_get_entry(entry_id)

        if entry is None:
            raise UnknownEntry

        if entry.state != ENTRY_STATE_NOT_LOADED:
            raise OperationNotAllowed

        # Setup Component if not set up yet
        if entry.domain in self.hass.config.components:
            await entry.async_setup(self.hass)
        else:
            # Setting up the component will set up all its config entries
            result = await async_setup_component(
                self.hass, entry.domain, self._hass_config
            )

            if not result:
                return result

        return entry.state == ENTRY_STATE_LOADED

    async def async_unload(self, entry_id: str) -> bool:
        """Unload a config entry."""
        entry = self.async_get_entry(entry_id)

        if entry is None:
            raise UnknownEntry

        if entry.state in UNRECOVERABLE_STATES:
            raise OperationNotAllowed

        return await entry.async_unload(self.hass)

    async def async_reload(self, entry_id: str) -> bool:
        """Reload an entry.

        If an entry was not loaded, will just load.
        """
        unload_result = await self.async_unload(entry_id)

        if not unload_result:
            return unload_result

        return await self.async_setup(entry_id)

    @callback
    def async_update_entry(
        self,
        entry: ConfigEntry,
        *,
        unique_id: Union[str, dict, None] = _UNDEF,
        data: dict = _UNDEF,
        options: dict = _UNDEF,
        system_options: dict = _UNDEF,
    ) -> None:
        """Update a config entry."""
        if unique_id is not _UNDEF:
            entry.unique_id = cast(Optional[str], unique_id)

        if data is not _UNDEF:
            entry.data = data

        if options is not _UNDEF:
            entry.options = options

        if system_options is not _UNDEF:
            entry.system_options.update(**system_options)

        for listener_ref in entry.update_listeners:
            listener = listener_ref()
            self.hass.async_create_task(listener(self.hass, entry))

        self._async_schedule_save()

    async def async_forward_entry_setup(self, entry: ConfigEntry, domain: str) -> bool:
        """Forward the setup of an entry to a different component.

        By default an entry is setup with the component it belongs to. If that
        component also has related platforms, the component will have to
        forward the entry to be setup by that component.

        You don't want to await this coroutine if it is called as part of the
        setup of a component, because it can cause a deadlock.
        """
        # Setup Component if not set up yet
        if domain not in self.hass.config.components:
            result = await async_setup_component(self.hass, domain, self._hass_config)

            if not result:
                return False

        integration = await loader.async_get_integration(self.hass, domain)

        await entry.async_setup(self.hass, integration=integration)
        return True

    async def async_forward_entry_unload(self, entry: ConfigEntry, domain: str) -> bool:
        """Forward the unloading of an entry to a different component."""
        # It was never loaded.
        if domain not in self.hass.config.components:
            return True

        integration = await loader.async_get_integration(self.hass, domain)

        return await entry.async_unload(self.hass, integration=integration)

    async def _async_finish_flow(
        self, flow: "ConfigFlow", result: Dict[str, Any]
    ) -> Dict[str, Any]:
        """Finish a config flow and add an entry."""
        # Remove notification if no other discovery config entries in progress
        if not any(
            ent["context"]["source"] in DISCOVERY_SOURCES
            for ent in self.hass.config_entries.flow.async_progress()
            if ent["flow_id"] != flow.flow_id
        ):
            self.hass.components.persistent_notification.async_dismiss(
                DISCOVERY_NOTIFICATION_ID
            )

        if result["type"] != data_entry_flow.RESULT_TYPE_CREATE_ENTRY:
            return result

        # Check if config entry exists with unique ID. Unload it.
        existing_entry = None

        if flow.unique_id is not None:
            # Abort all flows in progress with same unique ID.
            for progress_flow in self.flow.async_progress():
                if (
                    progress_flow["handler"] == flow.handler
                    and progress_flow["flow_id"] != flow.flow_id
                    and progress_flow["context"].get("unique_id") == flow.unique_id
                ):
                    self.flow.async_abort(progress_flow["flow_id"])

            # Find existing entry.
            for check_entry in self.async_entries(result["handler"]):
                if check_entry.unique_id == flow.unique_id:
                    existing_entry = check_entry
                    break

        # Unload the entry before setting up the new one.
        # We will remove it only after the other one is set up,
        # so that device customizations are not getting lost.
        if (
            existing_entry is not None
            and existing_entry.state not in UNRECOVERABLE_STATES
        ):
            await self.async_unload(existing_entry.entry_id)

        entry = ConfigEntry(
            version=result["version"],
            domain=result["handler"],
            title=result["title"],
            data=result["data"],
            options={},
            system_options={},
            source=flow.context["source"],
            connection_class=flow.CONNECTION_CLASS,
            unique_id=flow.unique_id,
        )
        self._entries.append(entry)

        await self.async_setup(entry.entry_id)

        if existing_entry is not None:
            await self.async_remove(existing_entry.entry_id)

        self._async_schedule_save()

        result["result"] = entry
        return result

    async def _async_create_flow(
        self, handler_key: str, *, context: Dict[str, Any], data: Dict[str, Any]
    ) -> "ConfigFlow":
        """Create a flow for specified handler.

        Handler key is the domain of the component that we want to set up.
        """
        try:
            integration = await loader.async_get_integration(self.hass, handler_key)
        except loader.IntegrationNotFound:
            _LOGGER.error("Cannot find integration %s", handler_key)
            raise data_entry_flow.UnknownHandler

        # Make sure requirements and dependencies of component are resolved
        await async_process_deps_reqs(self.hass, self._hass_config, integration)

        try:
            integration.get_platform("config_flow")
        except ImportError as err:
            _LOGGER.error(
                "Error occurred loading config flow for integration %s: %s",
                handler_key,
                err,
            )
            raise data_entry_flow.UnknownHandler

        handler = HANDLERS.get(handler_key)

        if handler is None:
            raise data_entry_flow.UnknownHandler

        source = context["source"]

        # Create notification.
        if source in DISCOVERY_SOURCES:
            self.hass.bus.async_fire(EVENT_FLOW_DISCOVERED)
            self.hass.components.persistent_notification.async_create(
                title="New devices discovered",
                message=(
                    "We have discovered new devices on your network. "
                    "[Check it out](/config/integrations)"
                ),
                notification_id=DISCOVERY_NOTIFICATION_ID,
            )

        flow = cast(ConfigFlow, handler())
        flow.init_step = source
        return flow

    def _async_schedule_save(self) -> None:
        """Save the entity registry to a file."""
        self._store.async_delay_save(self._data_to_save, SAVE_DELAY)

    @callback
    def _data_to_save(self) -> Dict[str, List[Dict[str, Any]]]:
        """Return data to save."""
        return {"entries": [entry.as_dict() for entry in self._entries]}


async def _old_conf_migrator(old_config: Dict[str, Any]) -> Dict[str, Any]:
    """Migrate the pre-0.73 config format to the latest version."""
    return {"entries": old_config}


class ConfigFlow(data_entry_flow.FlowHandler):
    """Base class for config flows with some helpers."""

    def __init_subclass__(cls, domain: Optional[str] = None, **kwargs: Any) -> None:
        """Initialize a subclass, register if possible."""
        super().__init_subclass__(**kwargs)  # type: ignore
        if domain is not None:
            HANDLERS.register(domain)(cls)

    CONNECTION_CLASS = CONN_CLASS_UNKNOWN

    @property
    def unique_id(self) -> Optional[str]:
        """Return unique ID if available."""
        # pylint: disable=no-member
        if not self.context:
            return None

        return cast(Optional[str], self.context.get("unique_id"))

    @staticmethod
    @callback
    def async_get_options_flow(config_entry: ConfigEntry) -> "OptionsFlow":
        """Get the options flow for this handler."""
        raise data_entry_flow.UnknownHandler

    @callback
    def _abort_if_unique_id_configured(self) -> None:
        """Abort if the unique ID is already configured."""
        if self.unique_id is None:
            return

        if self.unique_id in self._async_current_ids():
            raise data_entry_flow.AbortFlow("already_configured")

    async def async_set_unique_id(
        self, unique_id: str, *, raise_on_progress: bool = True
    ) -> Optional[ConfigEntry]:
        """Set a unique ID for the config flow.

        Returns optionally existing config entry with same ID.
        """
        if raise_on_progress:
            for progress in self._async_in_progress():
                if progress["context"].get("unique_id") == unique_id:
                    raise data_entry_flow.AbortFlow("already_in_progress")

        # pylint: disable=no-member
        self.context["unique_id"] = unique_id

        for entry in self._async_current_entries():
            if entry.unique_id == unique_id:
                return entry

        return None

    @callback
    def _async_current_entries(self) -> List[ConfigEntry]:
        """Return current entries."""
        assert self.hass is not None
        return self.hass.config_entries.async_entries(self.handler)

    @callback
    def _async_current_ids(self, include_ignore: bool = True) -> Set[Optional[str]]:
        """Return current unique IDs."""
        assert self.hass is not None
        return set(
            entry.unique_id
            for entry in self.hass.config_entries.async_entries(self.handler)
            if include_ignore or entry.source != SOURCE_IGNORE
        )

    @callback
    def _async_in_progress(self) -> List[Dict]:
        """Return other in progress flows for current domain."""
        assert self.hass is not None
        return [
            flw
            for flw in self.hass.config_entries.flow.async_progress()
            if flw["handler"] == self.handler and flw["flow_id"] != self.flow_id
        ]

    async def async_step_ignore(self, user_input: Dict[str, Any]) -> Dict[str, Any]:
        """Ignore this config flow."""
        await self.async_set_unique_id(user_input["unique_id"], raise_on_progress=False)
        return self.async_create_entry(title="Ignored", data={})

    async def async_step_unignore(self, user_input: Dict[str, Any]) -> Dict[str, Any]:
        """Rediscover a config entry by it's unique_id."""
        return self.async_abort(reason="not_implemented")


class OptionsFlowManager:
    """Flow to set options for a configuration entry."""

    def __init__(self, hass: HomeAssistant) -> None:
        """Initialize the options manager."""
        self.hass = hass
        self.flow = data_entry_flow.FlowManager(
            hass, self._async_create_flow, self._async_finish_flow
        )

    async def _async_create_flow(
        self, entry_id: str, *, context: Dict[str, Any], data: Dict[str, Any]
    ) -> Optional["OptionsFlow"]:
        """Create an options flow for a config entry.

        Entry_id and flow.handler is the same thing to map entry with flow.
        """
        entry = self.hass.config_entries.async_get_entry(entry_id)
        if entry is None:
            return None

        if entry.domain not in HANDLERS:
            raise data_entry_flow.UnknownHandler

        flow = cast(OptionsFlow, HANDLERS[entry.domain].async_get_options_flow(entry))
        return flow

    async def _async_finish_flow(
        self, flow: "OptionsFlow", result: Dict[str, Any]
    ) -> Optional[Dict[str, Any]]:
        """Finish an options flow and update options for configuration entry.

        Flow.handler and entry_id is the same thing to map flow with entry.
        """
        entry = self.hass.config_entries.async_get_entry(flow.handler)
        if entry is None:
            return None
        self.hass.config_entries.async_update_entry(entry, options=result["data"])

        result["result"] = True
        return result


class OptionsFlow(data_entry_flow.FlowHandler):
    """Base class for config option flows."""

    handler: str


@attr.s(slots=True)
class SystemOptions:
    """Config entry system options."""

    disable_new_entities = attr.ib(type=bool, default=False)

    def update(self, *, disable_new_entities: bool) -> None:
        """Update properties."""
        self.disable_new_entities = disable_new_entities

    def as_dict(self) -> Dict[str, Any]:
        """Return dictionary version of this config entrys system options."""
        return {"disable_new_entities": self.disable_new_entities}


class EntityRegistryDisabledHandler:
    """Handler to handle when entities related to config entries updating disabled_by."""

    RELOAD_AFTER_UPDATE_DELAY = 30

    def __init__(self, hass: HomeAssistant) -> None:
        """Initialize the handler."""
        self.hass = hass
        self.registry: Optional[entity_registry.EntityRegistry] = None
        self.changed: Set[str] = set()
        self._remove_call_later: Optional[Callable[[], None]] = None

    @callback
    def async_setup(self) -> None:
        """Set up the disable handler."""
        self.hass.bus.async_listen(
            entity_registry.EVENT_ENTITY_REGISTRY_UPDATED, self._handle_entry_updated
        )

    async def _handle_entry_updated(self, event: Event) -> None:
        """Handle entity registry entry update."""
        if (
            event.data["action"] != "update"
            or "disabled_by" not in event.data["changes"]
        ):
            return

        if self.registry is None:
            self.registry = await entity_registry.async_get_registry(self.hass)

        entity_entry = self.registry.async_get(event.data["entity_id"])

        if (
            # Stop if no entry found
            entity_entry is None
            # Stop if entry not connected to config entry
            or entity_entry.config_entry_id is None
            # Stop if the entry got disabled. In that case the entity handles it
            # themselves.
            or entity_entry.disabled_by
        ):
            return

        config_entry = self.hass.config_entries.async_get_entry(
            entity_entry.config_entry_id
        )
        assert config_entry is not None

        if config_entry.entry_id not in self.changed and await support_entry_unload(
            self.hass, config_entry.domain
        ):
            self.changed.add(config_entry.entry_id)

        if not self.changed:
            return

        # We are going to delay reloading on *every* entity registry change so that
        # if a user is happily clicking along, it will only reload at the end.

        if self._remove_call_later:
            self._remove_call_later()

        self._remove_call_later = self.hass.helpers.event.async_call_later(
            self.RELOAD_AFTER_UPDATE_DELAY, self._handle_reload
        )

    async def _handle_reload(self, _now: Any) -> None:
        """Handle a reload."""
        self._remove_call_later = None
        to_reload = self.changed
        self.changed = set()

        _LOGGER.info(
            "Reloading config entries because disabled_by changed in entity registry: %s",
            ", ".join(self.changed),
        )

        await asyncio.gather(
            *[self.hass.config_entries.async_reload(entry_id) for entry_id in to_reload]
        )


async def support_entry_unload(hass: HomeAssistant, domain: str) -> bool:
    """Test if a domain supports entry unloading."""
    integration = await loader.async_get_integration(hass, domain)
    component = integration.get_component()
    return hasattr(component, "async_unload_entry")
