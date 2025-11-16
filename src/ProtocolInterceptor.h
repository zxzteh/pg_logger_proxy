#pragma once

#include "Connection.h"

class IProtocolInterceptor {
public:
    // Client -> Server, always need this
    virtual void onClientData(Connection& conn, const char* data, std::size_t len) = 0;

    // Server -> client, can be useless
    virtual void onServerData(Connection& conn, const char* data, std::size_t len) {
        (void)conn;
        (void)data;
        (void)len;
    }
};
