"""
Core components of Home Assistant.

Home Assistant is a Home Automation framework for observing the state
of entities and react to changes.
"""

import enum
import functools as ft
import logging
import os
import re
import signal
import threading
import time
from types import MappingProxyType

# pylint: disable=unused-import
from typing import Optional, Any, Callable, List  # NOQA

import voluptuous as vol
from voluptuous.humanize import humanize_error

from homeassistant.const import (
    ATTR_DOMAIN, ATTR_FRIENDLY_NAME, ATTR_NOW, ATTR_SERVICE,
    ATTR_SERVICE_CALL_ID, ATTR_SERVICE_DATA, EVENT_CALL_SERVICE,
    EVENT_HOMEASSISTANT_START, EVENT_HOMEASSISTANT_STOP,
    EVENT_SERVICE_EXECUTED, EVENT_SERVICE_REGISTERED, EVENT_STATE_CHANGED,
    EVENT_TIME_CHANGED, MATCH_ALL, RESTART_EXIT_CODE,
    SERVICE_HOMEASSISTANT_RESTART, SERVICE_HOMEASSISTANT_STOP, __version__)
from homeassistant.exceptions import (
    HomeAssistantError, InvalidEntityFormatError)
import homeassistant.util as util
import homeassistant.util.dt as dt_util
import homeassistant.util.location as location
from homeassistant.util.unit_system import UnitSystem, METRIC_SYSTEM  # NOQA

DOMAIN = "homeassistant"

# How often time_changed event should fire
TIMER_INTERVAL = 1  # seconds

# How long we wait for the result of a service call
SERVICE_CALL_LIMIT = 10  # seconds

# Define number of MINIMUM worker threads.
# During bootstrap of HA (see bootstrap._setup_component()) worker threads
# will be added for each component that polls devices.
MIN_WORKER_THREAD = 2

# Pattern for validating entity IDs (format: <domain>.<entity>)
ENTITY_ID_PATTERN = re.compile(r"^(\w+)\.(\w+)$")

_LOGGER = logging.getLogger(__name__)


def split_entity_id(entity_id: str) -> List[str]:
    """Split a state entity_id into domain, object_id."""
    return entity_id.split(".", 1)


def valid_entity_id(entity_id: str) -> bool:
    """Test if an entity ID is a valid format."""
    return ENTITY_ID_PATTERN.match(entity_id) is not None


class CoreState(enum.Enum):
    """Represent the current state of Home Assistant."""

    not_running = "NOT_RUNNING"
    starting = "STARTING"
    running = "RUNNING"
    stopping = "STOPPING"

    def __str__(self) -> str:
        """Return the event."""
        return self.value


class JobPriority(util.OrderedEnum):
    """Provides job priorities for event bus jobs."""

    EVENT_CALLBACK = 0
    EVENT_SERVICE = 1
    EVENT_STATE = 2
    EVENT_TIME = 3
    EVENT_DEFAULT = 4

    @staticmethod
    def from_event_type(event_type):
        """Return a priority based on event type."""
        if event_type == EVENT_TIME_CHANGED:
            return JobPriority.EVENT_TIME
        elif event_type == EVENT_STATE_CHANGED:
            return JobPriority.EVENT_STATE
        elif event_type == EVENT_CALL_SERVICE:
            return JobPriority.EVENT_SERVICE
        elif event_type == EVENT_SERVICE_EXECUTED:
            return JobPriority.EVENT_CALLBACK
        return JobPriority.EVENT_DEFAULT


