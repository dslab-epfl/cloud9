#!/usr/bin/env python
#
# Cloud9 Parallel Symbolic Execution Engine
# 
# Copyright (c) 2011, Dependable Systems Laboratory, EPFL
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are met:
#     * Redistributions of source code must retain the above copyright
#       notice, this list of conditions and the following disclaimer.
#     * Redistributions in binary form must reproduce the above copyright
#       notice, this list of conditions and the following disclaimer in the
#       documentation and/or other materials provided with the distribution.
#     * Neither the name of the Dependable Systems Laboratory, EPFL nor the
#       names of its contributors may be used to endorse or promote products
#       derived from this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
# ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
# WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
# DISCLAIMED. IN NO EVENT SHALL THE DEPENDABLE SYSTEMS LABORATORY, EPFL BE LIABLE
# FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
# (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
# LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
# ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
# SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
#
# All contributors are listed in CLOUD9-AUTHORS file.
#

import sys
import os
import logging

import boto.ec2
import boto

import smtplib
from email.mime.text import MIMEText

from argparse import ArgumentParser
from datetime import datetime

REPORT = """
EC2 Account: %(account)s
Date: %(date)s

TOTAL RUNNING INSTANCES: %(tirunning)d (out of %(ti)d)


* State Distribution:
%(states)s

* AMI Distribution:
%(images)s
"""

def generate_report(conn):
    # Enumerating all instances
    inst = []
    for r in conn.get_all_instances():
        inst.extend(r.instances)

    # Setting up the possible states list
    inst_states = set([i.state for i in inst])
    inst_states.add("running")
    inst_states.add("stopped")
    inst_states.add("terminated")

    # Active images
    image_ids = set([i.image_id for i in inst] + [img.id for img in conn.get_all_images(owners=["self"])])
    images = conn.get_all_images(list(image_ids))

    # The Amazon account ID
    account = conn.get_all_images(owners=["self"])[0].owner_id

    return REPORT % {
        "account": account,
        "date": datetime.today().strftime("%A, %d %b %Y, %H:%M"),
        "tirunning": len(filter(lambda i: i.state == "running", inst)),
        "ti": len(inst),
        "states": "\n".join([" %-12s: %3d" % (s.capitalize(),
                                   len(filter(lambda i: i.state == s, inst)))
                             for s in sorted(inst_states)]),
        "images": "\n".join([" %s: %3d (%3d running) %s" % (img.id,
                                   len(filter(lambda i: i.image_id == img.id, inst)),
                                   len(filter(lambda i: i.image_id == img.id and i.state == "running", inst)),
                                   ("- %s" % img.name) if img.name else "")
                             for img in sorted(images, key=lambda img: img.id)])
        }

def main():
    # Checking environment variables
    if not "AWS_ACCESS_KEY_ID" in os.environ or \
            not "AWS_SECRET_ACCESS_KEY" in os.environ:
        logging.error("Environment variables AWS_ACCESS_KEY_ID and AWS_SECRET_ACCESS_KEY not set.")
        return 1

    if not "AWS_REGION" in os.environ:
        logging.warning("AWS_REGION variable not set. Defaulting to 'us-east-1'.")

    # Parsing command line arguments

    parser = ArgumentParser(description="Check EC2 status.")
    parser.add_argument("--email", help="Send an e-mail with the status to the designated recipient.")
    parser.add_argument("--sender", default="DSLab EC2 <ec2-no-reply@dslabpc10.epfl.ch", help="E-mail sender.")
    parser.add_argument("--subject", default="[ec2] Account Activity", help="E-mail subject.")
    parser.add_argument("--smtp", default="mail.epfl.ch", help="SMTP server name.")

    args = parser.parse_args()

    region = filter(lambda region: region.name == os.environ.get("AWS_REGION", "us_east-1"),
                    boto.ec2.regions())[0]
    conn = boto.connect_ec2(region=region)

    # Enumerating all instances
    report = generate_report(conn)

    print report

    if (args.email):
        msg = MIMEText(report)
        msg["Subject"] = args.subject
        msg["From"] = args.sender
        msg["To"] = args.email
        s = smtplib.SMTP(args.smtp)
        s.sendmail(args.sender, [args.email], msg.as_string())

    return 0

if __name__ == "__main__":
    sys.exit(main())
