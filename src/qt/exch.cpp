#include "exch.h"
#include "univalue.h"
#include "util.h"

//#include "protocol.h"
#include "net.h"

//#include <boost/asio.hpp>
//#include <boost/asio/ssl.hpp>

using namespace std;
//using namespace boost;
//using namespace boost::asio;

#include <boost/algorithm/string.hpp> 
//-----------------------------------------------------
ExchBox::ExchBox() {}

//-----------------------------------------------------
void ExchBox::Reset(const string &retAddr) {
  for(size_t i = 0; i < m_v_exch.size(); i++)
    delete m_v_exch[i];
  m_v_exch.clear();
  // out of service m_v_exch.push_back(new ExchCoinReform(retAddr));
  m_v_exch.push_back(new ExchEasyRabbit(retAddr));
  m_v_exch.push_back(new ExchCoinSwitch(retAddr));
} // ExchBox::Reset

//-----------------------------------------------------
ExchBox::~ExchBox() {
  for(size_t i = 0; i < m_v_exch.size(); i++)
    delete m_v_exch[i];
} // ExchBox::~ExchBox

//-----------------------------------------------------
Exch::Exch(const string &retAddr)
: m_retAddr(retAddr) {
  m_depAmo = m_outAmo = m_rate = m_limit = m_min = m_minerFee = 0.0;
}

//-----------------------------------------------------
// Get input path within server, like: /api/marketinfo/emc_btc.json
// Called from exchange-specific MarketInfo()
// Fill MarketInfo from exchange.
// Throws exception if error
const UniValue Exch::RawMarketInfo(const string &path) {
  m_rate = m_limit = m_min = m_minerFee = 0.0;
  m_pair.erase();
  return httpsFetch(path.c_str(), NULL);
} //  Exch::RawMarketInfo

//-----------------------------------------------------
// Returns extimated EMC to pay for specific pay_amount
// Must be called after MarketInfo
double Exch::EstimatedEMC(double pay_amount) const {
  return (m_rate <= 0.0)?
    m_rate : ceil(100.0 * (pay_amount + m_minerFee) / m_rate) / 100.0;
} // Exch::EstimatedEMC

//-----------------------------------------------------
// Blocking https request using Qt.
// Returns HTTP status code if OK, or -1 if error
// Ret contains server answer (if OK), or error text (-1)
int blockingHttps(const std::string & host, const std::string &path, const char *post,
			const std::map<std::string,std::string> &header, std::string & ret);
// Connect to the server by https, fetch JSON and parse to UniValue
// Throws exception if error
// This version uses https-cli module, based on libevent
UniValue Exch::httpsFetch(const char *get, const UniValue *post) {
  string strReply;
  string postBody;
  const char *post_txt = NULL;

  if(post != NULL) {
    // This is POST request - prepare postBody
    postBody = post->write(0, 0, 0) + '\n';
    post_txt = postBody.c_str();
  }

  if(m_header.empty())
	FillHeader();

  int rc = blockingHttps(Host(), get, post_txt, m_header, strReply);

  if(rc < 0)
    throw runtime_error(strReply.c_str());

  LogPrint("exch", "DBG: Exch::httpsFetch: Server returned HTTP: %d\n", rc);

  if(strReply.empty())
    throw runtime_error("Response from server is empty");

  LogPrint("exch", "DBG: Exch::httpsFetch: Reply from server: [%s]\n", strReply.c_str());

  size_t json_beg = strReply.find('{');
  size_t json_end = strReply.rfind('}');
  if(json_beg == string::npos || json_end == string::npos)
    throw runtime_error("Reply is not JSON");

   // Parse reply
  UniValue valReply(UniValue::VSTR);

  if(!valReply.read(strReply.substr(json_beg, json_end - json_beg + 1), 0))
    throw runtime_error("Couldn't parse reply from server");

  const UniValue& reply = valReply.get_obj();

  if(reply.empty())
    throw runtime_error("Empty JSON reply");

  // Check for error message in the reply
  CheckERR(reply);

  return reply;

} // Exch::httpsFetch

