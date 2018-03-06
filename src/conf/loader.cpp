// 
// loader.cpp : Provide function to load configuration.
// 
// Copyright (C) 2017  Richard Chien <richardchienthebest@gmail.com>
// 
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
// 
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
// 
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.
// 

#include "./loader.h"

#include "app.h"

#include <fstream>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/ini_parser.hpp>
#include <boost/filesystem.hpp>

#include "helpers.h"
#include "./config_struct.h"

using namespace std;
namespace fs = boost::filesystem;

static const auto DEFAULT_UPDATE_SOURCE =
        "https://raw.githubusercontent.com/richardchien/coolq-http-api-release/master/";

optional<Config> load_configuration(const string &filepath) {
    static const auto TAG = u8"����";

    Config config;

    Log::d(TAG, u8"���Լ��������ļ�");

    const auto ansi_filepath = ansi(filepath);
    if (!fs::is_regular_file(ansi_filepath)) {
        // create default config file
        Log::i(TAG, u8"û���ҵ������ļ���д��Ĭ������");
        if (ofstream file(ansi_filepath); file.is_open()) {
            file << "[general]" << endl
                    << "host=0.0.0.0" << endl
                    << "port=5700" << endl
                    << "use_http=no" << endl
                    << "ws_host=0.0.0.0" << endl
                    << "ws_port=6700" << endl
                    << "use_ws=no" << endl
                    << "post_url=" << endl
                    << "access_token=" << endl
                    << "secret=" << endl
                    << "post_message_format=string" << endl
                    << "serve_data_files=no" << endl
                    << "update_source=" << DEFAULT_UPDATE_SOURCE << endl
                    << "update_channel=stable" << endl
                    << "auto_check_update=no" << endl
                    << "auto_perform_update=no" << endl
                    << "thread_pool_size=4" << endl
                    << "server_thread_pool_size=1" << endl;
        } else {
            Log::e(TAG, u8"Ĭ������д��ʧ�ܣ������ļ�ϵͳȨ��");
        }
    }

    // load from config file
    try {
        const auto login_qq_str = to_string(sdk->get_login_qq());

        boost::property_tree::ptree pt;
        read_ini(ansi_filepath, pt);

        static struct {
            boost::optional<bool> get_value(const string &s) const {
                auto b_opt = to_bool(s);
                return b_opt ? boost::make_optional(b_opt.value()) : boost::none;
            }
        } bool_translator;

        #define GET_CONFIG(key, type) \
            auto __general_##key = pt.get<type>("general." #key, config.key); \
            config.key = pt.get<type>(login_qq_str + "." #key, __general_##key); \
            Log::d(TAG, #key "=" + to_string(config.key))
        #define GET_BOOL_CONFIG(key) \
            auto __general_##key = pt.get<bool>("general." #key, config.key, bool_translator); \
            config.key = pt.get<bool>(login_qq_str + "." #key, __general_##key, bool_translator); \
            Log::d(TAG, #key "=" + to_string(config.key))
        GET_CONFIG(host, string);
        GET_CONFIG(port, unsigned short);
        GET_BOOL_CONFIG(use_http);
        GET_CONFIG(ws_host, string);
        GET_CONFIG(ws_port, unsigned short);
        GET_BOOL_CONFIG(use_ws);
        GET_CONFIG(ws_reverse_api_url, string);
        GET_CONFIG(ws_reverse_event_url, string);
        GET_CONFIG(ws_reverse_reconnect_interval, unsigned long);
        GET_BOOL_CONFIG(ws_reverse_reconnect_on_code_1000);
        GET_BOOL_CONFIG(use_ws_reverse);
        GET_CONFIG(post_url, string);
        GET_CONFIG(access_token, string);
        GET_CONFIG(secret, string);
        GET_CONFIG(post_message_format, string);
        GET_BOOL_CONFIG(serve_data_files);
        GET_CONFIG(update_source, string);
        GET_CONFIG(update_channel, string);
        GET_BOOL_CONFIG(auto_check_update);
        GET_BOOL_CONFIG(auto_perform_update);
        GET_CONFIG(thread_pool_size, size_t);
        GET_CONFIG(server_thread_pool_size, size_t);
        GET_BOOL_CONFIG(convert_unicode_emoji);
        GET_BOOL_CONFIG(use_filter);
        #undef GET_CONFIG

        Log::i(TAG, u8"�����ļ����سɹ�");
    } catch (...) {
        // failed to load configurations
        Log::e(TAG, u8"�����ļ�����ʧ�ܣ����������ļ���ʽ�ͷ���Ȩ��");
        return nullopt;
    }

    return config;
}
