#!/bin/bash

if [ -z "$1" ]; then
    echo "Please specifiy a target name."
    exit 1
fi

IMAGE_ID=ami-27ff3c4e
CORE_COUNT=2
TARGET_NAME="$1"

INSTANCE_HOSTS=$(ec2-describe-instances -F image-id=$IMAGE_ID | egrep "^INSTANCE" | cut -f 4)

INSTANCE_COUNT=$(echo $INSTANCE_HOSTS | wc -w)

TOTAL_COUNT=$((INSTANCE_COUNT * CORE_COUNT))

echo -n "$TARGET_NAME $TOTAL_COUNT "
for INSTANCE in $INSTANCE_HOSTS; do
    echo -n "$INSTANCE $CORE_COUNT "
done
echo