#if 0
// This function uses boost:io, deprecated
UniValue Exch::httpsFetch(const char *get, const UniValue *post) {

  // Connect to exchange
  asio::io_service io_service;
  ssl::context context(io_service, ssl::context::sslv23);
  context.set_options(ssl::context::no_sslv2 | ssl::context::no_sslv3);
  asio::ssl::stream<asio::ip::tcp::socket> sslStream(io_service, context);
  SSL_set_tlsext_host_name(sslStream.native_handle(), Host().c_str());
  SSLIOStreamDevice<asio::ip::tcp> d(sslStream, true); // use SSL
  iostreams::stream< SSLIOStreamDevice<asio::ip::tcp> > stream(d);

  if(!d.connect(Host(), "443"))
    throw runtime_error("Couldn't connect to server");

  string postBody;
  const char *reqType = "GET ";

  if(post != NULL) {
    // This is POST request - prepare postBody
    reqType = "POST ";
    postBody = post->write(0, 0, 0) + '\n';
  }

  LogPrint("exch", "DBG: Exch::httpsFetch: Req: method=[%s] path=[%s] H=[%s]\n", reqType, get, Host().c_str());

  // Send request
  stream << reqType << (get? get : "/") << " HTTP/1.1\r\n"
         << "Host: " << Host() << "\r\n"
         << "User-Agent: emercoin-json-rpc/" << FormatFullVersion() << "\r\n";

  if(postBody.size()) {
    stream << "Content-Type: application/json\r\n"
           << "Content-Length: " << postBody.size() << "\r\n";
  }

  stream << "Connection: close\r\n"
         << "Accept: application/json\r\n\r\n"
	 << postBody << std::flush;

  // Receive HTTP reply status
  int nProto = 0;
  int nStatus = ReadHTTPStatus(stream, nProto);

  if(nStatus >= 400)
    throw runtime_error(strprintf("Server returned HTTP error %d", nStatus));

  // Receive HTTP reply message headers and body
  map<string, string> mapHeaders;
  string strReply;
  ReadHTTPMessage(stream, mapHeaders, strReply, nProto, 4 * 1024);

  LogPrint("exch", "DBG: Exch::httpsFetch: Server returned HTTP: %d\n", nStatus);

  if(strReply.empty())
    throw runtime_error("No response from server");

  LogPrint("exch", "DBG: Exch::httpsFetch: Reply from server: [%s]\n", strReply.c_str());

  size_t json_beg = strReply.find('{');
  size_t json_end = strReply.rfind('}');
  if(json_beg == string::npos || json_end == string::npos)
    throw runtime_error("Reply is not JSON");

   // Parse reply
  UniValue valReply(UniValue::VSTR);

  if(!valReply.read(strReply.substr(json_beg, json_end - json_beg + 1), 0))
    throw runtime_error("Couldn't parse reply from server");

  const UniValue& reply = valReply.get_obj();

  if(reply.empty())
    throw runtime_error("Empty JSON reply");

  // Check for error message in the reply
  CheckERR(reply);

  return reply;
} // UniValue Exch::httpsFetch
#endif

//-----------------------------------------------------
// Check JSON-answer for "error" key, and throw error
// message, if exists
void Exch::CheckERR(const UniValue &reply) const {
  const UniValue& err = find_value(reply, "error");
  if(err.isStr())
    throw runtime_error(err.get_str().c_str());
} // Exch::CheckERR

//-----------------------------------------------------
// Extract raw key from txkey
// Return NULL if "Not my key" or invalid key
const char *Exch::RawKey(const string &txkey) const {
  const char *key = txkey.empty()? m_txKey.c_str() : txkey.c_str();
  if(strncmp(key, Name().c_str(), Name().length()) != 0) 
    return NULL; // Not my key
  key += Name().length();
  if(*key++ != ':')
    return NULL; // Not my key
  return key;
} // Exch::RawKey

//=====================================================

//-----------------------------------------------------
ExchCoinReform::ExchCoinReform(const string &retAddr)
: Exch::Exch(retAddr) {
}

//-----------------------------------------------------
const string& ExchCoinReform::Name() const { 
  static const string rc("CoinReform");
  return rc;
}

//-----------------------------------------------------
const string& ExchCoinReform::Host() const {
  static const string rc("www.coinreform.com");
  return rc;
}

