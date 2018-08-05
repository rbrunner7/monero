/* 
 * File:   message_transporter.h
 * Author: root
 *
 * Created on August 3, 2018, 4:39 PM
 */

#pragma once
#include "serialization/keyvalue_serialization.h"
#include "cryptonote_basic/cryptonote_basic.h"
#include "cryptonote_basic/cryptonote_boost_serialization.h"
#include "cryptonote_basic/account_boost_serialization.h"
#include "cryptonote_basic/cryptonote_basic.h"
#include "net/http_server_impl_base.h"
#include "net/http_client.h"
#include "common/util.h"
#include "serialization/keyvalue_serialization.h"


namespace mms
{

struct transport_message
{
  cryptonote::account_public_address source_monero_address;
  std::string source_transport_address;
  cryptonote::account_public_address destination_monero_address;
  std::string destination_transport_address;
  crypto::chacha_iv iv;
  crypto::public_key encryption_public_key;
  uint64_t timestamp;
  uint32_t type;
  std::string content;
  crypto::hash hash;
  crypto::signature signature;
  std::string transport_id;

  BEGIN_KV_SERIALIZE_MAP()
    KV_SERIALIZE(source_monero_address)
    KV_SERIALIZE(source_transport_address)
    KV_SERIALIZE(destination_monero_address)
    KV_SERIALIZE(destination_transport_address)
    KV_SERIALIZE_VAL_POD_AS_BLOB_FORCE(iv)
    KV_SERIALIZE_VAL_POD_AS_BLOB_FORCE(encryption_public_key)
    KV_SERIALIZE(timestamp)
    KV_SERIALIZE(type)
    KV_SERIALIZE(content)
    KV_SERIALIZE_VAL_POD_AS_BLOB_FORCE(hash)
    KV_SERIALIZE_VAL_POD_AS_BLOB_FORCE(signature)
    KV_SERIALIZE(transport_id)
  END_KV_SERIALIZE_MAP()
};

#define MESSAGE_TRANSPORTER_ERROR_CODE_UNKNOWN_ERROR -1

class message_transporter {
public:
  message_transporter();
  bool send_message(const transport_message &message);
  bool receive_messages(const cryptonote::account_public_address &destination_monero_address,
                        const std::string &destination_transport_address,
                        std::vector<transport_message> &messages);
  bool delete_message(const std::string &transport_id);
  
private:
  epee::net_utils::http::http_simple_client m_http_client;
  std::string m_bitmessage_url;
  std::string m_bitmessage_user;
  std::string m_bitmessage_password;
  
  bool post_request(const std::string &request, std::string &answer);
  void handle_rpc_exception(const std::exception_ptr& e, epee::json_rpc::error& er, int default_error_code);
  std::string get_str_between_tags(const std::string &s, const std::string &start_delim, const std::string &stop_delim);

  void start_xml_rpc_cmd(std::string &xml, const std::string &method_name);
  void add_xml_rpc_string_param(std::string &xml, const std::string &param);
  void add_xml_rpc_base64_param(std::string &xml, const std::string &param);
  void add_xml_rpc_integer_param(std::string &xml, const int32_t &param);
  void end_xml_rpc_cmd(std::string &xml);

};

}
