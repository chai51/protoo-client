#ifndef CHAI51_PEER
#define CHAI51_PEER

#include <stdint.h>
#include <memory>
#include <random>
#include <functional>

#include "json.hpp"
#include "WebSocketTransport.h"

namespace protoo
{
	using nlohmann::json;

	typedef std::function<void(void)> event_handler;
	typedef event_handler open_handler;
	typedef event_handler disconnected_handler;
	typedef event_handler close_handler;
	typedef std::function<void(int)>  failed_handler;

	typedef std::function<void(const json&)> accept_handler;
	typedef std::function<void(int, const char*)> reject_handler;
	typedef std::function<void(const json&, accept_handler, reject_handler)> request_handler;
	typedef std::function<void(const json&)> notification_handler;

	class Peer
	{
	public:
		Peer(std::unique_ptr<WebSocketTransport> transport);
		~Peer();
		json request(const std::string& method, const json& data);
		void notify(const std::string& method, const json& data);
		void close();
		void set_open_handler(open_handler h) { open_handler_ = h; }
		void set_disconnected_handler(disconnected_handler h) { disconnected_handler_ = h; }
		void set_close_handler(close_handler h) { close_handler_ = h; }
		void set_failed_handler(failed_handler h) { failed_handler_ = h; }
		void set_request_handler(request_handler h) { request_handler_ = h; }
		void set_notification_handler(notification_handler h) { notification_handler_ = h; }

		bool closed() { return closed_; }
		bool connected() { return connected_; }
	protected:
		void onOpen();
		void onDisconnected();
		void onFailed(int currentAttempt);
		void onClose();
		void onMessage(const json& message);

		void handleRequest(const json& request);
		void handleResponse(const json& response);
		void handleNotification();

		void onTimer();

		typedef struct 
		{
			std::promise<json> promise;
			std::chrono::steady_clock::time_point clock;
		}sent_t;
	private:
		std::unique_ptr<WebSocketTransport> transport_;
		bool closed_{ false };
		bool connected_{ false };
		std::random_device random_;

		std::map<int, sent_t> sents_;
		std::mutex mtx_sents_;

		std::condition_variable cond_notification_;
		std::mutex mtx_notification_;
		std::list<json> notification_message_;
		std::unique_ptr<std::thread> notification_thread_;

		open_handler open_handler_;
		disconnected_handler disconnected_handler_;
		close_handler close_handler_;
		failed_handler failed_handler_;
		request_handler request_handler_;
		notification_handler notification_handler_;

		std::unique_ptr<std::thread> open_thread_;
		std::unique_ptr<std::thread> timer_thread_;
	};
}


#endif	// CHAI51_PEER