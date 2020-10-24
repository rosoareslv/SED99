"""
The methods for loading Home Assistant integrations.

This module has quite some complex parts. I have tried to add as much
documentation as possible to keep it understandable.
"""
import asyncio
import functools as ft
import importlib
import json
import logging
import pathlib
import sys
from types import ModuleType
from typing import (
    TYPE_CHECKING,
    Any,
    Callable,
    Dict,
    List,
    Optional,
    Set,
    TypeVar,
    Union,
    cast,
)

# Typing imports that create a circular dependency
# pylint: disable=unused-import
if TYPE_CHECKING:
    from homeassistant.core import HomeAssistant

CALLABLE_T = TypeVar("CALLABLE_T", bound=Callable)  # pylint: disable=invalid-name

DEPENDENCY_BLACKLIST = {"config"}

_LOGGER = logging.getLogger(__name__)

DATA_COMPONENTS = "components"
DATA_INTEGRATIONS = "integrations"
DATA_CUSTOM_COMPONENTS = "custom_components"
PACKAGE_CUSTOM_COMPONENTS = "custom_components"
PACKAGE_BUILTIN = "homeassistant.components"
LOOKUP_PATHS = [PACKAGE_CUSTOM_COMPONENTS, PACKAGE_BUILTIN]
CUSTOM_WARNING = (
    "You are using a custom integration for %s which has not "
    "been tested by Home Assistant. This component might "
    "cause stability problems, be sure to disable it if you "
    "do experience issues with Home Assistant."
)
_UNDEF = object()


def manifest_from_legacy_module(domain: str, module: ModuleType) -> Dict:
    """Generate a manifest from a legacy module."""
    return {
        "domain": domain,
        "name": domain,
        "documentation": None,
        "requirements": getattr(module, "REQUIREMENTS", []),
        "dependencies": getattr(module, "DEPENDENCIES", []),
        "codeowners": [],
    }


async def _async_get_custom_components(
    hass: "HomeAssistant",
) -> Dict[str, "Integration"]:
    """Return list of custom integrations."""
    try:
        import custom_components
    except ImportError:
        return {}

    def get_sub_directories(paths: List) -> List:
        """Return all sub directories in a set of paths."""
        return [
            entry
            for path in paths
            for entry in pathlib.Path(path).iterdir()
            if entry.is_dir()
        ]

    dirs = await hass.async_add_executor_job(
        get_sub_directories, custom_components.__path__
    )

    integrations = await asyncio.gather(
        *(
            hass.async_add_executor_job(
                Integration.resolve_from_root, hass, custom_components, comp.name
            )
            for comp in dirs
        )
    )

    return {
        integration.domain: integration
        for integration in integrations
        if integration is not None
    }


async def async_get_custom_components(
    hass: "HomeAssistant",
) -> Dict[str, "Integration"]:
    """Return cached list of custom integrations."""
    reg_or_evt = hass.data.get(DATA_CUSTOM_COMPONENTS)

    if reg_or_evt is None:
        evt = hass.data[DATA_CUSTOM_COMPONENTS] = asyncio.Event()

        reg = await _async_get_custom_components(hass)

        hass.data[DATA_CUSTOM_COMPONENTS] = reg
        evt.set()
        return reg

    if isinstance(reg_or_evt, asyncio.Event):
        await reg_or_evt.wait()
        return cast(Dict[str, "Integration"], hass.data.get(DATA_CUSTOM_COMPONENTS))

    return cast(Dict[str, "Integration"], reg_or_evt)


async def async_get_config_flows(hass: "HomeAssistant") -> Set[str]:
    """Return cached list of config flows."""
    from homeassistant.generated.config_flows import FLOWS

    flows: Set[str] = set()
    flows.update(FLOWS)

    integrations = await async_get_custom_components(hass)
    flows.update(
        [
            integration.domain
            for integration in integrations.values()
            if integration.config_flow
        ]
    )

    return flows


