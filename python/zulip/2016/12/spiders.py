#!/usr/bin/env python
from __future__ import print_function

import logging
import re
import scrapy

from scrapy import Request
from scrapy.linkextractors import IGNORED_EXTENSIONS
from scrapy.linkextractors.lxmlhtml import LxmlLinkExtractor
from scrapy.utils.url import url_has_any_extension

from typing import Any, Generator, List, Optional, Tuple


class BaseDocumentationSpider(scrapy.Spider):
    name = None # type: Optional[str]
    # Exclude domain address.
    deny_domains = [] # type: List[str]
    start_urls = [] # type: List[str]
    deny = () # type: Tuple
    file_extensions = ['.' + ext for ext in IGNORED_EXTENSIONS] # type: List[str]

    def _has_extension(self, url):
        # type: (str) -> bool
        return url_has_any_extension(url, self.file_extensions)

    def _is_external_url(self, url):
        # type: (str) -> bool
        return url.startswith('http') or self._has_extension(url)

    def check_existing(self, response):
        # type: (Any) -> None
        self.log(response)

    def check_permalink(self, response):
        # type: (Any) -> None
        self.log(response)
        xpath_template = "//*[@id='{permalink}' or @name='{permalink}']"
        m = re.match(r".+\#(?P<permalink>.*)$", response.request.url)  # Get anchor value.
        if not m:
            return
        permalink = m.group('permalink')
        # Check permalink existing on response page.
        if not response.selector.xpath(xpath_template.format(permalink=permalink)):
            raise Exception(
                "Permalink #{} is not found on page {}".format(permalink, response.request.url))

    def parse(self, response):
        # type: (Any) -> Generator[Request, None, None]
        self.log(response)
        for link in LxmlLinkExtractor(deny_domains=self.deny_domains, deny_extensions=[],
                                      deny=self.deny,
                                      canonicalize=False).extract_links(response):
            callback = self.parse  # type: Any
            dont_filter = False
            method = 'GET'

            if self._is_external_url(link.url):
                callback = self.check_existing
                method = 'HEAD'
            elif '#' in link.url:
                dont_filter = True
                callback = self.check_permalink
            yield Request(link.url, method=method, callback=callback, dont_filter=dont_filter,
                          errback=self.error_callback)

    def retry_request_with_get(self, request):
        # type: (Request) -> Generator[Request, None, None]
        request.method = 'GET'
        request.dont_filter = True
        yield request

    def error_callback(self, failure):
        # type: (Any) -> Optional[Generator[Any, None, None]]
        if hasattr(failure.value, 'response') and failure.value.response:
            response = failure.value.response
            if response.status == 404:
                raise Exception('Page not found: {}'.format(response))
            if response.status == 405 and response.request.method == 'HEAD':
                # Method 'HEAD' not allowed, repeat request with 'GET'
                return self.retry_request_with_get(response.request)
            self.log("Error! Please check link: {}".format(response), logging.ERROR)
        else:
            raise Exception(failure.value)
