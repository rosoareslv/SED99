"""Helpers for config validation using voluptuous."""
from datetime import (
    date as date_sys,
    datetime as datetime_sys,
    time as time_sys,
    timedelta,
)
from enum import Enum
import inspect
import logging
from numbers import Number
import os
import re
from socket import _GLOBAL_DEFAULT_TIMEOUT  # type: ignore # private, not in typeshed
from typing import (
    Any,
    Callable,
    Dict,
    Hashable,
    List,
    Optional,
    Pattern,
    Type,
    TypeVar,
    Union,
    cast,
)
from urllib.parse import urlparse
from uuid import UUID

from pkg_resources import parse_version
import voluptuous as vol
import voluptuous_serialize

from homeassistant.const import (
    ATTR_AREA_ID,
    ATTR_ENTITY_ID,
    CONF_ABOVE,
    CONF_ALIAS,
    CONF_BELOW,
    CONF_CONDITION,
    CONF_DEVICE_ID,
    CONF_DOMAIN,
    CONF_ENTITY_ID,
    CONF_ENTITY_NAMESPACE,
    CONF_FOR,
    CONF_PLATFORM,
    CONF_SCAN_INTERVAL,
    CONF_STATE,
    CONF_TIMEOUT,
    CONF_UNIT_SYSTEM_IMPERIAL,
    CONF_UNIT_SYSTEM_METRIC,
    CONF_VALUE_TEMPLATE,
    ENTITY_MATCH_ALL,
    SUN_EVENT_SUNRISE,
    SUN_EVENT_SUNSET,
    TEMP_CELSIUS,
    TEMP_FAHRENHEIT,
    WEEKDAYS,
    __version__,
)
from homeassistant.core import split_entity_id, valid_entity_id
from homeassistant.exceptions import TemplateError
from homeassistant.helpers import template as template_helper
from homeassistant.helpers.logging import KeywordStyleAdapter
from homeassistant.util import slugify as util_slugify
import homeassistant.util.dt as dt_util

# pylint: disable=invalid-name

TIME_PERIOD_ERROR = "offset {} should be format 'HH:MM' or 'HH:MM:SS'"


# Home Assistant types
byte = vol.All(vol.Coerce(int), vol.Range(min=0, max=255))
small_float = vol.All(vol.Coerce(float), vol.Range(min=0, max=1))
positive_int = vol.All(vol.Coerce(int), vol.Range(min=0))
latitude = vol.All(
    vol.Coerce(float), vol.Range(min=-90, max=90), msg="invalid latitude"
)
longitude = vol.All(
    vol.Coerce(float), vol.Range(min=-180, max=180), msg="invalid longitude"
)
gps = vol.ExactSequence([latitude, longitude])
sun_event = vol.All(vol.Lower, vol.Any(SUN_EVENT_SUNSET, SUN_EVENT_SUNRISE))
port = vol.All(vol.Coerce(int), vol.Range(min=1, max=65535))

# typing typevar
T = TypeVar("T")


# Adapted from:
# https://github.com/alecthomas/voluptuous/issues/115#issuecomment-144464666
def has_at_least_one_key(*keys: str) -> Callable:
    """Validate that at least one key exists."""

    def validate(obj: Dict) -> Dict:
        """Test keys exist in dict."""
        if not isinstance(obj, dict):
            raise vol.Invalid("expected dictionary")

        for k in obj.keys():
            if k in keys:
                return obj
        raise vol.Invalid("must contain at least one of {}.".format(", ".join(keys)))

    return validate


def has_at_most_one_key(*keys: str) -> Callable[[Dict], Dict]:
    """Validate that zero keys exist or one key exists."""

    def validate(obj: Dict) -> Dict:
        """Test zero keys exist or one key exists in dict."""
        if not isinstance(obj, dict):
            raise vol.Invalid("expected dictionary")

        if len(set(keys) & set(obj)) > 1:
            raise vol.Invalid("must contain at most one of {}.".format(", ".join(keys)))
        return obj

    return validate


