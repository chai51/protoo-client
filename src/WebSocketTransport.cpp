#include "WebSocketTransport.h"
#include <iostream>
#ifdef WIN32
#define strcasecmp _stricmp
#endif // WIN32


namespace protoo
{
	WebSocketTransport::WebSocketTransport(const std::string& url, void* options)
		: url_(url)
		, thread_(std::make_unique<std::thread>(&WebSocketTransport::runWebSocket, this))
	{
		PROTOO_LOG_TRACE(logger) << " [url:" << url << "]";
	}

	void WebSocketTransport::close()
	{
		if (closed_)
			return;

		closed_ = true;
		if(close_handler_) close_handler_();	

		endpoint_.reset();
	}

	void WebSocketTransport::send(const nlohmann::json& message)
	{
		if (closed_)
			throw std::exception("transport closed");
		if (ws_.expired())
			throw std::exception("transport expired");

		endpoint_->send(ws_, message.dump(), websocketpp::frame::opcode::text);
	}

	void WebSocketTransport::runWebSocket()
	{
		websocketpp::uri uri(url_);

		auto init = [this, &uri]()
		{
			using websocketpp::lib::placeholders::_1;
			using websocketpp::lib::placeholders::_2;
			using websocketpp::lib::bind;
			if (endpoint_)
			{
				endpoint_.reset();
			}
			endpoint_ = std::make_unique<client>();

			endpoint_->set_access_channels(websocketpp::log::alevel::none);
			//m_endpoint->set_error_channels(websocketpp::log::elevel::all);

			// Initialize ASIO
			endpoint_->init_asio();

			// Register our handlers
			endpoint_->set_message_handler(bind(&WebSocketTransport::onMessage, this, _1, _2));
			endpoint_->set_open_handler(bind(&WebSocketTransport::onOpen, this, _1));
			endpoint_->set_close_handler(bind(&WebSocketTransport::onClose, this, _1));
			endpoint_->set_fail_handler(bind(&WebSocketTransport::onFail, this, _1));
			endpoint_->set_tls_init_handler(bind(&WebSocketTransport::onTlsInit, this, uri.get_host().c_str(), _1));
		};

		try
		{
			currentAttempt_++;
			PROTOO_LOG_DEBUG(logger) << "[currentAttempt:" << currentAttempt_ << "]";

			init();
			websocketpp::lib::error_code ec;
			client::connection_ptr con = endpoint_->get_connection(url_, ec);

			if (ec)
			{
				err_ = ec.message();
				return;
			}
			con->add_subprotocol("protoo");

			endpoint_->connect(con);

			endpoint_->run();
		}
		catch (websocketpp::exception& e)
		{
			err_ = e.what();
		}
		catch (std::exception& e)
		{
			err_ = e.what();
		}
	}

	void WebSocketTransport::onMessage(websocketpp::connection_hdl hdl, client::message_ptr msg)
	{
		if (closed_)
			return;

		try
		{
			auto message = nlohmann::json::parse(msg->get_payload());

			if (!message_handler_)
			{
				PROTOO_LOG_ERROR(logger) << "no listeners for WebSocket 'message' event, ignoring received message";
				return;
			}
			message_handler_(message);
		}
		catch (const std::exception& e)
		{
			return;
		}
	}

	void WebSocketTransport::onOpen(websocketpp::connection_hdl hdl)
	{
		ws_ = hdl;
		wasConnected_ = true;
		if(open_handler_) open_handler_();
	}

	void WebSocketTransport::onClose(websocketpp::connection_hdl hdl)
	{
		if (closed_)
			return;

		PROTOO_LOG_ERROR(logger) << "WebSocket 'error' event";
	}

	void WebSocketTransport::onFail(websocketpp::connection_hdl hdl)
	{
		if (closed_)
			return;

		client::connection_ptr con = endpoint_->get_con_from_hdl(hdl);
		auto code = con->get_ec();
		PROTOO_LOG_WARN(logger) << "WebSocket 'close' event [code:" << code.value() << ", message:" << code.message() << "]";

		if (code.value() != 400)
		{
			if (!wasConnected_)
			{
				if(failed_handler_) failed_handler_(currentAttempt_);

				if (closed_)
					return;
			}
			else
			{
				if(disconnected_handler_) disconnected_handler_();
				if (closed_)
					return;

				auto thread = std::make_unique<std::thread>(&WebSocketTransport::runWebSocket, this);
				std::swap(thread_, thread);
				return;
			}
		}
		closed_ = true;
		close_handler_();
	}