class HomeAssistant(object):
    """Root object of the Home Assistant home automation."""

    def __init__(self):
        """Initialize new Home Assistant object."""
        self.pool = pool = create_worker_pool()
        self.bus = EventBus(pool)
        self.services = ServiceRegistry(self.bus, self.add_job)
        self.states = StateMachine(self.bus)
        self.config = Config()  # type: Config
        self.state = CoreState.not_running

    @property
    def is_running(self) -> bool:
        """Return if Home Assistant is running."""
        return self.state in (CoreState.starting, CoreState.running)

    def start(self) -> None:
        """Start home assistant."""
        _LOGGER.info(
            "Starting Home Assistant (%d threads)", self.pool.worker_count)
        self.state = CoreState.starting

        create_timer(self)
        self.bus.fire(EVENT_HOMEASSISTANT_START)
        self.pool.block_till_done()
        self.state = CoreState.running

    def add_job(self,
                target: Callable[..., None],
                *args: Any,
                priority: JobPriority=JobPriority.EVENT_DEFAULT) -> None:
        """Add job to the worker pool.

        target: target to call.
        args: parameters for method to call.
        """
        self.pool.add_job(priority, (target,) + args)

    def block_till_stopped(self) -> int:
        """Register service homeassistant/stop and will block until called."""
        request_shutdown = threading.Event()
        request_restart = threading.Event()

        def stop_homeassistant(*args):
            """Stop Home Assistant."""
            request_shutdown.set()

        def restart_homeassistant(*args):
            """Reset Home Assistant."""
            _LOGGER.warning('Home Assistant requested a restart.')
            request_restart.set()
            request_shutdown.set()

        self.services.register(
            DOMAIN, SERVICE_HOMEASSISTANT_STOP, stop_homeassistant)
        self.services.register(
            DOMAIN, SERVICE_HOMEASSISTANT_RESTART, restart_homeassistant)

        try:
            signal.signal(signal.SIGTERM, stop_homeassistant)
        except ValueError:
            _LOGGER.warning(
                'Could not bind to SIGTERM. Are you running in a thread?')
        try:
            signal.signal(signal.SIGHUP, restart_homeassistant)
        except ValueError:
            _LOGGER.warning(
                'Could not bind to SIGHUP. Are you running in a thread?')
        except AttributeError:
            pass
        try:
            while not request_shutdown.is_set():
                time.sleep(1)
        except KeyboardInterrupt:
            pass
        finally:
            self.stop()

        return RESTART_EXIT_CODE if request_restart.is_set() else 0

    def stop(self) -> None:
        """Stop Home Assistant and shuts down all threads."""
        _LOGGER.info("Stopping")
        self.state = CoreState.stopping
        self.bus.fire(EVENT_HOMEASSISTANT_STOP)
        self.pool.stop()
        self.state = CoreState.not_running


class EventOrigin(enum.Enum):
    """Represent the origin of an event."""

    local = "LOCAL"
    remote = "REMOTE"

    def __str__(self):
        """Return the event."""
        return self.value


class Event(object):
    # pylint: disable=too-few-public-methods
    """Represents an event within the Bus."""

    __slots__ = ['event_type', 'data', 'origin', 'time_fired']

    def __init__(self, event_type, data=None, origin=EventOrigin.local,
                 time_fired=None):
        """Initialize a new event."""
        self.event_type = event_type
        self.data = data or {}
        self.origin = origin
        self.time_fired = time_fired or dt_util.utcnow()

    def as_dict(self):
        """Create a dict representation of this Event."""
        return {
            'event_type': self.event_type,
            'data': dict(self.data),
            'origin': str(self.origin),
            'time_fired': self.time_fired,
        }

    def __repr__(self):
        """Return the representation."""
        # pylint: disable=maybe-no-member
        if self.data:
            return "<Event {}[{}]: {}>".format(
                self.event_type, str(self.origin)[0],
                util.repr_helper(self.data))
        else:
            return "<Event {}[{}]>".format(self.event_type,
                                           str(self.origin)[0])

    def __eq__(self, other):
        """Return the comparison."""
        return (self.__class__ == other.__class__ and
                self.event_type == other.event_type and
                self.data == other.data and
                self.origin == other.origin and
                self.time_fired == other.time_fired)


