#include "JwtUtil.h"
#include <Arduino.h>
#include "mbedtls/md.h"   // ESP32 Arduino 自带 mbedtls

namespace {

String base64UrlEncode(const uint8_t* data, size_t len) {
    // 先普通 base64，再做 URL 安全替换
    String out = "";
    size_t encodedLen = 4 * ((len + 2) / 3);
    out.reserve(encodedLen + 1);

    const char* base64Chars =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        "abcdefghijklmnopqrstuvwxyz"
        "0123456789+/";

    uint32_t temp = 0;
    int bits = 0;

    for (size_t i = 0; i < len; ++i) {
        temp = (temp << 8) | data[i];
        bits += 8;
        while (bits >= 6) {
            bits -= 6;
            out += base64Chars[(temp >> bits) & 0x3F];
        }
    }

    if (bits > 0) {
        temp <<= (6 - bits);
        out += base64Chars[temp & 0x3F];
    }

    // 补 '='
    while (out.length() % 4) out += '=';

    // 转成 base64url：+ → -, / → _ ，去掉 '='
    out.replace("+", "-");
    out.replace("/", "_");
    while (out.endsWith("=")) out.remove(out.length() - 1);

    return out;
}

String base64UrlEncode(const String& s) {
    return base64UrlEncode((const uint8_t*)s.c_str(), s.length());
}

// 使用 mbedtls 做 HMAC-SHA256
String hmacSha256Base64Url(const String& key, const String& data) {
    const mbedtls_md_info_t* mdInfo = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    if (!mdInfo) return "";

    unsigned char hmac[32];
    memset(hmac, 0, sizeof(hmac));

    mbedtls_md_context_t ctx;
    mbedtls_md_init(&ctx);
    if (mbedtls_md_setup(&ctx, mdInfo, 1) != 0) {
        mbedtls_md_free(&ctx);
        return "";
    }

    mbedtls_md_hmac_starts(&ctx,
                           (const unsigned char*)key.c_str(),
                           key.length());
    mbedtls_md_hmac_update(&ctx,
                           (const unsigned char*)data.c_str(),
                           data.length());
    mbedtls_md_hmac_finish(&ctx, hmac);

    mbedtls_md_free(&ctx);

    return base64UrlEncode(hmac, sizeof(hmac));
}

} // namespace

namespace JwtUtil {

String buildJwt(const String& callsign,
                const String& userId,
                const String& passwordSecret)
{
    // 1. header
    String header = R"({"alg":"HS256","typ":"JWT"})";
    String header64 = base64UrlEncode(header);

    // 2. payload
    // 加一个简单的 iat（签发时间）：秒级
    uint32_t nowSec = millis() / 1000;
    String payload = String("{\"cs\":\"") + callsign +
                     "\",\"uid\":\"" + userId +
                     "\",\"iat\":" + String(nowSec) + "}";

    String payload64 = base64UrlEncode(payload);

    // 3. 待签名字符串
    String toSign = header64 + "." + payload64;

    // 4. 使用 passwordSecret 作为 HMAC 秘钥
    String sig64 = hmacSha256Base64Url(passwordSecret, toSign);

    // 5. 拼接最终 token
    return toSign + "." + sig64;
}

} // namespace JwtUtil

void JwtProviderHS256::setSecret(const String& secret) {
    _secret = secret;
}

void JwtProviderHS256::setClaims(const JwtClaims& claims) {
    _claims = claims;
}

String JwtProviderHS256::issue() {
    _issuedAt = millis() / 1000;
    return JwtUtil::buildJwt(_claims.iss, _claims.sub, _secret);
}

bool JwtProviderHS256::willExpireIn(uint32_t sec) const {
    if (_claims.ttl_sec == 0) return true;
    if (_issuedAt == 0) return true;

    uint32_t nowSec = millis() / 1000;
    uint32_t exp = _issuedAt + _claims.ttl_sec;
    return exp <= (nowSec + sec);
}

String JwtProviderHS256::usernameHint() const {
    return _claims.sub;
}
