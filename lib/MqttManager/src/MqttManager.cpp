#include "MqttManager.h"

#include "ConfigManager.h"
#include "LogManager.h"
#include "NetManager.h"

WiFiClient MqttManager::_net;
PubSubClient MqttManager::_mqtt(_net);
MqttManager::MqttState MqttManager::_state = MqttState::IDLE;
DeviceConfig MqttManager::_cfg;

String MqttManager::_host, MqttManager::_clientId, MqttManager::_user, MqttManager::_pass;
uint16_t MqttManager::_port = 1883;

IJwtProvider* MqttManager::_jwt = nullptr;
JwtProviderHS256 MqttManager::_jwtLocal;
String MqttManager::_jwtCached;
bool MqttManager::_jwtEnabled = false;

String MqttManager::_willTopic, MqttManager::_willMsg;
bool MqttManager::_willRetain = false;
uint8_t MqttManager::_willQos = 0;
bool MqttManager::_willSet = false;

void (*MqttManager::_statusCb)(const char*) = nullptr;
void (*MqttManager::_msgCb)(const char*, const uint8_t*, unsigned int) = nullptr;
void (*MqttManager::_stateCb)(MqttState) = nullptr;

uint32_t MqttManager::_reconnectMin = 1000;
uint32_t MqttManager::_reconnectMax = 10000;
uint32_t MqttManager::_reconnectDelay = MqttManager::_reconnectMin;
uint32_t MqttManager::_lastAttemptMs = 0;

MqttManager::SubItem MqttManager::_subs[MqttManager::MAX_SUBS];
size_t MqttManager::_subsCount = 0;

MqttManager::OfflineItem MqttManager::_offlineQ[MqttManager::OFFLINE_Q_MAX];
size_t MqttManager::_offlineHead = 0;
size_t MqttManager::_offlineCount = 0;

void MqttManager::report(const char* s) {
    if (_statusCb) _statusCb(s);
    Serial.println(s);
}

void MqttManager::setState(MqttState s) {
    if (_state == s) return;
    _state = s;
    if (_stateCb) _stateCb(_state);

    switch (_state) {
        case MqttState::IDLE:       LogManager::push("MQTT", "IDLE"); break;
        case MqttState::TRY_CONNECT:LogManager::push("MQTT", "TRY_CONNECT"); break;
        case MqttState::BACKOFF:    LogManager::push("MQTT", "BACKOFF"); break;
        case MqttState::CONNECTED:  LogManager::push("MQTT", "CONNECTED"); break;
    }
}

bool MqttManager::netReady() {
    return NetManager::isConnected();
}

void MqttManager::_staticMsgCb(char* topic, uint8_t* payload, unsigned int len) {
    if (_msgCb) _msgCb(topic, payload, len);
}

void MqttManager::begin() {
    DeviceConfig cfg;
    ConfigManager::begin();
    if (!ConfigManager::loadDeviceConfig(cfg)) {
        ConfigManager::saveDeviceConfig(cfg);
    }
    begin(cfg);
}

void MqttManager::begin(const DeviceConfig& cfg) {
    _cfg = cfg;

    _subsCount = 0;
    _offlineHead = 0;
    _offlineCount = 0;
    _willSet = false;
    _willTopic = "";
    _willMsg = "";

    _host = _cfg.mqttHost;
    _port = _cfg.mqttPort ? _cfg.mqttPort : 1883;
    _clientId = _cfg.mqttClientId.isEmpty() ? "morselink-esp32s3" : _cfg.mqttClientId;
    _user = _cfg.mqttUser;
    _pass = _cfg.mqttPass;

    _jwtEnabled = false;
    _jwt = nullptr;
    _jwtCached = "";

    _mqtt.setServer(_host.c_str(), _port);
    _mqtt.setCallback(&_staticMsgCb);
    _mqtt.setBufferSize(512);
    _mqtt.setSocketTimeout(1);

    setKeepAlive(15);
    setReconnectPolicy(1000, 10000);

    if (_willTopic.isEmpty()) {
        String t = "devices/" + _clientId + "/status";
        setWill(t.c_str(), "offline", true, 1);
    }

    String cmd = "devices/" + _clientId + "/cmd";
    subscribe(cmd.c_str(), 0);

    if (_host.isEmpty()) {
        setState(MqttState::IDLE);
        report("[MQTT] disabled: empty host");
        return;
    }

    setState(MqttState::TRY_CONNECT);
    report("[MQTT] initialized from config");
}