class EventBus(object):
    """Allows firing of and listening for events."""

    def __init__(self, pool: util.ThreadPool) -> None:
        """Initialize a new event bus."""
        self._listeners = {}
        self._lock = threading.Lock()
        self._pool = pool

    @property
    def listeners(self):
        """Dict with events and the number of listeners."""
        with self._lock:
            return {key: len(self._listeners[key])
                    for key in self._listeners}

    def fire(self, event_type: str, event_data=None, origin=EventOrigin.local):
        """Fire an event."""
        if not self._pool.running:
            raise HomeAssistantError('Home Assistant has shut down.')

        with self._lock:
            # Copy the list of the current listeners because some listeners
            # remove themselves as a listener while being executed which
            # causes the iterator to be confused.
            get = self._listeners.get
            listeners = get(MATCH_ALL, []) + get(event_type, [])

            event = Event(event_type, event_data, origin)

            if event_type != EVENT_TIME_CHANGED:
                _LOGGER.info("Bus:Handling %s", event)

            if not listeners:
                return

            job_priority = JobPriority.from_event_type(event_type)

            for func in listeners:
                self._pool.add_job(job_priority, (func, event))

    def listen(self, event_type, listener):
        """Listen for all events or events of a specific type.

        To listen to all events specify the constant ``MATCH_ALL``
        as event_type.
        """
        with self._lock:
            if event_type in self._listeners:
                self._listeners[event_type].append(listener)
            else:
                self._listeners[event_type] = [listener]

    def listen_once(self, event_type, listener):
        """Listen once for event of a specific type.

        To listen to all events specify the constant ``MATCH_ALL``
        as event_type.

        Returns registered listener that can be used with remove_listener.
        """
        @ft.wraps(listener)
        def onetime_listener(event):
            """Remove listener from eventbus and then fire listener."""
            if hasattr(onetime_listener, 'run'):
                return
            # Set variable so that we will never run twice.
            # Because the event bus might have to wait till a thread comes
            # available to execute this listener it might occur that the
            # listener gets lined up twice to be executed.
            # This will make sure the second time it does nothing.
            setattr(onetime_listener, 'run', True)

            self.remove_listener(event_type, onetime_listener)

            listener(event)

        self.listen(event_type, onetime_listener)

        return onetime_listener

    def remove_listener(self, event_type, listener):
        """Remove a listener of a specific event_type."""
        with self._lock:
            try:
                self._listeners[event_type].remove(listener)

                # delete event_type list if empty
                if not self._listeners[event_type]:
                    self._listeners.pop(event_type)

            except (KeyError, ValueError):
                # KeyError is key event_type listener did not exist
                # ValueError if listener did not exist within event_type
                pass


class State(object):
    """Object to represent a state within the state machine.

    entity_id: the entity that is represented.
    state: the state of the entity
    attributes: extra information on entity and state
    last_changed: last time the state was changed, not the attributes.
    last_updated: last time this object was updated.
    """

    __slots__ = ['entity_id', 'state', 'attributes',
                 'last_changed', 'last_updated']

    # pylint: disable=too-many-arguments
    def __init__(self, entity_id, state, attributes=None, last_changed=None,
                 last_updated=None):
        """Initialize a new state."""
        if not valid_entity_id(entity_id):
            raise InvalidEntityFormatError((
                "Invalid entity id encountered: {}. "
                "Format should be <domain>.<object_id>").format(entity_id))

        self.entity_id = entity_id.lower()
        self.state = str(state)
        self.attributes = MappingProxyType(attributes or {})
        self.last_updated = last_updated or dt_util.utcnow()

        self.last_changed = last_changed or self.last_updated

    @property
    def domain(self):
        """Domain of this state."""
        return split_entity_id(self.entity_id)[0]

    @property
    def object_id(self):
        """Object id of this state."""
        return split_entity_id(self.entity_id)[1]

    @property
    def name(self):
        """Name of this state."""
        return (
            self.attributes.get(ATTR_FRIENDLY_NAME) or
            self.object_id.replace('_', ' '))

    def as_dict(self):
        """Return a dict representation of the State.

        To be used for JSON serialization.
        Ensures: state == State.from_dict(state.as_dict())
        """
        return {'entity_id': self.entity_id,
                'state': self.state,
                'attributes': dict(self.attributes),
                'last_changed': self.last_changed,
                'last_updated': self.last_updated}

    @classmethod
    def from_dict(cls, json_dict):
        """Initialize a state from a dict.

        Ensures: state == State.from_json_dict(state.to_json_dict())
        """
        if not (json_dict and 'entity_id' in json_dict and
                'state' in json_dict):
            return None

        last_changed = json_dict.get('last_changed')

        if isinstance(last_changed, str):
            last_changed = dt_util.parse_datetime(last_changed)

        last_updated = json_dict.get('last_updated')

        if isinstance(last_updated, str):
            last_updated = dt_util.parse_datetime(last_updated)

        return cls(json_dict['entity_id'], json_dict['state'],
                   json_dict.get('attributes'), last_changed, last_updated)

    def __eq__(self, other):
        """Return the comparison of the state."""
        return (self.__class__ == other.__class__ and
                self.entity_id == other.entity_id and
                self.state == other.state and
                self.attributes == other.attributes)

    def __repr__(self):
        """Return the representation of the states."""
        attr = "; {}".format(util.repr_helper(self.attributes)) \
               if self.attributes else ""

        return "<state {}={}{} @ {}>".format(
            self.entity_id, self.state, attr,
            dt_util.as_local(self.last_changed).isoformat())