class Integration:
    """An integration in Home Assistant."""

    @classmethod
    def resolve_from_root(
        cls, hass: "HomeAssistant", root_module: ModuleType, domain: str
    ) -> "Optional[Integration]":
        """Resolve an integration from a root module."""
        for base in root_module.__path__:  # type: ignore
            manifest_path = pathlib.Path(base) / domain / "manifest.json"

            if not manifest_path.is_file():
                continue

            try:
                manifest = json.loads(manifest_path.read_text())
            except ValueError as err:
                _LOGGER.error(
                    "Error parsing manifest.json file at %s: %s", manifest_path, err
                )
                continue

            return cls(
                hass, f"{root_module.__name__}.{domain}", manifest_path.parent, manifest
            )

        return None

    @classmethod
    def resolve_legacy(
        cls, hass: "HomeAssistant", domain: str
    ) -> "Optional[Integration]":
        """Resolve legacy component.

        Will create a stub manifest.
        """
        comp = _load_file(hass, domain, LOOKUP_PATHS)

        if comp is None:
            return None

        return cls(
            hass,
            comp.__name__,
            pathlib.Path(comp.__file__).parent,
            manifest_from_legacy_module(domain, comp),
        )

    def __init__(
        self,
        hass: "HomeAssistant",
        pkg_path: str,
        file_path: pathlib.Path,
        manifest: Dict[str, Any],
    ):
        """Initialize an integration."""
        self.hass = hass
        self.pkg_path = pkg_path
        self.file_path = file_path
        self.manifest = manifest
        _LOGGER.info("Loaded %s from %s", self.domain, pkg_path)

    @property
    def name(self) -> str:
        """Return name."""
        return cast(str, self.manifest["name"])

    @property
    def domain(self) -> str:
        """Return domain."""
        return cast(str, self.manifest["domain"])

    @property
    def dependencies(self) -> List[str]:
        """Return dependencies."""
        return cast(List[str], self.manifest.get("dependencies", []))

    @property
    def after_dependencies(self) -> List[str]:
        """Return after_dependencies."""
        return cast(List[str], self.manifest.get("after_dependencies", []))

    @property
    def requirements(self) -> List[str]:
        """Return requirements."""
        return cast(List[str], self.manifest.get("requirements", []))

    @property
    def config_flow(self) -> bool:
        """Return config_flow."""
        return cast(bool, self.manifest.get("config_flow", False))

    @property
    def documentation(self) -> Optional[str]:
        """Return documentation."""
        return cast(str, self.manifest.get("documentation"))

    @property
    def is_built_in(self) -> bool:
        """Test if package is a built-in integration."""
        return self.pkg_path.startswith(PACKAGE_BUILTIN)

    def get_component(self) -> ModuleType:
        """Return the component."""
        cache = self.hass.data.setdefault(DATA_COMPONENTS, {})
        if self.domain not in cache:
            cache[self.domain] = importlib.import_module(self.pkg_path)
        return cache[self.domain]  # type: ignore

    def get_platform(self, platform_name: str) -> ModuleType:
        """Return a platform for an integration."""
        cache = self.hass.data.setdefault(DATA_COMPONENTS, {})
        full_name = f"{self.domain}.{platform_name}"
        if full_name not in cache:
            cache[full_name] = importlib.import_module(
                f"{self.pkg_path}.{platform_name}"
            )
        return cache[full_name]  # type: ignore

    def __repr__(self) -> str:
        """Text representation of class."""
        return f"<Integration {self.domain}: {self.pkg_path}>"


async def async_get_integration(hass: "HomeAssistant", domain: str) -> Integration:
    """Get an integration."""
    cache = hass.data.get(DATA_INTEGRATIONS)
    if cache is None:
        if not _async_mount_config_dir(hass):
            raise IntegrationNotFound(domain)
        cache = hass.data[DATA_INTEGRATIONS] = {}

    int_or_evt: Union[Integration, asyncio.Event, None] = cache.get(domain, _UNDEF)

    if isinstance(int_or_evt, asyncio.Event):
        await int_or_evt.wait()
        int_or_evt = cache.get(domain, _UNDEF)

        # When we have waited and it's _UNDEF, it doesn't exist
        # We don't cache that it doesn't exist, or else people can't fix it
        # and then restart, because their config will never be valid.
        if int_or_evt is _UNDEF:
            raise IntegrationNotFound(domain)

    if int_or_evt is not _UNDEF:
        return cast(Integration, int_or_evt)

    event = cache[domain] = asyncio.Event()

    # Instead of using resolve_from_root we use the cache of custom
    # components to find the integration.
    integration = (await async_get_custom_components(hass)).get(domain)
    if integration is not None:
        _LOGGER.warning(CUSTOM_WARNING, domain)
        cache[domain] = integration
        event.set()
        return integration

    from homeassistant import components

    integration = await hass.async_add_executor_job(
        Integration.resolve_from_root, hass, components, domain
    )

    if integration is not None:
        cache[domain] = integration
        event.set()
        return integration

    integration = Integration.resolve_legacy(hass, domain)
    if integration is not None:
        cache[domain] = integration
    else:
        # Remove event from cache.
        cache.pop(domain)

    event.set()

    if not integration:
        raise IntegrationNotFound(domain)

    return integration


class LoaderError(Exception):
    """Loader base error."""


class IntegrationNotFound(LoaderError):
    """Raised when a component is not found."""

    def __init__(self, domain: str) -> None:
        """Initialize a component not found error."""
        super().__init__(f"Integration '{domain}' not found.")
        self.domain = domain


class CircularDependency(LoaderError):
    """Raised when a circular dependency is found when resolving components."""

    def __init__(self, from_domain: str, to_domain: str) -> None:
        """Initialize circular dependency error."""
        super().__init__(f"Circular dependency detected: {from_domain} -> {to_domain}.")
        self.from_domain = from_domain
        self.to_domain = to_domain


