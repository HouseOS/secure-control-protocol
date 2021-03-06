/*
secure-control-protocol
This is the central source file for the secure-control-protocol.

SPDX-License-Identifier: GPL-3.0-or-later

Copyright (C) 2018 Benjamin Schilling
*/

#include "SCP.h"

/**
 * Initialize the SCP library
 */
SCP::SCP()
{
    server = new ESP8266WebServer(19316);
    EEPROM.begin(512);
}

/*
 * Handle all requests on the /secure-control endpoint
 */
void SCP::handleSecureControl()
{
    scpDebug.println(scpDebug.base, "SCP.handleSecureControl: handleClient");

    String nonce = server->arg("nonce");
    scpDebug.println(scpDebug.base, "SCP::handleSecureControl - Nonce:" + nonce);
    String payload = server->arg("payload");
    scpDebug.println(scpDebug.base, "SCP::handleSecureControl - payload:" + payload);
    String payloadLength = server->arg("payloadLength");
    scpDebug.println(scpDebug.base, "SCP::handleSecureControl - payloadLength:" + payloadLength);
    String mac = server->arg("mac");
    scpDebug.println(scpDebug.base, "SCP::handleSecureControl - mac:" + mac);

    // Do input validation
    // check length of nonce
    // check for only valid base64 characters

    // whats the biggest payload? most propably wifi credentials (32 ssid, 32 pw)

    // check length of mac
    // check for only valid base64 characters

    // decrypt payload
    String password = scpPassword.readPassword();
    String decryptedPayload = scpCrypto.decodeAndDecrypt(payload, payloadLength.toInt(), password, nonce, mac);
    scpDebug.println(scpDebug.base, "SCP::handleSecureControl - decryptedPayload:" + decryptedPayload);
    if (decryptedPayload == "")
    {
        sendMalformedPayload();
        return;
    }
    // ====== disassemble payload ======

    // salt
    String salt = decryptedPayload.substring(0, decryptedPayload.indexOf(":"));
    scpDebug.println(scpDebug.base, "SCP::handleSecureControl - salt:" + salt);
    String remaining = decryptedPayload.substring(decryptedPayload.indexOf(":") + 1);
    // message type
    String messageType = remaining.substring(0, remaining.indexOf(":"));
    scpDebug.println(scpDebug.base, "SCP::handleSecureControl - messageType:" + messageType);
    remaining = remaining.substring(remaining.indexOf(":") + 1);
    //device ID
    String deviceId = remaining.substring(0, remaining.indexOf(":"));
    scpDebug.println(scpDebug.base, "SCP::handleSecureControl - deviceId:" + deviceId);
    remaining = remaining.substring(remaining.indexOf(":") + 1);

    // if device ID is invalid return
    if (!isDeviceIdValid(deviceId))
    {
        sendMalformedPayload();
        return;
    }
    // depending on message type
    if (messageType == "security-fetch-nvcn")
    {
        scpDebug.println(scpDebug.base, "SCP.handleSecurityFetchNVCN:  Handled request");
        String answer = scpResponseFactory.createResponseSecurityFetchNVCN(deviceID, scpCrypto.getNVCN());
        server->send(200, "application/json", answer);
        return;
    }
    else
    {
        // Get nvcn
        String nvcn = remaining.substring(0, remaining.indexOf(":"));
        remaining = remaining.substring(remaining.indexOf(":") + 1);
        scpDebug.println(scpDebug.base, "SCP::handleSecureControl - nvcn:" + nvcn);
        //check NVCN
        if (!isNVCNValid(nvcn))
        {
            sendMalformedPayload();
            return;
        }
        if (messageType == "security-pw-change")
        {
            scpDebug.println(scpDebug.base, "SCP::handleSecureControl - received security-pw-change");
            remaining = remaining.substring(remaining.indexOf(":") + 1);
            String newPassword = remaining.substring(0, remaining.indexOf(":"));
            scpPassword.writePassword(newPassword);
            String answer = scpResponseFactory.createResponseSecurityPwChange(this->deviceID, String(scpPassword.readCurrentPasswordNumber()), "done");
            String hmacAnswer = scpResponseFactory.createHmacResponse(answer);
            scpDebug.println(scpDebug.base, "SCP.handleSecureControl:  security-pw-change response: " + hmacAnswer);
            server->send(200, "application/json", hmacAnswer);
            return;
        }
        else if (messageType == "security-rename")
        {
            scpDebug.println(scpDebug.base, "SCP::handleSecureControl - received security-rename");
            remaining = remaining.substring(remaining.indexOf(":") + 1);
            String newName = remaining.substring(0, remaining.indexOf(":"));
            scpDeviceName.writeDeviceName(newName);
            String answer = scpResponseFactory.createResponseSecurityRename(this->deviceID, scpDeviceName.readDeviceName(), "done");
            String hmacAnswer = scpResponseFactory.createHmacResponse(answer);
            scpDebug.println(scpDebug.base, "SCP.handleSecureControl:  security-rename response: " + hmacAnswer);
            server->send(200, "application/json", hmacAnswer);
            return;
        }
        else if (messageType == "security-wifi-config")
        {
            scpDebug.println(scpDebug.base, "SCP::handleSecureControl - received security-wifi-config");
            String ssid = remaining.substring(0, remaining.indexOf(":"));
            remaining = remaining.substring(remaining.indexOf(":") + 1);
            String preSharedKey = remaining.substring(0, remaining.indexOf(":"));

            scpDebug.println(scpDebug.base, "SCP.handleSecureControl:  security-wifi-config: ssid: " + ssid);
            scpDebug.println(scpDebug.base, "SCP.handleSecureControl:  security-wifi-config: preSharedKey: " + preSharedKey);

            //try to connect to wifi
            wifiMulti.addAP(ssid.c_str(), preSharedKey.c_str());
            for (uint8_t tries = 0; tries < 40; tries++)
            {
                if (wifiMulti.run() != WL_CONNECTED)
                {
                    scpDebug.println(scpDebug.base, "SCP::handleSecureControl - Try to connect to Wifi, try: " + String(tries) + "/40");
                    delay(1000);
                }
                else
                {
                    break;
                }
            }
            String answer = "";
            if (wifiMulti.run() == WL_CONNECTED)
            {
                scpDebug.println(scpDebug.base, "SCP::handleSecureControl - Connected to wifi.");
                WiFi.disconnect();
                //if successful store credentials
                scpEepromController.setWifiSSID(ssid);
                scpEepromController.setWifiPassword(preSharedKey);
                scpEepromController.setAreWifiCredentialsSet();
                answer = scpResponseFactory.createResponseSecurityWifiConfig(this->deviceID, "success");
            }
            else
            {
                scpDebug.println(scpDebug.base, "SCP::handleSecureControl - Failed connecting to wifi.");
                //send failed response
                answer = scpResponseFactory.createResponseSecurityWifiConfig(this->deviceID, "error");
            }
            String hmacAnswer = scpResponseFactory.createHmacResponse(answer);
            scpDebug.println(scpDebug.base, "SCP.handleSecureControl:  security-wifi-config response: " + hmacAnswer);
            server->send(200, "application/json", hmacAnswer);
            return;
        }
        else if (messageType == "security-reset-to-default")
        {
            String answer = scpResponseFactory.createResponseSecurityResetToDefault(this->deviceID, "success");
            String hmacAnswer = scpResponseFactory.createHmacResponse(answer);
            scpDebug.println(scpDebug.base, "SCP.handleSecureControl:  security-reset-to-default response: " + hmacAnswer);
            server->send(200, "application/json", hmacAnswer);
            delay(1000);
            scpEepromController.resetToDefault();
            ESP.restart();
            return;
        }
        else if (messageType == "security-restart")
        {
            String answer = scpResponseFactory.createResponseSecurityRestart(this->deviceID, "success");
            String hmacAnswer = scpResponseFactory.createHmacResponse(answer);
            scpDebug.println(scpDebug.base, "SCP.handleSecureControl:  security-restart response: " + hmacAnswer);
            server->send(200, "application/json", hmacAnswer);
            delay(1000);
            ESP.restart();
            return;
        }
        else if (messageType == "control")
        {
            scpDebug.println(scpDebug.base, "SCP.handleSecureControl: received control");
            String action = remaining.substring(0, remaining.indexOf(":"));
            controlFunction(action);

            String answer = scpResponseFactory.createResponseControl(this->deviceID, action, "success");
            String hmacAnswer = scpResponseFactory.createHmacResponse(answer);
            scpDebug.println(scpDebug.base, "SCP.handleSecureControl:  control response: " + hmacAnswer);
            server->send(200, "application/json", hmacAnswer);
            return;
        }
        else if (messageType == "measure")
        {
            scpDebug.println(scpDebug.base, "SCP.handleSecureControl: received measure");
            String action = remaining.substring(0, remaining.indexOf(":"));
            double measureValue = measureFunction(action);
            String answer = scpResponseFactory.createResponseMeasure(this->deviceID, action, measureValue, "success");
            String hmacAnswer = scpResponseFactory.createHmacResponse(answer);
            scpDebug.println(scpDebug.base, "SCP.handleSecureControl:  measure response: " + hmacAnswer);
            server->send(200, "application/json", hmacAnswer);
            return;
        }
        else
        {
            sendMalformedPayload();
            return;
        }
    }
    sendMalformedPayload();
}

