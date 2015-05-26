#include "cocaine/detail/service/node_v2/dispatch/worker.hpp"

using namespace cocaine;

worker_rpc_dispatch_t::worker_rpc_dispatch_t(upstream<outcoming_tag>& stream_, callback_type callback):
    dispatch<incoming_tag>("W2C"),
    stream(std::move(stream_)),
    state(state_t::open),
    callback(callback)
{
    on<protocol::chunk>([&](const std::string& chunk) {
        try {
            stream = stream.send<protocol::chunk>(chunk);
        } catch (const cocaine::error_t&) {
            finalize(asio::error::connection_aborted);
        }
    });

    on<protocol::error>([&](int id, const std::string& reason) {
        try {
            stream.send<protocol::error>(id, reason);
            finalize();
        } catch (const cocaine::error_t&) {
            finalize(asio::error::connection_aborted);
        }
    });

    on<protocol::choke>([&]() {
        try {
            stream.send<protocol::choke>();
            finalize();
        } catch (const cocaine::error_t&) {
            finalize(asio::error::connection_aborted);
        }
    });
}

void
worker_rpc_dispatch_t::finalize(const std::error_code& ec) {
    std::lock_guard<std::mutex> lock(mutex);

    // Ensure that we call this method only once no matter what.
    if (state == state_t::closed) {
        // TODO: Log the error.
        return;
    }

    state = state_t::closed;
    // TODO: We have to send the error to the worker on any error occurred while writing to the
    // client stream, otherwise it may push its messages to the Hell forever.
    callback(ec);
}
