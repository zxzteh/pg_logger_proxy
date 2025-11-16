#pragma once

#include <string>

#include "ProtocolInterceptor.h"
#include "PgParser.h"
#include "Logger.h"

//  Get stream -> parse stream -> log stream

class PgQueryInterceptor : public IProtocolInterceptor {
public:
    explicit PgQueryInterceptor(Logger* logger);

    // Client -> Server
    void onClientData(Connection& conn, const char* data, std::size_t len) override;

private:
    Logger* p_logger_ = nullptr;
    PgQueryParser parser_;
};
