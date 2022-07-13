#include "Peer.h"
#include <boost/asio.hpp>
#include <chrono>

namespace protoo
{
	using nlohmann::json;

	Peer::Peer(std::unique_ptr<WebSocketTransport> transport)
		: transport_(std::move(transport))
	{
		if (transport_->closed_)
		{
			closed_ = true;
			if(close_handler_) close_handler_();
			return;
		}

		using websocketpp::lib::placeholders::_1;
		using websocketpp::lib::placeholders::_2;
		transport_->open_handler_ = std::bind(&Peer::onOpen, this);
		transport_->disconnected_handler_ = std::bind(&Peer::onDisconnected, this);
		transport_->close_handler_ = std::bind(&Peer::onClose, this);
		transport_->failed_handler_ = std::bind(&Peer::onFailed, this, _1);
		transport_->message_handler_ = std::bind(&Peer::onMessage, this, _1);
	}

	Peer::~Peer()
	{
		close();
	}

	json Peer::request(const std::string& method, const json& data)
	{
		std::uniform_int_distribution<uint16_t> u;
		int id = u(random_);
		json request =
		{
			{"request", true},
			{"id", id},
			{"method", method},
			{"data", data}
		};

		transport_->send(request);

		auto& promise = sents_[id];
		int size = sents_.size();

		boost::asio::io_service io;
		boost::asio::steady_timer timer(io, std::chrono::milliseconds(1500 * (15 + int(0.1 * size))));
		timer.async_wait([this, id](const boost::system::error_code& ec)
			{
				auto it = sents_.find(id);
				if (it == sents_.end())
				{
					return;
				}
				it->second.set_exception(std::make_exception_ptr(std::exception("request timeout")));
			});

		auto future = promise.get_future();
		const json& response = future.get();
		sents_.erase(id);
		return response;
	}

	void Peer::notify(const std::string& method, const json& data)
	{
		json notification =
		{
			{"notification", true},
			{"method", method},
			{"data", data}
		};
		transport_->send(notification);
	}

	void Peer::close()
	{
		closed_ = true;
		connected_ = false;

		transport_->close();
		sents_.clear();
		if (close_handler_) close_handler_();
	}

	void Peer::onOpen()
	{
		if (closed_)
			return;

		connected_ = true;
		if (open_handler_)
		{
			open_thread_ = std::make_unique<std::thread>(open_handler_);
		}
		notification_thread_ = std::make_unique<std::thread>(&Peer::handleNotification, this);
	}

	void Peer::onDisconnected()
	{
		if (closed_)
			return;

		connected_ = false;
		if(disconnected_handler_) disconnected_handler_();
	}

	void Peer::onFailed(int currentAttempt)
	{
		if (closed_)
			return;

		connected_ = false;
		if(failed_handler_) failed_handler_(currentAttempt);
	}

	void Peer::onClose()
	{
		if (closed_)
			return;

		closed_ = true;
		connected_ = false;
		if(close_handler_) close_handler_();
	}

	void Peer::onMessage(const json& message)
	{
		if (message.find("request") != message.end())
			handleRequest(message);
		else if (message.find("response") != message.end())
			handleResponse(message);
		else if (message.find("notification") != message.end())
		{
			mtx_notification_.lock();
			notification_message_.push_back(message);
			mtx_notification_.unlock();
			cond_notification_.notify_one();
		}
	}

	void Peer::handleRequest(const json& request)
	{
		try
		{
			if(request_handler_) request_handler_(request,
				[this, &request](const json& data)
				{
					json response =
					{
						{"response", true},
						{"id", request["id"]},
						{"ok", true},
						{"data", data}
					};
					transport_->send(response);
				},
				[this, &request](int errorCode, const std::string& errorReason)
				{
					json response =
					{
						{"response", true},
						{"id", request["id"]},
						{"ok", false},
						{"errorCode", errorCode},
						{"errorReason", errorReason}
					};
					transport_->send(response);
				});
		}
		catch (const std::exception& e)
		{
			json response =
			{
				{"response", true},
				{"id", request["id"]},
				{"ok", false},
				{"errorCode", 500},
				{"errorReason", e.what()}
			};
			transport_->send(response);
		}
	}

	void Peer::handleResponse(const json& response)
	{
		int id = response["id"];
		auto it = sents_.find(id);
		if (it == sents_.end())
		{
			PROTOO_LOG_ERROR(logger) << "received response does not match any sent request [id:" << id << "]";
			return;
		}

		auto okIt = response.find("ok");
		if (okIt != response.end()
			&& okIt->is_boolean()
			&& okIt->get<bool>())
		{
			it->second.set_value(response["data"]);
		}
		else
		{
			std::string reason = response["errorReason"];
			it->second.set_exception(std::make_exception_ptr(std::exception(reason.c_str())));
		}
	}

	void Peer::handleNotification()
	{
		while (!closed_)
		{
			std::unique_lock<std::mutex> lk(mtx_notification_);
			if (notification_message_.empty())
			{
				cond_notification_.wait(lk);
				if (closed_)
				{
					return;
				}
			}
			auto message = *notification_message_.begin();
			notification_message_.pop_front();
			lk.unlock();
			notification_handler_(message);
		}
	}

}

