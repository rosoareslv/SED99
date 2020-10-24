from __future__ import absolute_import
from __future__ import print_function

from django.core.management.base import BaseCommand

import sys

from zerver.lib.actions import do_deactivate_realm
from zerver.models import get_realm

class Command(BaseCommand):
    help = """Script to deactivate a realm."""

    def add_arguments(self, parser):
        parser.add_argument('domain', metavar='<domain>', type=str,
                            help='domain of realm to deactivate')

    def handle(self, *args, **options):
        realm = get_realm(options["domain"])
        if realm is None:
            print("Could not find realm %s" % (options["domain"],))
            sys.exit(1)
        print("Deactivating", options["domain"])
        do_deactivate_realm(realm)
        print("Done!")
