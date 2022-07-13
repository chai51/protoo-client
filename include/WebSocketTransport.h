#ifndef CHAI51_WEB_SOCKET_TRANSPORT
#define CHAI51_WEB_SOCKET_TRANSPORT

#include <string>
#include <websocketpp/client.hpp>
#include <websocketpp/config/asio_client.hpp>
#include <websocketpp/config/asio.hpp>
#include <websocketpp/config/asio_no_tls_client.hpp>
#include <websocketpp/client.hpp>
#include <websocketpp/config/asio_no_tls.hpp>

#include <boost/log/trivial.hpp>

#include "json.hpp"

#define PROTOO_LOG_TRACE(logger) std::cout << __FUNCTION__
#define PROTOO_LOG_DEBUG(logger) std::cout << __FUNCTION__
#define PROTOO_LOG_INFO(logger)  std::cout << __FUNCTION__
#define PROTOO_LOG_WARN(logger)  std::cout << __FUNCTION__
#define PROTOO_LOG_ERROR(logger) std::cerr << __FUNCTION__
#define PROTOO_LOG_FATAL(logger) std::cerr << __FUNCTION__

namespace protoo
{
	class WebSocketTransport
	{
		friend class Peer;
		typedef websocketpp::client<websocketpp::config::asio_tls_client> client;
		typedef websocketpp::lib::shared_ptr<websocketpp::lib::asio::ssl::context> context_ptr;
	public:
		WebSocketTransport(const std::string& url, void* options);

		void close();
		void send(const nlohmann::json& message);

	protected:
		void runWebSocket();

		void onMessage(websocketpp::connection_hdl hdl, client::message_ptr msg);
		void onOpen(websocketpp::connection_hdl hdl);
		void onClose(websocketpp::connection_hdl hdl);
		void onFail(websocketpp::connection_hdl hdl);
		context_ptr onTlsInit(const char* hostname, websocketpp::connection_hdl);

		static bool verify_certificate(const char* hostname, bool preverified, boost::asio::ssl::verify_context& ctx);
		static bool verify_subject_alternative_name(const char* hostname, X509* cert);
		static bool verify_common_name(char const* hostname, X509* cert);
	private:
		bool closed_{ false };
		bool wasConnected_{ false };
		std::string url_;
		std::string err_;

		std::unique_ptr<client> endpoint_;
		websocketpp::connection_hdl ws_;
		std::unique_ptr<std::thread> thread_;
		uint32_t currentAttempt_{ 0 };

		std::shared_ptr<client::alog_type> m_alog;
		std::shared_ptr<client::elog_type> m_elog;
		
		std::function<void(void)> open_handler_;
		std::function<void(void)> disconnected_handler_;
		std::function<void(void)> close_handler_;
		std::function<void(int)> failed_handler_;
		std::function<void(const nlohmann::json&)> message_handler_;
	};
}

#endif	// WEB_SOCKET_TRANSPORT