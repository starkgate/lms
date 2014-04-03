#ifndef REMOTE_CONNECTION_HPP
#define REMOTE_CONNECTION_HPP

#include <array>
#include <memory>

#include <boost/asio.hpp>

#include "RequestHandler.hpp"

#include "messages/Header.hpp"

namespace Remote {

namespace Server {

class ConnectionManager;

/// Represents a single connection from a client.
class Connection : public std::enable_shared_from_this<Connection>
{
	public:

		Connection(const Connection&) = delete;
		Connection& operator=(const Connection&) = delete;

		typedef std::shared_ptr<Connection> pointer;

		/// Construct a connection with the given io_service.
		explicit Connection(boost::asio::ip::tcp::socket socket,
				ConnectionManager& manager, RequestHandler& handler);

		boost::asio::ip::tcp::socket& socket();

		/// Start the first asynchronous operation for the connection.
		void start();

		/// Stop all asynchronous operations associated with the connection.
		void stop();

	private:
		/// Handle completion of a read operation.
		void handleRead(const boost::system::error_code& e,
				std::size_t bytes_transferred);

		/// Handle completion of a write operation.
		void handleWrite(const boost::system::error_code& e);

		/// Socket for the connection.
		boost::asio::ip::tcp::socket _socket;

		/// The manager for this connection.
		ConnectionManager& _connectionManager;

		/// The handler used to process the incoming requests.
		RequestHandler& _requestHandler;

		// TODO use streambuffers

		/// The incoming request.
//		request request_;

		/// The parser for the incoming request.
//		request_parser request_parser_;

		/// The reply to be sent back to the client.
//		reply reply_;

		std::array<unsigned char, Header::size>	_headerBuffer;
};


} // namespace Server

} // namespace Remote

#endif


