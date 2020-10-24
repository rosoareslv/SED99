"""
Proxy camera platform that enables image processing of camera data.

For more details about this platform, please refer to the documentation
https://www.home-assistant.io/components/camera.proxy/
"""
import asyncio
import logging

import aiohttp
import async_timeout
import voluptuous as vol

from homeassistant.components.camera import PLATFORM_SCHEMA, Camera
from homeassistant.const import CONF_ENTITY_ID, CONF_NAME, HTTP_HEADER_HA_AUTH
from homeassistant.helpers import config_validation as cv
from homeassistant.helpers.aiohttp_client import (
    async_aiohttp_proxy_web, async_get_clientsession)
from homeassistant.util.async_ import run_coroutine_threadsafe
import homeassistant.util.dt as dt_util

REQUIREMENTS = ['pillow==5.2.0']

_LOGGER = logging.getLogger(__name__)

CONF_CACHE_IMAGES = 'cache_images'
CONF_FORCE_RESIZE = 'force_resize'
CONF_IMAGE_QUALITY = 'image_quality'
CONF_IMAGE_REFRESH_RATE = 'image_refresh_rate'
CONF_MAX_IMAGE_WIDTH = 'max_image_width'
CONF_MAX_STREAM_WIDTH = 'max_stream_width'
CONF_STREAM_QUALITY = 'stream_quality'

DEFAULT_BASENAME = "Camera Proxy"
DEFAULT_QUALITY = 75

PLATFORM_SCHEMA = PLATFORM_SCHEMA.extend({
    vol.Required(CONF_ENTITY_ID): cv.entity_id,
    vol.Optional(CONF_CACHE_IMAGES, False): cv.boolean,
    vol.Optional(CONF_FORCE_RESIZE, False): cv.boolean,
    vol.Optional(CONF_IMAGE_QUALITY): int,
    vol.Optional(CONF_IMAGE_REFRESH_RATE): float,
    vol.Optional(CONF_MAX_IMAGE_WIDTH): int,
    vol.Optional(CONF_MAX_STREAM_WIDTH): int,
    vol.Optional(CONF_NAME): cv.string,
    vol.Optional(CONF_STREAM_QUALITY): int,
})


async def async_setup_platform(
        hass, config, async_add_entities, discovery_info=None):
    """Set up the Proxy camera platform."""
    async_add_entities([ProxyCamera(hass, config)])


def _resize_image(image, opts):
    """Resize image."""
    from PIL import Image
    import io

    if not opts:
        return image

    quality = opts.quality or DEFAULT_QUALITY
    new_width = opts.max_width

    try:
        img = Image.open(io.BytesIO(image))
    except IOError:
        return image
    imgfmt = str(img.format)
    if imgfmt not in ('PNG', 'JPEG'):
        _LOGGER.debug("Image is of unsupported type: %s", imgfmt)
        return image

    (old_width, old_height) = img.size
    old_size = len(image)
    if old_width <= new_width:
        if opts.quality is None:
            _LOGGER.debug("Image is smaller-than/equal-to requested width")
            return image
        new_width = old_width

    scale = new_width / float(old_width)
    new_height = int((float(old_height)*float(scale)))

    img = img.resize((new_width, new_height), Image.ANTIALIAS)
    imgbuf = io.BytesIO()
    img.save(imgbuf, 'JPEG', optimize=True, quality=quality)
    newimage = imgbuf.getvalue()
    if not opts.force_resize and len(newimage) >= old_size:
        _LOGGER.debug("Using original image(%d bytes) "
                      "because resized image (%d bytes) is not smaller",
                      old_size, len(newimage))
        return image

    _LOGGER.debug(
        "Resized image from (%dx%d - %d bytes) to (%dx%d - %d bytes)",
        old_width, old_height, old_size, new_width, new_height, len(newimage))
    return newimage


class ImageOpts():
    """The representation of image options."""

    def __init__(self, max_width, quality, force_resize):
        """Initialize image options."""
        self.max_width = max_width
        self.quality = quality
        self.force_resize = force_resize

    def __bool__(self):
        """Bool evaluation rules."""
        return bool(self.max_width or self.quality)


