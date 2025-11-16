#include "PgParser.h"

PgQueryParser::PgQueryParser(QueryCallback cb)
    : callback_(std::move(cb)) {}


/*
Convert big-endian value to uint16_t 
Examples
p = { 0x00, 0x00, 0x00, 0x0A } -> 10
p = { 0x00, 0x00, 0x01, 0x00 } -> 256
p = { 0x01, 0x02, 0x03, 0x04 } -> 16909060
p = { 0xFF, 0xFF, 0xFF, 0xFF } -> 4294967295
*/

std::uint32_t PgQueryParser::be32(const char* p) {
    const unsigned char* b = reinterpret_cast<const unsigned char*>(p);
    return (std::uint32_t(b[0]) << 24) |
           (std::uint32_t(b[1]) << 16) |
           (std::uint32_t(b[2]) << 8)  |
           (std::uint32_t(b[3]));
}

/*
Convert big-endian value to uint16_t 
Examples
p = { 0x01, 0x02 } -> 258
p = { 0x00, 0x0A } -> 10
p = { 0xFF, 0xFE } -> 65534
*/

std::uint16_t PgQueryParser::be16(const char* p) {
    const unsigned char* b = reinterpret_cast<const unsigned char*>(p);
    return (std::uint16_t(b[0]) << 8) |
           (std::uint16_t(b[1]));
}

PgQueryParser::ConnState& PgQueryParser::stateFor(Connection& conn) {
    return states_[&conn];
}

void PgQueryParser::onConnectionClosed(const Connection& conn) {
    states_.erase(&conn);
}

void PgQueryParser::onClientData(Connection& conn, const char* data, std::size_t len) {
    if (!callback_ || len == 0) return;

    auto& st = stateFor(conn);
    st.buf.append(data, len);
    processBuffer(conn, st);
}

std::string PgQueryParser::readCString(const char* msg, std::size_t total, std::size_t& pos) {
    if (pos >= total) {
        return std::string();
    }

    auto start = pos;
    while (pos < total && msg[pos] != '\0') {
        pos++;
    }

    std::string res(msg + start, msg + pos);
    pos++;  //  For terminator
    return res;
}

void PgQueryParser::processBuffer(Connection& conn, ConnState& state) {

    //  Tryin' to find StartupMessage first, then parse after it
    if (!state.startup_skipped) {
        if (state.buf.size() < 4) {
            return;
        }

        std::uint32_t len = be32(state.buf.data());
        if (len < 4 || len > (1u << 26)) {  //  64MB safety
            state.startup_skipped = true;   //  Too big len, assume skip
        } else {
            if (state.buf.size() < len) {
                return;  //  Waiting for whole data
            }
            state.buf.erase(0, len);
            state.startup_skipped = true;
        }
    }

    //  Then we can parse usual query here
    while (true) {
        if (state.buf.size() < 5) {
            return;  //  Waiting for whole data
        }

        const char* msg = state.buf.data();
        char type = msg[0];
        std::uint32_t len = be32(msg + 1);
        std::uint32_t total_len = len + 1;  //  Type field not counted, so we add it here

        // Safety, sanity
        if (len < 4 || total_len > (1u << 26)) {
            state.buf.clear();
            return;
        }

        if (state.buf.size() < total_len) {
            return;  //  Waiting for whole data
        }

        switch (type) {
            case 'Q':
                handleSimpleQuery(conn, state, msg, total_len);
                break;
            case 'P':
                handleParse(conn, state, msg, total_len);
                break;
            case 'B':
                handleBind(conn, state, msg, total_len);
                break;
            case 'E':
                handleExecute(conn, state, msg, total_len);
                break;
            case 'C':
                handleClose(conn, state, msg, total_len);
                break;
            default:
                break;
        }
        state.buf.erase(0, total_len);
    }
}

/**
Query (simple)
Format:
Byte1('Q')
int32 len
String: query string (zero-terminated)

| field      | pos | size      | description                     |
|------------|-----|-----------|---------------------------------|
| type       | 0   | 1         | 'Q'                             |
| length     | 1   | 4         | int32, len after   type         |
| query      | 5   | var+1     | C-string (0-terminated)         |
**/

