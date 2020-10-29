/*
secure-control-protocol
This is the central header file for the secure-control-protocol.

SPDX-License-Identifier: GPL-3.0-or-later

Copyright (C) 2018 Benjamin Schilling
*/

#ifndef SCP_h
#define SCP_h

#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <ESP8266WiFiMulti.h>
#include <ESP8266WebServer.h>
#include "ScpDeviceID.h"
#include "ScpDeviceName.h"
#include "ScpPassword.h"
#include "ScpCrypto.h"
#include "ScpResponseFactory.h"
#include "ScpEepromController.h"

#include "ScpDebug.h"

class SCP
{
public:
  /**
   * @brief Construct a new SCP object
   * 
   */
  SCP();

  /**
   * @brief 
   * 
   */
  void init(String deviceType, uint8_t numberOfActions, char *actions[]);

  /**
   * @brief 
   * 
   */
  void handleClient();

  void registerControlFunction(std::function<void(String)> fun);

private:
  ScpPassword scpPassword;
  ScpDeviceID scpDeviceID;
  ScpDeviceName scpDeviceName;
  ScpCrypto scpCrypto;
  ScpResponseFactory scpResponseFactory;
  ScpEepromController scpEepromController;
  ScpDebug scpDebug;

  String deviceID = "";
  String deviceType = "";
  ESP8266WebServer *server;
  ESP8266WiFiMulti wifiMulti;
  String DEFAULT_PW = "1234567890123456";
  uint8_t numberOfActions;
  char *actions[];

  std::function<void(String)> controlFunction;

  // Helpers
  bool isDeviceIdValid(String devId);

  bool isNVCNValid(String nvcn);

  // HTTP Endpoints
  /**
   * @brief 
   * 
   */
  void handleSecureControl();

  /**
   * @brief 
   * 
   */
  void handleDiscoverHello();

  /**
   * @brief 
   * 
   */
  void handleSecurityFetchNVCN();

  /**
   * @brief 
   * 
   */
  void handleNotFound();
  // HTTP Error Methods

  /**
   * @brief 
   * 
   */
  void sendMalformedPayload();

  // State Handling

  /**
   * @brief 
   * 
   */
  void provisioningMode();

  /**
   * @brief 
   * 
   */
  void controlMode();
};

#endif
