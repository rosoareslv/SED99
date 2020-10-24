#!/usr/bin/env python

"""
Deliver email messages that have been queued by various things
(at this time invitation reminders and day1/day2 followup emails).

This management command is run via supervisor. Do not run on multiple machines,
as you may encounter multiple sends in a specific race condition.
"""

from __future__ import absolute_import

from django.conf import settings
from django.core.management.base import BaseCommand
from django.core.mail import get_connection, send_mail
from django.utils.timezone import now as timezone_now
from django.utils.html import format_html

from zerver.models import ScheduledJob
from zerver.lib.context_managers import lockfile

import time
import logging
from datetime import datetime
from ujson import loads
from typing import Any, Dict

## Setup ##
log_format = "%(asctime)s: %(message)s"
logging.basicConfig(format=log_format)

formatter = logging.Formatter(log_format)
file_handler = logging.FileHandler(settings.EMAIL_DELIVERER_LOG_PATH)
file_handler.setFormatter(formatter)

logger = logging.getLogger(__name__)
logger.setLevel(logging.DEBUG)
logger.addHandler(file_handler)


def get_recipient_as_string(dictionary):
    # type: (Dict[str, str]) -> str
    if not dictionary["recipient_name"]:
        return dictionary["recipient_email"]
    return format_html(u"\"{0}\" <{1}>", dictionary["recipient_name"], dictionary["recipient_email"])

def get_sender_as_string(dictionary):
    # type: (Dict[str, str]) -> str
    if dictionary["sender_email"]:
        return dictionary["sender_email"] if not dictionary["sender_name"] else format_html(u"\"{0}\" <{1}>",
                                                                                            dictionary["sender_name"],
                                                                                            dictionary["sender_email"])
    return settings.DEFAULT_FROM_EMAIL

def send_email_job(job):
    # type: (ScheduledJob) -> bool
    data = loads(job.data)
    subject = data["email_subject"]
    message = data["email_text"]
    from_email = get_sender_as_string(data)
    to_email = get_recipient_as_string(data)

    if data["email_html"]:
        html_message = data["email_html"]
        return send_mail(subject, message, from_email, [to_email], html_message=html_message) > 0
    return send_mail(subject, message, from_email, [to_email]) > 0

class Command(BaseCommand):
    help = """Deliver emails queued by various parts of Zulip
(either for immediate sending or sending at a specified time).

Run this command under supervisor. We use Mandrill for zulip.com; this is for SMTP email delivery.

Usage: ./manage.py deliver_email
"""

    def handle(self, *args, **options):
        # type: (*Any, **Any) -> None
        # TODO: this only acquires a lock on the system, not on the DB:
        # be careful not to run this on multiple systems.

        # In the meantime, we have an option to prevent this job from
        # running on >1 machine
        if settings.EMAIL_DELIVERER_DISABLED:
            return

        with lockfile("/tmp/zulip_email_deliver.lockfile"):
            while True:
                email_jobs_to_deliver = ScheduledJob.objects.filter(type=ScheduledJob.EMAIL,
                                                                    scheduled_timestamp__lte=timezone_now())
                if email_jobs_to_deliver:
                    for job in email_jobs_to_deliver:
                        if not send_email_job(job):
                            logger.warn("No exception raised, but %r sent as 0 bytes" % (job,))
                        else:
                            job.delete()
                    time.sleep(10)
                else:
                    # Less load on the db during times of activity, and more responsiveness when the load is low
                    time.sleep(2)