void MqttManager::begin(const char* host, uint16_t port, const char* clientId) {
    DeviceConfig cfg;
    ConfigManager::begin();
    (void)ConfigManager::loadDeviceConfig(cfg);

    cfg.mqttHost = host ? host : "";
    cfg.mqttPort = port;
    if (clientId) cfg.mqttClientId = clientId;
    begin(cfg);
}

void MqttManager::setJwtProvider(IJwtProvider* provider) {
    _jwt = provider;
    _jwtEnabled = (_jwt != nullptr);
}

void MqttManager::setUsername(const char* user) {
    _user = user ? user : "";
}

void MqttManager::setPassword(const char* pass) {
    _pass = pass ? pass : "";
}

void MqttManager::setWill(const char* t, const char* m, bool r, uint8_t q) {
    _willSet = (t && m);
    if (_willSet) {
        _willTopic = t;
        _willMsg = m;
        _willRetain = r;
        _willQos = q;
    }
}

void MqttManager::setStatusCallback(void (*cb)(const char*)) {
    _statusCb = cb;
}

void MqttManager::setMessageCallback(void (*cb)(const char*, const uint8_t*, unsigned int)) {
    _msgCb = cb;
}

void MqttManager::setStateCallback(void (*cb)(MqttState)) {
    _stateCb = cb;
}

bool MqttManager::isConnected() {
    return _mqtt.connected();
}

MqttManager::MqttState MqttManager::state() {
    return _state;
}

void MqttManager::setReconnectPolicy(uint32_t minMs, uint32_t maxMs) {
    if (minMs < 100) minMs = 100;
    if (maxMs < minMs) maxMs = minMs;
    _reconnectMin = minMs;
    _reconnectMax = maxMs;
    _reconnectDelay = _reconnectMin;
}

void MqttManager::setKeepAlive(uint16_t s) {
    _mqtt.setKeepAlive(s);
}

void MqttManager::rememberSub(const char* topic, uint8_t qos) {
    for (size_t i = 0; i < _subsCount; ++i) {
        if (_subs[i].topic == topic) {
            _subs[i].qos = qos;
            return;
        }
    }
    if (_subsCount < MAX_SUBS) {
        _subs[_subsCount++] = {String(topic), qos};
    } else {
        report("[MQTT] sub list full");
    }
}

bool MqttManager::removeSub(const char* topic) {
    if (!topic || !*topic) return false;
    for (size_t i = 0; i < _subsCount; ++i) {
        if (_subs[i].topic == topic) {
            for (size_t j = i + 1; j < _subsCount; ++j) {
                _subs[j - 1] = _subs[j];
            }
            _subsCount--;
            return true;
        }
    }
    return false;
}

bool MqttManager::subscribe(const char* topic, uint8_t qos) {
    if (!topic || !*topic) return false;
    rememberSub(topic, qos);
    if (_mqtt.connected()) return _mqtt.subscribe(topic, qos);
    return true;
}

bool MqttManager::unsubscribe(const char* topic) {
    if (!topic || !*topic) return false;
    (void)removeSub(topic);
    if (_mqtt.connected()) return _mqtt.unsubscribe(topic);
    return true;
}

bool MqttManager::enqueueOffline(const char* topic, const char* payload, bool retain) {
    if (!topic || !payload) return false;

    if (_offlineCount == OFFLINE_Q_MAX) {
        _offlineHead = (_offlineHead + 1) % OFFLINE_Q_MAX;
        _offlineCount--;
        LogManager::push("MQTT", "offline queue full, drop oldest");
    }

    size_t idx = (_offlineHead + _offlineCount) % OFFLINE_Q_MAX;
    _offlineQ[idx].topic = topic;
    _offlineQ[idx].payload = payload;
    _offlineQ[idx].retain = retain;
    _offlineCount++;
    return true;
}

