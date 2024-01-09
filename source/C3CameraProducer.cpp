/**
 * Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0.
 */
#include <string>
#include <algorithm>
#include <condition_variable>
#include <iostream>
#include <vector>
#include <sstream>
#include <mutex>
#include <thread>

#include <aws/crt/Api.h>
#include <aws/crt/JsonObject.h>
#include <aws/crt/UUID.h>
#include <aws/crt/io/HostResolver.h>
#include <aws/iot/MqttClient.h>
#ifndef COMMANDLINE_UTIL_H
#define COMMANDLINE_UTIL_H
#include "utils/CommandLineUtils.h"
#endif // COMMANDLINE_UTIL_H

#include "ProducerSink.h"
// #include "Servo.h"
#include "Logger.h"

LOGGER_TAG("main")

//======================================================================================================================
/// IoT device SDK
using namespace Aws::Crt;




//======================================================================================================================
int main(int argc, char **argv)
{
    LOG_CONFIGURE("../kvs_log_configuration");

    /* ------------------------------------------------ */
    // Do the global initialization for the API.
    ApiHandle apiHandle;
    Utils::cmdData cmdData = Utils::parseSampleInputShadow(argc, argv, &apiHandle);


    /**
     * cmdData is the arguments/input from the command line placed into a single struct for
     * use in this sample. This handles all of the command line parsing, validating, etc.
     * See the Utils/CommandLineUtils for more information.
     */

    // Parse shadowPropery and store it to vector

    /* ------------------------------------------------ */
    /// stream to KVS
    int ret;
    // global data
    KVSCustomData kvsdata = {0};
    /* init GStreamer */
    gst_init(&argc, &argv);

    /* build gstreamer pipeline and start */
    ret = gst_init_resources_kvs(&kvsdata, &cmdData);
    if (ret != 0)
    {
        LOG_FATAL("Unable to start pipeline.");
        return 1;
    }

    // Start the appsink process thread
    std::thread thread_bus([&kvsdata]() -> void
                           { code_thread_bus(kvsdata.pipeline, &kvsdata, "RPI"); });

    /* ------------------------------------------------ */
    /// device shadow
    // Create the MQTT builder and populate it with data from cmdData.
    auto clientConfigBuilder =
        Aws::Iot::MqttClientConnectionConfigBuilder(cmdData.input_cert.c_str(), cmdData.input_key.c_str());
    clientConfigBuilder.WithEndpoint(cmdData.input_endpoint);
    if (cmdData.input_ca != "")
    {
        clientConfigBuilder.WithCertificateAuthority(cmdData.input_ca.c_str());
    }
    if (cmdData.input_proxyHost != "")
    {
        Aws::Crt::Http::HttpClientConnectionProxyOptions proxyOptions;
        proxyOptions.HostName = cmdData.input_proxyHost;
        proxyOptions.Port = static_cast<uint16_t>(cmdData.input_proxyPort);
        proxyOptions.AuthType = Aws::Crt::Http::AwsHttpProxyAuthenticationType::None;
        clientConfigBuilder.WithHttpProxyOptions(proxyOptions);
    }
    if (cmdData.input_port != 0)
    {
        clientConfigBuilder.WithPortOverride(static_cast<uint16_t>(cmdData.input_port));
    }

    // Create the MQTT connection from the MQTT builder
    auto clientConfig = clientConfigBuilder.Build();
    if (!clientConfig)
    {
        fprintf(
            stderr,
            "Client Configuration initialization failed with error %s\n",
            Aws::Crt::ErrorDebugString(clientConfig.LastError()));
        LOG_FATAL("[DEVICE] Client Configuration initialization failed with error " << ErrorDebugString(clientConfig.LastError()));
        exit(-1);
    }
    Aws::Iot::MqttClient client = Aws::Iot::MqttClient();
    auto connection = client.NewConnection(clientConfig);
    if (!*connection)
    {
        fprintf(
            stderr,
            "MQTT Connection Creation failed with error %s\n",
            Aws::Crt::ErrorDebugString(connection->LastError()));
        LOG_FATAL("[DEVICE] MQTT Connection Creation failed with error " << ErrorDebugString(connection->LastError()));
        exit(-1);
    }

    /**
     * In a real world application you probably don't want to enforce synchronous behavior
     * but this is a sample console application, so we'll just do that with a condition variable.
     */
    std::promise<bool> connectionCompletedPromise;
    std::promise<void> connectionClosedPromise;

    // Invoked when a MQTT connect has completed or failed
    auto onConnectionCompleted =
        [&](Aws::Crt::Mqtt::MqttConnection &, int errorCode, Aws::Crt::Mqtt::ReturnCode returnCode, bool)
    {
        if (errorCode)
        {
            LOG_FATAL("[DEVICE] Connection failed with error " << ErrorDebugString(errorCode));
            connectionCompletedPromise.set_value(false);
        }
        else
        {
            LOG_INFO("[DEVICE] Connection completed with return code " << returnCode);
            connectionCompletedPromise.set_value(true);
        }
    };

    // Invoked when a disconnect message has completed.
    auto onDisconnect = [&](Aws::Crt::Mqtt::MqttConnection &)
    {
        LOG_INFO("[DEVICE] Disconnect completed");
        connectionClosedPromise.set_value();
    };

    // Assign callbacks
    connection->OnConnectionCompleted = std::move(onConnectionCompleted);
    connection->OnDisconnect = std::move(onDisconnect);

    // Connect
    LOG_INFO("[DEVICE] Connecting...");
    if (!connection->Connect(cmdData.input_clientId.c_str(), true, 0))
    {
        LOG_FATAL("[DEVICE] MQTT Connection failed with error " << ErrorDebugString(connection->LastError()));
        exit(-1);
    }

    if (connectionCompletedPromise.get_future().get())
    {
    }

    // Disconnect
    if (connection->Disconnect())
    {
        connectionClosedPromise.get_future().wait();
    }
    /* ------------------------------------------------ */
    // Wait for threads
    // thread_bus.join();

    /* free gstreamer resources */
    gst_free_resources(kvsdata.pipeline);

    return 0;
}