class StateMachine(object):
    """Helper class that tracks the state of different entities."""

    def __init__(self, bus):
        """Initialize state machine."""
        self._states = {}
        self._bus = bus
        self._lock = threading.Lock()

    def entity_ids(self, domain_filter=None):
        """List of entity ids that are being tracked."""
        if domain_filter is None:
            return list(self._states.keys())

        domain_filter = domain_filter.lower()

        with self._lock:
            return [state.entity_id for state in self._states.values()
                    if state.domain == domain_filter]

    def all(self):
        """Create a list of all states."""
        with self._lock:
            return list(self._states.values())

    def get(self, entity_id):
        """Retrieve state of entity_id or None if not found."""
        return self._states.get(entity_id.lower())

    def is_state(self, entity_id, state):
        """Test if entity exists and is specified state."""
        entity_id = entity_id.lower()

        return (entity_id in self._states and
                self._states[entity_id].state == state)

    def is_state_attr(self, entity_id, name, value):
        """Test if entity exists and has a state attribute set to value."""
        entity_id = entity_id.lower()

        return (entity_id in self._states and
                self._states[entity_id].attributes.get(name, None) == value)

    def remove(self, entity_id):
        """Remove the state of an entity.

        Returns boolean to indicate if an entity was removed.
        """
        entity_id = entity_id.lower()

        with self._lock:
            old_state = self._states.pop(entity_id, None)

            if old_state is None:
                return False

            event_data = {
                'entity_id': entity_id,
                'old_state': old_state,
                'new_state': None,
            }

            self._bus.fire(EVENT_STATE_CHANGED, event_data)

            return True

    def set(self, entity_id, new_state, attributes=None, force_update=False):
        """Set the state of an entity, add entity if it does not exist.

        Attributes is an optional dict to specify attributes of this state.

        If you just update the attributes and not the state, last changed will
        not be affected.
        """
        entity_id = entity_id.lower()
        new_state = str(new_state)
        attributes = attributes or {}

        with self._lock:
            old_state = self._states.get(entity_id)

            is_existing = old_state is not None
            same_state = (is_existing and old_state.state == new_state and
                          not force_update)
            same_attr = is_existing and old_state.attributes == attributes

            if same_state and same_attr:
                return

            # If state did not exist or is different, set it
            last_changed = old_state.last_changed if same_state else None

            state = State(entity_id, new_state, attributes, last_changed)
            self._states[entity_id] = state

            event_data = {
                'entity_id': entity_id,
                'old_state': old_state,
                'new_state': state,
            }

            self._bus.fire(EVENT_STATE_CHANGED, event_data)


# pylint: disable=too-few-public-methods
class Service(object):
    """Represents a callable service."""

    __slots__ = ['func', 'description', 'fields', 'schema']

    def __init__(self, func, description, fields, schema):
        """Initialize a service."""
        self.func = func
        self.description = description or ''
        self.fields = fields or {}
        self.schema = schema

    def as_dict(self):
        """Return dictionary representation of this service."""
        return {
            'description': self.description,
            'fields': self.fields,
        }

    def __call__(self, call):
        """Execute the service."""
        try:
            if self.schema:
                call.data = self.schema(call.data)

            self.func(call)
        except vol.MultipleInvalid as ex:
            _LOGGER.error('Invalid service data for %s.%s: %s',
                          call.domain, call.service,
                          humanize_error(call.data, ex))


# pylint: disable=too-few-public-methods
class ServiceCall(object):
    """Represents a call to a service."""

    __slots__ = ['domain', 'service', 'data', 'call_id']

    def __init__(self, domain, service, data=None, call_id=None):
        """Initialize a service call."""
        self.domain = domain.lower()
        self.service = service.lower()
        self.data = data or {}
        self.call_id = call_id

    def __repr__(self):
        """Return the represenation of the service."""
        if self.data:
            return "<ServiceCall {}.{}: {}>".format(
                self.domain, self.service, util.repr_helper(self.data))
        else:
            return "<ServiceCall {}.{}>".format(self.domain, self.service)


