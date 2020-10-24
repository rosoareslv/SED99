"""Helpers for logging allowing more advanced logging styles to be used."""
import inspect
import logging
from typing import Any, Mapping, MutableMapping, Optional, Tuple


class KeywordMessage:
    """
    Represents a logging message with keyword arguments.

    Adapted from: https://stackoverflow.com/a/24683360/2267718
    """

    def __init__(self, fmt: Any, args: Any, kwargs: Mapping[str, Any]) -> None:
        """Initialize a new KeywordMessage object."""
        self._fmt = fmt
        self._args = args
        self._kwargs = kwargs

    def __str__(self) -> str:
        """Convert the object to a string for logging."""
        return str(self._fmt).format(*self._args, **self._kwargs)


class KeywordStyleAdapter(logging.LoggerAdapter):
    """Represents an adapter wrapping the logger allowing KeywordMessages."""

    def __init__(
        self, logger: logging.Logger, extra: Optional[Mapping[str, Any]] = None
    ) -> None:
        """Initialize a new StyleAdapter for the provided logger."""
        super().__init__(logger, extra or {})

    def log(self, level: int, msg: Any, *args: Any, **kwargs: Any) -> None:
        """Log the message provided at the appropriate level."""
        if self.isEnabledFor(level):
            msg, log_kwargs = self.process(msg, kwargs)
            self.logger._log(  # type: ignore # pylint: disable=protected-access
                level, KeywordMessage(msg, args, kwargs), (), **log_kwargs
            )

    def process(
        self, msg: Any, kwargs: MutableMapping[str, Any]
    ) -> Tuple[Any, MutableMapping[str, Any]]:
        """Process the keyward args in preparation for logging."""
        return (
            msg,
            {
                k: kwargs[k]
                for k in inspect.getfullargspec(
                    self.logger._log  # type: ignore # pylint: disable=protected-access
                ).args[1:]
                if k in kwargs
            },
        )