def boolean(value: Any) -> bool:
    """Validate and coerce a boolean value."""
    if isinstance(value, bool):
        return value
    if isinstance(value, str):
        value = value.lower().strip()
        if value in ("1", "true", "yes", "on", "enable"):
            return True
        if value in ("0", "false", "no", "off", "disable"):
            return False
    elif isinstance(value, Number):
        # type ignore: https://github.com/python/mypy/issues/3186
        return value != 0  # type: ignore
    raise vol.Invalid("invalid boolean value {}".format(value))


def isdevice(value: Any) -> str:
    """Validate that value is a real device."""
    try:
        os.stat(value)
        return str(value)
    except OSError:
        raise vol.Invalid("No device at {} found".format(value))


def matches_regex(regex: str) -> Callable[[Any], str]:
    """Validate that the value is a string that matches a regex."""
    compiled = re.compile(regex)

    def validator(value: Any) -> str:
        """Validate that value matches the given regex."""
        if not isinstance(value, str):
            raise vol.Invalid("not a string value: {}".format(value))

        if not compiled.match(value):
            raise vol.Invalid(
                "value {} does not match regular expression {}".format(
                    value, compiled.pattern
                )
            )

        return value

    return validator


def is_regex(value: Any) -> Pattern[Any]:
    """Validate that a string is a valid regular expression."""
    try:
        r = re.compile(value)
        return r
    except TypeError:
        raise vol.Invalid(
            "value {} is of the wrong type for a regular expression".format(value)
        )
    except re.error:
        raise vol.Invalid("value {} is not a valid regular expression".format(value))


def isfile(value: Any) -> str:
    """Validate that the value is an existing file."""
    if value is None:
        raise vol.Invalid("None is not file")
    file_in = os.path.expanduser(str(value))

    if not os.path.isfile(file_in):
        raise vol.Invalid("not a file")
    if not os.access(file_in, os.R_OK):
        raise vol.Invalid("file not readable")
    return file_in


def isdir(value: Any) -> str:
    """Validate that the value is an existing dir."""
    if value is None:
        raise vol.Invalid("not a directory")
    dir_in = os.path.expanduser(str(value))

    if not os.path.isdir(dir_in):
        raise vol.Invalid("not a directory")
    if not os.access(dir_in, os.R_OK):
        raise vol.Invalid("directory not readable")
    return dir_in


def ensure_list(value: Union[T, List[T], None]) -> List[T]:
    """Wrap value in list if it is not one."""
    if value is None:
        return []
    return value if isinstance(value, list) else [value]


def entity_id(value: Any) -> str:
    """Validate Entity ID."""
    str_value = string(value).lower()
    if valid_entity_id(str_value):
        return str_value

    raise vol.Invalid("Entity ID {} is an invalid entity id".format(value))


def entity_ids(value: Union[str, List]) -> List[str]:
    """Validate Entity IDs."""
    if value is None:
        raise vol.Invalid("Entity IDs can not be None")
    if isinstance(value, str):
        value = [ent_id.strip() for ent_id in value.split(",")]

    return [entity_id(ent_id) for ent_id in value]


comp_entity_ids = vol.Any(vol.All(vol.Lower, ENTITY_MATCH_ALL), entity_ids)


def entity_domain(domain: str) -> Callable[[Any], str]:
    """Validate that entity belong to domain."""

    def validate(value: Any) -> str:
        """Test if entity domain is domain."""
        ent_domain = entities_domain(domain)
        return ent_domain(value)[0]

    return validate


def entities_domain(domain: str) -> Callable[[Union[str, List]], List[str]]:
    """Validate that entities belong to domain."""

    def validate(values: Union[str, List]) -> List[str]:
        """Test if entity domain is domain."""
        values = entity_ids(values)
        for ent_id in values:
            if split_entity_id(ent_id)[0] != domain:
                raise vol.Invalid(
                    "Entity ID '{}' does not belong to domain '{}'".format(
                        ent_id, domain
                    )
                )
        return values

    return validate