class ServiceRegistry(object):
    """Offers services over the eventbus."""

    def __init__(self, bus, add_job):
        """Initialize a service registry."""
        self._services = {}
        self._lock = threading.Lock()
        self._add_job = add_job
        self._bus = bus
        self._cur_id = 0
        bus.listen(EVENT_CALL_SERVICE, self._event_to_service_call)

    @property
    def services(self):
        """Dict with per domain a list of available services."""
        with self._lock:
            return {domain: {key: value.as_dict() for key, value
                             in self._services[domain].items()}
                    for domain in self._services}

    def has_service(self, domain, service):
        """Test if specified service exists."""
        return service.lower() in self._services.get(domain.lower(), [])

    # pylint: disable=too-many-arguments
    def register(self, domain, service, service_func, description=None,
                 schema=None):
        """
        Register a service.

        Description is a dict containing key 'description' to describe
        the service and a key 'fields' to describe the fields.

        Schema is called to coerce and validate the service data.
        """
        domain = domain.lower()
        service = service.lower()
        description = description or {}
        service_obj = Service(service_func, description.get('description'),
                              description.get('fields', {}), schema)
        with self._lock:
            if domain in self._services:
                self._services[domain][service] = service_obj
            else:
                self._services[domain] = {service: service_obj}

            self._bus.fire(
                EVENT_SERVICE_REGISTERED,
                {ATTR_DOMAIN: domain, ATTR_SERVICE: service})

    def call(self, domain, service, service_data=None, blocking=False):
        """
        Call a service.

        Specify blocking=True to wait till service is executed.
        Waits a maximum of SERVICE_CALL_LIMIT.

        If blocking = True, will return boolean if service executed
        succesfully within SERVICE_CALL_LIMIT.

        This method will fire an event to call the service.
        This event will be picked up by this ServiceRegistry and any
        other ServiceRegistry that is listening on the EventBus.

        Because the service is sent as an event you are not allowed to use
        the keys ATTR_DOMAIN and ATTR_SERVICE in your service_data.
        """
        call_id = self._generate_unique_id()

        event_data = {
            ATTR_DOMAIN: domain.lower(),
            ATTR_SERVICE: service.lower(),
            ATTR_SERVICE_DATA: service_data,
            ATTR_SERVICE_CALL_ID: call_id,
        }

        if blocking:
            executed_event = threading.Event()

            def service_executed(call):
                """Callback method that is called when service is executed."""
                if call.data[ATTR_SERVICE_CALL_ID] == call_id:
                    executed_event.set()

            self._bus.listen(EVENT_SERVICE_EXECUTED, service_executed)

        self._bus.fire(EVENT_CALL_SERVICE, event_data)

        if blocking:
            success = executed_event.wait(SERVICE_CALL_LIMIT)
            self._bus.remove_listener(
                EVENT_SERVICE_EXECUTED, service_executed)
            return success

    def _event_to_service_call(self, event):
        """Callback for SERVICE_CALLED events from the event bus."""
        service_data = event.data.get(ATTR_SERVICE_DATA)
        domain = event.data.get(ATTR_DOMAIN).lower()
        service = event.data.get(ATTR_SERVICE).lower()
        call_id = event.data.get(ATTR_SERVICE_CALL_ID)

        if not self.has_service(domain, service):
            if event.origin == EventOrigin.local:
                _LOGGER.warning('Unable to find service %s/%s',
                                domain, service)
            return

        service_handler = self._services[domain][service]
        service_call = ServiceCall(domain, service, service_data, call_id)

        # Add a job to the pool that calls _execute_service
        self._add_job(self._execute_service, service_handler, service_call,
                      priority=JobPriority.EVENT_SERVICE)

    def _execute_service(self, service, call):
        """Execute a service and fires a SERVICE_EXECUTED event."""
        service(call)

        if call.call_id is not None:
            self._bus.fire(
                EVENT_SERVICE_EXECUTED, {ATTR_SERVICE_CALL_ID: call.call_id})

    def _generate_unique_id(self):
        """Generate a unique service call id."""
        self._cur_id += 1
        return "{}-{}".format(id(self), self._cur_id)


