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

INSTANCE_TYPES = {
    "m1.small": ("Small", 0.085),
    "m1.large": ("Large", 0.34),
    "m1.xlarge": ("Extra Large", 0.68),
    "t1.micro": ("Micro", 0.02),
    "m2.xlarge": ("High-Memory Extra Large", 0.50),
    "m2.2xlarge": ("High-Memory Double Extra Large", 1.00),
    "m2.4xlarge": ("High-Memory Quadruple Extra Large", 2.00),
    "c1.medium": ("High-CPU Medium", 0.17),
    "c1.xlarge": ("High-CPU Extra Large", 0.68),
    "cc1.4xlarge": ("Cluster Compute Quadruple Extra Large", 1.60),
    "cg1.4xlarge": ("Cluster GPU Quadruple Extra Large", 2.10)
}

REPORT = """
EC2 Account: %(account)s
Date: %(date)s

TOTAL RUNNING INSTANCES: %(tirunning)d (out of %(ti)d)
INSTANT COST: $%(cost).2f/hour

* State Distribution:
%(states)s

* Type Distribution:
%(types)s

* AMI Distribution:
%(images)s
"""

class EC2Report:
    def __init__(self, text, tinst, trunning, tcost):
        self.text = text
        self.total_inst = tinst
        self.total_running = trunning
        self.total_cost = tcost

def generate_report(conn, aggressive=False):
    # Enumerating all instances
    inst = []
    for r in conn.get_all_instances():
        inst.extend(r.instances)

    if not aggressive and not len(filter(lambda i: i.state == "running", inst)):
        return None

    # Setting up the possible states list
    inst_states = set([i.state for i in inst])
    inst_states.add("running")
    inst_states.add("stopped")
    inst_states.add("terminated")

    # Active instance types
    inst_types = set([i.instance_type for i in inst])

    # Active images
    image_ids = set([i.image_id for i in inst] + [img.id for img in conn.get_all_images(owners=["self"])])
    images = conn.get_all_images(list(image_ids))

    # The Amazon account ID
    account = conn.get_all_images(owners=["self"])[0].owner_id

    # Total costs
    costs = sum([INSTANCE_TYPES.get(ty, (ty, 0.0))[1] 
                     for ty in [i.instance_type 
                                for i in inst if i.state == "running"]])

    text = REPORT % {
        "account": account,
        "date": datetime.today().strftime("%A, %d %b %Y, %H:%M"),
        "tirunning": len(filter(lambda i: i.state == "running", inst)),
        "ti": len(inst),
        "cost": costs,
        "states": "\n".join([" %-12s: %3d" % (s.capitalize(),
                                   len(filter(lambda i: i.state == s, inst)))
                             for s in sorted(inst_states)]),
        "types": "\n".join([" %-12s: %3d (%3d running)" % (INSTANCE_TYPES.get(ty, (ty, 0.0))[0],
                                   len(filter(lambda i: i.instance_type == ty, inst)),
                                   len(filter(lambda i: i.instance_type == ty and i.state == "running", inst)))
                            for ty in sorted(inst_types)]),
        "images": "\n".join([" %s: %3d (%3d running) %s" % (img.id,
                                   len(filter(lambda i: i.image_id == img.id, inst)),
                                   len(filter(lambda i: i.image_id == img.id and i.state == "running", inst)),
                                   ("- %s" % img.name) if img.name else "")
                             for img in sorted(images, key=lambda img: img.id)])
        }

    return EC2Report(text, len(inst), 
                     len(filter(lambda i: i.state == "running", inst)),
                     costs)

def main():
    logging.basicConfig(level=logging.INFO)
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
    parser.add_argument("--sender", default="EC2 <ec2-no-reply@example.com>", help="E-mail sender.")
    parser.add_argument("--subject", default="[ec2] Account Activity", help="E-mail subject.")
    parser.add_argument("--smtp", default="mail.example.com", help="SMTP server name.")
    parser.add_argument("--aggressive", default=False, action="store_true", help="Send e-mail even with 0 running instances.")

    args = parser.parse_args()

    region = filter(lambda region: region.name == os.environ.get("AWS_REGION", "us_east-1"),
                    boto.ec2.regions())[0]
    conn = boto.connect_ec2(region=region)

    # Enumerating all instances
    report = generate_report(conn, aggressive=args.aggressive)

    if not report:
        logging.info("No running instances, aborting.")
        return 0

    print report.text

    if (args.email):
        msg = MIMEText(report.text)
        msg["Subject"] = "%s: %d Instance%s Running" % (args.subject, report.total_running, 
                                                        "s" if report.total_running != 1 else "")
        msg["From"] = args.sender
        msg["To"] = args.email
        s = smtplib.SMTP(args.smtp)
        s.sendmail(args.sender, [args.email], msg.as_string())
        logging.info("E-mail sent to %s." % args.email)

    return 0

if __name__ == "__main__":
    sys.exit(main())