def enum(enumClass: Type[Enum]) -> vol.All:
    """Create validator for specified enum."""
    return vol.All(vol.In(enumClass.__members__), enumClass.__getitem__)


def icon(value: Any) -> str:
    """Validate icon."""
    str_value = str(value)

    if ":" in str_value:
        return str_value

    raise vol.Invalid('Icons should be specified in the form "prefix:name"')


time_period_dict = vol.All(
    dict,
    vol.Schema(
        {
            "days": vol.Coerce(int),
            "hours": vol.Coerce(int),
            "minutes": vol.Coerce(int),
            "seconds": vol.Coerce(int),
            "milliseconds": vol.Coerce(int),
        }
    ),
    has_at_least_one_key("days", "hours", "minutes", "seconds", "milliseconds"),
    lambda value: timedelta(**value),
)


def time(value: Any) -> time_sys:
    """Validate and transform a time."""
    if isinstance(value, time_sys):
        return value

    try:
        time_val = dt_util.parse_time(value)
    except TypeError:
        raise vol.Invalid("Not a parseable type")

    if time_val is None:
        raise vol.Invalid("Invalid time specified: {}".format(value))

    return time_val


def date(value: Any) -> date_sys:
    """Validate and transform a date."""
    if isinstance(value, date_sys):
        return value

    try:
        date_val = dt_util.parse_date(value)
    except TypeError:
        raise vol.Invalid("Not a parseable type")

    if date_val is None:
        raise vol.Invalid("Could not parse date")

    return date_val


def time_period_str(value: str) -> timedelta:
    """Validate and transform time offset."""
    if isinstance(value, int):
        raise vol.Invalid("Make sure you wrap time values in quotes")
    if not isinstance(value, str):
        raise vol.Invalid(TIME_PERIOD_ERROR.format(value))

    negative_offset = False
    if value.startswith("-"):
        negative_offset = True
        value = value[1:]
    elif value.startswith("+"):
        value = value[1:]

    try:
        parsed = [int(x) for x in value.split(":")]
    except ValueError:
        raise vol.Invalid(TIME_PERIOD_ERROR.format(value))

    if len(parsed) == 2:
        hour, minute = parsed
        second = 0
    elif len(parsed) == 3:
        hour, minute, second = parsed
    else:
        raise vol.Invalid(TIME_PERIOD_ERROR.format(value))

    offset = timedelta(hours=hour, minutes=minute, seconds=second)

    if negative_offset:
        offset *= -1

    return offset


def time_period_seconds(value: Union[int, str]) -> timedelta:
    """Validate and transform seconds to a time offset."""
    try:
        return timedelta(seconds=int(value))
    except (ValueError, TypeError):
        raise vol.Invalid("Expected seconds, got {}".format(value))


time_period = vol.Any(time_period_str, time_period_seconds, timedelta, time_period_dict)


def match_all(value: T) -> T:
    """Validate that matches all values."""
    return value


def positive_timedelta(value: timedelta) -> timedelta:
    """Validate timedelta is positive."""
    if value < timedelta(0):
        raise vol.Invalid("Time period should be positive")
    return value


positive_time_period_dict = vol.All(time_period_dict, positive_timedelta)


def remove_falsy(value: List[T]) -> List[T]:
    """Remove falsy values from a list."""
    return [v for v in value if v]


def service(value: Any) -> str:
    """Validate service."""
    # Services use same format as entities so we can use same helper.
    str_value = string(value).lower()
    if valid_entity_id(str_value):
        return str_value
    raise vol.Invalid("Service {} does not match format <domain>.<name>".format(value))


def schema_with_slug_keys(value_schema: Union[T, Callable]) -> Callable:
    """Ensure dicts have slugs as keys.

    Replacement of vol.Schema({cv.slug: value_schema}) to prevent misleading
    "Extra keys" errors from voluptuous.
    """
    schema = vol.Schema({str: value_schema})

    def verify(value: Dict) -> Dict:
        """Validate all keys are slugs and then the value_schema."""
        if not isinstance(value, dict):
            raise vol.Invalid("expected dictionary")

        for key in value.keys():
            slug(key)

        return cast(Dict, schema(value))

    return verify