/*
 * Handle all requests on the /secure-control-discover endpoint
 */
void SCP::handleDiscoverHello()
{
    scpDebug.println(scpDebug.base, "SCP.handleDiscoverHello: Message: DiscoverHello");

    String payload = server->arg("payload");

    scpDebug.println(scpDebug.base, "SCP.handleDiscoverHello:  Payload:" + payload);

    // handle discover-hello message
    if (payload.equals("discover-hello"))
    {
        String currentPasswordNumber = String(scpPassword.readCurrentPasswordNumber());
        String answer = scpResponseFactory.createResponseDiscoverHello(deviceID, deviceType, scpDeviceName.readDeviceName(), controlActions, measureActions, currentPasswordNumber);
        server->send(200, "application/json", answer);

        scpDebug.println(scpDebug.base, "SCP.handleDiscoverHello:  discover-response send: " + answer);
    }
    else
    {
        sendMalformedPayload();
    }

    scpDebug.println(scpDebug.base, "SCP.handleDiscoverHello:  Message End: DiscoverHello");
}

// Respond with an error when a malformed payload is detected
void SCP::sendMalformedPayload()
{
    scpDebug.println(scpDebug.base, "Error: MalformedPayload");

    String message = "Malformed payload\n\n";
    for (uint8_t i = 0; i < server->args(); i++)
    {
        message += " " + server->argName(i) + ": " + server->arg(i) + "\n";
    }

    server->send(404, "text/plain", message);

    scpDebug.println(scpDebug.base, "Error End: MalformedPayload");
}

