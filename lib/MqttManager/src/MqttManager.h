#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>

#include "DeviceConfig.h"
#include "JwtUtil.h"

class MqttManager {
public:
    enum class MqttState : uint8_t {
        IDLE = 0,
        TRY_CONNECT,
        BACKOFF,
        CONNECTED
    };

    static void begin();
    static void begin(const DeviceConfig& cfg);
    static void begin(const char* host, uint16_t port, const char* clientId);

    static void setJwtProvider(IJwtProvider* provider);
    static void setUsername(const char* user);
    static void setPassword(const char* pass);
    static void setWill(const char* topic, const char* message, bool retain = false, uint8_t qos = 0);

    static void loop();

    static bool subscribe(const char* topic, uint8_t qos = 0);
    static bool unsubscribe(const char* topic);
    static bool publish(const char* topic, const char* payload, bool retain = false);
    static bool publish(const char* topic, const uint8_t* payload, size_t len, bool retain = false);

    static void setStatusCallback(void (*cb)(const char*));
    static void setMessageCallback(void (*cb)(const char* topic, const uint8_t* payload, unsigned int len));
    static void setStateCallback(void (*cb)(MqttState));

    static bool isConnected();
    static MqttState state();

    static void setReconnectPolicy(uint32_t minMs, uint32_t maxMs);
    static void setKeepAlive(uint16_t keepAliveSec);

private:
    struct SubItem { String topic; uint8_t qos; };
    struct OfflineItem { String topic; String payload; bool retain; };

    static void tryConnectIfNeeded();
    static void report(const char* s);
    static void setState(MqttState s);
    static bool netReady();
    static void rememberSub(const char* topic, uint8_t qos);
    static bool removeSub(const char* topic);
    static void flushOfflineQueue();
    static bool enqueueOffline(const char* topic, const char* payload, bool retain);

    static void _staticMsgCb(char* topic, uint8_t* payload, unsigned int len);

    static WiFiClient _net;
    static PubSubClient _mqtt;
    static MqttState _state;
    static DeviceConfig _cfg;

    static String _host, _clientId, _user, _pass;
    static uint16_t _port;

    static IJwtProvider* _jwt;
    static JwtProviderHS256 _jwtLocal;
    static String _jwtCached;
    static bool _jwtEnabled;

    static String _willTopic, _willMsg;
    static bool _willRetain;
    static uint8_t _willQos;
    static bool _willSet;

    static void (*_statusCb)(const char*);
    static void (*_msgCb)(const char*, const uint8_t*, unsigned int);
    static void (*_stateCb)(MqttState);

    static uint32_t _reconnectMin, _reconnectMax, _reconnectDelay, _lastAttemptMs;

    static constexpr size_t MAX_SUBS = 16;
    static SubItem _subs[MAX_SUBS];
    static size_t _subsCount;

    static constexpr size_t OFFLINE_Q_MAX = 32;
    static OfflineItem _offlineQ[OFFLINE_Q_MAX];
    static size_t _offlineHead;
    static size_t _offlineCount;
};