def slug(value: Any) -> str:
    """Validate value is a valid slug."""
    if value is None:
        raise vol.Invalid("Slug should not be None")
    str_value = str(value)
    slg = util_slugify(str_value)
    if str_value == slg:
        return str_value
    raise vol.Invalid("invalid slug {} (try {})".format(value, slg))


def slugify(value: Any) -> str:
    """Coerce a value to a slug."""
    if value is None:
        raise vol.Invalid("Slug should not be None")
    slg = util_slugify(str(value))
    if slg:
        return slg
    raise vol.Invalid("Unable to slugify {}".format(value))


def string(value: Any) -> str:
    """Coerce value to string, except for None."""
    if value is None:
        raise vol.Invalid("string value is None")
    if isinstance(value, (list, dict)):
        raise vol.Invalid("value should be a string")

    return str(value)


def temperature_unit(value: Any) -> str:
    """Validate and transform temperature unit."""
    value = str(value).upper()
    if value == "C":
        return TEMP_CELSIUS
    if value == "F":
        return TEMP_FAHRENHEIT
    raise vol.Invalid("invalid temperature unit (expected C or F)")


unit_system = vol.All(
    vol.Lower, vol.Any(CONF_UNIT_SYSTEM_METRIC, CONF_UNIT_SYSTEM_IMPERIAL)
)


def template(value: Optional[Any]) -> template_helper.Template:
    """Validate a jinja2 template."""

    if value is None:
        raise vol.Invalid("template value is None")
    if isinstance(value, (list, dict, template_helper.Template)):
        raise vol.Invalid("template value should be a string")

    template_value = template_helper.Template(str(value))  # type: ignore

    try:
        template_value.ensure_valid()
        return cast(template_helper.Template, template_value)
    except TemplateError as ex:
        raise vol.Invalid("invalid template ({})".format(ex))


def template_complex(value: Any) -> Any:
    """Validate a complex jinja2 template."""
    if isinstance(value, list):
        return_list = value.copy()
        for idx, element in enumerate(return_list):
            return_list[idx] = template_complex(element)
        return return_list
    if isinstance(value, dict):
        return_dict = value.copy()
        for key, element in return_dict.items():
            return_dict[key] = template_complex(element)
        return return_dict
    if isinstance(value, str):
        return template(value)
    return value


def datetime(value: Any) -> datetime_sys:
    """Validate datetime."""
    if isinstance(value, datetime_sys):
        return value

    try:
        date_val = dt_util.parse_datetime(value)
    except TypeError:
        date_val = None

    if date_val is None:
        raise vol.Invalid("Invalid datetime specified: {}".format(value))

    return date_val


def time_zone(value: str) -> str:
    """Validate timezone."""
    if dt_util.get_time_zone(value) is not None:
        return value
    raise vol.Invalid(
        "Invalid time zone passed in. Valid options can be found here: "
        "http://en.wikipedia.org/wiki/List_of_tz_database_time_zones"
    )


weekdays = vol.All(ensure_list, [vol.In(WEEKDAYS)])


def socket_timeout(value: Optional[Any]) -> object:
    """Validate timeout float > 0.0.

    None coerced to socket._GLOBAL_DEFAULT_TIMEOUT bare object.
    """
    if value is None:
        return _GLOBAL_DEFAULT_TIMEOUT
    try:
        float_value = float(value)
        if float_value > 0.0:
            return float_value
        raise vol.Invalid("Invalid socket timeout value." " float > 0.0 required.")
    except Exception as _:
        raise vol.Invalid("Invalid socket timeout: {err}".format(err=_))


# pylint: disable=no-value-for-parameter
def url(value: Any) -> str:
    """Validate an URL."""
    url_in = str(value)

    if urlparse(url_in).scheme in ["http", "https"]:
        return cast(str, vol.Schema(vol.Url())(url_in))

    raise vol.Invalid("invalid url")


