# (c) 2013, Jan-Piet Mens <jpmens(at)gmail.com>
#
# This file is part of Ansible
#
# Ansible is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# Ansible is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with Ansible.  If not, see <http://www.gnu.org/licenses/>.
from __future__ import (absolute_import, division, print_function)
__metaclass__ = type

import os
import urllib2
try:
    import json
except ImportError:
    import simplejson as json

from ansible.plugins.lookup import LookupBase

# this can be made configurable, not should not use ansible.cfg
ANSIBLE_ETCD_URL = 'http://127.0.0.1:4001'
if os.getenv('ANSIBLE_ETCD_URL') is not None:
    ANSIBLE_ETCD_URL = os.environ['ANSIBLE_ETCD_URL']

class etcd():
    def __init__(self, url=ANSIBLE_ETCD_URL):
        self.url = url
        self.baseurl = '%s/v1/keys' % (self.url)

    def get(self, key):
        url = "%s/%s" % (self.baseurl, key)

        data = None
        value = ""
        try:
            r = urllib2.urlopen(url)
            data = r.read()
        except:
            return value

        try:
            # {"action":"get","key":"/name","value":"Jane Jolie","index":5}
            item = json.loads(data)
            if 'value' in item:
                value = item['value']
            if 'errorCode' in item:
                value = "ENOENT"
        except:
            raise
            pass

        return value

class LookupModule(LookupBase):

    def run(self, terms, variables, **kwargs):

        if isinstance(terms, basestring):
            terms = [ terms ]

        etcd = etcd()

        ret = []
        for term in terms:
            key = term.split()[0]
            value = etcd.get(key)
            ret.append(value)
        return ret
