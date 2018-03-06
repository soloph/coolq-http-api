#include "./ws_reverse_service_class.h"
#include "./service_impl_common.h"

using namespace std;
using WsClient = SimpleWeb::SocketClient<SimpleWeb::WS>;
using WssClient = SimpleWeb::SocketClient<SimpleWeb::WSS>;

template <typename WsClientT>
shared_ptr<WsClientT> WsReverseService::SubServiceBase::init_ws_reverse_client(const string &server_port_path) {
    auto client = make_shared<WsClientT>(server_port_path);
    client->config.header.emplace("User-Agent", CQAPP_USER_AGENT);
    if (!config.access_token.empty()) {
        client->config.header.emplace("Authorization", "Token " + config.access_token);
    }
    client->on_close = [&](shared_ptr<typename WsClientT::Connection> connection,
                           int code, string reason) {
        if (config.ws_reverse_reconnect_on_code_1000 || code != 1000) {
            with_unique_lock(should_reconnect_mutex_, [&]() {
                should_reconnect_ = true;
            });
        }
    };
    client->on_error = [&](shared_ptr<typename WsClientT::Connection> connection,
                           const SimpleWeb::error_code &error_code) {
        with_unique_lock(should_reconnect_mutex_, [&]() {
            should_reconnect_ = true;
        });
    };
    return client;
}

void WsReverseService::SubServiceBase::init() {
    Log::d(TAG, u8"��ʼ������ WebSocket��" + name() + u8"��");

    auto ws_url = url();

    try {
        if (boost::algorithm::starts_with(ws_url, "ws://")) {
            client_.ws = init_ws_reverse_client<WsClient>(ws_url.substr(strlen("ws://")));
            client_is_wss_ = false;
        } else if (boost::algorithm::starts_with(ws_url, "wss://")) {
            client_.wss = init_ws_reverse_client<WssClient>(ws_url.substr(strlen("wss://")));
            client_is_wss_ = true;
        }
    } catch (...) {
        // in case "init_ws_reverse_client()" failed due to invalid "server_port_path"
        client_is_wss_ = nullopt;
    }

    ServiceBase::init();
}

void WsReverseService::SubServiceBase::finalize() {
    client_.ws = nullptr;
    client_.wss = nullptr;
    client_is_wss_ = nullopt;
    ServiceBase::finalize();
}

void WsReverseService::SubServiceBase::start() {
    if (config.use_ws_reverse) {
        init();

        reconnect_worker_thread_ = thread([&]() {
            try {
                set_reconnect_worker_running(true);
                while (is_reconnect_worker_running()) {
                    auto should_reconn = false;
                    with_unique_lock(should_reconnect_mutex_, [&]() {
                        should_reconn = should_reconnect_;
                        should_reconnect_ = false;
                    });
                    if (should_reconn) {
                        Log::w(TAG, u8"���� WebSocket��" + name() + u8"���ͻ�������ʧ�ܻ��쳣�Ͽ������� "
                               + to_string(config.ws_reverse_reconnect_interval) + u8" �����������");
                        Sleep(config.ws_reverse_reconnect_interval);
                        stop();
                        start();
                    }

                    if (is_reconnect_worker_running()) {
                        Sleep(500); // wait 500 ms for the next check
                    } else {
                        break;
                    }
                }
            } catch (...) {}
        });

        heartbeat_worker_thread_ = thread([&]() {
            try {
                set_heartbeat_worker_running(true);
                while (is_heartbeat_worker_running()) {
                    heartbeat();
                    Log::d(TAG, u8"���� WebSocket��" + name() + u8"���ͻ��˷��� heartbeat �ɹ�");
                    if (is_heartbeat_worker_running()) {
                        Sleep(30000); // wait 30000 ms for the next check
                    }
                    else {
                        break;
                    }
                }
            }
            catch (...) {}
        });

        if (client_is_wss_.has_value()) {
            // client successfully initialized
            thread_ = thread([&]() {
                started_ = true;
                try {
                    if (client_is_wss_.value() == false) {
                        client_.ws->start();
                    } else {
                        client_.wss->start();
                    }
                } catch (...) {
                    started_ = false;
                }
            });
            Log::d(TAG, u8"���� WebSocket ����ͻ��ˣ�" + name() + u8"���ɹ�����ʼ���� " + url());
        }
    }
}

void WsReverseService::SubServiceBase::stop() {
    // this will notify the reconnect worker to stop
    set_reconnect_worker_running(false);
    set_heartbeat_worker_running(false);

    // detach but not join, because we want the thread continue to run until its next check
    reconnect_worker_thread_.detach();
    heartbeat_worker_thread_.detach();

    if (started_) {
        if (client_is_wss_.value() == false) {
            client_.ws->stop();
        } else {
            client_.wss->stop();
        }
        started_ = false;
    }
    if (thread_.joinable()) {
        thread_.join();
    }

    finalize();
}

bool WsReverseService::SubServiceBase::heartbeat() const {
    if (started_) {
        try {
            if (client_is_wss_.value() == false) {
                const auto send_stream = make_shared<WsClient::SendStream>();
                *send_stream << "_hb";
                unique_lock<mutex> lock(client_.ws->connection_mutex);
                client_.ws->connection->send(send_stream);
                lock.unlock();
            }
            else {
                const auto send_stream = make_shared<WssClient::SendStream>();
                *send_stream << "_hb";
                unique_lock<mutex> lock(client_.wss->connection_mutex);
                client_.wss->connection->send(send_stream);
                lock.unlock();
            }
        }
        catch (...) {}
    }
    return true;
}

bool WsReverseService::SubServiceBase::good() const {
    if (config.use_ws_reverse) {
        return initialized_ && started_;
    }
    return ServiceBase::good();
}

string WsReverseService::ApiSubService::url() {
    return config.ws_reverse_api_url;
}

void WsReverseService::ApiSubService::init() {
    SubServiceBase::init();

    if (client_is_wss_.has_value()) {
        if (client_is_wss_.value() == false) {
            client_.ws->on_message = ws_api_on_message<WsClient>;
        } else {
            client_.wss->on_message = ws_api_on_message<WssClient>;
        }
    }
}

string WsReverseService::EventSubService::url() {
    return config.ws_reverse_event_url;
}

void WsReverseService::EventSubService::init() {
    SubServiceBase::init();
}

void WsReverseService::EventSubService::push_event(const json &payload) const {
    if (started_) {
        Log::d(TAG, u8"��ʼͨ�� WebSocket ����ͻ����ϱ��¼�");

        bool succeeded;
        try {
            if (client_is_wss_.value() == false) {
                const auto send_stream = make_shared<WsClient::SendStream>();
                *send_stream << payload.dump();
                // the WsClient class is modified by us ("connection" property made public),
                // so we must maintain the lock manually
                unique_lock<mutex> lock(client_.ws->connection_mutex);
                client_.ws->connection->send(send_stream);
                lock.unlock();
            } else {
                const auto send_stream = make_shared<WssClient::SendStream>();
                *send_stream << payload.dump();
                unique_lock<mutex> lock(client_.wss->connection_mutex);
                client_.wss->connection->send(send_stream);
                lock.unlock();
            }
            succeeded = true;
        } catch (...) {
            succeeded = false;
        }

        Log::d(TAG, u8"ͨ�� WebSocket ����ͻ����ϱ����ݵ� " + config.ws_reverse_event_url + (succeeded ? u8" �ɹ�" : u8" ʧ��"));
    }
}
