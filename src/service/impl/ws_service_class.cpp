#include "./ws_service_class.h"
#include "./service_impl_common.h"

using namespace std;
using WsServer = SimpleWeb::SocketServer<SimpleWeb::WS>;

void WsService::init() {
    Log::d(TAG, u8"��ʼ�� WebSocket");

    auto on_open_callback = [](shared_ptr<WsServer::Connection> connection) {
        Log::d(TAG, u8"�յ� WebSocket ���ӣ�" + connection->path);
        json args = SimpleWeb::QueryString::parse(connection->query_string);
        auto authorized = authorize(connection->header, args);
        if (!authorized) {
            Log::d(TAG, u8"û���ṩ Token �� Token �������ѹر�����");
            auto send_stream = make_shared<WsServer::SendStream>();
            *send_stream << "authorization failed";
            connection->send(send_stream);
            connection->send_close(1000); // we don't want this client any more
            return;
        }
    };

    server_ = make_shared<WsServer>();

    auto &api_endpoint = server_->endpoint["^/api/?$"];
    api_endpoint.on_open = on_open_callback;
    api_endpoint.on_message = ws_api_on_message<WsServer>;

    auto &event_endpoint = server_->endpoint["^/event/?$"];
    event_endpoint.on_open = on_open_callback;

    ServiceBase::init();
}

void WsService::finalize() {
    server_ = nullptr;
    ServiceBase::finalize();
}

void WsService::start() {
    if (config.use_ws) {
        init();

        server_->config.thread_pool_size = server_thread_pool_size();
        server_->config.address = config.ws_host;
        server_->config.port = config.ws_port;
        thread_ = thread([&]() {
            started_ = true;
            try {
                server_->start();
            } catch (...) {}
            started_ = false;
        });
        Log::d(TAG, u8"���� API WebSocket �������ɹ�����ʼ���� ws://"
               + server_->config.address + ":" + to_string(server_->config.port));
    }
}

void WsService::stop() {
    if (started_) {
        server_->stop();
        started_ = false;
    }
    if (thread_.joinable()) {
        thread_.join();
    }
    finalize();
}

bool WsService::heartbeat() const {
    return ServiceBase::heartbeat();
}

bool WsService::good() const {
    if (config.use_ws) {
        return initialized_ && started_;
    }
    return ServiceBase::good();
}

void WsService::push_event(const json &payload) const {
    if (started_) {
        Log::d(TAG, u8"��ʼͨ�� WebSocket ����������¼�");
        size_t total_count = 0;
        size_t succeeded_count = 0;
        for (const auto &connection : server_->get_connections()) {
            if (boost::algorithm::starts_with(connection->path, "/event")) {
                total_count++;
                try {
                    const auto send_stream = make_shared<WsServer::SendStream>();
                    *send_stream << payload.dump();
                    connection->send(send_stream);
                    succeeded_count++;
                } catch (...) {}
            }
        }
        Log::d(TAG, u8"�ѳɹ��� " + to_string(succeeded_count) + "/" + to_string(total_count) + u8" �� WebSocket �ͻ��������¼�");
    }
}
