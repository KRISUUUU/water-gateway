#include <cassert>
#include <string>

#include "mqtt_service/mqtt_payloads.hpp"
#include "mqtt_service/mqtt_topics.hpp"

int main() {
    const auto topic = mqtt_service::topic_status("watergw", "node1");
    assert(topic == "watergw/node1/status");

    const auto payload = mqtt_service::make_status_payload("connected", "1.0.0");
    assert(payload.find("\"state\":\"connected\"") != std::string::npos);
    assert(payload.find("\"version\":\"1.0.0\"") != std::string::npos);

    return 0;
}
