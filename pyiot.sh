#!/bin/bash

# Set environment variables
export AWS_IOT_CORE_THING_NAME=FahadPi
export CERTS_DIR=/greengrass/v2
export AWS_DEFAULT_REGION=us-east-1
export AWS_IOT_CORE_CREDENTIAL_ENDPOINT=c2xntca5km0ddu.credentials.iot.us-east-1.amazonaws.com
export AWS_IOT_CORE_ROLE_ALIAS=GreengrassV2TokenExchangeRoleAlias
export AWS_IOT_CORE_CERT=$CERTS_DIR/thingCert.crt
export AWS_IOT_CORE_PRIVATE_KEY=$CERTS_DIR/privKey.key
export IOT_CA_CERT_PATH=$CERTS_DIR/rootCA.pem

# Run the Python script with the parameters
python pyiot.py \
--endpoint adxdww5wpip4c-ats.iot.us-east-1.amazonaws.com \
--port 8883 \
--cert $AWS_IOT_CORE_CERT \
--key $AWS_IOT_CORE_PRIVATE_KEY \
--root-ca $IOT_CA_CERT_PATH \
--client-id $AWS_IOT_CORE_THING_NAME \
--topic "test/topic" \
--message "Hello World!" \
--count 10
