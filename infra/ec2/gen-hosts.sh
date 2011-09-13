#!/bin/bash

# Remote configuration
IMAGE_ID=ami-27ff3c4e
CORE_COUNT=2
C9_ROOT=/home/ubuntu/cloud9
USER_NAME=ubuntu
EXP_DIR=/var/cloud9/data
TARGETS_DIR=/var/cloud9/targets

# Local configuration
LOCAL_HOSTNAME=$(ec2metadata --public-hostname)
LOCAL_C9_ROOT=/home/ubuntu/cloud9
LOCAL_EXP_DIR=/var/cloud9/data

INSTANCE_HOSTS=$(ec2-describe-instances -F image-id=$IMAGE_ID | egrep "^INSTANCE" | cut -f 4)

echo "# <Hostname> <# of cores> <Cloud9 root> <Username> <Exp. dir> <Targets root>"

for INSTANCE_HOST in $INSTANCE_HOSTS
do
    if [ "$INSTANCE_HOST" = "$LOCAL_HOSTNAME" ]; then
	continue
    fi
    echo "$INSTANCE_HOST $CORE_COUNT $C9_ROOT $USER_NAME $EXP_DIR $TARGETS_DIR"
done
echo "# Local host"
echo "$LOCAL_HOSTNAME 0 $LOCAL_C9_ROOT - $LOCAL_EXP_DIR -"