def x10_address(value: str) -> str:
    """Validate an x10 address."""
    regex = re.compile(r"([A-Pa-p]{1})(?:[2-9]|1[0-6]?)$")
    if not regex.match(value):
        raise vol.Invalid("Invalid X10 Address")
    return str(value).lower()


def uuid4_hex(value: Any) -> str:
    """Validate a v4 UUID in hex format."""
    try:
        result = UUID(value, version=4)
    except (ValueError, AttributeError, TypeError) as error:
        raise vol.Invalid("Invalid Version4 UUID", error_message=str(error))

    if result.hex != value.lower():
        # UUID() will create a uuid4 if input is invalid
        raise vol.Invalid("Invalid Version4 UUID")

    return result.hex


def ensure_list_csv(value: Any) -> List:
    """Ensure that input is a list or make one from comma-separated string."""
    if isinstance(value, str):
        return [member.strip() for member in value.split(",")]
    return ensure_list(value)


def deprecated(
    key: str,
    replacement_key: Optional[str] = None,
    invalidation_version: Optional[str] = None,
    default: Optional[Any] = None,
) -> Callable[[Dict], Dict]:
    """
    Log key as deprecated and provide a replacement (if exists).

    Expected behavior:
        - Outputs the appropriate deprecation warning if key is detected
        - Processes schema moving the value from key to replacement_key
        - Processes schema changing nothing if only replacement_key provided
        - No warning if only replacement_key provided
        - No warning if neither key nor replacement_key are provided
            - Adds replacement_key with default value in this case
        - Once the invalidation_version is crossed, raises vol.Invalid if key
        is detected
    """
    module = inspect.getmodule(inspect.stack()[1][0])
    if module is not None:
        module_name = module.__name__
    else:
        # If Python is unable to access the sources files, the call stack frame
        # will be missing information, so let's guard.
        # https://github.com/home-assistant/home-assistant/issues/24982
        module_name = __name__

    if replacement_key and invalidation_version:
        warning = (
            "The '{key}' option (with value '{value}') is"
            " deprecated, please replace it with '{replacement_key}'."
            " This option will become invalid in version"
            " {invalidation_version}"
        )
    elif replacement_key:
        warning = (
            "The '{key}' option (with value '{value}') is"
            " deprecated, please replace it with '{replacement_key}'"
        )
    elif invalidation_version:
        warning = (
            "The '{key}' option (with value '{value}') is"
            " deprecated, please remove it from your configuration."
            " This option will become invalid in version"
            " {invalidation_version}"
        )
    else:
        warning = (
            "The '{key}' option (with value '{value}') is"
            " deprecated, please remove it from your configuration"
        )

    def check_for_invalid_version(value: Optional[Any]) -> None:
        """Raise error if current version has reached invalidation."""
        if not invalidation_version:
            return

        if parse_version(__version__) >= parse_version(invalidation_version):
            raise vol.Invalid(
                warning.format(
                    key=key,
                    value=value,
                    replacement_key=replacement_key,
                    invalidation_version=invalidation_version,
                )
            )

    def validator(config: Dict) -> Dict:
        """Check if key is in config and log warning."""
        if key in config:
            value = config[key]
            check_for_invalid_version(value)
            KeywordStyleAdapter(logging.getLogger(module_name)).warning(
                warning,
                key=key,
                value=value,
                replacement_key=replacement_key,
                invalidation_version=invalidation_version,
            )
            if replacement_key:
                config.pop(key)
        else:
            value = default
        keys = [key]
        if replacement_key:
            keys.append(replacement_key)
            if value is not None and (
                replacement_key not in config or default == config.get(replacement_key)
            ):
                config[replacement_key] = value

        return has_at_most_one_key(*keys)(config)

    return validator


# Validator helpers


