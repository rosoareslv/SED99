"""Module to handle installing requirements."""
import asyncio
import logging
import os
from pathlib import Path
from typing import Any, Dict, Iterable, List, Optional, Set

from homeassistant.core import HomeAssistant
from homeassistant.exceptions import HomeAssistantError
from homeassistant.loader import Integration, async_get_integration
import homeassistant.util.package as pkg_util

DATA_PIP_LOCK = "pip_lock"
DATA_PKG_CACHE = "pkg_cache"
CONSTRAINT_FILE = "package_constraints.txt"
PROGRESS_FILE = ".pip_progress"
_LOGGER = logging.getLogger(__name__)
DISCOVERY_INTEGRATIONS: Dict[str, Iterable[str]] = {
    "ssdp": ("ssdp",),
    "zeroconf": ("zeroconf", "homekit"),
}


class RequirementsNotFound(HomeAssistantError):
    """Raised when a component is not found."""

    def __init__(self, domain: str, requirements: List) -> None:
        """Initialize a component not found error."""
        super().__init__(f"Requirements for {domain} not found: {requirements}.")
        self.domain = domain
        self.requirements = requirements


async def async_get_integration_with_requirements(
    hass: HomeAssistant, domain: str, done: Set[str] = None
) -> Integration:
    """Get an integration with all requirements installed, including the dependencies.

    This can raise IntegrationNotFound if manifest or integration
    is invalid, RequirementNotFound if there was some type of
    failure to install requirements.
    """
    if done is None:
        done = {domain}
    else:
        done.add(domain)

    integration = await async_get_integration(hass, domain)

    if hass.config.skip_pip:
        return integration

    if integration.requirements:
        await async_process_requirements(
            hass, integration.domain, integration.requirements
        )

    deps_to_check = [
        dep
        for dep in integration.dependencies + integration.after_dependencies
        if dep not in done
    ]

    for check_domain, to_check in DISCOVERY_INTEGRATIONS.items():
        if (
            check_domain not in done
            and check_domain not in deps_to_check
            and any(check in integration.manifest for check in to_check)
        ):
            deps_to_check.append(check_domain)

    if deps_to_check:
        await asyncio.gather(
            *[
                async_get_integration_with_requirements(hass, dep, done)
                for dep in deps_to_check
            ]
        )

    return integration


async def async_process_requirements(
    hass: HomeAssistant, name: str, requirements: List[str]
) -> None:
    """Install the requirements for a component or platform.

    This method is a coroutine. It will raise RequirementsNotFound
    if an requirement can't be satisfied.
    """
    pip_lock = hass.data.get(DATA_PIP_LOCK)
    if pip_lock is None:
        pip_lock = hass.data[DATA_PIP_LOCK] = asyncio.Lock()

    kwargs = pip_kwargs(hass.config.config_dir)

    async with pip_lock:
        for req in requirements:
            if pkg_util.is_installed(req):
                continue

            ret = await hass.async_add_executor_job(_install, hass, req, kwargs)

            if not ret:
                raise RequirementsNotFound(name, [req])


def _install(hass: HomeAssistant, req: str, kwargs: Dict) -> bool:
    """Install requirement."""
    progress_path = Path(hass.config.path(PROGRESS_FILE))
    progress_path.touch()
    try:
        return pkg_util.install_package(req, **kwargs)
    finally:
        progress_path.unlink()


def pip_kwargs(config_dir: Optional[str]) -> Dict[str, Any]:
    """Return keyword arguments for PIP install."""
    is_docker = pkg_util.is_docker_env()
    kwargs = {
        "constraints": os.path.join(os.path.dirname(__file__), CONSTRAINT_FILE),
        "no_cache_dir": is_docker,
    }
    if "WHEELS_LINKS" in os.environ:
        kwargs["find_links"] = os.environ["WHEELS_LINKS"]
    if not (config_dir is None or pkg_util.is_virtual_env()) and not is_docker:
        kwargs["target"] = os.path.join(config_dir, "deps")
    return kwargs