//-----------------------------------------------------
// Get currency for exchnagge to, like btc, ltc, etc
// Fill MarketInfo from exchange.
// Returns empty string if OK, or error message, if error
string ExchCoinReform::MarketInfo(const string &currency, double amount) {
  try {
    const UniValue mi(RawMarketInfo("/api/marketinfo/ltc_" + currency + ".json"));
    //const UniValue mi(RawMarketInfo("/api/marketinfo/emc_" + currency + ".json"));
    LogPrint("exch", "DBG: ExchCoinReform::MarketInfo(%s|%s) returns <%s>\n\n", Host().c_str(), currency.c_str(), mi.write(0, 0, 0).c_str());
    m_pair     = mi["pair"].get_str();
    m_rate     = atof(mi["rate"].get_str().c_str());
    m_limit    = atof(mi["limit"].get_str().c_str());
    m_min      = atof(mi["min"].get_str().c_str());
    m_minerFee = atof(mi["minerFee"].get_str().c_str());
    return "";
  } catch(std::exception &e) {
    return e.what();
  }
} // ExchCoinReform::MarketInfo
//coinReform
//{"pair":"EMC_BTC","rate":"0.00016236","limit":"0.01623600","min":"0.00030000","minerFee":"0.00050000"}

//-----------------------------------------------------
// Creatse SEND exchange channel for 
// Send "amount" in external currecny "to" address
// Fills m_depAddr..m_txKey, and updates m_rate
// Returns error text, or empty string, if OK
string ExchCoinReform::Send(const string &to, double amount) {
  if(amount < m_min)
   return strprintf("amount=%lf is less than minimum=%lf", amount, m_min);
  if(amount > m_limit)
   return strprintf("amount=%lf is greater than limit=%lf", amount, m_limit);

  // Cleanum output
  m_depAddr.erase();
  m_outAddr.erase();
  m_txKey.erase();
  m_depAmo = m_outAmo = 0.0;
  m_txKey.erase();

  try {
    UniValue Req(UniValue::VOBJ);
    Req.push_back(Pair("amount", amount));
    Req.push_back(Pair("withdrawal", to));
    Req.push_back(Pair("pair", m_pair));
    Req.push_back(Pair("refund_address", m_retAddr));
    // The public disclosure for RefID usage: https://bitcointalk.org/index.php?topic=362513
    Req.push_back(Pair("ref_id", "2f77783d"));

    UniValue Resp(httpsFetch("/api/sendamount", &Req));
    LogPrint("exch", "DBG: ExchCoinReform::Send(%s|%s) returns <%s>\n\n", Host().c_str(), m_pair.c_str(), Resp.write(0, 0, 0).c_str());
    m_rate     = atof(Resp["rate"].get_str().c_str());
    m_depAddr  = Resp["deposit"].get_str();			// Address to pay EMC
    m_outAddr  = Resp["withdrawal"].get_str();			// Address to pay from exchange
    m_depAmo   = atof(Resp["deposit_amount"].get_str().c_str());// amount in EMC
    m_outAmo   = atof(Resp["withdrawal_amount"].get_str().c_str());// Amount transferred to BTC
    m_txKey    = Name() + ':' + Resp["key"].get_str();		// TX reference key

    // Adjust deposit amount to 1EMCent, upward
    m_depAmo = ceil(m_depAmo * 100.0) / 100.0;

    return "";
  } catch(std::exception &e) { // something wrong at HTTPS
    return e.what();
  }
} // ExchCoinReform::Send

//-----------------------------------------------------
// Check status of existing transaction.
// If key is empty, used the last key
// Returns status (including err), or minus "-", if "not my" key
string ExchCoinReform::TxStat(const string &txkey, UniValue &details) {
  const char *key = RawKey(txkey);
  if(key == NULL)
      return "-"; // Not my key

  char buf[400];
  snprintf(buf, sizeof(buf), "/api/txstat/%s.json", key);
  try {
    details = httpsFetch(buf, NULL);
    LogPrint("exch", "DBG: ExchCoinReform::TxStat(%s|%s) returns <%s>\n\n", Host().c_str(), buf, details.write(0, 0, 0).c_str());
    return details["status"].get_str();
  } catch(std::exception &e) { // something wrong at HTTPS
    return e.what();
  }
} // ExchCoinReform::TxStat