void PgQueryParser::handleSimpleQuery(Connection& conn, ConnState&, const char* msg, std::size_t total_len) {
    const char* query_data = msg + 5;
    std::size_t query_len = total_len - 5;

    if (query_len > 0 && query_data[query_len - 1] == '\0') {
        query_len--;
    }

    std::string query(query_data, query_len);
    callback_(conn, query);
}

/**
Parse
Format:
Byte1('P')
int32 len
String: statement name, empty string = unnamed statement
String: query string to parse (zero-terminated)
int16: number of parameter type OIDs (N)
Repeat N times:
    int32: parameter type Object ID (0 = unspecified)

| field            | pos | size      | description                                |
|------------------|-----|-----------|--------------------------------------------|
| type             | 0   | 1         | 'P'                                        |
| length           | 1   | 4         | int32                                      |
| stmnt_name       | 5   | var+1     | C-string ("" = unnamed)                    |
| query_string     | ... | var+1     | C-string SQL                               |
| n_param_types    | ... | 2         | int16 (N)                                  |
| param_type_oid   | ... | 4*(N)     | N × int32 OIDs                             |
**/

void PgQueryParser::handleParse(Connection&, ConnState& st, const char* msg, std::size_t total) {
    std::size_t pos = 5;

    std::string statement_name = readCString(msg, total, pos);
    std::string query          = readCString(msg, total, pos);

    if (pos + 2 > total) return;
    std::uint16_t nparams = be16(msg + pos);
    pos += 2;

    std::vector<std::uint32_t> types;
    types.reserve(nparams);
    for (std::uint16_t i = 0; i < nparams; ++i) {
        if (pos + 4 > total) return;
        std::uint32_t t = be32(msg + pos);
        pos += 4;
        types.push_back(t);
    }

    Statement statement;
    statement.pg_template = std::move(query);
    statement.param_types  = std::move(types);

    st.statements[statement_name] = std::move(statement);
}

/**
Bind
Format:
Byte1('B')
int32 len
String: destination portal name (empty string = unnamed portal)
String: source prepared statement name (empty string = unnamed statement)
int16: number of parameter format codes (M)
Repeat M times:
    int16: format code (0 = text, 1 = binary)

int16: number of parameters (N)
Repeat N times:
    int32: parameter value length in bytes, or -1 for NULL
    Byten: parameter value bytes (skipped if length = -1)

int16: number of result-column format codes (R)
Repeat R times:
    int16: format code (0 = text, 1 = binary)

| field                | pos | size          | description                                   |
|----------------------|-----|---------------|-----------------------------------------------|
| type                 | 0   | 1             | 'B'                                           |
| length               | 1   | 4             | int32                                         |
| portal_name          | 5   | var+1         | C-string                                      |
| statement_name       | ... | var+1         | C-string                                      |
| n_format_codes       | ... | 2             | int16 (M)                                     |
| format_codes         | ... | 2 * M         | M × int16 (0=text, 1=binary)                  |
| n_params             | ... | 2             | int16 (N)                                     |
| param[i].len         | ... | 4             | int32 (−1 = NULL)                             |
| param[i].value       | ... | len[i] bytes  | skipped if NULL                               |
| n_result_formats     | ... | 2             | int16 (R)                                     |
| result_format_codes  | ... | 2 * R         | R × int16                                     |
**/

