"""Helpers for listening to events."""
from datetime import datetime, timedelta
import functools as ft
from typing import Any, Callable, Dict, Iterable, Optional, Union, cast

import attr

from homeassistant.const import (
    ATTR_NOW,
    EVENT_CORE_CONFIG_UPDATE,
    EVENT_STATE_CHANGED,
    EVENT_TIME_CHANGED,
    MATCH_ALL,
    SUN_EVENT_SUNRISE,
    SUN_EVENT_SUNSET,
)
from homeassistant.core import CALLBACK_TYPE, Event, HomeAssistant, State, callback
from homeassistant.helpers.sun import get_astral_event_next
from homeassistant.helpers.template import Template
from homeassistant.loader import bind_hass
from homeassistant.util import dt as dt_util
from homeassistant.util.async_ import run_callback_threadsafe

# PyLint does not like the use of threaded_listener_factory
# pylint: disable=invalid-name


def threaded_listener_factory(async_factory: Callable[..., Any]) -> CALLBACK_TYPE:
    """Convert an async event helper to a threaded one."""

    @ft.wraps(async_factory)
    def factory(*args: Any, **kwargs: Any) -> CALLBACK_TYPE:
        """Call async event helper safely."""
        hass = args[0]

        if not isinstance(hass, HomeAssistant):
            raise TypeError("First parameter needs to be a hass instance")

        async_remove = run_callback_threadsafe(
            hass.loop, ft.partial(async_factory, *args, **kwargs)
        ).result()

        def remove() -> None:
            """Threadsafe removal."""
            run_callback_threadsafe(hass.loop, async_remove).result()

        return remove

    return factory


@callback
@bind_hass
def async_track_state_change(
    hass: HomeAssistant,
    entity_ids: Union[str, Iterable[str]],
    action: Callable[[str, State, State], None],
    from_state: Union[None, str, Iterable[str]] = None,
    to_state: Union[None, str, Iterable[str]] = None,
) -> CALLBACK_TYPE:
    """Track specific state changes.

    entity_ids, from_state and to_state can be string or list.
    Use list to match multiple.

    Returns a function that can be called to remove the listener.

    Must be run within the event loop.
    """
    match_from_state = _process_state_match(from_state)
    match_to_state = _process_state_match(to_state)

    # Ensure it is a lowercase list with entity ids we want to match on
    if entity_ids == MATCH_ALL:
        pass
    elif isinstance(entity_ids, str):
        entity_ids = (entity_ids.lower(),)
    else:
        entity_ids = tuple(entity_id.lower() for entity_id in entity_ids)

    @callback
    def state_change_listener(event: Event) -> None:
        """Handle specific state changes."""
        if (
            entity_ids != MATCH_ALL
            and cast(str, event.data.get("entity_id")) not in entity_ids
        ):
            return

        old_state = event.data.get("old_state")
        if old_state is not None:
            old_state = old_state.state

        new_state = event.data.get("new_state")
        if new_state is not None:
            new_state = new_state.state

        if match_from_state(old_state) and match_to_state(new_state):
            hass.async_run_job(
                action,
                event.data.get("entity_id"),
                event.data.get("old_state"),
                event.data.get("new_state"),
            )

    return hass.bus.async_listen(EVENT_STATE_CHANGED, state_change_listener)


track_state_change = threaded_listener_factory(async_track_state_change)


@callback
@bind_hass
def async_track_template(
    hass: HomeAssistant,
    template: Template,
    action: Callable[[str, State, State], None],
    variables: Optional[Dict[str, Any]] = None,
) -> CALLBACK_TYPE:
    """Add a listener that track state changes with template condition."""
    from . import condition

    # Local variable to keep track of if the action has already been triggered
    already_triggered = False

    @callback
    def template_condition_listener(entity_id: str, from_s: State, to_s: State) -> None:
        """Check if condition is correct and run action."""
        nonlocal already_triggered
        template_result = condition.async_template(hass, template, variables)

        # Check to see if template returns true
        if template_result and not already_triggered:
            already_triggered = True
            hass.async_run_job(action, entity_id, from_s, to_s)
        elif not template_result:
            already_triggered = False

    return async_track_state_change(
        hass, template.extract_entities(variables), template_condition_listener
    )


track_template = threaded_listener_factory(async_track_template)