void MqttManager::flushOfflineQueue() {
    const uint8_t kFlushBudgetPerLoop = 3;
    uint8_t sent = 0;

    while (_mqtt.connected() && _offlineCount > 0 && sent < kFlushBudgetPerLoop) {
        OfflineItem& item = _offlineQ[_offlineHead];
        bool ok = _mqtt.publish(item.topic.c_str(), item.payload.c_str(), item.retain);
        if (!ok) {
            report("[MQTT] flush paused");
            break;
        }
        _offlineHead = (_offlineHead + 1) % OFFLINE_Q_MAX;
        _offlineCount--;
        sent++;
    }
}

bool MqttManager::publish(const char* topic, const char* payload, bool retain) {
    if (_mqtt.connected()) {
        bool ok = _mqtt.publish(topic, payload, retain);
        if (!ok) {
            char msg[128];
            snprintf(msg, sizeof(msg), "[MQTT] publish failed topic=%s payload_len=%u",
                     topic ? topic : "<null>", payload ? (unsigned)strlen(payload) : 0);
            report(msg);
        }
        return ok;
    }
    return enqueueOffline(topic, payload, retain);
}

bool MqttManager::publish(const char* topic, const uint8_t* payload, size_t len, bool retain) {
    if (!_mqtt.connected()) {
        char msg[128];
        snprintf(msg, sizeof(msg), "[MQTT] publish skipped offline topic=%s len=%u",
                 topic ? topic : "<null>", static_cast<unsigned>(len));
        report(msg);
        return false;
    }
    bool ok = _mqtt.publish(topic, payload, len, retain);
    if (!ok) {
        char msg[128];
        snprintf(msg, sizeof(msg), "[MQTT] publish failed topic=%s len=%u",
                 topic ? topic : "<null>", static_cast<unsigned>(len));
        report(msg);
    }
    return ok;
}

void MqttManager::tryConnectIfNeeded() {
    if (_host.isEmpty()) {
        setState(MqttState::IDLE);
        return;
    }

    uint32_t now = millis();
    if (now - _lastAttemptMs < _reconnectDelay) return;
    _lastAttemptMs = now;

    if (!netReady()) {
        setState(MqttState::TRY_CONNECT);
        return;
    }

    const char* user = _user.length() ? _user.c_str() : nullptr;
    const char* pass = _pass.length() ? _pass.c_str() : nullptr;

    String jwtUser;
    if (_jwtEnabled && _jwt) {
        if (_jwtCached.isEmpty() || _jwt->willExpireIn(60)) {
            _jwtCached = _jwt->issue();
        }
        jwtUser = _jwt->usernameHint();
        if (jwtUser.length()) user = jwtUser.c_str();
        pass = _jwtCached.c_str();
    }

    report("[MQTT] connecting...");
    bool ok = false;
    if (_willSet) {
        ok = _mqtt.connect(_clientId.c_str(), user, pass,
                           _willTopic.c_str(), _willQos, _willRetain, _willMsg.c_str());
    } else {
        ok = _mqtt.connect(_clientId.c_str(), user, pass);
    }

    if (ok) {
        report("[MQTT] connected");
        setState(MqttState::CONNECTED);
        _reconnectDelay = _reconnectMin;

        for (size_t i = 0; i < _subsCount; ++i) {
            if (_subs[i].topic.length()) {
                _mqtt.subscribe(_subs[i].topic.c_str(), _subs[i].qos);
            }
        }
    } else {
        char msg[96];
        snprintf(msg,
                 sizeof(msg),
                 "[MQTT] connect failed rc=%d id=%s",
                 _mqtt.state(),
                 _clientId.c_str());
        report(msg);
        setState(MqttState::BACKOFF);
        uint32_t doubled = _reconnectDelay * 2;
        _reconnectDelay = (doubled < _reconnectMax) ? doubled : _reconnectMax;
    }
}

void MqttManager::loop() {
    switch (_state) {
        case MqttState::IDLE:
            break;

        case MqttState::TRY_CONNECT:
        case MqttState::BACKOFF:
            tryConnectIfNeeded();
            break;

        case MqttState::CONNECTED:
            if (!_mqtt.connected()) {
                setState(MqttState::TRY_CONNECT);
                _reconnectDelay = _reconnectMin;
            } else {
                _mqtt.loop();
                flushOfflineQueue();
                if (_jwtEnabled && _jwt && _jwt->willExpireIn(30)) {
                    _jwtCached = _jwt->issue();
                }
            }
            break;
    }
}
