#include <hlclient/network/network_address.hpp>
#include <hlclient/network/network_runtime.hpp>

#include <array>
#include <charconv>
#include <memory>

#ifdef _WIN32
#    ifndef WIN32_LEAN_AND_MEAN
#        define WIN32_LEAN_AND_MEAN
#    endif
#    include <winsock2.h>
#    include <ws2tcpip.h>
#else
#    include <arpa/inet.h>
#    include <netdb.h>
#    include <netinet/in.h>
#    include <sys/socket.h>
#endif

namespace hlclient::network {
namespace {

[[nodiscard]] bool parse_decimal(
    const std::string_view text,
    unsigned int maximum,
    unsigned int& output) noexcept
{
    if (text.empty()) {
        return false;
    }

    unsigned int value = 0;
    const auto result = std::from_chars(text.data(), text.data() + text.size(), value, 10);
    if (result.ec != std::errc{} || result.ptr != text.data() + text.size() || value > maximum) {
        return false;
    }
    output = value;
    return true;
}

[[nodiscard]] std::optional<std::uint32_t> parse_ipv4(const std::string_view text) noexcept
{
    std::array<unsigned int, 4> octets{};
    std::size_t begin = 0;

    for (std::size_t index = 0; index < octets.size(); ++index) {
        const auto end = text.find('.', begin);
        if ((index < octets.size() - 1 && end == std::string_view::npos) ||
            (index == octets.size() - 1 && end != std::string_view::npos)) {
            return std::nullopt;
        }

        const auto segment_end = end == std::string_view::npos ? text.size() : end;
        if (!parse_decimal(text.substr(begin, segment_end - begin), 255, octets[index])) {
            return std::nullopt;
        }
        begin = segment_end + 1;
    }

    return (octets[0] << 24U) | (octets[1] << 16U) | (octets[2] << 8U) | octets[3];
}

} // namespace

std::optional<NetworkAddress> NetworkAddress::parse(const std::string_view endpoint) noexcept
{
    const auto separator = endpoint.rfind(':');
    if (separator == std::string_view::npos || separator == 0 || separator + 1 >= endpoint.size()) {
        return std::nullopt;
    }

    const auto address = parse_ipv4(endpoint.substr(0, separator));
    unsigned int port = 0;
    if (!address || !parse_decimal(endpoint.substr(separator + 1), 65'535, port)) {
        return std::nullopt;
    }

    return NetworkAddress{*address, static_cast<std::uint16_t>(port)};
}

std::optional<NetworkAddress> NetworkAddress::resolve_ipv4(
    const NetworkRuntime& runtime,
    const std::string_view host,
    const std::uint16_t port,
    std::string& error)
{
    error.clear();
    if (!runtime.valid()) {
        error = "Network runtime is unavailable: " + runtime.error_message();
        return std::nullopt;
    }
    if (host.empty()) {
        error = "Host name is empty";
        return std::nullopt;
    }
    if (host.find('\0') != std::string_view::npos) {
        error = "Host name contains an embedded NUL byte";
        return std::nullopt;
    }

    const std::string host_string{host};
    const auto service = std::to_string(port);
    addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_protocol = IPPROTO_UDP;
    hints.ai_flags = AI_NUMERICSERV;

    addrinfo* raw_results = nullptr;
    const int result = getaddrinfo(host_string.c_str(), service.c_str(), &hints, &raw_results);
    if (result != 0) {
        error = "IPv4 resolution failed with error " + std::to_string(result);
        return std::nullopt;
    }
    const std::unique_ptr<addrinfo, decltype(&freeaddrinfo)> results{raw_results, &freeaddrinfo};

    std::optional<NetworkAddress> resolved;
    if (results && results->ai_addr != nullptr &&
        results->ai_addrlen >= static_cast<int>(sizeof(sockaddr_in))) {
        const auto* address = reinterpret_cast<const sockaddr_in*>(results->ai_addr);
        resolved = NetworkAddress{ntohl(address->sin_addr.s_addr), ntohs(address->sin_port)};
    } else {
        error = "IPv4 resolution returned no usable address";
    }
    return resolved;
}

std::string NetworkAddress::to_string() const
{
    const auto octet = [this](const unsigned int shift) {
        return (ipv4_host_order_ >> shift) & 0xffU;
    };
    return std::to_string(octet(24)) + '.' + std::to_string(octet(16)) + '.' +
           std::to_string(octet(8)) + '.' + std::to_string(octet(0)) + ':' +
           std::to_string(port_);
}

} // namespace hlclient::network