	WebSocketTransport::context_ptr WebSocketTransport::onTlsInit(const char* hostname, websocketpp::connection_hdl)
	{
		using websocketpp::lib::placeholders::_1;
		using websocketpp::lib::placeholders::_2;
		using websocketpp::lib::bind;
		context_ptr ctx = websocketpp::lib::make_shared<boost::asio::ssl::context>(boost::asio::ssl::context::sslv23);

		try {
			ctx->set_options(boost::asio::ssl::context::default_workarounds |
				boost::asio::ssl::context::no_sslv2 |
				boost::asio::ssl::context::no_sslv3 |
				boost::asio::ssl::context::single_dh_use);


			ctx->set_verify_mode(boost::asio::ssl::verify_none);
			ctx->set_verify_callback(bind(&WebSocketTransport::verify_certificate, hostname, _1, _2));

			// Here we load the CA certificates of all CA's that this client trusts.
			// ctx->load_verify_file("ca-chain.cert.pem");
		}
		catch (std::exception& e) {
			std::cout << e.what() << std::endl;
		}
		return ctx;
	}

	bool WebSocketTransport::verify_certificate(const char* hostname, bool preverified, boost::asio::ssl::verify_context& ctx)
	{
		// The verify callback can be used to check whether the certificate that is
		// being presented is valid for the peer. For example, RFC 2818 describes
		// the steps involved in doing this for HTTPS. Consult the OpenSSL
		// documentation for more details. Note that the callback is called once
		// for each certificate in the certificate chain, starting from the root
		// certificate authority.

		// Retrieve the depth of the current cert in the chain. 0 indicates the
		// actual server cert, upon which we will perform extra validation
		// (specifically, ensuring that the hostname matches. For other certs we
		// will use the 'preverified' flag from Asio, which incorporates a number of
		// non-implementation specific OpenSSL checking, such as the formatting of
		// certs and the trusted status based on the CA certs we imported earlier.
		int depth = X509_STORE_CTX_get_error_depth(ctx.native_handle());

		// if we are on the final cert and everything else checks out, ensure that
		// the hostname is present on the list of SANs or the common name (CN).
		if (depth == 0 && preverified) {
			X509* cert = X509_STORE_CTX_get_current_cert(ctx.native_handle());

			if (verify_subject_alternative_name(hostname, cert)) {
				return true;
			}
			else if (verify_common_name(hostname, cert)) {
				return true;
			}
			else {
				return false;
			}
		}

		return preverified;
	}

	bool WebSocketTransport::verify_subject_alternative_name(const char* hostname, X509* cert)
	{
		STACK_OF(GENERAL_NAME)* san_names = NULL;

		san_names = (STACK_OF(GENERAL_NAME)*) X509_get_ext_d2i(cert, NID_subject_alt_name, NULL, NULL);
		if (san_names == NULL) {
			return false;
		}

		int san_names_count = sk_GENERAL_NAME_num(san_names);

		bool result = false;

		for (int i = 0; i < san_names_count; i++) {
			const GENERAL_NAME* current_name = sk_GENERAL_NAME_value(san_names, i);

			if (current_name->type != GEN_DNS) {
				continue;
			}

			char const* dns_name = (char const*)ASN1_STRING_get0_data(current_name->d.dNSName);

			// Make sure there isn't an embedded NUL character in the DNS name
			if (ASN1_STRING_length(current_name->d.dNSName) != strlen(dns_name)) {
				break;
			}
			// Compare expected hostname with the CN
			result = (strcasecmp(hostname, dns_name) == 0);
		}
		sk_GENERAL_NAME_pop_free(san_names, GENERAL_NAME_free);

		return result;
	}

	bool WebSocketTransport::verify_common_name(char const* hostname, X509* cert)
	{
		// Find the position of the CN field in the Subject field of the certificate
		int common_name_loc = X509_NAME_get_index_by_NID(X509_get_subject_name(cert), NID_commonName, -1);
		if (common_name_loc < 0) {
			return false;
		}

		// Extract the CN field
		X509_NAME_ENTRY* common_name_entry = X509_NAME_get_entry(X509_get_subject_name(cert), common_name_loc);
		if (common_name_entry == NULL) {
			return false;
		}

		// Convert the CN field to a C string
		ASN1_STRING* common_name_asn1 = X509_NAME_ENTRY_get_data(common_name_entry);
		if (common_name_asn1 == NULL) {
			return false;
		}

		char const* common_name_str = (char const*)ASN1_STRING_get0_data(common_name_asn1);

		// Make sure there isn't an embedded NUL character in the CN
		if (ASN1_STRING_length(common_name_asn1) != strlen(common_name_str)) {
			return false;
		}

		// Compare expected hostname with the CN
		return (strcasecmp(hostname, common_name_str) == 0);
	}

}
