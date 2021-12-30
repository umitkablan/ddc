#include <uv.h>
#include <amqpcpp.h>
#include <amqpcpp/libuv.h>

#include <iostream>
#include <future>
#include <fstream>
#include <future>
#include <iostream>
#include <map>
#include <string>
#include <sstream>
#include <string>

struct AMQPConf {
    std::string userPassFile;
    std::string host;
    std::string exchange;
    std::string routingKey;
};

AMQPConf parseConf(std::ifstream& fin)
{
    AMQPConf ret;
    auto extractNonSpace = [](auto& b, auto e) {
        while(b != e && (*b == ' ' || *b == '\t')) ++b;
        if (b == e) return e;
        while (b != (e-1) && (*(e-1) == ' ' || *(e-1) == '\t')) --e;
        return e;
    };
    std::map<std::string, std::pair<std::string*, bool>> confParseMap {
        { "AMQPUserPasswordFile", { &ret.userPassFile, false } },
        { "AMQPHost", { &ret.host, false } },
        { "AMQPExchange", { &ret.exchange, false } },
        { "AMQPRoutingKey", { &ret.routingKey, false } }
    };

    std::string line;
    while(std::getline(fin, line)) {
        if (line.empty()) continue;   // empty line
        if (line[0] == '#') continue; // comment line
        auto it = find(line.begin(), line.end(), '=');
        if (it == line.end())
            throw std::invalid_argument("'=' is not found in line: '" + line + "'");
        auto keyb = line.begin();
        const std::string key(keyb, extractNonSpace(keyb, it));
        if (key.empty())
            throw std::invalid_argument("configuration key is not found in line: '" + line + "'");
        auto vale = extractNonSpace(++it, line.end());
        if (it == vale)
            throw std::invalid_argument("configuration key's ('" + key + "') value is not found in line: '" + line + "'");
        auto mit = confParseMap.find(key);
        if (mit == confParseMap.end())
            throw std::invalid_argument("unknown key '" + key + "' in configuration");
        std::copy(it, vale, std::back_inserter(*(mit->second.first)));
        mit->second.second = true;
    }
    for (const auto& c : confParseMap)
        if (!c.second.second)
            throw std::invalid_argument("Configuration key '" + c.first + "' is not set");

    return ret;
}

bool isHelpCline(int argc, const char* argv[])
{
    for (int i = 1; i < argc; ++i)
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0)
            return true;
    return false;
}

std::string getCurDateStr()
{
    auto sys_now = std::chrono::system_clock::now();
    std::time_t now = std::chrono::system_clock::to_time_t(sys_now);
    auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(sys_now.time_since_epoch() % std::chrono::seconds { 1 }).count();

    std::string s(64, '\0');
    struct tm buf = { 0 };
    auto l = std::strftime(&s[0], s.size(), "%Y-%m-%d %T.", gmtime_r(&now, &buf));
    s.resize(l);

    if (now_ms < 100)
        s.append(1, '0');
    if (now_ms < 10)
        s.append(1, '0');
    s += std::to_string(now_ms);
    return s;
}

int main(int argc, const char* argv[])
{
    if (argc < 4 || isHelpCline(argc, argv)) {
        std::cerr << "Usage: " << argv[0] << " <DeviceID> <scale> <file> [<config>]\n"
                  << "\tScale is free text; could be Centigrade (temperature), Pascal (pressure) or anything meaningful.\n"
                  << "\tFile could be '-' meaning standard input. Configuration file is optional.\n"
                  << "\tDefault configuration path is /etc/ddc/devadapter.conf"
                  << std::endl;
        return 127;
    }

    const std::string confpath(argc > 4 ? argv[4] : "/etc/ddc/devadapter.conf");

    std::ifstream confin(confpath);
    if (!confin.is_open()) {
        std::cerr << "Configuration file '" + confpath + "' could not be found" << std::endl;
        return 1;
    }

    const auto amqpconf = parseConf(confin);
    std::ifstream userpfin(amqpconf.userPassFile);
    if (!userpfin.is_open()) {
        std::cerr << "User-password file could not be opened: " + amqpconf.userPassFile << std::endl;
        return 2;
    }
    std::string userPass;
    std::getline(userpfin, userPass);

    const std::string amqpAddr("amqp://" + userPass + "@" + amqpconf.host);
    const std::string deviceID(argv[1]);
    const std::string scale(argv[2]);
    const std::string datafile(argv[3]);

    auto loop = uv_default_loop();
    AMQP::LibUvHandler handler(loop);
    AMQP::Address addr(amqpAddr);
    AMQP::TcpConnection conn(&handler, addr);
    AMQP::TcpChannel chan(&conn);

    bool exit_heartbeat = false;
    auto hbeat_fut = std::async(std::launch::async,
    [&conn, &exit_heartbeat]() {
        while(true) {
            for (int i = 0; i < 10; ++i) {
                std::this_thread::sleep_for(std::chrono::milliseconds(200));
                if (exit_heartbeat)
                    return;
            }
            conn.heartbeat();
        }
    });

    std::ifstream fin_data(datafile);
    if (datafile != "-" && !fin_data.is_open())
    {
        std::cerr << "data input file could not be found: '" << datafile << "'" << std::endl;
        return 4;
    }

    auto send_data_fut = std::async(std::launch::async,
    [&fin_data, &chan, &amqpconf, &datafile, &deviceID, &scale, &loop]() {
        int sent_line_cnt=0;
        std::string line;
        while (std::getline(datafile == "-" ? std::cin : fin_data, line)) {
            if (line == "-" || line.empty()){
                break;
            }
            std::ostringstream sbuf;
            sbuf << R"({"version":"ddc/measure/0.1", "devID":")" << deviceID
                 << R"(", "val":)" << line << R"(, "scale":")" << scale
                 << R"(", "date":")" << getCurDateStr() << "\"}";
            std::string s(std::move(sbuf).str());
            auto res = chan.publish(amqpconf.exchange, amqpconf.routingKey, s.c_str(), s.size());
            if (!res)
                std::cerr << "publish: false -> " << line << std::endl;
            else ++sent_line_cnt;
        }
        std::cout << "Sent line count: " << sent_line_cnt << std::endl;
        uv_stop(loop);
        std::cout << "Finalizing..." << std::endl;
    });

    uv_run(loop, UV_RUN_DEFAULT);
    send_data_fut.get();
    chan.close();

    exit_heartbeat = true;
    hbeat_fut.get();

    return 0;

}