// Respond to unknown endpoints
void SCP::handleNotFound()
{
    scpDebug.println(scpDebug.base, "Error: HandleNotFound");
    String message = "File Not Found\n\n";
    message += "URI: ";
    message += server->uri();
    message += "\nMethod: ";
    message += (server->method() == HTTP_GET) ? "GET" : "POST";
    message += "\nArguments: ";
    message += server->args();
    message += "\n";
    for (uint8_t i = 0; i < server->args(); i++)
    {
        message += " " + server->argName(i) + ": " + server->arg(i) + "\n";
    }
    server->send(404, "text/plain", message);
    scpDebug.println(scpDebug.base, "Error End: HandleNotFound");
}

// Wrapper function for the server
void SCP::handleClient() { server->handleClient(); }

// Set the device to provisioning mode
void SCP::provisioningMode()
{
    // Get wifi ssid
    String ssid = this->deviceType + "-" + WiFi.macAddress();
    ssid.replace(":", "");
    String password = "1234567890123456";

    // Set Wifi persistent to false,
    // otherwise on every WiFi.begin the
    // ssid and password will be written to
    // the same area in the flash which will
    // destroy the device in the long run
    // See: https://github.com/esp8266/Arduino/issues/1054
    WiFi.persistent(false);
    WiFi.softAP(ssid, password);
    scpDebug.print(scpDebug.base, "SCP.provisioningMode: Open Wifi: ");
    scpDebug.println(scpDebug.base, ssid);
    scpDebug.print(scpDebug.base, "SCP.provisioningMode: Password: ");
    scpDebug.println(scpDebug.base, password);
}

