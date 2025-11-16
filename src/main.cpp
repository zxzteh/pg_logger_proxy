#include <iostream>
#include <signal.h>

#include "Proxy.h"
#include "RawHexInterceptor.h"
#include "PgQueryInterceptor.h"

int main(int argc, char* argv[]) {
    if (getuid() != 0) {
        std::cerr <<  "You have no power here, permission denied" <<  std::endl;
        return 1;
    }

    if (argc != 5) {
        std::cerr << "Usage: " << argv[0]
                  << " <listen_host> <listen_port> <db_host> <db_port>\n";
        return 1;
    }

    std::string listen_host = argv[1];
    uint16_t listen_port = static_cast<uint16_t>(std::stoi(argv[2]));
    std::string db_host = argv[3];
    uint16_t db_port = static_cast<uint16_t>(std::stoi(argv[4]));

    //  Ignore SIGPIPE, to keep app alive
    signal(SIGPIPE, SIG_IGN);

    Logger logger("logs", "query");
    Proxy proxy(listen_host, listen_port, db_host, db_port);

    // auto interceptor = std::make_unique<RawHexInterceptor>("hex_dump.log");
    auto interceptor = std::make_unique<PgQueryInterceptor>(&logger);
    proxy.setInterceptor(std::move(interceptor));

    if (!proxy.init()) {
        std::cerr << "Failed to init proxy\n";
        return 1;
    }

    proxy.run();

    return 0;
}
