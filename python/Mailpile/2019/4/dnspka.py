# Note, this code is not used by default, because:
#
#   1. DNS is almost entirely insecure and not private
#   2. Our current code-base cannot route DNS lookups over Tor
#   3. Nobody uses this. Even Werner himself has an obsolete key in DNS.
#   4. Web Key Directory solves the same problem, properly.
#
# The code is left here in the repo as a historic oddity; a resource for
# people to explore and learn.

try:
    import DNS
except:
    DNS = None

import urllib2

from mailpile.conn_brokers import Master as ConnBroker
from mailpile.i18n import gettext
from mailpile.plugins.keylookup import LookupHandler
from mailpile.plugins.keylookup import register_crypto_key_lookup_handler

_ = lambda t: t

#
#  Support for DNS PKA (_pka) entries.
#  See http://www.gushi.org/make-dns-cert/HOWTO.html
#

class DNSPKALookupHandler(LookupHandler):
    NAME = _("DNS PKA records")
    TIMEOUT = 10
    PRIORITY = 100
    PRIVACY_FRIENDLY = False  # Bypasses Tor, currently.
    SCORE = 3

    def __init__(self, *args, **kwargs):
        LookupHandler.__init__(self, *args, **kwargs)
        if not DNS:
            return
        DNS.ParseResolvConf()
        self.req = DNS.Request(qtype="TXT")

    def _score(self, key):
        return (self.SCORE, _('Found key in DNS PKA'))

    def _lookup(self, address, strict_email_match=True):
        """
        >>> from mailpile.crypto.dnspka import *
        >>> d = DNSPKALookup()
        >>> res = d.lookup("smari@immi.is")
        >>> res["result"]["count"] == 1
        """
        if not DNS:
            return {}
        dom = address.replace("@", "._pka.")
        result = self.req.req(dom)
        for res in (result.answers if result else []):
            if res["typename"] != "TXT":
                continue
            for entry in res["data"]:
                return self._keyinfo(entry)

        return {}

    def _keyinfo(self, entry):
        pkaver = None
        fingerprint = None
        url = None

        for stmt in entry.split(";"):
            key, value = stmt.split("=", 1)
            if key == "v":
                pkaver = value
            elif key == "fpr":
                fingerprint = value
            elif key == "uri":
                url = value

        if pkaver != "pka1":
            raise ValueError("We only know how to deal with pka version 1")

        return {fingerprint: {
            "fingerprint": fingerprint,
            "url": url,
            "pkaver": pkaver}}

    def _getkey(self, key):
        if key["url"] and key["url"][:6].lower() in ('http:/', 'https:'):
            with ConnBroker.context(need=[ConnBroker.OUTGOING_HTTP]):
                r = urllib2.urlopen(key["url"])
            result = r.readlines()
            start = 0
            end = len(result)
            # Hack to deal with possible HTML results from keyservers:
            for i in range(len(result)):
                if result[i].startswith("-----BEGIN PGP"):
                    start = i
                elif result[i].startswith("-----END PGP"):
                    end = i
            result = "".join(result[start:end])
            res = self._gnupg().import_keys(result)
            return res
        elif key["fingerprint"]:
            res = self._gnupg().recv_key(key["fingerprint"])
        else:
            raise ValueError("Need a fingerprint or a URL. key=%s" % key)


_ = gettext
register_crypto_key_lookup_handler(DNSPKALookupHandler)