@callback
@bind_hass
def async_track_same_state(
    hass: HomeAssistant,
    period: timedelta,
    action: Callable[..., None],
    async_check_same_func: Callable[[str, State, State], bool],
    entity_ids: Union[str, Iterable[str]] = MATCH_ALL,
) -> CALLBACK_TYPE:
    """Track the state of entities for a period and run an action.

    If async_check_func is None it use the state of orig_value.
    Without entity_ids we track all state changes.
    """
    async_remove_state_for_cancel: Optional[CALLBACK_TYPE] = None
    async_remove_state_for_listener: Optional[CALLBACK_TYPE] = None

    @callback
    def clear_listener() -> None:
        """Clear all unsub listener."""
        nonlocal async_remove_state_for_cancel, async_remove_state_for_listener

        if async_remove_state_for_listener is not None:
            async_remove_state_for_listener()
            async_remove_state_for_listener = None
        if async_remove_state_for_cancel is not None:
            async_remove_state_for_cancel()
            async_remove_state_for_cancel = None

    @callback
    def state_for_listener(now: Any) -> None:
        """Fire on state changes after a delay and calls action."""
        nonlocal async_remove_state_for_listener
        async_remove_state_for_listener = None
        clear_listener()
        hass.async_run_job(action)

    @callback
    def state_for_cancel_listener(
        entity: str, from_state: State, to_state: State
    ) -> None:
        """Fire on changes and cancel for listener if changed."""
        if not async_check_same_func(entity, from_state, to_state):
            clear_listener()

    async_remove_state_for_listener = async_track_point_in_utc_time(
        hass, state_for_listener, dt_util.utcnow() + period
    )

    async_remove_state_for_cancel = async_track_state_change(
        hass, entity_ids, state_for_cancel_listener
    )

    return clear_listener


track_same_state = threaded_listener_factory(async_track_same_state)


@callback
@bind_hass
def async_track_point_in_time(
    hass: HomeAssistant, action: Callable[..., None], point_in_time: datetime
) -> CALLBACK_TYPE:
    """Add a listener that fires once after a specific point in time."""
    utc_point_in_time = dt_util.as_utc(point_in_time)

    @callback
    def utc_converter(utc_now: datetime) -> None:
        """Convert passed in UTC now to local now."""
        hass.async_run_job(action, dt_util.as_local(utc_now))

    return async_track_point_in_utc_time(hass, utc_converter, utc_point_in_time)


track_point_in_time = threaded_listener_factory(async_track_point_in_time)


@callback
@bind_hass
def async_track_point_in_utc_time(
    hass: HomeAssistant, action: Callable[..., None], point_in_time: datetime
) -> CALLBACK_TYPE:
    """Add a listener that fires once after a specific point in UTC time."""
    # Ensure point_in_time is UTC
    point_in_time = dt_util.as_utc(point_in_time)

    @callback
    def point_in_time_listener(event: Event) -> None:
        """Listen for matching time_changed events."""
        now = event.data[ATTR_NOW]

        if now < point_in_time or hasattr(point_in_time_listener, "run"):
            return

        # Set variable so that we will never run twice.
        # Because the event bus might have to wait till a thread comes
        # available to execute this listener it might occur that the
        # listener gets lined up twice to be executed. This will make
        # sure the second time it does nothing.
        setattr(point_in_time_listener, "run", True)
        async_unsub()

        hass.async_run_job(action, now)

    async_unsub = hass.bus.async_listen(EVENT_TIME_CHANGED, point_in_time_listener)

    return async_unsub


track_point_in_utc_time = threaded_listener_factory(async_track_point_in_utc_time)


@callback
@bind_hass
def async_call_later(
    hass: HomeAssistant, delay: float, action: Callable[..., None]
) -> CALLBACK_TYPE:
    """Add a listener that is called in <delay>."""
    return async_track_point_in_utc_time(
        hass, action, dt_util.utcnow() + timedelta(seconds=delay)
    )


call_later = threaded_listener_factory(async_call_later)


@callback
@bind_hass
def async_track_time_interval(
    hass: HomeAssistant, action: Callable[..., None], interval: timedelta
) -> CALLBACK_TYPE:
    """Add a listener that fires repetitively at every timedelta interval."""
    remove = None

    def next_interval() -> datetime:
        """Return the next interval."""
        return dt_util.utcnow() + interval

    @callback
    def interval_listener(now: datetime) -> None:
        """Handle elapsed intervals."""
        nonlocal remove
        remove = async_track_point_in_utc_time(hass, interval_listener, next_interval())
        hass.async_run_job(action, now)

    remove = async_track_point_in_utc_time(hass, interval_listener, next_interval())

    def remove_listener() -> None:
        """Remove interval listener."""
        remove()

    return remove_listener


track_time_interval = threaded_listener_factory(async_track_time_interval)