void PgQueryParser::handleBind(Connection&, ConnState& state, const char* msg, std::size_t total_len) {
    std::size_t pos = 5;
    std::string portal_name = readCString(msg, total_len, pos);
    std::string statement_name = readCString(msg, total_len, pos);

    if (pos + 2 > total_len) return;
    std::uint16_t num_format_codes = be16(msg + pos);
    pos += 2;

    std::vector<std::uint16_t> format_codes;
    format_codes.reserve(num_format_codes);
    for (std::uint16_t i = 0; i < num_format_codes; i++) {
        if (pos + 2 > total_len) return;
        format_codes.push_back(be16(msg + pos));
        pos += 2;
    }

    if (pos + 2 > total_len) return;
    std::uint16_t num_params = be16(msg + pos);
    pos += 2;

    std::vector<std::string> params;
    std::vector<std::uint16_t> param_formats;
    params.reserve(num_params);
    param_formats.reserve(num_params);

    auto format_for_param = [&](std::size_t idx) -> std::uint16_t {
        if (num_format_codes == 0) {  //  text by default
            return 0;  
        }
        if (num_format_codes == 1) {  //  one for all
            return format_codes[0]; 
        }
        if (idx < format_codes.size()) {  //  individual
            return format_codes[idx];
        }
        return 0;
    };

    for (std::uint16_t i = 0; i < num_params; i++) {
        if (pos + 4 > total_len) return;
        std::int32_t param_len = static_cast<std::int32_t>(be32(msg + pos));
        pos += 4;

        std::uint16_t format = format_for_param(i);
        param_formats.push_back(format);

        if (param_len == -1) {
            params.emplace_back("NULL");
        } else {
            if (param_len < 0 || pos + static_cast<std::size_t>(param_len) > total_len) return;
            
            //  usual parameter case
            params.emplace_back(msg + pos, msg + pos + static_cast<std::size_t>(param_len));
            pos += static_cast<std::size_t>(param_len);
        }
    }

    //  we don't need this, that's for DB
    if (pos + 2 > total_len) return;
    std::uint16_t num_result_formats = be16(msg + pos);
    pos += 2 + 2 * num_result_formats;
    if (pos > total_len) return;

    //  Fill portal
    Portal portal;
    portal.statement_name = std::move(statement_name);
    portal.param_values = std::move(params);
    portal.param_formats = std::move(param_formats);

    state.portals[portal_name] = std::move(portal);
}

/**
Execute
Format:
Byte1('E')
int32 len
String: portal name to execute (empty string = unnamed portal)
int32: max number of rows to return (0 = no limit)


| field           | pos | size      | description                                  |
|-----------------|-----|-----------|----------------------------------------------|
| type            | 0   | 1         | 'E'                                          |
| length          | 1   | 4         | int32                                        |
| portal_name     | 5   | var+1     | C-string                                     |
| max_rows        | ... | 4         | int32 (0 = unlimited)                        |
**/

void PgQueryParser::handleExecute(Connection& conn, ConnState& state, const char* msg, std::size_t total_len) {
    std::size_t pos = 5;
    std::string portal_name = readCString(msg, total_len, pos);
    if (pos + 4 > total_len) return;

    //  Find portal
    auto portal_it = state.portals.find(portal_name);
    if (portal_it == state.portals.end()) {  //  if portal does not exist
        return;
    }

    //  Find statement, that was in portal
    const Portal& portal = portal_it->second;
    auto statement_it = state.statements.find(portal.statement_name);
    if (statement_it == state.statements.end()) {
        return;  //  if statement does not exist
    }
    const Statement& statement = statement_it->second;

    //  Align
    std::string pg_query = makeupPreparedQuery(statement, portal);
    callback_(conn, pg_query);

    //  We should delete unnamed portal
    if (portal_name.empty()) {
        state.portals.erase(portal_it);
    }
}

/**
Close
Format:
Byte1('C')
int32 len
Byte1: 'S'=statement, 'P'=portal
String: name

| field      | pos | size      | description                                  |
|------------|-----|-----------|----------------------------------------------|
| type       | 0   | 1         | 'C'                                          |
| length     | 1   | 4         | int32                                        |
| target     | 5   | 1         | 'S' = statement, 'P' = portal                |
| name       | 6   | var+1     | C-string                                     |
**/

void PgQueryParser::handleClose(Connection&, ConnState& st, const char* msg, std::size_t total) {
    std::size_t pos = 5;
    if (pos >= total) return;

    char target = msg[pos];
    pos++;

    std::string name = readCString(msg, total, pos);

    if (target == 'S') {
        st.statements.erase(name);
    } else if (target == 'P') {
        st.portals.erase(name);
    }
}