// Set the device to control mode
void SCP::controlMode()
{
    String wifiSSID = scpEepromController.getWifiSSID();
    String wifiPassword = scpEepromController.getWifiPassword();

    // Set Wifi persistent to false,
    // otherwise on every WiFi.begin the
    // ssid and password will be written to
    // the same area in the flash which will
    // destroy the device in the long run
    // See: https://github.com/esp8266/Arduino/issues/1054
    WiFi.persistent(false);
    WiFi.mode(WIFI_STA);
    WiFi.begin(wifiSSID, wifiPassword);

    // Wait until connection is established
    scpDebug.print(scpDebug.base, "SCP.controlMode: Connecting to Wifi:");
    scpDebug.println(scpDebug.base, wifiSSID);
    while (WiFi.status() != WL_CONNECTED)
    {
        delay(500);
        scpDebug.println(scpDebug.base, "SCP.controlMode: Connecting...");
        //digitalWrite(D5, !digitalRead(D5));
    }

    scpDebug.print(scpDebug.base, "SCP.controlMode: Connected to Wifi:");
    scpDebug.println(scpDebug.base, wifiSSID);
    scpDebug.print(scpDebug.base, "SCP.controlMode: IP address:");
    String ipAddress = WiFi.localIP().toString();
    scpDebug.println(scpDebug.base, ipAddress);
}

/* 
 * Initialize the library
 */
void SCP::init(String deviceType, String controlActions, String measureActions)
{
    // Set the device type
    this->deviceType = deviceType;

    // Set the supported actions
    this->controlActions = controlActions;
    this->measureActions = measureActions;

    // If the default password was not set, set it now
    if (!scpPassword.isDefaultPasswordSetOnce())
    {
        scpDebug.println(scpDebug.base, "SCP.init: password not set, initializing default password");
        scpPassword.setDefaultPassword();
    }

    // If the device id is not set, set it now
    if (!scpDeviceID.isDeviceIDSet())
    {
        scpDebug.println(scpDebug.base, "SCP.init: device ID not set, setting device ID");
        scpDeviceID.setDeviceID();
    }

    // read the device id
    deviceID = scpDeviceID.readDeviceID();

    scpDebug.println(scpDebug.base, "SCP.init: DeviceID: " + deviceID);

    // if the default password is set or no wifi credentials are set,
    // go to provisioning mode, otherwise go to control mode
    if (scpPassword.readPassword() == scpPassword.DEFAULT_PW && !scpEepromController.areWifiCredentialsSet())
    {
        scpDebug.println(scpDebug.base, "SCP.init: Default password set and no Wifi Credentials available, going to provisioning mode.");
        provisioningMode();
    }
    else if (scpPassword.readPassword() != scpPassword.DEFAULT_PW && !scpEepromController.areWifiCredentialsSet())
    {
        scpDebug.println(scpDebug.base, "SCP.init: New password set but no Wifi Credentials available, going to provisioning mode.");
        scpPassword.setDefaultPassword();
        provisioningMode();
    }
    else
    {
        controlMode();
    }

    server->on("/secure-control", std::bind(&SCP::handleSecureControl, this));
    server->on("/secure-control/discover-hello",
               std::bind(&SCP::handleDiscoverHello, this));
    server->onNotFound(std::bind(&SCP::handleNotFound, this));
    server->begin();

    scpDebug.println(scpDebug.base, "SCP.init: HTTP server started");

    scpDebug.println(scpDebug.base, "SCP.init: SCP initialized");
}

/*
 *
 */
void SCP::registerControlFunction(std::function<void(String)> fun)
{
    controlFunction = fun;
}
void SCP::registerMeasureFunction(std::function<double(String)> fun)
{
    measureFunction = fun;
}

/*
 * Check whether a supplied device ID matches
 * the deviceId of the device
 */
bool SCP::isDeviceIdValid(String devId)
{
    if (devId.equals(this->deviceID))
    {
        return true;
    }
    else
    {
        return false;
    }
}

/*
 * Check whether a supplied NVCN
 * the NVCN of the device
 */
bool SCP::isNVCNValid(String nvcn)
{
    if (scpCrypto.checkNVCN(nvcn))
    {
        return true;
    }
    else
    {
        return false;
    }
}