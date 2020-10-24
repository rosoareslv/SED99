"""Service calling related helpers."""
import asyncio
import logging
from os import path

import voluptuous as vol

from homeassistant.auth.permissions.const import POLICY_CONTROL
from homeassistant.const import ATTR_ENTITY_ID, ENTITY_MATCH_ALL
import homeassistant.core as ha
from homeassistant.exceptions import TemplateError, Unauthorized, UnknownUser
from homeassistant.helpers import template
from homeassistant.loader import get_component, bind_hass
from homeassistant.util.yaml import load_yaml
import homeassistant.helpers.config_validation as cv
from homeassistant.util.async_ import run_coroutine_threadsafe

CONF_SERVICE = 'service'
CONF_SERVICE_TEMPLATE = 'service_template'
CONF_SERVICE_ENTITY_ID = 'entity_id'
CONF_SERVICE_DATA = 'data'
CONF_SERVICE_DATA_TEMPLATE = 'data_template'

_LOGGER = logging.getLogger(__name__)

SERVICE_DESCRIPTION_CACHE = 'service_description_cache'


@bind_hass
def call_from_config(hass, config, blocking=False, variables=None,
                     validate_config=True):
    """Call a service based on a config hash."""
    run_coroutine_threadsafe(
        async_call_from_config(hass, config, blocking, variables,
                               validate_config), hass.loop).result()


@bind_hass
async def async_call_from_config(hass, config, blocking=False, variables=None,
                                 validate_config=True, context=None):
    """Call a service based on a config hash."""
    if validate_config:
        try:
            config = cv.SERVICE_SCHEMA(config)
        except vol.Invalid as ex:
            _LOGGER.error("Invalid config for calling service: %s", ex)
            return

    if CONF_SERVICE in config:
        domain_service = config[CONF_SERVICE]
    else:
        try:
            config[CONF_SERVICE_TEMPLATE].hass = hass
            domain_service = config[CONF_SERVICE_TEMPLATE].async_render(
                variables)
            domain_service = cv.service(domain_service)
        except TemplateError as ex:
            if blocking:
                raise
            _LOGGER.error('Error rendering service name template: %s', ex)
            return
        except vol.Invalid:
            if blocking:
                raise
            _LOGGER.error('Template rendered invalid service: %s',
                          domain_service)
            return

    domain, service_name = domain_service.split('.', 1)
    service_data = dict(config.get(CONF_SERVICE_DATA, {}))

    if CONF_SERVICE_DATA_TEMPLATE in config:
        try:
            template.attach(hass, config[CONF_SERVICE_DATA_TEMPLATE])
            service_data.update(template.render_complex(
                config[CONF_SERVICE_DATA_TEMPLATE], variables))
        except TemplateError as ex:
            _LOGGER.error('Error rendering data template: %s', ex)
            return

    if CONF_SERVICE_ENTITY_ID in config:
        service_data[ATTR_ENTITY_ID] = config[CONF_SERVICE_ENTITY_ID]

    await hass.services.async_call(
        domain, service_name, service_data, blocking=blocking, context=context)


@bind_hass
def extract_entity_ids(hass, service_call, expand_group=True):
    """Extract a list of entity ids from a service call.

    Will convert group entity ids to the entity ids it represents.

    Async friendly.
    """
    if not (service_call.data and ATTR_ENTITY_ID in service_call.data):
        return []

    group = hass.components.group

    # Entity ID attr can be a list or a string
    service_ent_id = service_call.data[ATTR_ENTITY_ID]

    if expand_group:

        if isinstance(service_ent_id, str):
            return group.expand_entity_ids([service_ent_id])

        return [ent_id for ent_id in
                group.expand_entity_ids(service_ent_id)]

    if isinstance(service_ent_id, str):
        return [service_ent_id]

    return service_ent_id


