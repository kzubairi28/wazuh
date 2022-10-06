#ifndef _API_WAZUH_REQUEST_HPP
#define _API_WAZUH_REQUEST_HPP

#include <json/json.hpp>

namespace api
{

/**
 * @brief A standard protocol for internal communication between Wazuh components
 *
 * https://github.com/wazuh/wazuh/issues/5934
 */
class WazuhRequest
{
    static constexpr auto VERSION_SUPPORTED {1};

    int m_version;
    json::Json m_jrequest;
    std::optional<std::string> m_error;

public:
    // TODO Delete explicit when json constructor does not throw exceptions
    /**
     * @brief Construct a new Wazuh Request object
     *
     * @param json
     */
    explicit WazuhRequest(const json::Json& json)
        : m_jrequest(json)
    {
        m_version = -1;
        m_error = validate();
    }

    /**
     * @brief Destroy the Wazuh Request object
     */
    ~WazuhRequest() = default;

    /**
     * @brief Get command from the request
     *
     * @return std::string command
     * @return empty if the request is not valid
     */
    std::optional<std::string> getCommand() const {
        return  isValid() ? m_jrequest.getString("/command") : std::nullopt;
    };

    /**
     * @brief Get parameters from the request
     *
     * @return json::Json parameters
     * @return empty if the request is not valid
     */
    std::optional<json::Json> getParameters() const
    {
        return  isValid() ? m_jrequest.getJson("/parameters") : std::nullopt;
    }

    /**
     * @brief Check if the request is valid
     *
     * @return true if the request is valid
     * @return false if the request is not valid
     */
    bool isValid() const { return !m_error.has_value(); }

    /**
     * @brief Get the error message
     *
     * @return empty if the request is valid
     * @return std::optional<std::string> error message
     */
    std::optional<std::string> error() const { return m_error; }

    /**
     * @brief Create a Wazuh Request object from a command and parameters
     *
     * @param command Command name
     * @param parameters Parameters
     * @return WazuhRequest
     *
     * @throw std::runtime_error if the command is empty or the parameters are not a JSON
     * object
     */
    static WazuhRequest create(std::string_view command, const json::Json& parameters);

private:
    /**
     * @brief Validate the wazuh request protocol
     *
     * @return std::optional<std::string> Error message if the request is not valid
     * @return nullopt if the request is valid
     */
    std::optional<std::string> validate() const;
};

}

#endif // _API_WAZUH_REQUEST_HPP