class ProxyCamera(Camera):
    """The representation of a Proxy camera."""

    def __init__(self, hass, config):
        """Initialize a proxy camera component."""
        super().__init__()
        self.hass = hass
        self._proxied_camera = config.get(CONF_ENTITY_ID)
        self._name = (
            config.get(CONF_NAME) or
            "{} - {}".format(DEFAULT_BASENAME, self._proxied_camera))
        self._image_opts = ImageOpts(
            config.get(CONF_MAX_IMAGE_WIDTH),
            config.get(CONF_IMAGE_QUALITY),
            config.get(CONF_FORCE_RESIZE))

        self._stream_opts = ImageOpts(
            config.get(CONF_MAX_STREAM_WIDTH), config.get(CONF_STREAM_QUALITY),
            True)

        self._image_refresh_rate = config.get(CONF_IMAGE_REFRESH_RATE)
        self._cache_images = bool(
            config.get(CONF_IMAGE_REFRESH_RATE)
            or config.get(CONF_CACHE_IMAGES))
        self._last_image_time = 0
        self._last_image = None
        self._headers = (
            {HTTP_HEADER_HA_AUTH: self.hass.config.api.api_password}
            if self.hass.config.api.api_password is not None else None)

    def camera_image(self):
        """Return camera image."""
        return run_coroutine_threadsafe(
            self.async_camera_image(), self.hass.loop).result()

    async def async_camera_image(self):
        """Return a still image response from the camera."""
        now = dt_util.utcnow()

        if (self._image_refresh_rate and
                now < self._last_image_time + self._image_refresh_rate):
            return self._last_image

        self._last_image_time = now
        url = "{}/api/camera_proxy/{}".format(
            self.hass.config.api.base_url, self._proxied_camera)
        try:
            websession = async_get_clientsession(self.hass)
            with async_timeout.timeout(10, loop=self.hass.loop):
                response = await websession.get(url, headers=self._headers)
            image = await response.read()
        except asyncio.TimeoutError:
            _LOGGER.error("Timeout getting camera image")
            return self._last_image
        except aiohttp.ClientError as err:
            _LOGGER.error("Error getting new camera image: %s", err)
            return self._last_image

        image = await self.hass.async_add_job(
            _resize_image, image, self._image_opts)

        if self._cache_images:
            self._last_image = image
        return image

    async def handle_async_mjpeg_stream(self, request):
        """Generate an HTTP MJPEG stream from camera images."""
        websession = async_get_clientsession(self.hass)
        url = "{}/api/camera_proxy_stream/{}".format(
            self.hass.config.api.base_url, self._proxied_camera)
        stream_coro = websession.get(url, headers=self._headers)

        if not self._stream_opts:
            return await async_aiohttp_proxy_web(
                self.hass, request, stream_coro)

        response = aiohttp.web.StreamResponse()
        response.content_type = (
            'multipart/x-mixed-replace; boundary=--frameboundary')
        await response.prepare(request)

        async def write(img_bytes):
            """Write image to stream."""
            await response.write(bytes(
                '--frameboundary\r\n'
                'Content-Type: {}\r\n'
                'Content-Length: {}\r\n\r\n'.format(
                    self.content_type, len(img_bytes)),
                'utf-8') + img_bytes + b'\r\n')

        with async_timeout.timeout(10, loop=self.hass.loop):
            req = await stream_coro

        try:
            # This would be nicer as an async generator
            # But that would only be supported for python >=3.6
            data = b''
            stream = req.content
            while True:
                chunk = await stream.read(102400)
                if not chunk:
                    break
                data += chunk
                jpg_start = data.find(b'\xff\xd8')
                jpg_end = data.find(b'\xff\xd9')
                if jpg_start != -1 and jpg_end != -1:
                    image = data[jpg_start:jpg_end + 2]
                    image = await self.hass.async_add_job(
                        _resize_image, image, self._stream_opts)
                    await write(image)
                    data = data[jpg_end + 2:]
        finally:
            req.close()

        return response

    @property
    def name(self):
        """Return the name of this camera."""
        return self._name