@bind_hass
async def async_get_all_descriptions(hass):
    """Return descriptions (i.e. user documentation) for all service calls."""
    if SERVICE_DESCRIPTION_CACHE not in hass.data:
        hass.data[SERVICE_DESCRIPTION_CACHE] = {}
    description_cache = hass.data[SERVICE_DESCRIPTION_CACHE]

    format_cache_key = '{}.{}'.format

    def domain_yaml_file(domain):
        """Return the services.yaml location for a domain."""
        if domain == ha.DOMAIN:
            from homeassistant import components
            component_path = path.dirname(components.__file__)
        else:
            component_path = path.dirname(get_component(hass, domain).__file__)
        return path.join(component_path, 'services.yaml')

    def load_services_files(yaml_files):
        """Load and parse services.yaml files."""
        loaded = {}
        for yaml_file in yaml_files:
            try:
                loaded[yaml_file] = load_yaml(yaml_file)
            except FileNotFoundError:
                loaded[yaml_file] = {}

        return loaded

    services = hass.services.async_services()

    # Load missing files
    missing = set()
    for domain in services:
        for service in services[domain]:
            if format_cache_key(domain, service) not in description_cache:
                missing.add(domain_yaml_file(domain))
                break

    if missing:
        loaded = await hass.async_add_job(load_services_files, missing)

    # Build response
    catch_all_yaml_file = domain_yaml_file(ha.DOMAIN)
    descriptions = {}
    for domain in services:
        descriptions[domain] = {}
        yaml_file = domain_yaml_file(domain)

        for service in services[domain]:
            cache_key = format_cache_key(domain, service)
            description = description_cache.get(cache_key)

            # Cache missing descriptions
            if description is None:
                if yaml_file == catch_all_yaml_file:
                    yaml_services = loaded[yaml_file].get(domain, {})
                else:
                    yaml_services = loaded[yaml_file]
                yaml_description = yaml_services.get(service, {})

                description = description_cache[cache_key] = {
                    'description': yaml_description.get('description', ''),
                    'fields': yaml_description.get('fields', {})
                }

            descriptions[domain][service] = description

    return descriptions


@bind_hass
async def entity_service_call(hass, platforms, func, call):
    """Handle an entity service call.

    Calls all platforms simultaneously.
    """
    if call.context.user_id:
        user = await hass.auth.async_get_user(call.context.user_id)
        if user is None:
            raise UnknownUser(context=call.context)
        entity_perms = user.permissions.check_entity
    else:
        entity_perms = None

    # Are we trying to target all entities
    if ATTR_ENTITY_ID in call.data:
        target_all_entities = call.data[ATTR_ENTITY_ID] == ENTITY_MATCH_ALL
    else:
        _LOGGER.warning('Not passing an entity ID to a service to target all '
                        'entities is deprecated. Use instead: entity_id: "*"')
        target_all_entities = True

    if not target_all_entities:
        # A set of entities we're trying to target.
        entity_ids = set(
            extract_entity_ids(hass, call, True))

    # If the service function is a string, we'll pass it the service call data
    if isinstance(func, str):
        data = {key: val for key, val in call.data.items()
                if key != ATTR_ENTITY_ID}
    # If the service function is not a string, we pass the service call
    else:
        data = call

    # Check the permissions

    # A list with for each platform in platforms a list of entities to call
    # the service on.
    platforms_entities = []

    if entity_perms is None:
        for platform in platforms:
            if target_all_entities:
                platforms_entities.append(list(platform.entities.values()))
            else:
                platforms_entities.append([
                    entity for entity in platform.entities.values()
                    if entity.entity_id in entity_ids
                ])

    elif target_all_entities:
        # If we target all entities, we will select all entities the user
        # is allowed to control.
        for platform in platforms:
            platforms_entities.append([
                entity for entity in platform.entities.values()
                if entity_perms(entity.entity_id, POLICY_CONTROL)])

    else:
        for platform in platforms:
            platform_entities = []
            for entity in platform.entities.values():
                if entity.entity_id not in entity_ids:
                    continue

                if not entity_perms(entity.entity_id, POLICY_CONTROL):
                    raise Unauthorized(
                        context=call.context,
                        entity_id=entity.entity_id,
                        permission=POLICY_CONTROL
                    )

                platform_entities.append(entity)

            platforms_entities.append(platform_entities)

    tasks = [
        _handle_service_platform_call(func, data, entities, call.context)
        for platform, entities in zip(platforms, platforms_entities)
    ]

    if tasks:
        await asyncio.wait(tasks)


async def _handle_service_platform_call(func, data, entities, context):
    """Handle a function call."""
    tasks = []

    for entity in entities:
        if not entity.available:
            continue

        entity.async_set_context(context)

        if isinstance(func, str):
            await getattr(entity, func)(**data)
        else:
            await func(entity, data)

        if entity.should_poll:
            tasks.append(entity.async_update_ha_state(True))

    if tasks:
        await asyncio.wait(tasks)
