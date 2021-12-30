#include <amqpcpp.h>
#include <amqpcpp/libuv.h>
#include <redis++.h>
#include <rapidjson/document.h>
#include <uv.h>

#include <fstream>
#include <future>
#include <iostream>
#include <map>
#include <queue>
#include <sstream>

volatile int exit_process = 0;

const std::string Device_Measurements_Key_Prefix = "ddc/device-measurements/";

bool isHelpCline(int argc, const char* argv[])
{
    for (int i = 1; i < argc; ++i)
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0)
            return true;
    return false;
}

std::map<std::string, std::string>
parseConfFile(std::ifstream& fin)
{
    std::map<std::string, std::string> ret;
    auto extractNonSpace = [](auto& b, auto e) {
        while(b != e && (*b == ' ' || *b == '\t')) ++b;
        if (b == e) return e;
        while (b != (e-1) && (*(e-1) == ' ' || *(e-1) == '\t')) --e;
        return e;
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
        auto& v = ret[key];
        std::copy(it, vale, std::back_inserter(v));
    }
    return ret;
}

int getConfs(const std::string& confpath, std::string& rabbitURI, std::string& redisURI, std::string& amqpQueue)
{
    std::ifstream confin(confpath);
    if (!confin.is_open()) {
        std::cerr << "Configuration file '" + confpath + "' could not be found" << std::endl;
        return 1;
    }

    std::map<std::string, std::string> confmap;
    try {
        confmap = parseConfFile(confin);
    } catch(const std::exception& e) {
        std::cerr << e.what() << std::endl;
        return 2;
    }
    for (const auto& k : { "AMQPUserPasswordFile", "AMQPHost", "AMQPQueue", "RedisURI" })
        if (!confmap.count(k)) {
            std::cerr << "Value for '" << k << "' is not found in configuration" << std::endl;
            return 3;
        }
    std::ifstream userpfin(confmap["AMQPUserPasswordFile"]);
    if (!userpfin.is_open()) {
        std::cerr << "User-password file could not be opened: " + confmap["AMQPUserPasswordFile"] << std::endl;
        return 4;
    }
    std::string userPass;
    std::getline(userpfin, userPass);

    rabbitURI = "amqp://" + userPass + "@" + confmap["AMQPHost"];
    redisURI = confmap["RedisURI"];
    amqpQueue = confmap["AMQPQueue"];
    return 0;
}

void sigINT(int sig)
{
    std::cerr << "Signal received: " << sig << std::endl;
    exit_process = 1;
}

int main(int argc, const char *argv[])
{
    signal(SIGINT, sigINT);
    signal(SIGTERM, sigINT);

    if (isHelpCline(argc, argv)) {
        std::cerr << "Usage: " << argv[0] << " [<config>]\n"
                  << "\tDefault configuration path is /etc/ddc/qlistener.conf"
                  << std::endl;
        return 127;
    }

    std::string rabbitURL, redisURI, amqpQueue;
    int res = getConfs(argc > 1 ? argv[1] : "/etc/ddc/qlistener.conf", rabbitURL, redisURI, amqpQueue);
    if(res)
        return res;


    std::mutex msgq_mtx;
    std::queue<std::string> msg_que;

    auto loop = uv_default_loop();

    AMQP::LibUvHandler handler(loop);
    AMQP::Address addr(rabbitURL);
    AMQP::TcpConnection conn(&handler, addr);
    AMQP::TcpChannel chan(&conn);
    std::string consume_tag;
    int failed_heartbeats = 0;

    sw::redis::Redis redis(redisURI);

    auto get_msg_from_queue = [&msg_que, &msgq_mtx](std::string& msg) {
        bool is_empty = false;
        std::lock_guard<std::mutex> lck(msgq_mtx);
        if (!(is_empty = msg_que.empty())) {
            msg = std::move(msg_que.front());
            msg_que.pop();
        }
        return is_empty;
    };
    auto parse_notify_msg = [&redis](const std::string& msg) {
        rapidjson::Document d;
        try {
            d.Parse(msg.c_str(), msg.size());
            if (d.HasParseError()) {
                std::cerr << "JSON parse error:" << d.GetParseError() << std::endl;
                return;
            }
            const auto dev_id = d["devID"].GetString();
            // std::cout << "devID:" << dev_id << ", val:" << d["val"].GetFloat() << std::endl;

            const auto key = Device_Measurements_Key_Prefix + dev_id;
            if (!redis.incr(key))
                std::cerr << "INCR failed: " << key << std::endl;
        } catch (const std::exception& e) {
            std::cerr << e.what() << " ..skip." << std::endl;
        }
    };


    auto heartbeat_fut = std::async(std::launch::async, [&conn, &chan, &consume_tag, &failed_heartbeats]() {
        while(true) {
            for (int i = 0; i < 10; ++i) {
                std::this_thread::sleep_for(std::chrono::milliseconds(200));
                if (exit_process == 1) {
                    std::cerr << "Terminating " << consume_tag << std::endl;
                    if (!consume_tag.empty()) {
                        bool cancelled = false;
                        chan.cancel(consume_tag)
                            .onSuccess([&cancelled](){
                                    cancelled = true;
                        });
                        for (int i=0; i<20 && !cancelled; ++i)
                            std::this_thread::sleep_for(std::chrono::milliseconds(50));
                    }
                    ++exit_process;
                    return;
                }
            }

            if (!conn.heartbeat()) {
                ++failed_heartbeats;
                if (failed_heartbeats > 3) {
                    std::cout << "failed heartbeat EXCEEDED THRESHOLD, count: " << failed_heartbeats << ". Igniting EXIT" << std::endl;
                    exit_process = 1;
                }
                else
                    std::cout << "failed heartbeat, count: " << failed_heartbeats << std::endl;
            } else {
                failed_heartbeats = 0;
            }
        }
    });

    chan.consume(amqpQueue, AMQP::noack) // no need to: chan.ack(deliveryTag);
        .onSuccess([&consume_tag](const std::string& tag) {
            consume_tag = tag;
        })
        .onMessage([&msg_que, &msgq_mtx](const AMQP::Message& msg, uint64_t deliveryTag, bool redelivered) {
            std::string s = std::string(msg.body(), msg.body() + msg.bodySize());
            std::lock_guard<std::mutex> lck(msgq_mtx);
            msg_que.push(std::move(s));
        })
        .onError([](const char* err) {
            std::cerr << "error while fetching queue: " << err << std::endl;
    });

    auto fut_send = std::async(std::launch::async, [&loop, &get_msg_from_queue, &parse_notify_msg] {
        std::string msg;

        while (!exit_process) {
            if (get_msg_from_queue(msg)) {
                std::this_thread::sleep_for(std::chrono::milliseconds(150));
                continue;
            }
            parse_notify_msg(msg);
        }
        while (exit_process == 1) { // let chan.consume() to populate messages
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        while (!get_msg_from_queue(msg)) {
            parse_notify_msg(msg);
        }

        uv_stop(loop);
    });

    uv_run(loop, UV_RUN_DEFAULT);
    fut_send.get();
    heartbeat_fut.get();
    conn.close();

    return 0;
}

