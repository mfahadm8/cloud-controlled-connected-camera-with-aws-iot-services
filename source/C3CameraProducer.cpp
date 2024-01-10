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


bool streamStarted = false;
std::mutex streamMutex;
std::condition_variable streamCondition;

// Function to start streaming
void startStream(KVSCustomData& kvsdata, Utils::cmdData& cmdData) {
    std::lock_guard<std::mutex> lock(streamMutex);
    if (!streamStarted) {
        int ret = gst_init_resources_kvs(&kvsdata, &cmdData);
        if (ret != 0) {
            LOG_FATAL("Unable to start pipeline.");
            return;
        }
        std::thread thread_bus([&kvsdata]() -> void { code_thread_bus(kvsdata.pipeline, &kvsdata, "RPI"); });
        thread_bus.detach();  // Detach the thread as we won't join it here
        streamStarted = true;
    }
}

// Function to stop streaming
void stopStream(KVSCustomData& kvsdata) {
    std::lock_guard<std::mutex> lock(streamMutex);
    if (streamStarted) {
        // Insert code to stop the stream properly
        gst_free_resources(kvsdata.pipeline);
        streamStarted = false;
    }
}


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

    // Invoked when a MQTT connection was interrupted/lost
    auto onInterrupted = [&](Aws::Crt::Mqtt::MqttConnection &, int error) {
        fprintf(stdout, "Connection interrupted with error %s\n", Aws::Crt::ErrorDebugString(error));
    };

    // Invoked when a MQTT connection was interrupted/lost, but then reconnected successfully
    auto onResumed = [&](Aws::Crt::Mqtt::MqttConnection &, Aws::Crt::Mqtt::ReturnCode, bool) {
        fprintf(stdout, "Connection resumed\n");
    };

    // Invoked when a disconnect message has completed.
    auto onDisconnect = [&](Aws::Crt::Mqtt::MqttConnection &) {
        fprintf(stdout, "Disconnect completed\n");
        connectionClosedPromise.set_value();
    };

    // Assign callbacks
    connection->OnConnectionCompleted = std::move(onConnectionCompleted);
    connection->OnDisconnect = std::move(onDisconnect);
    connection->OnConnectionInterrupted = std::move(onInterrupted);
    connection->OnConnectionResumed = std::move(onResumed);

    // Connect
    LOG_INFO("[DEVICE] Connecting...");
    if (!connection->Connect(cmdData.input_clientId.c_str(), true, 0))
    {
        LOG_FATAL("[DEVICE] MQTT Connection failed with error " << ErrorDebugString(connection->LastError()));
        exit(-1);
    }

    if (connectionCompletedPromise.get_future().get())
    {  // Update the lambda to capture cmdData by copy or reference
        std::mutex receiveMutex;
        std::condition_variable receiveSignal;
        uint32_t receivedCount = 0;

        // This is invoked upon the receipt of a Publish on a subscribed topic.
        auto onMessage = [&](Mqtt::MqttConnection &,
                             const String &topic,
                             const ByteBuf &byteBuf,
                             bool /*dup*/,
                             Mqtt::QOS /*qos*/,
                             bool /*retain*/) {
            {
                std::lock_guard<std::mutex> lock(receiveMutex);
                ++receivedCount;
                fprintf(stdout, "Publish #%d received on topic %s\n", receivedCount, topic.c_str());
                fprintf(stdout, "Message: ");
                fwrite(byteBuf.buffer, 1, byteBuf.len, stdout);
                fprintf(stdout, "\n");
            }

            receiveSignal.notify_all();
        };

        // Subscribe for incoming publish messages on topic.
        std::promise<void> subscribeFinishedPromise;
        auto onSubAck =
            [&](Mqtt::MqttConnection &, uint16_t packetId, const String &topic, Mqtt::QOS QoS, int errorCode) {
                if (errorCode)
                {
                    fprintf(stderr, "Subscribe failed with error %s\n", aws_error_debug_str(errorCode));
                    exit(-1);
                }
                else
                {
                    if (!packetId || QoS == AWS_MQTT_QOS_FAILURE)
                    {
                        fprintf(stderr, "Subscribe rejected by the broker.");
                        exit(-1);
                    }
                    else
                    {
                        fprintf(stdout, "Subscribe on topic %s on packetId %d Succeeded\n", topic.c_str(), packetId);
                    }
                }
                subscribeFinishedPromise.set_value();
            };

        connection->Subscribe("thing/kvs/start", AWS_MQTT_QOS_AT_LEAST_ONCE, onMessage, onSubAck);
        subscribeFinishedPromise.get_future().wait();
    }
    // Define onSubAck handler
    // auto onSubAck = [](Aws::Crt::Mqtt::MqttConnection&, uint16_t packetId, const Aws::Crt::String &topic, Aws::Crt::Mqtt::QOS qos, int errorCode) {
    //     if (errorCode == AWS_OP_SUCCESS) {
    //         LOG_INFO("[DEVICE] Subscription to topic " << topic << " successful with packetId " << packetId);
    //     } else {
    //         LOG_ERROR("[DEVICE] Subscription to topic " << topic << " failed with error: " << errorCode);
    //     }
    // };

    // // Define onMessageReceived with capture list
    // auto onMessageReceived = [&kvsdata, &cmdData](Aws::Crt::Mqtt::MqttConnection&, const Aws::Crt::String& topic, const Aws::Crt::ByteBuf&, bool, Aws::Crt::Mqtt::QOS, bool) {
    //         LOG_INFO("Got message");
    //     if (topic == "thingname/kvs/start") {
    //         LOG_INFO("Got message to start stream");
    //         startStream(kvsdata, cmdData); 
    //     } else if (topic == "thingname/kvs/stop") {
    //         LOG_INFO("Got message to start stream");
    //         stopStream(kvsdata);
    //     }
    // };

    // // Subscribe to topics
    // connection->Subscribe("thingname/kvs/start", Aws::Crt::Mqtt::QOS::AWS_MQTT_QOS_AT_LEAST_ONCE, onMessageReceived, onSubAck);
    // connection->Subscribe("thingname/kvs/stop", Aws::Crt::Mqtt::QOS::AWS_MQTT_QOS_AT_LEAST_ONCE, onMessageReceived, onSubAck);
    // }
    // Main loop
    // std::unique_lock<std::mutex> lock(streamMutex);
    bool running = true;
    while (running) {
        // Processing loop
        // Add a condition to set 'running' to false when needed
    }

    // Disconnect
    if (connection->Disconnect())
    {
        connectionClosedPromise.get_future().wait();
    }
  
    return 0;
}