class Config(object):
    """Configuration settings for Home Assistant."""

    # pylint: disable=too-many-instance-attributes
    def __init__(self):
        """Initialize a new config object."""
        self.latitude = None  # type: Optional[float]
        self.longitude = None  # type: Optional[float]
        self.elevation = None  # type: Optional[int]
        self.location_name = None  # type: Optional[str]
        self.time_zone = None  # type: Optional[str]
        self.units = METRIC_SYSTEM  # type: UnitSystem

        # If True, pip install is skipped for requirements on startup
        self.skip_pip = False  # type: bool

        # List of loaded components
        self.components = []

        # Remote.API object pointing at local API
        self.api = None

        # Directory that holds the configuration
        self.config_dir = None

    def distance(self: object, lat: float, lon: float) -> float:
        """Calculate distance from Home Assistant."""
        return self.units.length(
            location.distance(self.latitude, self.longitude, lat, lon), 'm')

    def path(self, *path):
        """Generate path to the file within the config dir."""
        if self.config_dir is None:
            raise HomeAssistantError("config_dir is not set")
        return os.path.join(self.config_dir, *path)

    def as_dict(self):
        """Create a dict representation of this dict."""
        time_zone = self.time_zone or dt_util.UTC

        return {
            'latitude': self.latitude,
            'longitude': self.longitude,
            'unit_system': self.units.as_dict(),
            'location_name': self.location_name,
            'time_zone': time_zone.zone,
            'components': self.components,
            'version': __version__
        }


def create_timer(hass, interval=TIMER_INTERVAL):
    """Create a timer that will start on HOMEASSISTANT_START."""
    # We want to be able to fire every time a minute starts (seconds=0).
    # We want this so other modules can use that to make sure they fire
    # every minute.
    assert 60 % interval == 0, "60 % TIMER_INTERVAL should be 0!"

    def timer():
        """Send an EVENT_TIME_CHANGED on interval."""
        stop_event = threading.Event()

        def stop_timer(event):
            """Stop the timer."""
            stop_event.set()

        hass.bus.listen_once(EVENT_HOMEASSISTANT_STOP, stop_timer)

        _LOGGER.info("Timer:starting")

        last_fired_on_second = -1

        calc_now = dt_util.utcnow

        while not stop_event.is_set():
            now = calc_now()

            # First check checks if we are not on a second matching the
            # timer interval. Second check checks if we did not already fire
            # this interval.
            if now.second % interval or \
               now.second == last_fired_on_second:

                # Sleep till it is the next time that we have to fire an event.
                # Aim for halfway through the second that fits TIMER_INTERVAL.
                # If TIMER_INTERVAL is 10 fire at .5, 10.5, 20.5, etc seconds.
                # This will yield the best results because time.sleep() is not
                # 100% accurate because of non-realtime OS's
                slp_seconds = interval - now.second % interval + \
                    .5 - now.microsecond/1000000.0

                time.sleep(slp_seconds)

                now = calc_now()

            last_fired_on_second = now.second

            # Event might have been set while sleeping
            if not stop_event.is_set():
                try:
                    hass.bus.fire(EVENT_TIME_CHANGED, {ATTR_NOW: now})
                except HomeAssistantError:
                    # HA raises error if firing event after it has shut down
                    break

    def start_timer(event):
        """Start the timer."""
        thread = threading.Thread(target=timer, name='Timer')
        thread.daemon = True
        thread.start()

    hass.bus.listen_once(EVENT_HOMEASSISTANT_START, start_timer)


def create_worker_pool(worker_count=None):
    """Create a worker pool."""
    if worker_count is None:
        worker_count = MIN_WORKER_THREAD

    def job_handler(job):
        """Called whenever a job is available to do."""
        try:
            func, *args = job
            func(*args)
        except Exception:  # pylint: disable=broad-except
            # Catch any exception our service/event_listener might throw
            # We do not want to crash our ThreadPool
            _LOGGER.exception("BusHandler:Exception doing job")

    def busy_callback(worker_count, current_jobs, pending_jobs_count):
        """Callback to be called when the pool queue gets too big."""
        _LOGGER.warning(
            "WorkerPool:All %d threads are busy and %d jobs pending",
            worker_count, pending_jobs_count)

        for start, job in current_jobs:
            _LOGGER.warning("WorkerPool:Current job from %s: %s",
                            dt_util.as_local(start).isoformat(), job)

    return util.ThreadPool(job_handler, worker_count, busy_callback)