def _load_file(
    hass: "HomeAssistant", comp_or_platform: str, base_paths: List[str]
) -> Optional[ModuleType]:
    """Try to load specified file.

    Looks in config dir first, then built-in components.
    Only returns it if also found to be valid.
    Async friendly.
    """
    try:
        return hass.data[DATA_COMPONENTS][comp_or_platform]  # type: ignore
    except KeyError:
        pass

    cache = hass.data.get(DATA_COMPONENTS)
    if cache is None:
        if not _async_mount_config_dir(hass):
            return None
        cache = hass.data[DATA_COMPONENTS] = {}

    for path in (f"{base}.{comp_or_platform}" for base in base_paths):
        try:
            module = importlib.import_module(path)

            # In Python 3 you can import files from directories that do not
            # contain the file __init__.py. A directory is a valid module if
            # it contains a file with the .py extension. In this case Python
            # will succeed in importing the directory as a module and call it
            # a namespace. We do not care about namespaces.
            # This prevents that when only
            # custom_components/switch/some_platform.py exists,
            # the import custom_components.switch would succeed.
            # __file__ was unset for namespaces before Python 3.7
            if getattr(module, "__file__", None) is None:
                continue

            cache[comp_or_platform] = module

            if module.__name__.startswith(PACKAGE_CUSTOM_COMPONENTS):
                _LOGGER.warning(CUSTOM_WARNING, comp_or_platform)

            return module

        except ImportError as err:
            # This error happens if for example custom_components/switch
            # exists and we try to load switch.demo.
            # Ignore errors for custom_components, custom_components.switch
            # and custom_components.switch.demo.
            white_listed_errors = []
            parts = []
            for part in path.split("."):
                parts.append(part)
                white_listed_errors.append(
                    "No module named '{}'".format(".".join(parts))
                )

            if str(err) not in white_listed_errors:
                _LOGGER.exception(
                    ("Error loading %s. Make sure all " "dependencies are installed"),
                    path,
                )

    return None


class ModuleWrapper:
    """Class to wrap a Python module and auto fill in hass argument."""

    def __init__(self, hass: "HomeAssistant", module: ModuleType) -> None:
        """Initialize the module wrapper."""
        self._hass = hass
        self._module = module

    def __getattr__(self, attr: str) -> Any:
        """Fetch an attribute."""
        value = getattr(self._module, attr)

        if hasattr(value, "__bind_hass"):
            value = ft.partial(value, self._hass)

        setattr(self, attr, value)
        return value


class Components:
    """Helper to load components."""

    def __init__(self, hass: "HomeAssistant") -> None:
        """Initialize the Components class."""
        self._hass = hass

    def __getattr__(self, comp_name: str) -> ModuleWrapper:
        """Fetch a component."""
        # Test integration cache
        integration = self._hass.data.get(DATA_INTEGRATIONS, {}).get(comp_name)

        if isinstance(integration, Integration):
            component: Optional[ModuleType] = integration.get_component()
        else:
            # Fallback to importing old-school
            component = _load_file(self._hass, comp_name, LOOKUP_PATHS)

        if component is None:
            raise ImportError(f"Unable to load {comp_name}")

        wrapped = ModuleWrapper(self._hass, component)
        setattr(self, comp_name, wrapped)
        return wrapped


class Helpers:
    """Helper to load helpers."""

    def __init__(self, hass: "HomeAssistant") -> None:
        """Initialize the Helpers class."""
        self._hass = hass

    def __getattr__(self, helper_name: str) -> ModuleWrapper:
        """Fetch a helper."""
        helper = importlib.import_module(f"homeassistant.helpers.{helper_name}")
        wrapped = ModuleWrapper(self._hass, helper)
        setattr(self, helper_name, wrapped)
        return wrapped


def bind_hass(func: CALLABLE_T) -> CALLABLE_T:
    """Decorate function to indicate that first argument is hass."""
    setattr(func, "__bind_hass", True)
    return func


async def async_component_dependencies(hass: "HomeAssistant", domain: str) -> Set[str]:
    """Return all dependencies and subdependencies of components.

    Raises CircularDependency if a circular dependency is found.
    """
    return await _async_component_dependencies(hass, domain, set(), set())


async def _async_component_dependencies(
    hass: "HomeAssistant", domain: str, loaded: Set[str], loading: Set
) -> Set[str]:
    """Recursive function to get component dependencies.

    Async friendly.
    """
    integration = await async_get_integration(hass, domain)

    loading.add(domain)

    for dependency_domain in integration.dependencies:
        # Check not already loaded
        if dependency_domain in loaded:
            continue

        # If we are already loading it, we have a circular dependency.
        if dependency_domain in loading:
            raise CircularDependency(domain, dependency_domain)

        dep_loaded = await _async_component_dependencies(
            hass, dependency_domain, loaded, loading
        )

        loaded.update(dep_loaded)

    loaded.add(domain)
    loading.remove(domain)

    return loaded


def _async_mount_config_dir(hass: "HomeAssistant") -> bool:
    """Mount config dir in order to load custom_component.

    Async friendly but not a coroutine.
    """
    if hass.config.config_dir is None:
        _LOGGER.error("Can't load integrations - config dir is not set")
        return False
    if hass.config.config_dir not in sys.path:
        sys.path.insert(0, hass.config.config_dir)
    return True
