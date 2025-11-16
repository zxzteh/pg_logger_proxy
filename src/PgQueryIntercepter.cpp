#include "PgQueryInterceptor.h"

PgQueryInterceptor::PgQueryInterceptor(Logger* logger)
    : p_logger_(logger)
    , parser_([this](const Connection& conn, const std::string& pg_query) {
        if (p_logger_) {
            std::string message = conn.client_addr;
            message += " ";
            message += pg_query;
            p_logger_->write(message);
        }
    })
{}

void PgQueryInterceptor::onClientData(Connection& conn, const char* data, std::size_t len) {
    parser_.onClientData(conn, data, len);
}

