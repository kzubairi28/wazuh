/* Copyright (C) 2015-2022, Wazuh Inc.
 * All rights reserved.
 *
 * This program is free software; you can redistribute it
 * and/or modify it under the terms of the GNU General Public
 * License (version 2) as published by the FSF - Free Software
 * Foundation.
 */

#include "eventEndpoint.hpp"

#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <stdexcept>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>
#include <unistd.h>

#include <logging/logging.hpp>

using uvw::ErrorEvent;
using uvw::Loop;
using uvw::UDPDataEvent;
using uvw::UDPHandle;

static constexpr int MAX_MSG_SIZE = 65536 + 512;

namespace engineserver::endpoints
{

/**
 * @brief This function opens, binds and configures a unix datagram socket.
 * @todo The code was extracted from the Wazuh source, so it must be adapted
 * when the engine is integrated to the rest of the Wazuh code to avoid
 * code duplicity.
 * @param path (const char *) Contains the absolute path to the unix datagram
 * socket
 * @return (int) Returns either the file descriptor value or -1
 */
static inline int bindUnixDatagramSocket(const char* path)
{
    struct sockaddr_un n_us;
    int socketFd = 0;

    /* TODO: Check the unlink's parameter before unlinking it (to be sure that it is a
     * socket and not a regular file) */
    unlink(path);

    memset(&n_us, 0, sizeof(n_us));
    n_us.sun_family = AF_UNIX;
    strncpy(n_us.sun_path, path, sizeof(n_us.sun_path) - 1);

    socketFd = socket(PF_UNIX, SOCK_DGRAM, 0);
    if (0 > socketFd)
    {
        return -1;
    }

    if (bind(socketFd, (struct sockaddr*)&n_us, SUN_LEN(&n_us)) < 0)
    {
        close(socketFd);
        return -1;
    }

    /* Change permissions */
    if (chmod(path, 0660) < 0)
    {
        close(socketFd);
        return -1;
    }

    int len;
    socklen_t optlen = sizeof(len);

    /* Get current maximum size */
    if (-1 == getsockopt(socketFd, SOL_SOCKET, SO_RCVBUF, (void*)&len, &optlen))
    {
        len = 0;
    }

    /* Set maximum message size */
    if (len < MAX_MSG_SIZE)
    {
        len = MAX_MSG_SIZE;
        if (setsockopt(socketFd, SOL_SOCKET, SO_RCVBUF, (const void*)&len, optlen) < 0)
        {
            close(socketFd);
            return -1;
        }
    }

    // Set close-on-exec
    if (-1 == fcntl(socketFd, F_SETFD, FD_CLOEXEC))
    {
        WAZUH_LOG_ERROR(
            "Cannot set close-on-exec flag to socket: {} ({})", strerror(errno), errno);
    }

    return (socketFd);
}

EventEndpoint::EventEndpoint(const std::string& path, ServerOutput& eventBuffer)
    : BaseEndpoint {path, eventBuffer}
    , m_loop {Loop::getDefault()}
    , m_handle {m_loop->resource<DatagramSocketHandle>()}
{

    m_handle->on<ErrorEvent>(
        [this](const ErrorEvent& event, DatagramSocketHandle& datagramSocketHandle)
        {
            WAZUH_LOG_ERROR("Datagram Socket ErrorEvent: endpoint[{}] error: code=[{}]; "
                            "name=[{}]; message=[{}]",
                            m_path,
                            event.code(),
                            event.name(),
                            event.what());
        });

    m_handle->on<DatagramSocketEvent>(
        [this](const DatagramSocketEvent& event, DatagramSocketHandle& handle)
        {
            auto client = handle.loop().resource<DatagramSocketHandle>();

            client->on<ErrorEvent>(
                [this](const ErrorEvent& event, DatagramSocketHandle& client)
                {
                    WAZUH_LOG_ERROR("Datagram Socket ErrorEvent: endpoint[{}] "
                                    "error: code=[{}]; "
                                    "name=[{}]; message=[{}]",
                                    m_path,
                                    event.code(),
                                    event.name(),
                                    event.what());
                });

            auto strRequest = std::string {event.data.get(), event.length};
            while (!m_out.try_enqueue(strRequest))
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
        });

    m_socketFd = bindUnixDatagramSocket(m_path.c_str());

    if (m_socketFd <= 0)
    {
        WAZUH_LOG_ERROR("Error while opening Datagram Socket ({}): {} ({})",
                        m_path,
                        strerror(errno),
                        errno);
    }
}

void EventEndpoint::configure(void)
{
    if (m_socketFd > 0)
    {
        m_handle->open(m_socketFd);
        m_handle->recv();
    }
    else
    {
        WAZUH_LOG_ERROR("Datagram Socket ({}) file descriptor is invalid: FD={}.",
                        m_path,
                        m_socketFd);
    }
}

void EventEndpoint::run()
{
    m_loop->run<Loop::Mode::DEFAULT>();
}

void EventEndpoint::close(void)
{
    m_loop->stop(); /// Stops the loop
    m_loop->walk([](uvw::BaseHandle& handle)
                 { handle.close(); }); /// Triggers every handle's close callback
    m_loop->run(); /// Runs the loop again, so every handle is able to receive
                   /// its close callback
    m_loop->clear();
    m_loop->close();
}

EventEndpoint::~EventEndpoint()
{
    close();
}

} // namespace engineserver::endpoints