//-----------------------------------------------------
// Cancel TX by txkey.
// If key is empty, the last key used.
// Returns error text, or an empty string, if OK
// Returns minus "-", if "not my" key
string ExchCoinReform::CancelTX(const string &txkey) {
  const char *key = RawKey(txkey);
  if(key == NULL)
      return "-"; // Not my key

  char buf[400];
  snprintf(buf, sizeof(buf), "/api/cancel/%s.json", key);
  try {
    UniValue Resp(httpsFetch(buf, NULL));
	LogPrint("exch", "DBG: ExchCoinReform::CancelTX(%s|%s) returns <%s>\n\n", Host().c_str(), buf, Resp.write(0, 0, 0).c_str());
    m_txKey.erase(); // Preserve from double Cancel
    return m_txKey;
  } catch(std::exception &e) { // something wrong at HTTPS
    return e.what();
  }
} // ExchCoinReform::CancelTX

//-----------------------------------------------------
// Check time in secs, left in the contract, created by prev Send()
// If key is empty, used the last key
// Returns time or zero, if contract expired
// Returns -1, if "not my" key
int ExchCoinReform::Remain(const string &txkey) {
  const char *key = RawKey(txkey);
  if(key == NULL)
      return -1; // Not my key

  char buf[400];
  snprintf(buf, sizeof(buf), "/api/timeremaining/%s.json", key);
  try {
    UniValue Resp(httpsFetch(buf, NULL));
	LogPrint("exch", "DBG: ExchCoinReform::CancelTX(%s|%s) returns <%s>\n\n", Host().c_str(), buf, Resp.write(0, 0, 0).c_str());
    return Resp["seconds_remaining"].get_int();
  } catch(std::exception &e) { // something wrong at HTTPS
    return 0;
  }
} // ExchCoinReform::TimeLeft

//=====================================================

ExchCoinSwitch::ExchCoinSwitch(const string &retAddr)
: Exch::Exch(retAddr) {}

//-----------------------------------------------------
const string& ExchCoinSwitch::Name() const { 
  static const string rc("CoinSwitch");
  return rc;
}

//-----------------------------------------------------
const string& ExchCoinSwitch::Host() const {
  static const string rc("api.coinswitch.co");
  return rc;
}
//-----------------------------------------------------
// Check JSON-answer for "error" key, and throw error message, if exists
void ExchCoinSwitch::CheckERR(const UniValue &reply) const {
  const char *err_str = "";
  const UniValue& success = find_value(reply, "success");
  if(success.isNull())
    err_str = "Missing success code in the response";
  else
  if(!success.isBool())
    err_str = "Success code in the response is not a bool";
  else 
  if(!success.isTrue()) {
    const UniValue& msg = find_value(reply, "msg");
    if(!msg.isNull() && msg.isStr())
      err_str = msg.get_str().c_str();
    if(err_str[0] == 0)
      err_str = "No error message";
  }
  if(*err_str)
    throw runtime_error(err_str);
} // ExchcoinSwitch::CheckERR


//-----------------------------------------------------
void ExchCoinSwitch::FillHeader() {
  struct in_addr dummy_addr;
  dummy_addr.s_addr = 0x08080808; // 8.8.8.8 - Google address
  CNetAddr fake_server_addr(dummy_addr);
  string ipAddr(GetLocalAddress(&fake_server_addr, NODE_NONE).ToStringIP());
  LogPrint("exch", "DBG: ExchCoinSwitch::FillHeader() x-user-ip=%s\n", ipAddr.c_str());
  m_header["x-user-ip"] = ipAddr;
  if(_sandBox)
	m_header["x-api-key"] = "cRbHFJTlL6aSfZ0K2q7nj6MgV5Ih4hbA2fUG0ueO"; // sandbox API key
  else
	m_header["x-api-key"] = "ty7smoqSte5Ku3GKeRM4F3m8xrIksJfM723sutEI"; // real API key
}
//-----------------------------------------------------
// Get currency for exchnagge to, like btc, ltc, etc
// Fill MarketInfo from exchange.
// Returns empty string if OK, or error message, if error
string ExchCoinSwitch::MarketInfo(const string &currency, double amount) {
  m_rate = m_limit = m_min = m_minerFee = 0.0;
  string currLowercase(boost::algorithm::to_lower_copy(currency));
  m_pair = "emc/" + currLowercase;

  try {
    UniValue Req(UniValue::VOBJ);
    Req.push_back(Pair("depositCoin", "emc"));
	Req.push_back(Pair("destinationCoin", currLowercase));
    UniValue Resp(httpsFetch("/v2/rate", &Req));
	LogPrint("exch", "DBG: ExchCoinSwitch::MarketInfo(%s|%s) returns <%s>\n\n", Host().c_str(), currLowercase.c_str(), Resp.write(0, 0, 0).c_str());
    const UniValue& mi  = find_value(Resp, "data");
    m_rate     = mi["rate"].get_real();
    m_limit    = mi["limitMaxDestinationCoin"].get_real();
    m_min      = mi["limitMinDestinationCoin"].get_real();
    m_minerFee = mi["minerFee"].get_real();
    return "";
  } catch(std::exception &e) {
    return e.what();
  }
} // ExchCoinSwitch::MarketInfo
#if 0
coinSwitch
{"success": true, "code": "OK", 
  "data": {"rate": 9.727e-05, "minerFee": 0.0015, "limitMinDepositCoin": 70.0, "limitMaxDepositCoin": 18419.44194394, 
           "limitMinDestinationCoin": 0.00680915, "limitMaxDestinationCoin": 1.79172663}, 
  "msg": ""}