def key_dependency(
    key: Hashable, dependency: Hashable
) -> Callable[[Dict[Hashable, Any]], Dict[Hashable, Any]]:
    """Validate that all dependencies exist for key."""

    def validator(value: Dict[Hashable, Any]) -> Dict[Hashable, Any]:
        """Test dependencies."""
        if not isinstance(value, dict):
            raise vol.Invalid("key dependencies require a dict")
        if key in value and dependency not in value:
            raise vol.Invalid(
                'dependency violation - key "{}" requires '
                'key "{}" to exist'.format(key, dependency)
            )

        return value

    return validator


def custom_serializer(schema: Any) -> Any:
    """Serialize additional types for voluptuous_serialize."""
    if schema is positive_time_period_dict:
        return {"type": "positive_time_period_dict"}

    return voluptuous_serialize.UNSUPPORTED


# Schemas
PLATFORM_SCHEMA = vol.Schema(
    {
        vol.Required(CONF_PLATFORM): string,
        vol.Optional(CONF_ENTITY_NAMESPACE): string,
        vol.Optional(CONF_SCAN_INTERVAL): time_period,
    }
)

PLATFORM_SCHEMA_BASE = PLATFORM_SCHEMA.extend({}, extra=vol.ALLOW_EXTRA)


def make_entity_service_schema(
    schema: dict, *, extra: int = vol.PREVENT_EXTRA
) -> vol.All:
    """Create an entity service schema."""
    return vol.All(
        vol.Schema(
            {
                **schema,
                vol.Optional(ATTR_ENTITY_ID): comp_entity_ids,
                vol.Optional(ATTR_AREA_ID): vol.All(ensure_list, [str]),
            },
            extra=extra,
        ),
        has_at_least_one_key(ATTR_ENTITY_ID, ATTR_AREA_ID),
    )


EVENT_SCHEMA = vol.Schema(
    {
        vol.Optional(CONF_ALIAS): string,
        vol.Required("event"): string,
        vol.Optional("event_data"): dict,
        vol.Optional("event_data_template"): {match_all: template_complex},
    }
)

SERVICE_SCHEMA = vol.All(
    vol.Schema(
        {
            vol.Optional(CONF_ALIAS): string,
            vol.Exclusive("service", "service name"): service,
            vol.Exclusive("service_template", "service name"): template,
            vol.Optional("data"): dict,
            vol.Optional("data_template"): {match_all: template_complex},
            vol.Optional(CONF_ENTITY_ID): comp_entity_ids,
        }
    ),
    has_at_least_one_key("service", "service_template"),
)

NUMERIC_STATE_CONDITION_SCHEMA = vol.All(
    vol.Schema(
        {
            vol.Required(CONF_CONDITION): "numeric_state",
            vol.Required(CONF_ENTITY_ID): entity_id,
            CONF_BELOW: vol.Coerce(float),
            CONF_ABOVE: vol.Coerce(float),
            vol.Optional(CONF_VALUE_TEMPLATE): template,
        }
    ),
    has_at_least_one_key(CONF_BELOW, CONF_ABOVE),
)

STATE_CONDITION_SCHEMA = vol.All(
    vol.Schema(
        {
            vol.Required(CONF_CONDITION): "state",
            vol.Required(CONF_ENTITY_ID): entity_id,
            vol.Required(CONF_STATE): str,
            vol.Optional(CONF_FOR): vol.All(time_period, positive_timedelta),
            # To support use_trigger_value in automation
            # Deprecated 2016/04/25
            vol.Optional("from"): str,
        }
    ),
    key_dependency("for", "state"),
)

SUN_CONDITION_SCHEMA = vol.All(
    vol.Schema(
        {
            vol.Required(CONF_CONDITION): "sun",
            vol.Optional("before"): sun_event,
            vol.Optional("before_offset"): time_period,
            vol.Optional("after"): vol.All(
                vol.Lower, vol.Any(SUN_EVENT_SUNSET, SUN_EVENT_SUNRISE)
            ),
            vol.Optional("after_offset"): time_period,
        }
    ),
    has_at_least_one_key("before", "after"),
)

