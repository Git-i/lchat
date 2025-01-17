#pragma once
#include "asio.hpp"
#include "asio/impl/read.hpp"
#include "asio/placeholders.hpp"
#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <list>
#include <memory>
#include <span>
#include <system_error>
#include <vector>
#include "message.h"
class ChatConnection
{
private:
    asio::ip::tcp::socket m_socket;
    std::vector<std::byte> m_buffer;
    ChatConnection(asio::io_context& ctx): m_socket(ctx) {m_buffer.resize(12);}
public:
    uint32_t id;
    Glib::ustring username;
    static std::shared_ptr<ChatConnection> Create(asio::io_context& ctx)
    {
        return std::shared_ptr<ChatConnection>(new ChatConnection(ctx));
    }
    asio::ip::tcp::socket& socket()
    {
        return m_socket;
    }
    std::vector<std::byte>& buffer()
    {
        return m_buffer;
    }
};
class ChatServer
{
    asio::io_context& m_ctx;
    asio::ip::tcp::acceptor m_acceptor;
    std::list<std::shared_ptr<ChatConnection>> connections;
    bool listening = false;
    
public:
    ChatServer(asio::io_context& ctx, uint16_t port_no) : 
        m_ctx(ctx),
        m_acceptor(m_ctx, asio::ip::tcp::endpoint(asio::ip::tcp::v4(), port_no))
    {
        listen();
    }
    void listen()
    {
        auto connection = ChatConnection::Create(m_ctx);
        m_acceptor.async_accept(connection->socket(), std::bind(&ChatServer::on_accept, this, asio::placeholders::error, connection));
    }
    void broadcast_message(const std::vector<std::byte>& msg)
    {
        for(auto connection: connections)
        {
            asio::write(connection->socket(), asio::buffer(msg));
        }
    }
    void on_message(const std::error_code& error, std::shared_ptr<ChatConnection> connection)
    {
        std::cout << "msg recieved from id: " << connection->id << std::endl;
        auto [t, room_idx, msg_size] = Message::DecomposeHeader(connection->buffer());
        switch(t)
        {
            case MessageType::Communication:
            {
                std::vector<std::byte> msg_buf((size_t)msg_size+8);
                asio::read(connection->socket(), asio::buffer(msg_buf));
                Chat ch = Message::DecomposeChatBody(msg_buf);
                ch.user = connection->id;
                std::cout << "Recieved Message: " << ch.text << std::endl;
                broadcast_message(Message::MakeChatForForwarding(room_idx, ch));
                break;
            }
            case MessageType::UserDetails:
            {
                std::vector<std::byte> msg_buf((size_t)msg_size);
                asio::read(connection->socket(), asio::buffer(msg_buf));
                if(room_idx == connection->id)
                {
                    std::cout << "User name supply from user: " << connection->id;
                    connection->username = Message::DecomposeUserDetailBody(msg_buf);
                    std::cout << " name: " << connection->username << std::endl;
                    broadcast_message(Message::MakeUserDetailForForwarding(connection->username, connection->id));
                }
                //user detail request for a different id means to send that ids details
                else
                {
                    Glib::ustring name = "<Error Name>";
                    auto tgt = std::find_if(connections.begin(), connections.end(), [room_idx](auto ptr)->bool{
                        return ptr->id == room_idx;
                    });
                    std::cout << "User name of user " << room_idx << "requested by user " << connection->id << std::endl;
                    if(tgt == connections.end())
                        std::cerr << "Specified User doesn't exist" << std::endl;
                    else
                        name = (*tgt)->username;
                    asio::write(connection->socket(),
                        asio::buffer(Message::MakeUserDetailForForwarding(name, room_idx)));
                }
            }
            default: break;
        }
        listen_message(connection);
    }
    void listen_message(std::shared_ptr<ChatConnection> connection)
    {
        asio::async_read(connection->socket(), asio::buffer(connection->buffer().data(), Message::header_length), std::bind(&ChatServer::on_message, this, 
            asio::placeholders::error,
            connection));
    }
    void on_accept(const std::error_code& error, std::shared_ptr<ChatConnection> connection)
    {
        static uint32_t l_id = 0;
        if(!error)
        {
            connection->id = ++l_id;
            connections.push_back(connection);
            std::cout << "Successful connection made to user: " << connection->id << std::endl;
            asio::write(connection->socket(), asio::buffer(Message::ComposeUserId(connection->id)));
            listen_message(connection);
        }
        listen();
    }
};