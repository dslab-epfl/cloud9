#!/bin/bash

git archive --format=tar --prefix=cloud9/ HEAD | gzip >cloud9.tar.gz