@attr.s
class SunListener:
    """Helper class to help listen to sun events."""

    hass = attr.ib(type=HomeAssistant)
    action: Callable[..., None] = attr.ib()
    event: str = attr.ib()
    offset: Optional[timedelta] = attr.ib()
    _unsub_sun: Optional[CALLBACK_TYPE] = attr.ib(default=None)
    _unsub_config: Optional[CALLBACK_TYPE] = attr.ib(default=None)

    @callback
    def async_attach(self) -> None:
        """Attach a sun listener."""
        assert self._unsub_config is None

        self._unsub_config = self.hass.bus.async_listen(
            EVENT_CORE_CONFIG_UPDATE, self._handle_config_event
        )

        self._listen_next_sun_event()

    @callback
    def async_detach(self) -> None:
        """Detach the sun listener."""
        assert self._unsub_sun is not None
        assert self._unsub_config is not None

        self._unsub_sun()
        self._unsub_sun = None
        self._unsub_config()
        self._unsub_config = None

    @callback
    def _listen_next_sun_event(self) -> None:
        """Set up the sun event listener."""
        assert self._unsub_sun is None

        self._unsub_sun = async_track_point_in_utc_time(
            self.hass,
            self._handle_sun_event,
            get_astral_event_next(self.hass, self.event, offset=self.offset),
        )

    @callback
    def _handle_sun_event(self, _now: Any) -> None:
        """Handle solar event."""
        self._unsub_sun = None
        self._listen_next_sun_event()
        self.hass.async_run_job(self.action)

    @callback
    def _handle_config_event(self, _event: Any) -> None:
        """Handle core config update."""
        assert self._unsub_sun is not None
        self._unsub_sun()
        self._unsub_sun = None
        self._listen_next_sun_event()


@callback
@bind_hass
def async_track_sunrise(
    hass: HomeAssistant, action: Callable[..., None], offset: Optional[timedelta] = None
) -> CALLBACK_TYPE:
    """Add a listener that will fire a specified offset from sunrise daily."""
    listener = SunListener(hass, action, SUN_EVENT_SUNRISE, offset)
    listener.async_attach()
    return listener.async_detach


track_sunrise = threaded_listener_factory(async_track_sunrise)


@callback
@bind_hass
def async_track_sunset(
    hass: HomeAssistant, action: Callable[..., None], offset: Optional[timedelta] = None
) -> CALLBACK_TYPE:
    """Add a listener that will fire a specified offset from sunset daily."""
    listener = SunListener(hass, action, SUN_EVENT_SUNSET, offset)
    listener.async_attach()
    return listener.async_detach


track_sunset = threaded_listener_factory(async_track_sunset)


@callback
@bind_hass
def async_track_utc_time_change(
    hass: HomeAssistant,
    action: Callable[..., None],
    hour: Optional[Any] = None,
    minute: Optional[Any] = None,
    second: Optional[Any] = None,
    local: bool = False,
) -> CALLBACK_TYPE:
    """Add a listener that will fire if time matches a pattern."""
    # We do not have to wrap the function with time pattern matching logic
    # if no pattern given
    if all(val is None for val in (hour, minute, second)):

        @callback
        def time_change_listener(event: Event) -> None:
            """Fire every time event that comes in."""
            hass.async_run_job(action, event.data[ATTR_NOW])

        return hass.bus.async_listen(EVENT_TIME_CHANGED, time_change_listener)

    matching_seconds = dt_util.parse_time_expression(second, 0, 59)
    matching_minutes = dt_util.parse_time_expression(minute, 0, 59)
    matching_hours = dt_util.parse_time_expression(hour, 0, 23)

    next_time = None

    def calculate_next(now: datetime) -> None:
        """Calculate and set the next time the trigger should fire."""
        nonlocal next_time

        localized_now = dt_util.as_local(now) if local else now
        next_time = dt_util.find_next_time_expression_time(
            localized_now, matching_seconds, matching_minutes, matching_hours
        )

    # Make sure rolling back the clock doesn't prevent the timer from
    # triggering.
    last_now: Optional[datetime] = None

    @callback
    def pattern_time_change_listener(event: Event) -> None:
        """Listen for matching time_changed events."""
        nonlocal next_time, last_now

        now = event.data[ATTR_NOW]

        if last_now is None or now < last_now:
            # Time rolled back or next time not yet calculated
            calculate_next(now)

        last_now = now

        if next_time <= now:
            hass.async_run_job(action, dt_util.as_local(now) if local else now)
            calculate_next(now + timedelta(seconds=1))

    # We can't use async_track_point_in_utc_time here because it would
    # break in the case that the system time abruptly jumps backwards.
    # Our custom last_now logic takes care of resolving that scenario.
    return hass.bus.async_listen(EVENT_TIME_CHANGED, pattern_time_change_listener)


track_utc_time_change = threaded_listener_factory(async_track_utc_time_change)


@callback
@bind_hass
def async_track_time_change(
    hass: HomeAssistant,
    action: Callable[..., None],
    hour: Optional[Any] = None,
    minute: Optional[Any] = None,
    second: Optional[Any] = None,
) -> CALLBACK_TYPE:
    """Add a listener that will fire if UTC time matches a pattern."""
    return async_track_utc_time_change(hass, action, hour, minute, second, local=True)


track_time_change = threaded_listener_factory(async_track_time_change)


def _process_state_match(
    parameter: Union[None, str, Iterable[str]]
) -> Callable[[str], bool]:
    """Convert parameter to function that matches input against parameter."""
    if parameter is None or parameter == MATCH_ALL:
        return lambda _: True

    if isinstance(parameter, str) or not hasattr(parameter, "__iter__"):
        return lambda state: state == parameter

    parameter_tuple = tuple(parameter)
    return lambda state: state in parameter_tuple
