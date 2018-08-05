#include "message_transporter.h"
#include "string_coding.h"
#include <boost/format.hpp>


#undef MONERO_DEFAULT_LOG_CATEGORY
#define MONERO_DEFAULT_LOG_CATEGORY "wallet.mms"

namespace mms
{

namespace bitmessage_rpc
{

  struct message_info
  {
    uint32_t encodingType;
    std::string toAddress;
    uint32_t read;
    std::string msgid;
    std::string message;
    std::string fromAddress;
    std::string receivedTime;
    std::string subject;

    BEGIN_KV_SERIALIZE_MAP()
      KV_SERIALIZE(encodingType)
      KV_SERIALIZE(toAddress)
      KV_SERIALIZE(read)
      KV_SERIALIZE(msgid)
      KV_SERIALIZE(message);
      KV_SERIALIZE(fromAddress)
      KV_SERIALIZE(receivedTime)
      KV_SERIALIZE(subject)
    END_KV_SERIALIZE_MAP()
  };

  struct inbox_messages_response
  {
    std::vector<message_info> inboxMessages;

    BEGIN_KV_SERIALIZE_MAP()
      KV_SERIALIZE(inboxMessages)
    END_KV_SERIALIZE_MAP()
  };

}

message_transporter::message_transporter()
{
  m_bitmessage_url = "http://localhost:8442/";
  m_bitmessage_user = "username";
  m_bitmessage_password = "password";
  
  boost::optional<epee::net_utils::http::login> bitmessage_login{};
  bitmessage_login.emplace(m_bitmessage_user, m_bitmessage_password);

  m_http_client.set_server(m_bitmessage_url, bitmessage_login, false);
}

bool message_transporter::receive_messages(const cryptonote::account_public_address &destination_monero_address,
                                           const std::string &destination_transport_address,
                                           std::vector<transport_message> &messages)
{
  // The message body of the Bitmessage message is basically the transport message, as JSON (and nothing more).
  // Weeding out other, non-MMS messages is done in a simple way: If it deserializes without error, it's an MMS message
  // That JSON is Base64-encoded by the MMS because the Monero epee JSON serializer does not escape anything and happily
  // includes even 0 (NUL) in strings, which might confuse Bitmessage or at least display confusingly in the client.
  // There is yet another Base64-encoding of course as part of the Bitmessage API for the message body parameter
  // The Bitmessage API call "getAllInboxMessages" gives back a JSON array with all the messages (despite using
  // XML-RPC for the calls, and not JSON-RPC ...)
  try
  {
    std::string request;
    start_xml_rpc_cmd(request, "getAllInboxMessages");
    end_xml_rpc_cmd(request);
    std::string answer;
    post_request(request, answer);
    
    std::string json = get_str_between_tags(answer, "<string>", "</string>");
    bitmessage_rpc::inbox_messages_response bitmessage_res;
    epee::serialization::load_t_from_json(bitmessage_res, json);
    size_t size = bitmessage_res.inboxMessages.size();
    messages.clear();
    
    for (size_t i = 0; i < size; ++i)
    {
      bitmessage_rpc::message_info message_info = bitmessage_res.inboxMessages[i];
      transport_message message;
      bool is_mms_message = false;
      try
      {
	// First Base64-decoding: The message body is Base64 in the Bitmessage API
	std::string message_body = epee::string_encoding::base64_decode(message_info.message);
	// Second Base64-decoding: The MMS uses Base64 to hide non-textual data in its JSON from Bitmessage
	json = epee::string_encoding::base64_decode(message_body);
        epee::serialization::load_t_from_json(message, json);
	is_mms_message = true;
      }
      catch(const std::exception& e)
      {
      }
      if (is_mms_message)
      {
	if (message.destination_monero_address == destination_monero_address)
	{
	  message.transport_id = message_info.msgid;
	  messages.push_back(message);
	}
      }
    }

  }
  catch (const std::exception& e)
  {
    epee::json_rpc::error er;
    handle_rpc_exception(std::current_exception(), er, MESSAGE_TRANSPORTER_ERROR_CODE_UNKNOWN_ERROR);
    return false;
  }
  return true;
}

bool message_transporter::send_message(const transport_message &message)
{
  // <toAddress> <fromAddress> <subject> <message> [encodingType [TTL]]
  try {
    std::string request;
    start_xml_rpc_cmd(request, "sendMessage");
    add_xml_rpc_string_param(request, message.destination_transport_address);
    add_xml_rpc_string_param(request, message.source_transport_address);
    add_xml_rpc_base64_param(request, "MMS");
    std::string json = epee::serialization::store_t_to_json(message);
    std::string message_body = epee::string_encoding::base64_encode(json);  // See comment in "receive_message" about reason for (double-)Base64 encoding
    add_xml_rpc_base64_param(request, message_body);
    add_xml_rpc_integer_param(request, 2);
    end_xml_rpc_cmd(request);
    std::string answer;
    post_request(request, answer);
  }
  catch (const std::exception& e)
  {
    epee::json_rpc::error er;
    handle_rpc_exception(std::current_exception(), er, MESSAGE_TRANSPORTER_ERROR_CODE_UNKNOWN_ERROR);
    return false;
  }
  return true;
}

bool message_transporter::delete_message(const std::string &transport_id)
{
  try {
    std::string request;
    start_xml_rpc_cmd(request, "trashMessage");
    add_xml_rpc_string_param(request, transport_id);
    end_xml_rpc_cmd(request);
    std::string answer;
    post_request(request, answer);
  }
  catch (const std::exception& e)
  {
    epee::json_rpc::error er;
    handle_rpc_exception(std::current_exception(), er, MESSAGE_TRANSPORTER_ERROR_CODE_UNKNOWN_ERROR);
    return false;
  }
  return true;
}

void message_transporter::handle_rpc_exception(const std::exception_ptr& e, epee::json_rpc::error& er, int default_error_code)
{
  try
  {
    std::rethrow_exception(e);
  }
  catch (const std::exception& e)
  {
    er.code = default_error_code;
    er.message = e.what();
  }
  catch (...)
  {
    er.code = MESSAGE_TRANSPORTER_ERROR_CODE_UNKNOWN_ERROR;
    er.message = "MESSAGE_TRANSPORTER_ERROR_CODE_UNKNOWN_ERROR";
  }
}

bool message_transporter::post_request(const std::string &request, std::string &answer)
{
  // Somehow things do not work out if one tries to connect "m_http_client" to Bitmessage
  // and keep it connected over the course of several calls. But with a new connection per
  // call and disconnecting after the call there is no problem (despite perhaps a small
  // slowdown)
  epee::net_utils::http::fields_list additional_params;
  
  // Basic access authentication according to RFC 7617 (which the epee HTTP classes do not seem to support?)
  std::string user_password(m_bitmessage_user + ":" + m_bitmessage_password);
  std::string auth_string = epee::string_encoding::base64_encode(user_password);
  auth_string.insert(0, "Basic ");
  additional_params.push_back(std::make_pair("Authorization", auth_string));
  
  additional_params.push_back(std::make_pair("Content-Type", "application/xml; charset=utf-8"));
  const epee::net_utils::http::http_response_info* response = NULL;
  std::chrono::milliseconds timeout = std::chrono::seconds(15);
  bool r = m_http_client.invoke("/", "POST", request, timeout, std::addressof(response), std::move(additional_params));
  if (r)
  {
    answer = response->m_body;
  }
  else
  {
    LOG_ERROR("POST request to Bitmessage failed: " << request);
  }
  m_http_client.disconnect();  // see comment above
  
  return r;
}

std::string message_transporter::get_str_between_tags(const std::string &s, const std::string &start_delim, const std::string &stop_delim)
{
    size_t first_delim_pos = s.find(start_delim);
    size_t end_pos_of_first_delim = first_delim_pos + start_delim.length();
    size_t last_delim_pos = s.find(stop_delim);
    return s.substr(end_pos_of_first_delim, last_delim_pos - end_pos_of_first_delim);
}

void message_transporter::start_xml_rpc_cmd(std::string &xml, const std::string &method_name)
{
  xml = (boost::format("<?xml version=\"1.0\"?><methodCall><methodName>%s</methodName><params>") % method_name).str();
}

void message_transporter::add_xml_rpc_string_param(std::string &xml, const std::string &param)
{
  xml += (boost::format("<param><value><string>%s</string></value></param>") % param).str();
}

void message_transporter::add_xml_rpc_base64_param(std::string &xml, const std::string &param)
{
  // Bitmessage expects some arguments Base64-encoded, but it wants them as parameters of type "string", not "base64" that is also part of XML-RPC
  std::string encoded_param = epee::string_encoding::base64_encode(param);
  xml += (boost::format("<param><value><string>%s</string></value></param>") % encoded_param).str();
}

void message_transporter::add_xml_rpc_integer_param(std::string &xml, const int32_t &param)
{
  xml += (boost::format("<param><value><int>%i</int></value></param>") % param).str();
}

void message_transporter::end_xml_rpc_cmd(std::string &xml)
{
  xml += "</params></methodCall>";
}



}

