#pragma once

#include <Arduino.h>

namespace JwtUtil {

String buildJwt(const String& callsign,
                const String& userId,
                const String& passwordSecret);

} // namespace JwtUtil

struct JwtClaims {
    String iss;
    String sub;
    String aud;
    uint32_t ttl_sec = 3600;
};

class IJwtProvider {
public:
    virtual ~IJwtProvider() = default;
    virtual String issue() = 0;
    virtual bool willExpireIn(uint32_t sec) const = 0;
    virtual String usernameHint() const = 0;
};

class JwtProviderHS256 : public IJwtProvider {
public:
    void setSecret(const String& secret);
    void setClaims(const JwtClaims& claims);

    String issue() override;
    bool willExpireIn(uint32_t sec) const override;
    String usernameHint() const override;

private:
    String _secret;
    JwtClaims _claims;
    uint32_t _issuedAt = 0;
};