TEMPLATE_CONDITION_SCHEMA = vol.Schema(
    {
        vol.Required(CONF_CONDITION): "template",
        vol.Required(CONF_VALUE_TEMPLATE): template,
    }
)

TIME_CONDITION_SCHEMA = vol.All(
    vol.Schema(
        {
            vol.Required(CONF_CONDITION): "time",
            "before": time,
            "after": time,
            "weekday": weekdays,
        }
    ),
    has_at_least_one_key("before", "after", "weekday"),
)

ZONE_CONDITION_SCHEMA = vol.Schema(
    {
        vol.Required(CONF_CONDITION): "zone",
        vol.Required(CONF_ENTITY_ID): entity_id,
        "zone": entity_id,
        # To support use_trigger_value in automation
        # Deprecated 2016/04/25
        vol.Optional("event"): vol.Any("enter", "leave"),
    }
)

AND_CONDITION_SCHEMA = vol.Schema(
    {
        vol.Required(CONF_CONDITION): "and",
        vol.Required("conditions"): vol.All(
            ensure_list,
            # pylint: disable=unnecessary-lambda
            [lambda value: CONDITION_SCHEMA(value)],
        ),
    }
)

OR_CONDITION_SCHEMA = vol.Schema(
    {
        vol.Required(CONF_CONDITION): "or",
        vol.Required("conditions"): vol.All(
            ensure_list,
            # pylint: disable=unnecessary-lambda
            [lambda value: CONDITION_SCHEMA(value)],
        ),
    }
)

DEVICE_CONDITION_BASE_SCHEMA = vol.Schema(
    {
        vol.Required(CONF_CONDITION): "device",
        vol.Required(CONF_DEVICE_ID): str,
        vol.Required(CONF_DOMAIN): str,
    }
)

DEVICE_CONDITION_SCHEMA = DEVICE_CONDITION_BASE_SCHEMA.extend({}, extra=vol.ALLOW_EXTRA)

CONDITION_SCHEMA: vol.Schema = vol.Any(
    NUMERIC_STATE_CONDITION_SCHEMA,
    STATE_CONDITION_SCHEMA,
    SUN_CONDITION_SCHEMA,
    TEMPLATE_CONDITION_SCHEMA,
    TIME_CONDITION_SCHEMA,
    ZONE_CONDITION_SCHEMA,
    AND_CONDITION_SCHEMA,
    OR_CONDITION_SCHEMA,
    DEVICE_CONDITION_SCHEMA,
)

_SCRIPT_DELAY_SCHEMA = vol.Schema(
    {
        vol.Optional(CONF_ALIAS): string,
        vol.Required("delay"): vol.Any(
            vol.All(time_period, positive_timedelta), template, template_complex
        ),
    }
)

_SCRIPT_WAIT_TEMPLATE_SCHEMA = vol.Schema(
    {
        vol.Optional(CONF_ALIAS): string,
        vol.Required("wait_template"): template,
        vol.Optional(CONF_TIMEOUT): vol.All(time_period, positive_timedelta),
        vol.Optional("continue_on_timeout"): boolean,
    }
)

DEVICE_ACTION_BASE_SCHEMA = vol.Schema(
    {vol.Required(CONF_DEVICE_ID): string, vol.Required(CONF_DOMAIN): str}
)

DEVICE_ACTION_SCHEMA = DEVICE_ACTION_BASE_SCHEMA.extend({}, extra=vol.ALLOW_EXTRA)

_SCRIPT_SCENE_SCHEMA = vol.Schema({vol.Required("scene"): entity_domain("scene")})

SCRIPT_SCHEMA = vol.All(
    ensure_list,
    [
        vol.Any(
            SERVICE_SCHEMA,
            _SCRIPT_DELAY_SCHEMA,
            _SCRIPT_WAIT_TEMPLATE_SCHEMA,
            EVENT_SCHEMA,
            CONDITION_SCHEMA,
            DEVICE_ACTION_SCHEMA,
            _SCRIPT_SCENE_SCHEMA,
        )
    ],
)