#endif


//-----------------------------------------------------
static void AddAddr(UniValue &rc, const string &key, const string &val) {
  UniValue out(UniValue::VOBJ);
  out.push_back(Pair("address", val));
  rc.push_back(Pair(key, out));
}
//-----------------------------------------------------
// Create SEND exchange channel for 
// Send "amount" in external currecny "to" address
// Fills m_depAddr..m_txKey, and updates m_rate
// Returns error text, or empty string, if OK
string ExchCoinSwitch::Send(const string &to, double amount) {
  if(amount < m_min)
   return strprintf("amount=%lf is less than minimum=%lf", amount, m_min);
  if(amount > m_limit)
   return strprintf("amount=%lf is greater than limit=%lf", amount, m_limit);

  // Cleanup output
  m_depAddr.erase();
  m_outAddr.erase();
  m_txKey.erase();
  m_depAmo = m_outAmo = 0.0;

  try {
    UniValue Req(UniValue::VOBJ);
    Req.push_back(Pair("depositCoin", "emc"));
    Req.push_back(Pair("destinationCoin", string(strchr(m_pair.c_str(), '/') + 1)));
    Req.push_back(Pair("destinationCoinAmount", amount));
    AddAddr(Req, "destinationAddress", to);
    AddAddr(Req, "refundAddress", m_retAddr);
    const UniValue Resp(httpsFetch("/v2/order", &Req));
    LogPrint("exch", "DBG: ExchCoinSwitch::Send(%s|%s) returns <%s>\n\n", Host().c_str(), m_pair.c_str(), Resp.write(0, 0, 0).c_str());
    const UniValue& r = find_value(Resp, "data");

    m_txKey    = Name() + ':' + r["orderId"].get_str();		// TX reference key
    const UniValue& depAddr = find_value(r, "exchangeAddress");
    if(depAddr.isNull())
      throw runtime_error("Missing exchangeAddress");
    m_depAddr  = depAddr["address"].get_str();			// Address to pay EMC
    m_outAddr  = to;                    			// Address to pay from exchange

    m_depAmo   = r["expectedDepositCoinAmount"].get_real();     // amount in EMC
    m_outAmo   = r["expectedDestinationCoinAmount"].get_real(); // Amount transferred to BTC
    m_depAmo   = ceil(m_depAmo * COIN) / COIN;
    m_rate     = m_outAmo / m_depAmo;

    return "";
  } catch(std::exception &e) { // something wrong at HTTPS
    return e.what();
  }
} // ExchCoinSwitch::Send
#if 0
{"success": true, "code": "OK", "data": 
  {"orderId": "4ff7b926-8c6c-4c8f-b0d2-4088f86045be", 
    "exchangeAddress": {"address": "EZCDVCgHxEB86Cku6JknueA8GcS5bCx1zx", "tag": null}, 
    "expectedDepositCoinAmount": 197.005516154452, "expectedDestinationCoinAmount": 0.02}
#endif

//-----------------------------------------------------
// Check status of existing transaction.
// If key is empty, used the last key
// Returns status (including err), or minus "-", if "not my" key
string ExchCoinSwitch::TxStat(const string &txkey, UniValue &details) {
  const char *key = RawKey(txkey);
  if(key == NULL)
      return "-"; // Not my key

  char buf[400];
  snprintf(buf, sizeof(buf), "/v2/order/%s", key);
  try {
    details = httpsFetch(buf, NULL);
    LogPrint("exch", "DBG: ExchCoinSwitch::TxStat(%s|%s) returns <%s>\n\n", Host().c_str(), buf, details.write(0, 0, 0).c_str());
    const UniValue& r = find_value(details, "data");
    return r["status"].get_str();
  } catch(std::exception &e) { // something wrong at HTTPS
    return e.what();
  }
} // ExchCoinSwitch::TxStat

//-----------------------------------------------------
// Cancel TX by txkey.
// If key is empty, used the last key
// Returns error text, or an empty string, if OK
// Returns minus "-", if "not my" key
string ExchCoinSwitch::CancelTX(const string &txkey) {
  const char *key = RawKey(txkey);
  if(key == NULL)
      return "-"; // Not my key
  return txkey + "; CANCEL is not supported";
#if 0
  char buf[400];
  snprintf(buf, sizeof(buf), "/api/cancel/%s.json", key);
  try {
    UniValue Resp(httpsFetch(buf, NULL));
	LogPrint("exch", "DBG: ExchCoinSwitch::CancelTX(%s|%s) returns <%s>\n\n", Host().c_str(), buf, Resp.write(0, 0, 0).c_str());
    m_txKey.erase(); // Preserve from double Cancel
    return m_txKey;
  } catch(std::exception &e) { // something wrong at HTTPS
    return e.what();
  }
#endif
}

//-----------------------------------------------------
// Check time in secs, left in the contract, created by prev Send()
// If key is empty, used the last key
// Returns time or zero, if contract expired
// Returns -1, if "not my" key
int ExchCoinSwitch::Remain(const string &txkey) {
  const char *key = RawKey(txkey);
  if(key == NULL)
      return -1; // Not my key

  char buf[400];
  snprintf(buf, sizeof(buf), "/v2/order/%s", key);
  try {
    const UniValue Resp(httpsFetch(buf, NULL));
    LogPrint("exch", "DBG: ExchCoinSwitch::Remain(%s|%s) returns <%s>\n\n", Host().c_str(), buf, Resp.write(0, 0, 0).c_str());
    const UniValue& r = find_value(Resp, "data");
    return r["validTill"].get_int64() / 1000 - time(NULL);
  } catch(std::exception &e) { // something wrong at HTTPS
    return 0;
  }
} // ExchCoinSwitch::TimeLeft

//=====================================================

const static char EasyRabbitAPIKey[] = "B2bYf04VPN34uo8pjDgMerVXc";
ExchEasyRabbit::ExchEasyRabbit(const string &retAddr)
: Exch::Exch(retAddr) {}

//-----------------------------------------------------
const string& ExchEasyRabbit::Name() const { 
  static const string rc("EasyRabbit");
  return rc;
}

//-----------------------------------------------------
const string& ExchEasyRabbit::Host() const {
  static const string rc("easyrabbit.net");
  return rc;
}
//-----------------------------------------------------
// Check JSON-answer for "error" key, and throw error message, if exists
void ExchEasyRabbit::CheckERR(const UniValue &reply) const {
  const char *err_str = "";
  const UniValue& resp_txt = find_value(reply, "Response");
  if(resp_txt.isNull() || !resp_txt.isStr())
    err_str = "Missing response text";
  else
  if(resp_txt.get_str() != "success") {
    const UniValue& msg = find_value(reply, "Message");
    if(!msg.isNull() && msg.isStr())
      err_str = msg.get_str().c_str();
    if(err_str[0] == 0)
      err_str = "Empty error message";
  }
  if(*err_str)
    throw runtime_error(err_str);
} // ExchcoinSwitch::CheckERR

//-----------------------------------------------------
// Get currency for exchnagge to, like btc, ltc, etc
// Fill MarketInfo from exchange.
// Returns empty string if OK, or error message, if error
string ExchEasyRabbit::MarketInfo(const string &currency, double amount) {
  m_rate = m_limit = m_min = m_minerFee = 0.0;
  string curUC(boost::algorithm::to_upper_copy(currency));
  try {
    char https_get[200];
    sprintf(https_get, "/api/pairinfo?apikey=%s&from=EMC&to=%s", EasyRabbitAPIKey, curUC.c_str());
    const UniValue Resp1(httpsFetch(https_get, NULL));
    LogPrint("exch", "DBG: ExchEasyRabbit::MarketInfo1(%s|%s) returns <%s>\n\n", Host().c_str(), https_get, Resp1.write(0, 0, 0).c_str());
    const UniValue& mi1(find_value(Resp1, "Data")[0]);
    m_pair     = "EMC/" + curUC;
    m_min      = atof(mi1["Min"].get_str().c_str());
    m_limit    = atof(mi1["Max"].get_str().c_str());
    m_minerFee = atof(mi1["Network_fee"].get_str().c_str());
    sprintf(https_get, "/api/exrates?apikey=%s&from=EMC&to=%s&amount=%.4lf", EasyRabbitAPIKey, curUC.c_str(), m_min + 0.1);
    const UniValue Resp2(httpsFetch(https_get, NULL));
    LogPrint("exch", "DBG: ExchEasyRabbit::MarketInfo2(%s|%s) returns <%s>\n\n", Host().c_str(), https_get, Resp2.write(0, 0, 0).c_str());
    const UniValue& mi2(find_value(Resp2, "Data")[0]);
    m_rate     = atof(mi2["Rate"].get_str().c_str());
    m_min     *= m_rate; // SRC->DST
    m_limit   *= m_rate;
// incorrect doc?    m_minerFee*= m_rate;
    return "";
  } catch(std::exception &e) {
    return e.what();
  }
} // ExchEasyRabbit::MarketInfo

//-----------------------------------------------------
// Create SEND exchange channel for 
// Send "amount" in external currecny "to" address
// Fills m_depAddr..m_txKey, and updates m_rate
// Returns error text, or empty string, if OK
string ExchEasyRabbit::Send(const string &to, double amount) {
  if(amount < m_min)
   return strprintf("amount=%lf is less than minimum=%lf", amount, m_min);
  if(amount > m_limit)
   return strprintf("amount=%lf is greater than limit=%lf", amount, m_limit);

  amount = (amount + m_minerFee) / m_rate;

  // Cleanup output
  m_depAddr.erase();
  m_outAddr.erase();
  m_txKey.erase();
  m_depAmo = m_outAmo = 0.0;

  try {
    char https_get[600];
    sprintf(https_get, "/api/placeorder?apikey=%s&from=EMC&to=%s&amount=%lf&address=%s&refundaddress=%s", EasyRabbitAPIKey, strchr(m_pair.c_str(), '/') + 1, amount, to.c_str(), m_retAddr.c_str());
    const UniValue Resp(httpsFetch(https_get, NULL));
    LogPrint("exch", "DBG: ExchEasyRabbit::Send(%s|%s) returns <%s>\n\n", Host().c_str(), https_get, Resp.write(0, 0, 0).c_str());
    const UniValue& r(find_value(Resp, "Data")[0]);
    m_txKey    = Name() + ':' + r["Id"].get_str();		// TX reference key
    m_depAddr  = r["Deposit_address"].get_str();                // Address to pay EMC
    m_outAddr  = r["Receive_address"].get_str();                // Address to pay from exchange
    m_depAmo   = atof(r["Deposit_amount"].get_str().c_str());   // amount in EMC
    m_outAmo   = atof(r["Receive_amount"].get_str().c_str());   // Amount transferred to BTC
    // Adjust deposit amount to 1Subent, upward
    m_depAmo   = ceil(m_depAmo * COIN) / COIN;
    // ?? m_rate     = m_outAmo / m_depAmo;
    return "";
  } catch(std::exception &e) { // something wrong at HTTPS
    return e.what();
  }
} // ExchEasyRabbit::Send

//-----------------------------------------------------
// Check status of existing transaction.
// If key is empty, used the last key
// Returns status (including err), or minus "-", if "not my" key
string ExchEasyRabbit::TxStat(const string &txkey, UniValue &details) {
  const char *key = RawKey(txkey);
  if(key == NULL)
      return "-"; // Not my key

  char buf[400];
  snprintf(buf, sizeof(buf), "/api/orderstatus?apikey=%s&id=%s", EasyRabbitAPIKey, key);
  try {
    details = httpsFetch(buf, NULL);
    LogPrint("exch", "DBG: ExchEasyRabbit::TxStat(%s|%s) returns <%s>\n\n", Host().c_str(), buf, details.write(0, 0, 0).c_str());
    const UniValue& r = find_value(details, "Data")[0];
    return r["Status"].get_str();
  } catch(std::exception &e) { // something wrong at HTTPS
    return e.what();
  }
} // ExchEasyRabbit::TxStat

//-----------------------------------------------------
// Cancel TX by txkey.
// If key is empty, used the last key
// Returns error text, or an empty string, if OK
// Returns minus "-", if "not my" key
string ExchEasyRabbit::CancelTX(const string &txkey) {
  const char *key = RawKey(txkey);
  if(key == NULL)
      return "-"; // Not my key
  return txkey + "; CANCEL is not supported";
}

//-----------------------------------------------------
// Check time in secs, left in the contract, created by prev Send()
// If key is empty, used the last key
// Returns time or zero, if contract expired
// Returns -1, if "not my" key
int ExchEasyRabbit::Remain(const string &txkey) {
  const char *key = RawKey(txkey);
  if(key == NULL)
      return -1; // Not my key
  return 600; // fictive, ER does not support this
} // ExchEasyRabbit::TimeLeft



//=====================================================
//
// ShapeShift
// <{"pair":"ltc_btc","rate":0.00320953,"minerFee":0.0012,"limit":256.45898362,"minimum":0.74136083,"maxLimit":641.14745904}
/*
    m_rate     = mi["rate"].get_real();
    m_limit    = mi["limit"].get_real();
    m_min      = mi["min"].get_real();
    m_minerFee = mi["minerFee"].get_real();
*/

//-----------------------------------------------------
//=====================================================
void exch_test() {
#if 1
  ExchBox eBox;
  eBox.Reset("ESgQZ4oU5TN6BRK3DqZX3qDSrQjPwWHP7t");
  do {
      Exch    *exch = eBox.m_v_exch[0];
      printf("exch_test()\nwork with exch=0, Name=%s URL=%s\n", exch->Name().c_str(), exch->Host().c_str());

      string err(exch->MarketInfo("btc", 0.0));
      printf("exch_test:MarketInfo returned: [%s]\n", err.c_str());
      if(!err.empty()) break;
      printf("exch_test:Values from exch: m_rate=%lf; m_limit=%lf; m_min=%lf; m_minerFee=%lf\n", 
	      exch->m_rate, exch->m_limit, exch->m_min, exch->m_minerFee);

      printf("\nexch_test:Tryint to send BTC\n");
      err = exch->Send("19rem1SSWTphjsFLmcNEAvnfHaBFuDMMae", 0.02); // good addr (JSN)
      // bad err = exch->Send("1Evqeh5pWphbWzmRAc4d3Wb82mAUBhWEVz", 0.02); // bad addr
      printf("exch_test:Send returned: [%s]\n", err.c_str());
      if(!err.empty()) break;
      printf("m_depAddr=%s, m_outAddr=%s m_depAmo=%lf m_outAmo=%lf m_txKey=%s\n",
	      exch->m_depAddr.c_str(), exch->m_outAddr.c_str(), exch->m_depAmo, exch->m_outAmo, exch->m_txKey.c_str());

      MilliSleep(1000);

      printf("\nexch_test:Checking TX status\n");
      UniValue Det(UniValue::VOBJ);
      err = exch->TxStat("", Det);
      printf("exch_test:TxStat returned: [%s]\n", err.c_str());
      printf("exch_test:TxStat Details: <%s>\n\n", Det.write(0, 0, 0).c_str());

      printf("exch_test:Remain=%d\n", exch->Remain(""));

	  //err = exch->CancelTX("");
      //printf("exch_test:Cancel returned: [%s]\n", err.c_str());

  } while(0);

#endif
  printf("exch_test:Quit from test\n");
  exit(0);
}

//-----------------------------------------------------
