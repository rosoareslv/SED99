"""Helper to check the configuration file."""
from collections import OrderedDict
from typing import List, NamedTuple, Optional

import attr
import voluptuous as vol

from homeassistant import loader
from homeassistant.config import (
    CONF_CORE,
    CONF_PACKAGES,
    CORE_CONFIG_SCHEMA,
    _format_config_error,
    config_per_platform,
    extract_domain_configs,
    find_config_file,
    load_yaml_config_file,
    merge_packages_config,
)
from homeassistant.core import HomeAssistant
from homeassistant.exceptions import HomeAssistantError
from homeassistant.helpers.typing import ConfigType
from homeassistant.requirements import (
    RequirementsNotFound,
    async_get_integration_with_requirements,
)
import homeassistant.util.yaml.loader as yaml_loader


class CheckConfigError(NamedTuple):
    """Configuration check error."""

    message: str
    domain: Optional[str]
    config: Optional[ConfigType]


@attr.s
class HomeAssistantConfig(OrderedDict):
    """Configuration result with errors attribute."""

    errors: List[CheckConfigError] = attr.ib(default=attr.Factory(list))

    def add_error(
        self,
        message: str,
        domain: Optional[str] = None,
        config: Optional[ConfigType] = None,
    ) -> "HomeAssistantConfig":
        """Add a single error."""
        self.errors.append(CheckConfigError(str(message), domain, config))
        return self

    @property
    def error_str(self) -> str:
        """Return errors as a string."""
        return "\n".join([err.message for err in self.errors])


async def async_check_ha_config_file(hass: HomeAssistant) -> HomeAssistantConfig:
    """Load and check if Home Assistant configuration file is valid.

    This method is a coroutine.
    """
    config_dir = hass.config.config_dir
    result = HomeAssistantConfig()

    def _pack_error(
        package: str, component: str, config: ConfigType, message: str
    ) -> None:
        """Handle errors from packages: _log_pkg_error."""
        message = "Package {} setup failed. Component {} {}".format(
            package, component, message
        )
        domain = f"homeassistant.packages.{package}.{component}"
        pack_config = core_config[CONF_PACKAGES].get(package, config)
        result.add_error(message, domain, pack_config)

    def _comp_error(ex: Exception, domain: str, config: ConfigType) -> None:
        """Handle errors from components: async_log_exception."""
        result.add_error(_format_config_error(ex, domain, config), domain, config)

    # Load configuration.yaml
    try:
        config_path = await hass.async_add_executor_job(find_config_file, config_dir)
        if not config_path:
            return result.add_error("File configuration.yaml not found.")
        config = await hass.async_add_executor_job(load_yaml_config_file, config_path)
    except FileNotFoundError:
        return result.add_error(f"File not found: {config_path}")
    except HomeAssistantError as err:
        return result.add_error(f"Error loading {config_path}: {err}")
    finally:
        yaml_loader.clear_secret_cache()

    # Extract and validate core [homeassistant] config
    try:
        core_config = config.pop(CONF_CORE, {})
        core_config = CORE_CONFIG_SCHEMA(core_config)
        result[CONF_CORE] = core_config
    except vol.Invalid as err:
        result.add_error(err, CONF_CORE, core_config)
        core_config = {}

    # Merge packages
    await merge_packages_config(
        hass, config, core_config.get(CONF_PACKAGES, {}), _pack_error
    )
    core_config.pop(CONF_PACKAGES, None)

    # Filter out repeating config sections
    components = set(key.split(" ")[0] for key in config.keys())

    # Process and validate config
    for domain in components:
        try:
            integration = await async_get_integration_with_requirements(hass, domain)
        except (RequirementsNotFound, loader.IntegrationNotFound) as ex:
            result.add_error(f"Component error: {domain} - {ex}")
            continue

        try:
            component = integration.get_component()
        except ImportError as ex:
            result.add_error(f"Component error: {domain} - {ex}")
            continue

        config_schema = getattr(component, "CONFIG_SCHEMA", None)
        if config_schema is not None:
            try:
                config = config_schema(config)
                result[domain] = config[domain]
            except vol.Invalid as ex:
                _comp_error(ex, domain, config)
                continue

        component_platform_schema = getattr(
            component,
            "PLATFORM_SCHEMA_BASE",
            getattr(component, "PLATFORM_SCHEMA", None),
        )

        if component_platform_schema is None:
            continue

        platforms = []
        for p_name, p_config in config_per_platform(config, domain):
            # Validate component specific platform schema
            try:
                p_validated = component_platform_schema(p_config)
            except vol.Invalid as ex:
                _comp_error(ex, domain, config)
                continue

            # Not all platform components follow same pattern for platforms
            # So if p_name is None we are not going to validate platform
            # (the automation component is one of them)
            if p_name is None:
                platforms.append(p_validated)
                continue

            try:
                p_integration = await async_get_integration_with_requirements(
                    hass, p_name
                )
                platform = p_integration.get_platform(domain)
            except (
                loader.IntegrationNotFound,
                RequirementsNotFound,
                ImportError,
            ) as ex:
                result.add_error(f"Platform error {domain}.{p_name} - {ex}")
                continue

            # Validate platform specific schema
            platform_schema = getattr(platform, "PLATFORM_SCHEMA", None)
            if platform_schema is not None:
                try:
                    p_validated = platform_schema(p_validated)
                except vol.Invalid as ex:
                    _comp_error(ex, f"{domain}.{p_name}", p_validated)
                    continue

            platforms.append(p_validated)

        # Remove config for current component and add validated config back in.
        for filter_comp in extract_domain_configs(config, domain):
            del config[filter_comp]
        result[domain] = platforms

    return result