//  Bind to query 
//  format_code = 0 — text param
//  format_code = 1 — binary param (bytea)

std::string PgQueryParser::formatParamForSql(const std::string& value, std::uint16_t format_code) {
    if (value == "NULL") {
        return "NULL";
    }

    if (format_code == 1) {
        return formatByteaLiteral(value);
    }

    if (isIntegerLiteral(value) || isFloatLiteral(value)) {
        return value;
    }

    return formatStringLiteral(value);
}

std::string PgQueryParser::makeupPreparedQuery(const Statement& statement, const Portal& portal) {
    const std::string& tmpl = statement.pg_template;
    const auto& params = portal.param_values;
    const auto& formats = portal.param_formats;

    std::string out;
    //  Gotta go fast, 32 is ok overhead
    out.reserve(tmpl.size() + params.size() * 32);

    for (std::size_t i = 0; i < tmpl.size(); i++) {
        char c = tmpl[i];
        if (c == '$') {
            std::size_t j = i + 1;  //  after $ position
            int num = 0;
            bool has_digit = false;

            //  While we see digits -> collect a number
            while (j < tmpl.size() && std::isdigit(static_cast<unsigned char>(tmpl[j]))) {
                has_digit = true;
                num = num * 10 + (tmpl[j] - '0');  //  conversion from string to int
                j++;
            }

            //  Validate and insert
            if (has_digit && num >= 1 && static_cast<std::size_t>(num) <= params.size()) {
                std::size_t idx = static_cast<std::size_t>(num - 1);
                std::uint16_t format_code = (idx < formats.size()) ? formats[idx] : 0;
                out += formatParamForSql(params[idx], format_code);
                i = j - 1; 
                continue;
            }
        }

        out.push_back(c);
    }

    return out;
}

bool PgQueryParser::isIntegerLiteral(std::string_view s) {
    if (s.empty()) return false;

    std::size_t i = 0;
    if (s[0] == '+' || s[0] == '-') {
        if (s.size() == 1) return false;
        i = 1;
    }

    for (; i < s.size(); ++i) {
        if (!std::isdigit(static_cast<unsigned char>(s[i]))) {
            return false;
        }
    }
    return true;
}

bool PgQueryParser::isFloatLiteral(std::string_view s) {
    if (s.empty()) return false;

    std::size_t i = 0;
    bool seen_digit = false;
    bool seen_dot   = false;
    bool seen_exp   = false;

    if (s[i] == '+' || s[i] == '-') {
        i++;
        if (i == s.size()) return false;
    }

    for (; i < s.size(); i++) {
        unsigned char ch = static_cast<unsigned char>(s[i]);

        if (std::isdigit(ch)) {
            seen_digit = true;
        } else if (ch == '.') {
            if (seen_dot || seen_exp) return false;
            seen_dot = true;
        } else if (ch == 'e' || ch == 'E') {
            if (seen_exp || !seen_digit) return false;
            seen_exp = true;
            seen_digit = false;

            if (i + 1 < s.size() && (s[i + 1] == '+' || s[i + 1] == '-')) {
                i++;
            }
        } else {
            return false;
        }
    }

    return seen_digit && (seen_dot || seen_exp);
}

std::string PgQueryParser::formatByteaLiteral(const std::string& value) {
    static const char* hex = "0123456789abcdef";
    std::string out = "E'\\\\x";

    for (std::uint8_t byte : value) {
        out.push_back(hex[byte >> 4]);
        out.push_back(hex[byte & 0x0F]);
    }

    out += "'::bytea";
    return out;
}

std::string PgQueryParser::formatStringLiteral(const std::string& value) {
    std::string out;
    out.reserve(value.size() + 2);
    out.push_back('\'');
    for (char c : value) {
        if (c == '\'') out.push_back('\'');
        out.push_back(c);
    }
    out.push_back('\'');
    return out;
}