#include <redis++.h>

#include <iostream>

#define CROW_MAIN
#include "crow_all.h"

const std::string SERVICE_VERSION = "v0.0.1";

const std::string devIDPrefix("ddc/device-measurements/*");

int main(int argc, const char* argv[])
{
  if (argc < 2) {
    std::cerr << "Usage: " << argv[0] << " <redisURL>" << std::endl;
    return 127;
  }

  const std::string redisURL(argv[1]); // tcp://127.0.0.1:6379
  crow::SimpleApp app;

  CROW_ROUTE(app, "/")(
  [](){
    return "DDC/Counter " + SERVICE_VERSION;
  });

  CROW_ROUTE(app, "/measurements")(
  [&redisURL](){
    crow::json::wvalue x;
    sw::redis::Redis redis(redisURL);

    std::vector<std::string> keys;
    redis.keys(devIDPrefix, std::back_inserter(keys));

    std::vector<sw::redis::OptionalString> vals;
    redis.mget(keys.begin(), keys.end(), std::back_inserter(vals));
    for (int i = 0; i < keys.size(); ++i) {
      auto& k = keys[i];
      if (vals[i]) {
        std::copy(k.begin() + devIDPrefix.size() - 1, k.end(), k.begin());
        k.resize(k.size() - devIDPrefix.size() + 1); // remove prefix
        x[k] = *vals[i];
      }
    }
    return x;
  });

  app.port(18080).multithreaded().run();
}
