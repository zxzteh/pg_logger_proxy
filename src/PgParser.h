#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

#include "Connection.h"

//  Postgres raw stream paraser
//  Supports Q/P/B/E/C

class PgQueryParser {
public:
    using QueryCallback = std::function<void(const Connection&, const std::string& pg_query)>;
    explicit PgQueryParser(QueryCallback cb);

    //  Raw data from client
    void onClientData(Connection& conn, const char* data, std::size_t len);

    //  Clean connections
    void onConnectionClosed(const Connection& conn);

private:
    struct Statement {
        std::string pg_template;
        std::vector<std::uint32_t> param_types;
    };

    struct Portal {
        std::string statement_name;
        std::vector<std::string> param_values;
        std::vector<std::uint16_t> param_formats;  //  0=text, 1=binary
    };

    struct ConnState {
        std::string buf;  //  bytestream from client
        bool startup_skipped = false;

        //  Wow, so unordered, such perfomance
        std::unordered_map<std::string, Statement> statements;
        std::unordered_map<std::string, Portal> portals;
    };

    QueryCallback callback_;
    std::unordered_map<const Connection*, ConnState> states_;

    ConnState& stateFor(Connection& conn);

    //  Parser functional
    void processBuffer(Connection& conn, ConnState& st);
    void handleSimpleQuery(Connection& conn, ConnState& st, const char* msg, std::size_t total_len);
    void handleParse(Connection& conn, ConnState& st, const char* msg, std::size_t total_len);
    void handleBind(Connection& conn, ConnState& st, const char* msg, std::size_t total_len);
    void handleExecute(Connection& conn, ConnState& st, const char* msg, std::size_t total_len);
    void handleClose(Connection& conn, ConnState& st, const char* msg, std::size_t total_len);

    static bool isIntegerLiteral(std::string_view s);
    static bool isFloatLiteral(std::string_view s);
    static std::string formatByteaLiteral(const std::string& value);
    static std::string formatStringLiteral(const std::string& value);

    static std::uint32_t be32(const char* p);
    static std::uint16_t be16(const char* p);

    static std::string readCString(const char* msg, std::size_t total_len, std::size_t& pos);
    static std::string makeupPreparedQuery(const Statement& stmt, const Portal& portal);
    static std::string formatParamForSql(const std::string& value, std::uint16_t format_code);
};
